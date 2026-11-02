/**
 * Mini-E-Reader ESP32-S3 Web Serial Firmware Flasher
 * Standard Vanilla JS Web Component (Custom Elements v1)
 * Powered by esptool-js & Web Serial API
 * 
 * Rules: AGENTS.md (Zero-Mock, Zero Silent Fallback, Ponytail)
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
    console.warn('[EspWebFlasher] Failed to import from unpkg, trying esm.sh...', err1);
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

    // State Variables
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

    // Settings
    this.selectedBaudrate = 460800; // Standard recommended baudrate for ESP32-S3
    this.eraseBeforeFlash = false;
    this.autoScroll = true;

    // Binary file slot: Combined factory firmware @ 0x0
    this.fileSlots = [
      {
        id: 'slot_factory',
        offset: '0x0',
        fileData: null,
        fileName: null,
        fileSize: 0
      }
    ];

    // GitHub Release options
    this.releaseRepo = 'khad1r/Mini-E-Reader';
    this.releaseTag = '';
    this.isFetchingRelease = false;
    this.showCustomRepo = false;

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
        'Web Serial API is NOT supported in this browser. Please use Chrome, Edge, or Opera on Desktop.\n',
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
    const hasFile = slot && slot.fileData && slot.fileData.length > 0;
    if (hasFile) {
      return `
        <div style="font-size: 0.88rem; font-weight: 600; color: var(--text-primary); margin-bottom: 0.2rem;">
          ${this.escapeHtml(slot.fileName)} (${this.formatBytes(slot.fileSize)})
        </div>
        <div style="font-size: 0.76rem; color: var(--text-secondary);">
          Siap di-flash ke offset 0x0
        </div>
      `;
    }
    return `
      <div style="font-size: 0.88rem; font-weight: 500; color: var(--text-primary); margin-bottom: 0.2rem;">
        Pilih atau letakkan <code>bacain_factory.bin</code> (flash di 0x0)
      </div>
      <div style="font-size: 0.76rem; color: var(--text-secondary);">
        Klik atau seret file firmware ke area ini
      </div>
    `;
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
        label.textContent = this.isDeviceConnected ? 'Disconnect Serial' : (this.isConnecting ? 'Connecting...' : 'Connect Serial Port');
      }
    }

    // Device Info Container
    const infoContainer = this.querySelector('#flasherDeviceInfoContainer');
    if (infoContainer) {
      infoContainer.innerHTML = this.renderDeviceInfo();
    }

    // Dropzone Content
    const dropzoneContent = this.querySelector('#fileDropzoneContent');
    if (dropzoneContent) {
      dropzoneContent.innerHTML = this.renderDropzoneContent();
    }

    const fileInput = this.querySelector('#firmwareFileInput');
    if (fileInput) {
      fileInput.disabled = this.isFlashing || this.isFetchingRelease;
    }

    const eraseCheck = this.querySelector('#eraseBeforeFlashCheck');
    if (eraseCheck) {
      eraseCheck.disabled = this.isFlashing || this.isFetchingRelease;
    }

    // GitHub Release Controls
    const fetchReleaseBtn = this.querySelector('#fetchReleaseBtn');
    if (fetchReleaseBtn) {
      fetchReleaseBtn.disabled = this.isFlashing || this.isFetchingRelease;
      const releaseBtnText = this.querySelector('#fetchReleaseBtnText');
      if (releaseBtnText) {
        releaseBtnText.textContent = this.isFetchingRelease
          ? 'Mengunduh Firmware...'
          : 'Unduh Firmware Terbaru dari GitHub Release';
      }
    }

    const customContainer = this.querySelector('#customReleaseContainer');
    if (customContainer) {
      customContainer.style.display = this.showCustomRepo ? 'block' : 'none';
    }

    const toggleCustomBtn = this.querySelector('#toggleCustomReleaseBtn');
    if (toggleCustomBtn) {
      toggleCustomBtn.textContent = this.showCustomRepo ? 'Tutup Pengaturan Repo' : 'Custom Repo / Tag';
    }

    const fetchCustomBtn = this.querySelector('#fetchCustomReleaseBtn');
    if (fetchCustomBtn) {
      fetchCustomBtn.disabled = this.isFlashing || this.isFetchingRelease;
    }

    // Flash Action Button
    const startFlashBtn = this.querySelector('#startFlashBtn');
    if (startFlashBtn) {
      const hasFiles = this.hasValidFirmware();
      startFlashBtn.disabled = this.isFlashing || this.isFetchingRelease || !hasFiles;
      const flashLabel = this.querySelector('#startFlashBtnText');
      if (flashLabel) {
        flashLabel.textContent = this.isFlashing ? 'Writing to ESP32 Flash...' : 'Flash Firmware';
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
        <div id="serialCompatAlert" class="alert-banner alert-warning" style="display: ${isSupported ? 'none' : 'flex'};">
          <div>
            <strong>Web Serial API Not Supported:</strong> Your current browser cannot communicate directly with serial devices.
            Please open this page in <strong>Google Chrome</strong>, <strong>Microsoft Edge</strong>, or <strong>Opera</strong> on Desktop.
          </div>
        </div>

        <!-- 1 Unified Main Card -->
        <div class="card flasher-card">

          <!-- Bagian 1: Koneksi -->
          <div class="flasher-section flasher-section-connection">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.75rem;">
              <span style="font-weight: 600; font-size: 0.95rem;">Serial Connection</span>
              <span id="flasherStatusBadge" class="card-badge ${this.isDeviceConnected ? 'badge-success' : 'badge-muted'}">
                ${this.isDeviceConnected ? 'Connected' : (this.isConnecting ? 'Connecting...' : 'Disconnected')}
              </span>
            </div>

            <div style="display: flex; gap: 0.5rem; align-items: center; flex-wrap: wrap;">
              <button id="flasherConnectBtn" class="btn-connect ${this.isDeviceConnected ? 'connected' : ''}" ${this.isConnecting || this.isFlashing ? 'disabled' : ''}>
                <span class="status-dot ${this.isDeviceConnected ? 'connected' : (this.isConnecting ? 'connecting' : 'disconnected')}"></span>
                <span id="flasherConnectBtnText">${this.isDeviceConnected ? 'Disconnect Serial' : (this.isConnecting ? 'Connecting...' : 'Connect Serial Port')}</span>
              </button>
            </div>

            <div id="flasherDeviceInfoContainer">
              ${this.renderDeviceInfo()}
            </div>
          </div>

          <!-- Bagian 2: Pilih Firmware -->
          <div class="flasher-section flasher-section-firmware" style="margin-top: 1.25rem; padding-top: 1rem; border-top: 1px solid var(--border-color);">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.6rem;">
              <span style="font-weight: 600; font-size: 0.95rem;">Firmware</span>
              <button id="toggleCustomReleaseBtn" type="button" class="btn-sm btn-secondary" style="font-size: 0.75rem; cursor: pointer;">
                ${this.showCustomRepo ? 'Tutup Pengaturan Repo' : 'Custom Repo / Tag'}
              </button>
            </div>

            <!-- GitHub Release Action -->
            <div style="margin-bottom: 0.75rem;">
              <button id="fetchReleaseBtn" type="button" class="btn-primary" style="padding: 0.5rem 0.85rem; font-size: 0.82rem; display: inline-flex; align-items: center; justify-content: center; gap: 0.45rem; cursor: pointer; width: 100%; border-radius: var(--radius-sm);" ${this.isFlashing || this.isFetchingRelease ? 'disabled' : ''}>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width: 15px; height: 15px;">
                  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                  <polyline points="7 10 12 15 17 10"></polyline>
                  <line x1="12" y1="15" x2="12" y2="3"></line>
                </svg>
                <span id="fetchReleaseBtnText">${this.isFetchingRelease ? 'Mengunduh Firmware...' : 'Unduh Firmware Terbaru dari GitHub Release'}</span>
              </button>

              <!-- Custom Repo & Tag Input Form -->
              <div id="customReleaseContainer" style="display: ${this.showCustomRepo ? 'block' : 'none'}; margin-top: 0.6rem; padding: 0.65rem 0.75rem; background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: var(--radius-sm);">
                <div style="font-size: 0.75rem; color: var(--text-secondary); margin-bottom: 0.4rem;">
                  GitHub Repository & Release Tag:
                </div>
                <div style="display: flex; gap: 0.5rem; align-items: center; flex-wrap: wrap;">
                  <input type="text" id="customReleaseRepoInput" placeholder="khad1r/Mini-E-Reader" value="${this.escapeHtml(this.releaseRepo)}" style="flex: 1; min-width: 170px; padding: 0.4rem 0.6rem; font-size: 0.8rem; background: var(--bg-input); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); outline: none;">
                  <input type="text" id="customReleaseTagInput" placeholder="Tag (kosong = latest)" value="${this.escapeHtml(this.releaseTag)}" style="width: 130px; padding: 0.4rem 0.6rem; font-size: 0.8rem; background: var(--bg-input); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); outline: none;">
                  <button id="fetchCustomReleaseBtn" type="button" class="btn-sm btn-primary" style="padding: 0.4rem 0.75rem; font-size: 0.8rem; cursor: pointer;" ${this.isFlashing || this.isFetchingRelease ? 'disabled' : ''}>
                    Ambil
                  </button>
                </div>
              </div>
            </div>

            <!-- Divider -->
            <div style="display: flex; align-items: center; gap: 0.6rem; margin: 0.85rem 0 0.65rem 0; color: var(--text-muted); font-size: 0.76rem;">
              <span style="flex: 1; height: 1px; background: var(--border-color);"></span>
              <span>atau upload file lokal</span>
              <span style="flex: 1; height: 1px; background: var(--border-color);"></span>
            </div>

            <div class="file-picker-wrapper dropzone" id="fileDropzone" style="padding: 1.25rem 1rem; text-align: center; cursor: pointer; position: relative;">
              <input type="file" id="firmwareFileInput" class="file-picker-input" accept=".bin,application/octet-stream" ${this.isFlashing || this.isFetchingRelease ? 'disabled' : ''}>
              <div id="fileDropzoneContent">
                ${this.renderDropzoneContent()}
              </div>
            </div>

            <div style="margin-top: 0.6rem; display: flex; justify-content: flex-end;">
              <label class="checkbox-label">
                <input type="checkbox" id="eraseBeforeFlashCheck" ${this.eraseBeforeFlash ? 'checked' : ''} ${this.isFlashing || this.isFetchingRelease ? 'disabled' : ''}>
                <span>Erase flash sebelum menulis</span>
              </label>
            </div>
          </div>

          <!-- Bagian 3: Flash & Progress -->
          <div class="flasher-section flasher-section-actions" style="margin-top: 1.25rem; padding-top: 1rem; border-top: 1px solid var(--border-color);">
            <button id="startFlashBtn" class="btn-primary btn-flash" style="width: 100%; justify-content: center;" ${this.isFlashing || !this.hasValidFirmware() ? 'disabled' : ''}>
              <span id="startFlashBtnText">${this.isFlashing ? 'Writing to ESP32 Flash...' : 'Flash Firmware'}</span>
            </button>

            <!-- Progress Bar (hanya muncul saat flashing / setelah selesai) -->
            <div id="flasherProgressContainer" class="flasher-progress-container" style="display: ${this.isFlashing || this.flashProgress > 0 ? 'block' : 'none'}; margin-top: 0.75rem;">
              <div class="progress-meta">
                <span id="flasherProgressStatusText" class="progress-status-text">${this.flashStatusText}</span>
                <span id="flasherProgressPctText" class="progress-pct-text">${this.flashProgress.toFixed(1)}%</span>
              </div>
              <div class="meter-bar flasher-meter-bar">
                <div id="flasherProgressBarFill" class="meter-fill meter-flasher" style="width: ${this.flashProgress}%;"></div>
              </div>
              <div class="progress-bytes-meta" id="flasherProgressBytesText">
                ${this.progressDetails.total > 0 ? `${this.formatBytes(this.progressDetails.written)} / ${this.formatBytes(this.progressDetails.total)}` : ''}
              </div>
            </div>
          </div>

          <!-- Bagian 4: Log Konsol -->
          <div class="flasher-section flasher-section-console" style="margin-top: 1.25rem; padding-top: 1rem; border-top: 1px solid var(--border-color);">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.4rem;">
              <span style="font-size: 0.82rem; font-weight: 500; color: var(--text-secondary);">Log Konsol</span>
              <button id="clearTerminalBtn" class="btn-sm btn-secondary" title="Clear console">Clear</button>
            </div>
            <div id="flasherTerminalOutput" class="flasher-terminal-output" tabindex="0" role="region" aria-label="Serial Console"></div>
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
      startFlashBtn.addEventListener('click', () => this.startFlashProcess());
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
        if (this.isFlashing || this.isFetchingRelease) return;

        const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
        if (file) {
          await this.loadBinaryFile(file);
        }
      });
    }

    // GitHub Release Fetch button
    const fetchReleaseBtn = this.querySelector('#fetchReleaseBtn');
    if (fetchReleaseBtn) {
      fetchReleaseBtn.addEventListener('click', async () => {
        try {
          await this.fetchLatestFirmwareFromRelease(this.releaseRepo);
        } catch (err) {
          // Handled and logged to terminal
        }
      });
    }

    // Toggle custom repo/tag settings
    const toggleCustomBtn = this.querySelector('#toggleCustomReleaseBtn');
    if (toggleCustomBtn) {
      toggleCustomBtn.addEventListener('click', () => {
        this.showCustomRepo = !this.showCustomRepo;
        this.updateUI();
      });
    }

    // Fetch custom repo/tag button
    const fetchCustomBtn = this.querySelector('#fetchCustomReleaseBtn');
    if (fetchCustomBtn) {
      fetchCustomBtn.addEventListener('click', async () => {
        const repoInput = this.querySelector('#customReleaseRepoInput');
        const tagInput = this.querySelector('#customReleaseTagInput');
        const customRepo = repoInput ? repoInput.value.trim() : '';
        const customTag = tagInput ? tagInput.value.trim() : '';
        if (customRepo) {
          this.releaseRepo = customRepo;
        }
        this.releaseTag = customTag;
        try {
          await this.fetchLatestFirmwareFromRelease(this.releaseRepo, this.releaseTag || null);
        } catch (err) {
          // Handled and logged to terminal
        }
      });
    }

    const repoInput = this.querySelector('#customReleaseRepoInput');
    const tagInput = this.querySelector('#customReleaseTagInput');
    const onEnter = (e) => {
      if (e.key === 'Enter') {
        const btn = this.querySelector('#fetchCustomReleaseBtn');
        if (btn) btn.click();
      }
    };
    if (repoInput) repoInput.addEventListener('keydown', onEnter);
    if (tagInput) tagInput.addEventListener('keydown', onEnter);

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
      this.appendTerminal(`Selected combined firmware binary [${file.name}] (${this.formatBytes(file.size)}) for flash @ 0x0\n`, 'info');
      this.updateUI();
    } catch (err) {
      this.appendTerminal(`Failed to read file ${file.name}: ${err.message}\n`, 'error');
    }
  }

  // ==========================================================================
  // GitHub Release Firmware Operations
  // ==========================================================================
  async fetchLatestFirmwareFromRelease(repo = 'khad1r/Mini-E-Reader', tag = null) {
    const targetRepo = (repo && repo.trim()) || 'khad1r/Mini-E-Reader';
    const targetTag = (typeof tag === 'string' && tag.trim().length > 0) ? tag.trim() : null;

    this.isFetchingRelease = true;
    this.updateUI();

    try {
      if (targetTag) {
        this.appendTerminal(`Mengambil informasi rilis ${targetTag} dari GitHub (${targetRepo})...\n`, 'info');
      } else {
        this.appendTerminal(`Mengambil informasi rilis terbaru dari GitHub (${targetRepo})...\n`, 'info');
      }

      const releaseUrl = targetTag
        ? `https://api.github.com/repos/${targetRepo}/releases/tags/${encodeURIComponent(targetTag)}`
        : `https://api.github.com/repos/${targetRepo}/releases/latest`;

      const releaseRes = await fetch(releaseUrl);
      if (!releaseRes.ok) {
        if (releaseRes.status === 404) {
          throw new Error(`Rilis GitHub tidak ditemukan untuk repository '${targetRepo}'.`);
        } else if (releaseRes.status === 403) {
          throw new Error('GitHub API rate limit terlampaui atau akses dibatasi (HTTP 403).');
        } else {
          throw new Error(`Gagal mengambil data release dari GitHub (HTTP ${releaseRes.status}: ${releaseRes.statusText}).`);
        }
      }

      const releaseData = await releaseRes.json();
      const assets = Array.isArray(releaseData.assets) ? releaseData.assets : [];

      let asset = assets.find(a => a && a.name === 'bacain_factory.bin');
      if (!asset) {
        asset = assets.find(a => a && typeof a.name === 'string' && a.name.endsWith('.bin'));
      }

      if (!asset || !asset.browser_download_url) {
        throw new Error('Asset firmware .bin tidak ditemukan pada rilis terbaru.');
      }

      this.appendTerminal(`Mengunduh firmware asset: ${asset.name} (${this.formatBytes(asset.size || 0)})...\n`, 'info');

      const downloadRes = await fetch(asset.browser_download_url);
      if (!downloadRes.ok) {
        throw new Error(`Gagal mengunduh file binary firmware dari GitHub (HTTP ${downloadRes.status}: ${downloadRes.statusText}).`);
      }

      const buffer = await downloadRes.arrayBuffer();

      this.fileSlots[0] = {
        id: 'slot_factory',
        offset: '0x0',
        fileData: new Uint8Array(buffer),
        fileName: asset.name,
        fileSize: buffer.byteLength
      };

      this.appendTerminal(`[GitHub Release] Firmware ${asset.name} (${this.formatBytes(buffer.byteLength)}) berhasil dimuat. Siap di-flash.\n`, 'success');
      this.updateUI();
      return this.fileSlots[0];
    } catch (err) {
      this.appendTerminal(`[GitHub Release Error] ${err.message}\n`, 'error');
      throw err;
    } finally {
      this.isFetchingRelease = false;
      this.updateUI();
    }
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
      this.appendTerminal('Requesting Serial Port... Please select your ESP32-S3 device.\n', 'info');

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

      this.appendTerminal('Initializing WebSerial Transport...\n', 'info');
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
      this.appendTerminal('Hard reset sent. Device is booting into application!\n', 'success');
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
      this.appendTerminal('Device not connected. Opening serial port selection dialog...\n', 'info');
      await this.connect();
      if (!this.isDeviceConnected || !this.esploader) {
        this.appendTerminal('Flashing cancelled: serial connection not established.\n', 'warning');
        return;
      }
    }

    const slot = this.fileSlots && this.fileSlots[0];
    if (!slot || !slot.fileData || slot.fileData.length === 0) {
      const msg = 'Pilih atau letakkan bacain_factory.bin (flash di 0x0).';
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

    this.appendTerminal('\n===========================================\n', 'info');
    this.appendTerminal(`Starting firmware flash: ${filesToFlash[0].name} (${this.formatBytes(totalBytesAll)}) @ 0x0\n`, 'info');
    this.appendTerminal('===========================================\n\n', 'info');

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
          this.flashStatusText = `Writing ${filesToFlash[0].name} to ESP32 Flash...`;
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
      this.flashStatusText = 'Flashing Complete!';
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
customElements.define('esp-web-flasher', EspWebFlasher);

export { EspWebFlasher };
