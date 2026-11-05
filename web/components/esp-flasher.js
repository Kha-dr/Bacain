/**
 * Mini-E-Reader ESP32-S3 Web Serial Firmware Flasher
 * Standard Vanilla JS Web Component (Custom Elements v1)
 * Powered by esptool-js & Web Serial API
 * 
 * Rules: AGENTS.md (Zero-Mock, Zero Silent Fallback, Ponytail, Antislop)
 */

// Dynamic ESM Loader for esptool-js with CDN fallback
let esptoolCache = null;

async function loadEsptool() {
  if (esptoolCache) return esptoolCache;
  try {
    const mod = await import('https://unpkg.com/esptool-js@0.5.4/bundle.js');
    esptoolCache = mod;
    return mod;
  } catch (err1) {
    try {
      const mod = await import('https://esm.sh/esptool-js@0.5.4');
      esptoolCache = mod;
      return mod;
    } catch (err2) {
      throw new Error(`Failed to load esptool-js library: ${err1.message}; ${err2.message}`);
    }
  }
}

// Supported USB Serial Vendor IDs (ESP32-S3 USB-JTAG/Serial, CH340, CP2102, FTDI)
const SERIAL_DEVICE_FILTERS = [
  { usbVendorId: 0x303a }, // Espressif Systems (ESP32-S3 native USB-JTAG/Serial & USB CDC)
  { usbVendorId: 0x1a86 }, // QinHeng Electronics (CH340 / CH341)
  { usbVendorId: 0x10c4 }, // Silicon Labs (CP2102 / CP210x)
  { usbVendorId: 0x0403 }, // FTDI
];

class EspWebFlasher extends HTMLElement {
  constructor() {
    super();

    // Serial Connection State
    this.port = null;
    this.transport = null;
    this.esploader = null;

    this.isDeviceConnected = false;
    this.isConnecting = false;
    this.isFlashing = false;

    // Hardware Details
    this.chipName = null;
    this.chipDesc = null;
    this.macAddress = null;

    const cfg = (typeof window !== 'undefined' && (window.CONFIG || window.BACA_CONFIG)) || null;
    const defaultRepoSlug = cfg?.repoSlug || (cfg?.repo ? cfg.repo.replace(/^https?:\/\/github\.com\//i, '').replace(/\/+$/, '') : 'khad1r/BacaIn');

    // Settings
    this.selectedBaudrate = cfg?.flasher?.baud || 460800;
    this.eraseBeforeFlash = false;
    this.autoScroll = true;

    // Binary file slot: Combined factory firmware @ 0x0
    this.fileSlots = [
      {
        id: 'slot_factory',
        offset: cfg?.flasher?.offset || '0x0',
        fileData: null,
        fileName: null,
        fileSize: 0
      }
    ];

    // GitHub Release options
    this.releaseRepo = defaultRepoSlug;
    this.releaseTag = '';
    this.releasesList = [];
    this.selectedRelease = null;
    this.selectedAsset = null;
    this.selectedAssetUrl = null;
    this.availableReleaseAssets = [];

    this.isFetchingReleasesList = false;
    this.isFetchingBinary = false;
    this.isFetchingRelease = false;

    // Terminal log records
    this.terminalLogs = [];

    // Flashing progress
    this.flashProgress = 0;
    this.flashStatusText = 'Ready';
    this.progressDetails = {
      written: 0,
      total: 0,
      fileIndex: 0,
      totalFiles: 0
    };
  }

  connectedCallback() {
    this.render();
    if (this.releasesList.length === 0 && !this.isFetchingReleasesList) {
      this.fetchReleasesList();
    }
  }

  disconnectedCallback() {
    this.cleanUpConnection();
  }

  hasValidFirmware() {
    const slot = this.fileSlots && this.fileSlots[0];
    return Boolean(slot && slot.fileData && slot.fileData.length > 0);
  }

  checkWebSerialSupport() {
    const isSupported = 'serial' in navigator;
    const warningEl = this.querySelector('#serialCompatAlert');
    const connectBtn = this.querySelector('#flasherConnectBtn');

    if (!isSupported) {
      if (warningEl) warningEl.style.display = 'flex';
      if (connectBtn) connectBtn.disabled = true;
      this.appendTerminal(
        'Web Serial API is not supported in this browser. Please use Chrome, Edge, or Opera on Desktop.\n',
        'error'
      );
    } else {
      if (warningEl) warningEl.style.display = 'none';
      if (connectBtn) connectBtn.disabled = false;
    }
  }

  renderDeviceInfo() {
    if (!this.isDeviceConnected) return '';
    return `
      <div style="margin-top: 0.6rem; font-size: 0.82rem; color: var(--text-secondary);">
        Device: <strong style="color: var(--text-primary);">${this.chipName || 'ESP32-S3'}</strong>
        ${this.macAddress ? ` &bull; MAC: <span class="mono">${this.macAddress}</span>` : ''}
      </div>
    `;
  }

  renderDropzoneContent() {
    const slot = this.fileSlots && this.fileSlots[0];
    const hasFile = slot && slot.fileData && slot.fileData.length > 0 && !this.selectedRelease;
    if (hasFile) {
      return `
        <div style="font-size: 0.84rem; font-weight: 600; color: var(--text-primary); margin-bottom: 0.2rem;">
          ${this.escapeHtml(slot.fileName)} (${this.formatBytes(slot.fileSize)})
        </div>
        <div style="font-size: 0.74rem; color: var(--text-muted);">
          Ready to flash at offset 0x0
        </div>
      `;
    }
    return `
      <div style="font-size: 0.84rem; font-weight: 500; color: var(--text-primary); margin-bottom: 0.2rem;">
        Choose or drop firmware binary (.bin)
      </div>
      <div style="font-size: 0.74rem; color: var(--text-muted);">
        Target address: 0x0
      </div>
    `;
  }

  renderReleaseOptions() {
    if (this.isFetchingReleasesList) {
      return '<option value="" disabled selected>Loading releases from GitHub...</option>';
    }
    if (!this.releasesList || this.releasesList.length === 0) {
      return '<option value="" disabled selected>No releases available</option>';
    }

    return this.releasesList.map((rel, index) => {
      const isSelected = this.selectedRelease
        ? this.selectedRelease.tag_name === rel.tag_name
        : index === 0;
      const dateStr = this.formatDate(rel.published_at);
      const dateSuffix = dateStr ? ` (${dateStr})` : '';
      const prereleaseSuffix = rel.prerelease ? ' [pre-release]' : '';
      return `<option value="${this.escapeHtml(rel.tag_name)}" ${isSelected ? 'selected' : ''}>${this.escapeHtml(rel.tag_name + prereleaseSuffix + dateSuffix)}</option>`;
    }).join('');
  }

  renderDeviceOptions() {
    if (!this.availableReleaseAssets || this.availableReleaseAssets.length === 0) {
      return '<option value="" disabled selected>No board binary available</option>';
    }

    return this.availableReleaseAssets.map(asset => {
      const isSelected = Boolean(
        this.selectedAsset && (this.selectedAsset.browser_download_url === asset.browser_download_url || this.selectedAsset.name === asset.name)
      );
      const sizeStr = asset.size ? ` (${this.formatBytes(asset.size)})` : '';
      return `<option value="${this.escapeHtml(asset.browser_download_url)}" data-name="${this.escapeHtml(asset.name)}" ${isSelected ? 'selected' : ''}>${this.escapeHtml(asset.name + sizeStr)}</option>`;
    }).join('');
  }

  renderReleaseMetadata() {
    const slot = this.fileSlots && this.fileSlots[0];
    const hasLocalFile = Boolean(slot && slot.fileData && slot.fileData.length > 0 && !this.selectedRelease);

    if (hasLocalFile) {
      return `
        <div class="release-meta-grid" role="region" aria-label="Loaded firmware details">
          <div class="release-meta-item">
            <span class="meta-label">Source</span>
            <span class="meta-value">Local file</span>
          </div>
          <div class="release-meta-item">
            <span class="meta-label">File</span>
            <span class="meta-value meta-mono">${this.escapeHtml(slot.fileName)}</span>
          </div>
          <div class="release-meta-item">
            <span class="meta-label">Size</span>
            <span class="meta-value meta-mono">${this.formatBytes(slot.fileSize)}</span>
          </div>
        </div>
      `;
    }

    if (!this.selectedRelease) {
      return '';
    }

    const rel = this.selectedRelease;
    const asset = this.selectedAsset;
    const dateStr = this.formatDate(rel.published_at) || 'Unknown';
    const binaryName = asset ? asset.name : 'None';
    const binarySize = asset && asset.size ? ` (${this.formatBytes(asset.size)})` : '';

    return `
      <div class="release-meta-grid" role="region" aria-label="Release details">
        <div class="release-meta-item">
          <span class="meta-label">Version</span>
          <span class="meta-value meta-mono">${this.escapeHtml(rel.tag_name)}</span>
        </div>
        <div class="release-meta-item">
          <span class="meta-label">Published</span>
          <span class="meta-value">${this.escapeHtml(dateStr)}</span>
        </div>
        <div class="release-meta-item">
          <span class="meta-label">Binary</span>
          <span class="meta-value meta-mono">${this.escapeHtml(binaryName + binarySize)}</span>
        </div>
      </div>
    `;
  }

  renderFlashButtonText() {
    if (this.isFlashing) return 'Writing flash memory...';
    if (this.isFetchingBinary) return 'Downloading binary...';
    if (this.isFetchingReleasesList) return 'Loading releases...';

    const slot = this.fileSlots && this.fileSlots[0];
    const hasLocalFile = Boolean(slot && slot.fileData && slot.fileData.length > 0 && !this.selectedRelease);
    if (hasLocalFile) {
      return 'Flash file';
    }

    if (this.selectedRelease) {
      return `Install ${this.selectedRelease.tag_name}`;
    }

    return 'Flash firmware';
  }

  updateUI() {
    // Status Badge
    const badge = this.querySelector('#flasherStatusBadge');
    if (badge) {
      badge.className = `card-badge ${this.isDeviceConnected ? 'badge-success' : 'badge-muted'}`;
      badge.textContent = this.isDeviceConnected ? 'Connected' : (this.isConnecting ? 'Connecting...' : 'Disconnected');
    }

    // Connect Button
    const connectBtn = this.querySelector('#flasherConnectBtn');
    if (connectBtn) {
      connectBtn.className = `btn-connect ${this.isDeviceConnected ? 'connected' : ''}`;
      connectBtn.disabled = !('serial' in navigator) || this.isConnecting || this.isFlashing;
      const dot = connectBtn.querySelector('.status-dot');
      if (dot) {
        dot.className = `status-dot ${this.isDeviceConnected ? 'connected' : (this.isConnecting ? 'connecting' : 'disconnected')}`;
      }
      const label = this.querySelector('#flasherConnectBtnText');
      if (label) {
        label.textContent = this.isDeviceConnected ? 'Disconnect' : (this.isConnecting ? 'Connecting...' : 'Connect');
      }
    }

    // Device Info Container
    const infoContainer = this.querySelector('#flasherDeviceInfoContainer');
    if (infoContainer) {
      infoContainer.innerHTML = this.renderDeviceInfo();
    }

    // Refresh Releases Button
    const refreshBtn = this.querySelector('#refreshReleasesBtn');
    if (refreshBtn) {
      refreshBtn.disabled = this.isFetchingReleasesList || this.isFlashing;
    }

    // Release Selector Dropdown
    const releaseSelect = this.querySelector('#releaseSelect');
    if (releaseSelect) {
      releaseSelect.disabled = this.isFetchingReleasesList || this.isFlashing;
      releaseSelect.innerHTML = this.renderReleaseOptions();
      if (this.selectedRelease) {
        releaseSelect.value = this.selectedRelease.tag_name;
      }
    }

    // Multi-Board Device Select Controls
    const deviceContainer = this.querySelector('#flasherDeviceSelectContainer');
    const deviceSelect = this.querySelector('#flasherDeviceSelect');
    if (deviceContainer) {
      deviceContainer.style.display = (this.availableReleaseAssets && this.availableReleaseAssets.length > 1) ? 'block' : 'none';
    }
    if (deviceSelect) {
      deviceSelect.disabled = this.isFlashing || this.isFetchingBinary;
      deviceSelect.innerHTML = this.renderDeviceOptions();
      if (this.selectedAsset) {
        deviceSelect.value = this.selectedAsset.browser_download_url;
      }
    }

    // Release Metadata Container
    const metaContainer = this.querySelector('#releaseMetadataContainer');
    if (metaContainer) {
      metaContainer.innerHTML = this.renderReleaseMetadata();
    }

    // Dropzone Content
    const dropzoneContent = this.querySelector('#fileDropzoneContent');
    if (dropzoneContent) {
      dropzoneContent.innerHTML = this.renderDropzoneContent();
    }

    const fileInput = this.querySelector('#firmwareFileInput');
    if (fileInput) {
      fileInput.disabled = this.isFlashing || this.isFetchingBinary;
    }

    const eraseCheck = this.querySelector('#eraseBeforeFlashCheck');
    if (eraseCheck) {
      eraseCheck.disabled = this.isFlashing;
    }

    // Flash Action Button
    const startFlashBtn = this.querySelector('#startFlashBtn');
    if (startFlashBtn) {
      const slot = this.fileSlots && this.fileSlots[0];
      const hasLocal = Boolean(slot && slot.fileData && slot.fileData.length > 0 && !this.selectedRelease);
      const canFlash = Boolean(hasLocal || (this.selectedRelease && this.selectedAsset));

      startFlashBtn.disabled = this.isFlashing || this.isFetchingBinary || this.isFetchingReleasesList || !canFlash;
      const flashLabel = this.querySelector('#startFlashBtnText');
      if (flashLabel) {
        flashLabel.textContent = this.renderFlashButtonText();
      }
    }

    // Progress Bar Container
    const progressContainer = this.querySelector('#flasherProgressContainer');
    if (progressContainer) {
      progressContainer.style.display = (this.isFlashing || this.flashProgress > 0) ? 'block' : 'none';
    }
  }

  render() {
    if (!this.querySelector('.flasher-container')) {
      this.renderTemplate();
      this.checkWebSerialSupport();
      this.initEventListeners();
    }
    this.updateUI();
  }

  renderTemplate() {
    const isSupported = 'serial' in navigator;

    this.innerHTML = `
      <div class="flasher-container">

        <!-- Browser Compatibility Notice -->
        <div id="serialCompatAlert" class="alert-banner alert-warning" style="display: ${isSupported ? 'none' : 'flex'};" role="alert">
          <div>
            <strong>Web Serial not supported:</strong> Use Chrome, Edge, or Opera on desktop to flash firmware over USB.
          </div>
        </div>

        <!-- 1 Unified Main Card -->
        <div class="card flasher-card">

          <!-- Section 1: Connection -->
          <div class="flasher-section flasher-section-connection">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.75rem;">
              <span style="font-weight: 600; font-size: 0.92rem;">Serial connection</span>
              <span id="flasherStatusBadge" class="card-badge ${this.isDeviceConnected ? 'badge-success' : 'badge-muted'}">
                ${this.isDeviceConnected ? 'Connected' : (this.isConnecting ? 'Connecting...' : 'Disconnected')}
              </span>
            </div>

            <div style="display: flex; gap: 0.5rem; align-items: center; flex-wrap: wrap;">
              <button id="flasherConnectBtn" type="button" class="btn-connect ${this.isDeviceConnected ? 'connected' : ''}" ${this.isConnecting || this.isFlashing ? 'disabled' : ''} aria-label="Toggle serial port connection">
                <span class="status-dot ${this.isDeviceConnected ? 'connected' : (this.isConnecting ? 'connecting' : 'disconnected')}"></span>
                <span id="flasherConnectBtnText">${this.isDeviceConnected ? 'Disconnect' : (this.isConnecting ? 'Connecting...' : 'Connect')}</span>
              </button>
            </div>

            <div id="flasherDeviceInfoContainer">
              ${this.renderDeviceInfo()}
            </div>
          </div>

          <!-- Section 2: Firmware Installation -->
          <div class="flasher-section flasher-section-firmware" style="margin-top: 1.25rem; padding-top: 1rem; border-top: 1px solid var(--border-color);">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.6rem;">
              <span style="font-weight: 600; font-size: 0.92rem;">Firmware</span>
              <button id="refreshReleasesBtn" type="button" class="btn-sm btn-secondary" style="font-size: 0.75rem; cursor: pointer;" title="Refresh releases from GitHub" ${this.isFetchingReleasesList || this.isFlashing ? 'disabled' : ''}>
                Refresh
              </button>
            </div>

            <!-- Release Dropdown -->
            <div class="release-select-group">
              <label for="releaseSelect" class="release-select-label">
                Release
              </label>
              <select id="releaseSelect" class="control-select" style="width: 100%; padding: 0.45rem 0.65rem; font-size: 0.82rem; background: var(--bg-input); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); outline: none;" ${this.isFetchingReleasesList || this.isFlashing ? 'disabled' : ''} aria-label="Firmware release version">
                ${this.renderReleaseOptions()}
              </select>
            </div>

            <!-- Board Target Dropdown (shown when multiple board binaries exist) -->
            <div id="flasherDeviceSelectContainer" style="display: ${this.availableReleaseAssets && this.availableReleaseAssets.length > 1 ? 'block' : 'none'}; margin-bottom: 0.85rem;">
              <label for="flasherDeviceSelect" class="release-select-label">
                Board target
              </label>
              <select id="flasherDeviceSelect" class="control-select" style="width: 100%; padding: 0.45rem 0.65rem; font-size: 0.82rem; background: var(--bg-input); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); outline: none;" ${this.isFlashing ? 'disabled' : ''} aria-label="Board hardware variant">
                ${this.renderDeviceOptions()}
              </select>
            </div>

            <!-- Metadata Box -->
            <div id="releaseMetadataContainer">
              ${this.renderReleaseMetadata()}
            </div>

            <!-- Secondary Option: Custom file upload -->
            <details class="custom-file-details">
              <summary class="custom-file-summary">Custom file (.bin)</summary>
              <div class="custom-file-content">
                <div class="file-picker-wrapper dropzone" id="fileDropzone" style="padding: 1rem; text-align: center; cursor: pointer; position: relative;">
                  <input type="file" id="firmwareFileInput" class="file-picker-input" accept=".bin,application/octet-stream" ${this.isFlashing ? 'disabled' : ''} aria-label="Select local binary file">
                  <div id="fileDropzoneContent">
                    ${this.renderDropzoneContent()}
                  </div>
                </div>
              </div>
            </details>

            <!-- Erase Checkbox -->
            <div style="margin-top: 0.75rem; display: flex; justify-content: flex-end;">
              <label class="checkbox-label">
                <input type="checkbox" id="eraseBeforeFlashCheck" ${this.eraseBeforeFlash ? 'checked' : ''} ${this.isFlashing ? 'disabled' : ''}>
                <span>Erase entire flash before write</span>
              </label>
            </div>

            <!-- Primary Action Button -->
            <div style="margin-top: 0.85rem;">
              <button id="startFlashBtn" type="button" class="btn-primary btn-flash" style="width: 100%; justify-content: center; height: 42px; font-weight: 600;" ${this.isFlashing || this.isFetchingBinary || this.isFetchingReleasesList ? 'disabled' : ''}>
                <span id="startFlashBtnText">${this.renderFlashButtonText()}</span>
              </button>
            </div>

            <!-- Progress Bar -->
            <div id="flasherProgressContainer" class="flasher-progress-container" style="display: ${this.isFlashing || this.flashProgress > 0 ? 'block' : 'none'}; margin-top: 0.75rem;">
              <div class="progress-meta">
                <span id="flasherProgressStatusText" class="progress-status-text">${this.flashStatusText}</span>
                <span id="flasherProgressPctText" class="progress-pct-text">${this.flashProgress.toFixed(1)}%</span>
              </div>
              <div class="meter-bar flasher-meter-bar" role="progressbar" aria-valuenow="${this.flashProgress.toFixed(0)}" aria-valuemin="0" aria-valuemax="100">
                <div id="flasherProgressBarFill" class="meter-fill meter-flasher" style="width: ${this.flashProgress}%;"></div>
              </div>
              <div class="progress-bytes-meta" id="flasherProgressBytesText">
                ${this.progressDetails.total > 0 ? `${this.formatBytes(this.progressDetails.written)} / ${this.formatBytes(this.progressDetails.total)}` : ''}
              </div>
            </div>
          </div>

          <!-- Section 3: Console Output -->
          <div class="flasher-section flasher-section-console" style="margin-top: 1.25rem; padding-top: 1rem; border-top: 1px solid var(--border-color);">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.4rem;">
              <span style="font-size: 0.8rem; font-weight: 500; color: var(--text-secondary);">Console</span>
              <button id="clearTerminalBtn" type="button" class="btn-sm btn-secondary" title="Clear console">Clear</button>
            </div>
            <div id="flasherTerminalOutput" class="flasher-terminal-output" tabindex="0" role="region" aria-label="Serial console"></div>
          </div>

        </div>

      </div>
    `;

    this.hydrateTerminal();
  }

  initEventListeners() {
    // Connect / Disconnect button
    const connectBtn = this.querySelector('#flasherConnectBtn');
    if (connectBtn) {
      connectBtn.addEventListener('click', () => {
        if (this.isDeviceConnected) {
          this.disconnect();
        } else {
          this.connect();
        }
      });
    }

    // Refresh releases button
    const refreshBtn = this.querySelector('#refreshReleasesBtn');
    if (refreshBtn) {
      refreshBtn.addEventListener('click', () => this.fetchReleasesList());
    }

    // Release select dropdown
    const releaseSelect = this.querySelector('#releaseSelect');
    if (releaseSelect) {
      releaseSelect.addEventListener('change', (e) => {
        const tag = e.target.value;
        const rel = this.releasesList.find(r => r.tag_name === tag);
        if (rel) {
          this.selectedRelease = rel;
          this.releaseTag = rel.tag_name;
          this.resolveReleaseAssets(rel);

          // Clear previously loaded buffer so selected release binary is used
          this.fileSlots[0].fileData = null;
          this.fileSlots[0].fileName = null;
          this.fileSlots[0].fileSize = 0;

          this.appendTerminal(`Selected release ${rel.tag_name}\n`, 'info');
          this.updateUI();
        }
      });
    }

    // Device / Board binary dropdown
    const deviceSelect = this.querySelector('#flasherDeviceSelect');
    if (deviceSelect) {
      deviceSelect.addEventListener('change', (e) => {
        const val = e.target.value;
        const asset = this.availableReleaseAssets.find(a => 
          a.browser_download_url === val || a.name === val
        );
        if (asset) {
          this.selectedAsset = asset;
          this.selectedAssetUrl = asset.browser_download_url;

          this.fileSlots[0].fileData = null;
          this.fileSlots[0].fileName = null;
          this.fileSlots[0].fileSize = 0;

          this.appendTerminal(`Selected board target: ${asset.name}\n`, 'info');
          this.updateUI();
        }
      });
    }

    // Erase before flash checkbox
    const eraseCheck = this.querySelector('#eraseBeforeFlashCheck');
    if (eraseCheck) {
      eraseCheck.addEventListener('change', (e) => {
        this.eraseBeforeFlash = e.target.checked;
      });
    }

    // Start flash button
    const startFlashBtn = this.querySelector('#startFlashBtn');
    if (startFlashBtn) {
      startFlashBtn.addEventListener('click', () => this.handleInstallAction());
    }

    // Terminal clear button
    const clearTerminalBtn = this.querySelector('#clearTerminalBtn');
    if (clearTerminalBtn) {
      clearTerminalBtn.addEventListener('click', () => this.clearTerminal());
    }

    // Dropzone & File Input delegation
    const fileInput = this.querySelector('#firmwareFileInput');
    if (fileInput) {
      fileInput.addEventListener('change', async (e) => {
        const file = e.target.files && e.target.files[0];
        if (file) {
          await this.loadBinaryFile(file);
        }
      });
    }

    const dropzone = this.querySelector('#fileDropzone');
    if (dropzone) {
      dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.classList.add('dragover');
      });

      dropzone.addEventListener('dragleave', (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');
      });

      dropzone.addEventListener('drop', async (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');
        if (this.isFlashing || this.isFetchingBinary) return;

        const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
        if (file) {
          await this.loadBinaryFile(file);
        }
      });
    }

    // Listen for unexpected device disconnect
    if ('serial' in navigator) {
      navigator.serial.addEventListener('disconnect', (event) => {
        if (this.port && event.target === this.port) {
          this.appendTerminal('Serial cable disconnected from computer.\n', 'warning');
          this.disconnect();
        }
      });
    }
  }

  async loadBinaryFile(file) {
    if (!file) return;
    try {
      const buffer = await file.arrayBuffer();
      this.fileSlots[0] = {
        id: 'slot_factory',
        offset: '0x0',
        fileData: new Uint8Array(buffer),
        fileName: file.name,
        fileSize: file.size
      };
      this.selectedRelease = null;
      this.selectedAsset = null;
      this.selectedAssetUrl = null;
      this.appendTerminal(`Selected custom binary [${file.name}] (${this.formatBytes(file.size)}) for flash at 0x0\n`, 'info');
      this.updateUI();
    } catch (err) {
      this.appendTerminal(`Failed to read file ${file.name}: ${err.message}\n`, 'error');
    }
  }

  // ==========================================================================
  // GitHub Release Firmware Operations
  // ==========================================================================
  async fetchReleasesList(repo = this.releaseRepo) {
    const cfg = (typeof window !== 'undefined' && (window.CONFIG || window.BACA_CONFIG)) || null;
    const fallbackRepo = cfg?.repoSlug || (cfg?.repo ? cfg.repo.replace(/^https?:\/\/github\.com\//i, '').replace(/\/+$/, '') : 'khad1r/BacaIn');
    const targetRepo = (repo && repo.trim()) || fallbackRepo;
    this.isFetchingReleasesList = true;
    this.updateUI();

    try {
      this.appendTerminal(`Fetching releases from github.com/${targetRepo}\n`, 'info');

      const res = await fetch(`https://api.github.com/repos/${targetRepo}/releases?per_page=30`);

      if (!res.ok) {
        if (res.status === 403) {
          throw new Error('GitHub API rate limit exceeded (HTTP 403). Select a local .bin file instead.');
        } else if (res.status === 404) {
          throw new Error(`Repository '${targetRepo}' not found.`);
        } else {
          throw new Error(`Failed to fetch releases (HTTP ${res.status}: ${res.statusText})`);
        }
      }

      const list = await res.json();
      if (!Array.isArray(list) || list.length === 0) {
        throw new Error(`No releases found for ${targetRepo}.`);
      }

      const published = list.filter(r => !r.draft);
      const withBin = published.filter(r => 
        Array.isArray(r.assets) && r.assets.some(a => a && typeof a.name === 'string' && a.name.toLowerCase().endsWith('.bin'))
      );

      this.releasesList = withBin.length > 0 ? withBin : published;

      if (this.releasesList.length === 0) {
        throw new Error('No published releases with firmware binary (.bin) assets found.');
      }

      // Default to latest release
      this.selectedRelease = this.releasesList[0];
      this.releaseTag = this.selectedRelease.tag_name;
      this.resolveReleaseAssets(this.selectedRelease);

      this.appendTerminal(`Loaded ${this.releasesList.length} release(s). Selected ${this.selectedRelease.tag_name}.\n`, 'success');
    } catch (err) {
      this.appendTerminal(`[Releases Error] ${err.message}\n`, 'error');
    } finally {
      this.isFetchingReleasesList = false;
      this.updateUI();
    }
  }

  resolveReleaseAssets(release) {
    if (!release || !Array.isArray(release.assets)) {
      this.availableReleaseAssets = [];
      this.selectedAsset = null;
      this.selectedAssetUrl = null;
      return;
    }

    const binAssets = release.assets.filter(a => 
      a && typeof a.name === 'string' && a.name.toLowerCase().endsWith('.bin') && Boolean(a.browser_download_url)
    );

    const factoryAssets = binAssets.filter(a => /^bacain_.*factory\.bin$/i.test(a.name));
    this.availableReleaseAssets = factoryAssets.length > 0 ? factoryAssets : binAssets;

    if (this.availableReleaseAssets.length > 0) {
      this.selectedAsset = this.availableReleaseAssets[0];
      this.selectedAssetUrl = this.selectedAsset.browser_download_url;
    } else {
      this.selectedAsset = null;
      this.selectedAssetUrl = null;
    }
  }

  async handleInstallAction() {
    if (this.isFlashing || this.isFetchingBinary || this.isFetchingReleasesList) return;

    const slot = this.fileSlots && this.fileSlots[0];
    const hasLocalFile = Boolean(slot && slot.fileData && slot.fileData.length > 0 && !this.selectedRelease);

    // If using GitHub release and binary not yet loaded in buffer, download it
    if (!hasLocalFile) {
      if (!this.selectedAsset) {
        this.appendTerminal('No firmware release binary selected.\n', 'warning');
        return;
      }

      const needDownload = !slot || !slot.fileData || slot.fileName !== this.selectedAsset.name;
      if (needDownload) {
        try {
          await this.downloadReleaseAsset(this.selectedAsset);
        } catch (err) {
          return;
        }
      }
    }

    if (!this.hasValidFirmware()) {
      this.appendTerminal('No valid firmware loaded to flash.\n', 'warning');
      return;
    }

    if (!this.isDeviceConnected || !this.esploader) {
      this.appendTerminal('Opening serial port selection...\n', 'info');
      await this.connect();
      if (!this.isDeviceConnected || !this.esploader) {
        this.appendTerminal('Flash cancelled: device not connected.\n', 'warning');
        return;
      }
    }

    await this.startFlashing();
  }

  async fetchLatestFirmwareFromRelease(repo = this.releaseRepo, tag = null) {
    const cfg = (typeof window !== 'undefined' && (window.CONFIG || window.BACA_CONFIG)) || null;
    const fallbackRepo = cfg?.repoSlug || (cfg?.repo ? cfg.repo.replace(/^https?:\/\/github\.com\//i, '').replace(/\/+$/, '') : 'khad1r/BacaIn');
    const targetRepo = (repo && repo.trim()) || fallbackRepo;
    const targetTag = (typeof tag === 'string' && tag.trim().length > 0) ? tag.trim() : null;

    this.isFetchingBinary = true;
    this.isFetchingRelease = true;
    this.updateUI();

    let targetAssetToDownload = null;

    try {
      this.appendTerminal(`Fetching release ${targetTag || 'latest'} from GitHub (${targetRepo})\n`, 'info');

      const releaseUrl = targetTag
        ? `https://api.github.com/repos/${targetRepo}/releases/tags/${encodeURIComponent(targetTag)}`
        : `https://api.github.com/repos/${targetRepo}/releases/latest`;

      const releaseRes = await fetch(releaseUrl);
      if (!releaseRes.ok) {
        if (releaseRes.status === 404) {
          throw new Error(`Release not found for repository '${targetRepo}'.`);
        } else if (releaseRes.status === 403) {
          throw new Error('GitHub API rate limit exceeded (HTTP 403).');
        } else {
          throw new Error(`Failed to fetch release (HTTP ${releaseRes.status}: ${releaseRes.statusText}).`);
        }
      }

      const releaseData = await releaseRes.json();
      this.selectedRelease = releaseData;
      this.releaseTag = releaseData.tag_name;
      this.resolveReleaseAssets(releaseData);

      if (this.availableReleaseAssets.length === 0) {
        throw new Error('No .bin firmware assets found in release.');
      }

      if (this.availableReleaseAssets.length === 1) {
        targetAssetToDownload = this.availableReleaseAssets[0];
      } else {
        this.appendTerminal(`Found ${this.availableReleaseAssets.length} board binaries in this release. Select board target.\n`, 'info');
        return this.availableReleaseAssets;
      }
    } catch (err) {
      this.appendTerminal(`[GitHub Release Error] ${err.message}\n`, 'error');
      throw err;
    } finally {
      this.isFetchingBinary = false;
      this.isFetchingRelease = false;
      this.updateUI();
    }

    if (targetAssetToDownload) {
      return await this.downloadReleaseAsset(targetAssetToDownload);
    }
  }

  async downloadReleaseAsset(asset) {
    if (!asset || !asset.browser_download_url) {
      const err = new Error('Invalid release asset: download URL missing');
      this.appendTerminal(`[Download Error] ${err.message}\n`, 'error');
      throw err;
    }

    this.isFetchingBinary = true;
    this.isFetchingRelease = true;
    this.selectedAsset = asset;
    this.selectedAssetUrl = asset.browser_download_url;
    this.updateUI();

    try {
      this.appendTerminal(`Downloading ${asset.name} (${this.formatBytes(asset.size || 0)})...\n`, 'info');

      const downloadRes = await fetch(asset.browser_download_url);
      if (!downloadRes.ok) {
        throw new Error(`Download failed (HTTP ${downloadRes.status}: ${downloadRes.statusText})`);
      }

      const buffer = await downloadRes.arrayBuffer();

      this.fileSlots[0] = {
        id: 'slot_factory',
        offset: '0x0',
        fileData: new Uint8Array(buffer),
        fileName: asset.name,
        fileSize: buffer.byteLength
      };

      this.appendTerminal(`Firmware ${asset.name} (${this.formatBytes(buffer.byteLength)}) loaded. Ready to flash at 0x0.\n`, 'success');
      this.updateUI();
      return this.fileSlots[0];
    } catch (err) {
      this.appendTerminal(`[Download Error] ${err.message}\n`, 'error');
      throw err;
    } finally {
      this.isFetchingBinary = false;
      this.isFetchingRelease = false;
      this.updateUI();
    }
  }

  async selectDevice(nameOrUrl) {
    if (!nameOrUrl) return;
    const asset = this.availableReleaseAssets.find(a => 
      a.browser_download_url === nameOrUrl || 
      a.name === nameOrUrl ||
      (a.browser_download_url && a.browser_download_url.endsWith('/' + nameOrUrl))
    );
    if (!asset) {
      throw new Error(`Device firmware '${nameOrUrl}' not found.`);
    }
    this.selectedAsset = asset;
    this.selectedAssetUrl = asset.browser_download_url;
    const selectEl = this.querySelector('#flasherDeviceSelect');
    if (selectEl) {
      selectEl.value = asset.browser_download_url;
    }
    return await this.downloadReleaseAsset(asset);
  }

  // ==========================================================================
  // Web Serial Connection Operations
  // ==========================================================================
  async connect() {
    if (!('serial' in navigator)) {
      this.appendTerminal('Web Serial API is not supported in this browser.\n', 'error');
      return;
    }

    this.isConnecting = true;
    this.updateUI();

    try {
      this.appendTerminal('Requesting serial port... Select your ESP32-S3 device.\n', 'info');

      let port;
      try {
        port = await navigator.serial.requestPort({ filters: SERIAL_DEVICE_FILTERS });
      } catch (filterErr) {
        if (filterErr.name === 'NotFoundError') {
          this.appendTerminal('Serial port selection cancelled by user.\n', 'warning');
          this.isConnecting = false;
          this.updateUI();
          return;
        }
        port = await navigator.serial.requestPort();
      }

      this.port = port;
      this.appendTerminal('Loading esptool-js flashing engine...\n', 'info');
      const { ESPLoader, Transport } = await loadEsptool();

      this.appendTerminal('Initializing WebSerial transport...\n', 'info');
      this.transport = new Transport(this.port, true);

      const targetBaud = this.selectedBaudrate || 460800;
      this.appendTerminal(`Opening connection (Flash: ${targetBaud} bps)...\n`, 'info');

      const termObj = {
        clean: () => this.clearTerminal(),
        writeLine: (data) => this.appendTerminal(data + '\n'),
        write: (data) => this.appendTerminal(data)
      };

      this.esploader = new ESPLoader({
        transport: this.transport,
        baudrate: targetBaud,
        romBaudrate: 115200,
        terminal: termObj
      });

      this.appendTerminal('Connecting to ROM bootloader...\n', 'info');
      const chip = await this.esploader.main();

      this.chipName = chip || (this.esploader.chip ? this.esploader.chip.CHIP_NAME : 'ESP32-S3');
      this.appendTerminal(`Connected to ${this.chipName}\n`, 'success');

      // Query MAC Address
      try {
        if (this.esploader.chip && typeof this.esploader.chip.readMac === 'function') {
          this.macAddress = await this.esploader.chip.readMac(this.esploader);
          this.appendTerminal(`MAC Address: ${this.macAddress}\n`, 'info');
        }
      } catch (macErr) {
        // Non-fatal
      }

      // Query Chip Model / Description
      try {
        if (this.esploader.chip && typeof this.esploader.chip.getChipDescription === 'function') {
          const desc = await this.esploader.chip.getChipDescription(this.esploader);
          if (desc) {
            this.chipDesc = desc;
          }
        }
      } catch (descErr) {
        // Non-fatal
      }

      this.isDeviceConnected = true;
      this.isConnecting = false;
      this.updateUI();

    } catch (err) {
      this.isDeviceConnected = false;
      this.isConnecting = false;
      this.appendTerminal(`Serial Connection Error: ${err.message}\n`, 'error');
      await this.cleanUpConnection();
      this.updateUI();
    }
  }

  async disconnect() {
    this.appendTerminal('Disconnecting from serial port...\n', 'info');
    await this.cleanUpConnection();
    this.isDeviceConnected = false;
    this.isConnecting = false;
    this.isFlashing = false;
    this.appendTerminal('Disconnected from device.\n', 'info');
    this.updateUI();
  }

  async cleanUpConnection() {
    if (this.transport) {
      try {
        await this.transport.disconnect();
      } catch (e) {
        console.warn('[EspWebFlasher] Error during transport disconnect:', e);
      }
      this.transport = null;
    }
    this.esploader = null;
    this.port = null;
    this.chipName = null;
    this.macAddress = null;
  }

  async rebootDevice() {
    if (!this.esploader) {
      this.appendTerminal('Cannot reboot: device is not connected.\n', 'error');
      return;
    }
    try {
      this.appendTerminal('Triggering hardware reset via DTR/RTS signals...\n', 'info');
      await this.esploader.after('hard_reset');
      this.appendTerminal('Hard reset sent. Device is booting.\n', 'success');
    } catch (err) {
      this.appendTerminal(`Failed to reset device: ${err.message}\n`, 'error');
    }
  }

  async eraseEntireFlash() {
    if (!this.esploader) {
      this.appendTerminal('Cannot erase: device is not connected.\n', 'error');
      return;
    }

    try {
      this.isFlashing = true;
      this.flashStatusText = 'Erasing flash memory...';
      this.flashProgress = 0;
      this.updateUI();

      this.appendTerminal('Erasing entire flash chip... Please wait.\n', 'warning');
      await this.esploader.eraseFlash();
      this.appendTerminal('Flash memory erased completely.\n', 'success');

    } catch (err) {
      this.appendTerminal(`Erase flash failed: ${err.message}\n`, 'error');
    } finally {
      this.isFlashing = false;
      this.flashStatusText = 'Ready';
      this.updateUI();
    }
  }

  // ==========================================================================
  // Flash Firmware Execution (Zero Mock - Single Combined Binary @ 0x0)
  // ==========================================================================
  async startFlashing() {
    if (!this.isDeviceConnected || !this.esploader) {
      this.appendTerminal('Device not connected. Opening serial port selection...\n', 'info');
      await this.connect();
      if (!this.isDeviceConnected || !this.esploader) {
        this.appendTerminal('Flashing cancelled: serial connection not established.\n', 'warning');
        return;
      }
    }

    const slot = this.fileSlots && this.fileSlots[0];
    if (!slot || !slot.fileData || slot.fileData.length === 0) {
      const msg = 'No firmware binary loaded. Select a release or custom .bin file.';
      this.appendTerminal(msg + '\n', 'warning');
      return;
    }

    const filesToFlash = [
      {
        data: slot.fileData,
        address: 0x0,
        name: slot.fileName || 'bacain_factory.bin'
      }
    ];

    const totalBytesAll = slot.fileData.length;

    this.isFlashing = true;
    this.flashProgress = 0;
    this.flashStatusText = 'Preparing flash parameters...';
    this.progressDetails = {
      written: 0,
      total: totalBytesAll,
      fileIndex: 1,
      totalFiles: 1
    };
    this.updateUI();

    this.appendTerminal('\n-------------------------------------------\n', 'info');
    this.appendTerminal(`Starting firmware flash: ${filesToFlash[0].name} (${this.formatBytes(totalBytesAll)}) at 0x0\n`, 'info');
    this.appendTerminal('-------------------------------------------\n\n', 'info');

    try {
      const flashOptions = {
        fileArray: filesToFlash.map(f => ({ data: f.data, address: f.address })),
        flashSize: 'keep',
        flashMode: 'keep',
        flashFreq: 'keep',
        eraseAll: this.eraseBeforeFlash,
        compress: true,
        reportProgress: (fileIndex, written, total) => {
          // Zero-Mock: Pure real progress computation from esptool-js written bytes
          const overallWritten = written;
          const pct = Math.min(100, Math.max(0, (overallWritten / totalBytesAll) * 100));

          this.flashProgress = pct;
          this.flashStatusText = `Writing ${filesToFlash[0].name}...`;
          this.progressDetails = {
            written: overallWritten,
            total: totalBytesAll,
            fileIndex: 1,
            totalFiles: 1
          };

          this.updateProgressBarUI();
        }
      };

      await this.esploader.writeFlash(flashOptions);

      // Flashing finished
      this.flashProgress = 100;
      this.flashStatusText = 'Flashing complete';
      this.progressDetails.written = totalBytesAll;
      this.updateProgressBarUI();

      this.appendTerminal('\nFlash write completed successfully.\n', 'success');
      this.appendTerminal(`Wrote ${this.formatBytes(totalBytesAll)} at offset 0x0.\n`, 'success');

    } catch (flashErr) {
      this.appendTerminal(`Flash Error: ${flashErr.message}\n`, 'error');
      this.flashStatusText = `Error: ${flashErr.message}`;
    } finally {
      this.isFlashing = false;
      this.updateUI();
    }
  }

  async startFlashProcess() {
    return this.startFlashing();
  }

  updateProgressBarUI() {
    const container = this.querySelector('#flasherProgressContainer');
    const fill = this.querySelector('#flasherProgressBarFill');
    const pctText = this.querySelector('#flasherProgressPctText');
    const statusText = this.querySelector('#flasherProgressStatusText');
    const bytesText = this.querySelector('#flasherProgressBytesText');

    if (container) container.style.display = 'block';
    if (fill) fill.style.width = `${this.flashProgress.toFixed(1)}%`;
    if (pctText) pctText.textContent = `${this.flashProgress.toFixed(1)}%`;
    if (statusText) statusText.textContent = this.flashStatusText;
    if (bytesText && this.progressDetails.total > 0) {
      bytesText.textContent = `${this.formatBytes(this.progressDetails.written)} / ${this.formatBytes(this.progressDetails.total)}`;
    }
  }

  // ==========================================================================
  // Terminal / Logging Helpers
  // ==========================================================================
  appendTerminal(rawText, type = 'normal') {
    // Strip ANSI escape codes
    const cleanText = rawText.replace(/\x1B\[[0-9;]*[a-zA-Z]/g, '');

    this.terminalLogs.push({ text: cleanText, type });
    if (this.terminalLogs.length > 500) {
      this.terminalLogs.shift();
    }

    const termEl = this.querySelector('#flasherTerminalOutput');
    if (termEl) {
      const span = document.createElement('span');
      span.className = `term-line ${type}`;
      span.textContent = cleanText;
      termEl.appendChild(span);

      if (this.autoScroll) {
        termEl.scrollTop = termEl.scrollHeight;
      }

      while (termEl.childElementCount > 500) {
        termEl.removeChild(termEl.firstChild);
      }
    }
  }

  hydrateTerminal() {
    const termEl = this.querySelector('#flasherTerminalOutput');
    if (!termEl) return;

    termEl.innerHTML = '';
    const fragment = document.createDocumentFragment();
    for (const log of this.terminalLogs) {
      const span = document.createElement('span');
      span.className = `term-line ${log.type}`;
      span.textContent = log.text;
      fragment.appendChild(span);
    }
    termEl.appendChild(fragment);

    if (this.autoScroll) {
      termEl.scrollTop = termEl.scrollHeight;
    }
  }

  clearTerminal() {
    this.terminalLogs = [];
    const termEl = this.querySelector('#flasherTerminalOutput');
    if (termEl) termEl.innerHTML = '';
  }

  formatBytes(bytes) {
    if (isNaN(bytes) || bytes === null || bytes < 0) return '0 B';
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  }

  formatDate(isoStr) {
    if (!isoStr) return '';
    try {
      const d = new Date(isoStr);
      if (isNaN(d.getTime())) return '';
      return d.toISOString().split('T')[0];
    } catch {
      return '';
    }
  }

  escapeHtml(str) {
    if (!str) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}

// Register Custom Element
if (!customElements.get('esp-web-flasher')) {
  customElements.define('esp-web-flasher', EspWebFlasher);
}

export { EspWebFlasher };
