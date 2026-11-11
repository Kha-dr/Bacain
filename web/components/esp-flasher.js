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
    const mod = await import("https://unpkg.com/esptool-js@0.5.4/bundle.js");
    esptoolCache = mod;
    return mod;
  } catch (err1) {
    try {
      const mod = await import("https://esm.sh/esptool-js@0.5.4");
      esptoolCache = mod;
      return mod;
    } catch (err2) {
      throw new Error(
        `Failed to load esptool-js library: ${err1.message}; ${err2.message}`,
      );
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

    const cfg =
      (typeof window !== "undefined" &&
        (window.CONFIG || window.BACA_CONFIG)) ||
      null;
    const defaultRepoSlug =
      cfg?.repoSlug ||
      (cfg?.repo
        ? cfg.repo.replace(/^https?:\/\/github\.com\//i, "").replace(/\/+$/, "")
        : "Kha-dr/BacaIn");

    // Settings
    this.selectedBaudrate = cfg?.flasher?.baud || 460800;
    this.preserveContent = true;
    this.eraseBeforeFlash = false;
    this.autoScroll = true;

    // Binary file slot: Combined factory firmware @ 0x0
    this.fileSlots = [
      {
        id: "slot_factory",
        offset: cfg?.flasher?.offset || "0x0",
        fileData: null,
        fileName: null,
        fileSize: 0,
      },
    ];

    // GitHub Release options
    this.releaseRepo = defaultRepoSlug;
    this.releaseTag = "";
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
    this.flashStatusText = "Ready";
    this.progressDetails = {
      written: 0,
      total: 0,
      fileIndex: 0,
      totalFiles: 0,
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
    const isSupported = "serial" in navigator;
    const warningEl = this.querySelector("#serialCompatAlert");
    const connectBtn = this.querySelector("#flasherConnectBtn");

    if (!isSupported) {
      if (warningEl) warningEl.style.display = "flex";
      if (connectBtn) connectBtn.disabled = true;
      this.appendTerminal(
        "Web Serial API is not supported in this browser. Please use Chrome, Edge, or Opera on Desktop.\n",
        "error",
      );
    } else {
      if (warningEl) warningEl.style.display = "none";
      if (connectBtn) connectBtn.disabled = false;
    }
  }

  renderDeviceInfo() {
    if (!this.isDeviceConnected) return "";
    return `
      <div style="margin-top: 0.6rem; font-size: 0.82rem; color: var(--text-secondary);">
        Device: <strong style="color: var(--text-primary);">${this.chipName || "ESP32-S3"}</strong>
        ${this.macAddress ? ` &bull; MAC: <span class="mono">${this.macAddress}</span>` : ""}
      </div>
    `;
  }

  renderDropzoneContent() {
    const slot = this.fileSlots && this.fileSlots[0];
    const hasFile =
      slot &&
      slot.fileData &&
      slot.fileData.length > 0 &&
      !this.selectedRelease;
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

    return this.releasesList
      .map((rel, index) => {
        const isSelected = this.selectedRelease
          ? this.selectedRelease.tag_name === rel.tag_name
          : index === 0;
        const dateStr = this.formatDate(rel.published_at);
        const dateSuffix = dateStr ? ` (${dateStr})` : "";
        const prereleaseSuffix = rel.prerelease ? " [pre-release]" : "";
        return `<option value="${this.escapeHtml(rel.tag_name)}" ${isSelected ? "selected" : ""}>${this.escapeHtml(rel.tag_name + prereleaseSuffix + dateSuffix)}</option>`;
      })
      .join("");
  }

  renderDeviceOptions() {
    if (
      !this.availableReleaseAssets ||
      this.availableReleaseAssets.length === 0
    ) {
      return '<option value="" disabled selected>No board binary available</option>';
    }

    return this.availableReleaseAssets
      .map((asset) => {
        const isSelected = Boolean(
          this.selectedAsset &&
          (this.selectedAsset.browser_download_url ===
            asset.browser_download_url ||
            this.selectedAsset.name === asset.name),
        );
        const sizeStr = asset.size ? ` (${this.formatBytes(asset.size)})` : "";
        return `<option value="${this.escapeHtml(asset.browser_download_url)}" data-name="${this.escapeHtml(asset.name)}" ${isSelected ? "selected" : ""}>${this.escapeHtml(asset.name + sizeStr)}</option>`;
      })
      .join("");
  }

  renderReleaseMetadata() {
    const slot = this.fileSlots && this.fileSlots[0];
    const hasLocalFile = Boolean(
      slot &&
      slot.fileData &&
      slot.fileData.length > 0 &&
      !this.selectedRelease,
    );

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
      return "";
    }

    const rel = this.selectedRelease;
    const asset = this.selectedAsset;
    const dateStr = this.formatDate(rel.published_at) || "Unknown";
    const binaryName = asset ? asset.name : "None";
    const binarySize =
      asset && asset.size ? ` (${this.formatBytes(asset.size)})` : "";

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
    if (this.isFlashing) return "Writing flash memory...";
    if (this.isFetchingBinary) return "Downloading binary...";
    if (this.isFetchingReleasesList) return "Loading releases...";

    const slot = this.fileSlots && this.fileSlots[0];
    const hasLocalFile = Boolean(
      slot &&
      slot.fileData &&
      slot.fileData.length > 0 &&
      !this.selectedRelease,
    );
    if (hasLocalFile) {
      return "Flash file";
    }

    if (this.selectedRelease) {
      return `Install ${this.selectedRelease.tag_name}`;
    }

    return "Flash firmware";
  }

  updateUI() {
    // Status Badge
    const badge = this.querySelector("#flasherStatusBadge");
    if (badge) {
      badge.className = `card-badge ${this.isDeviceConnected ? "badge-success" : "badge-muted"}`;
      badge.textContent = this.isDeviceConnected
        ? "Connected"
        : this.isConnecting
          ? "Connecting..."
          : "Disconnected";
    }

    // Connect Button
    const connectBtn = this.querySelector("#flasherConnectBtn");
    if (connectBtn) {
      connectBtn.className = `btn-connect ${this.isDeviceConnected ? "connected" : ""}`;
      connectBtn.disabled =
        !("serial" in navigator) || this.isConnecting || this.isFlashing;
      const dot = connectBtn.querySelector(".status-dot");
      if (dot) {
        dot.className = `status-dot ${this.isDeviceConnected ? "connected" : this.isConnecting ? "connecting" : "disconnected"}`;
      }
      const label = this.querySelector("#flasherConnectBtnText");
      if (label) {
        label.textContent = this.isDeviceConnected
          ? "Disconnect"
          : this.isConnecting
            ? "Connecting..."
            : "Connect";
      }
    }

    // Device Info Container
    const infoContainer = this.querySelector("#flasherDeviceInfoContainer");
    if (infoContainer) {
      infoContainer.innerHTML = this.renderDeviceInfo();
    }

    // Refresh Releases Button
    const refreshBtn = this.querySelector("#refreshReleasesBtn");
    if (refreshBtn) {
      refreshBtn.disabled = this.isFetchingReleasesList || this.isFlashing;
    }

    // Release Selector Dropdown
    const releaseSelect = this.querySelector("#releaseSelect");
    if (releaseSelect) {
      releaseSelect.disabled = this.isFetchingReleasesList || this.isFlashing;
      releaseSelect.innerHTML = this.renderReleaseOptions();
      if (this.selectedRelease) {
        releaseSelect.value = this.selectedRelease.tag_name;
      }
    }

    // Multi-Board Device Select Controls
    const deviceContainer = this.querySelector("#flasherDeviceSelectContainer");
    const deviceSelect = this.querySelector("#flasherDeviceSelect");
    if (deviceContainer) {
      deviceContainer.style.display =
        this.availableReleaseAssets && this.availableReleaseAssets.length > 1
          ? "block"
          : "none";
    }
    if (deviceSelect) {
      deviceSelect.disabled = this.isFlashing || this.isFetchingBinary;
      deviceSelect.innerHTML = this.renderDeviceOptions();
      if (this.selectedAsset) {
        deviceSelect.value = this.selectedAsset.browser_download_url;
      }
    }

    // Release Metadata Container
    const metaContainer = this.querySelector("#releaseMetadataContainer");
    if (metaContainer) {
      metaContainer.innerHTML = this.renderReleaseMetadata();
    }

    // Dropzone Content
    const dropzoneContent = this.querySelector("#fileDropzoneContent");
    if (dropzoneContent) {
      dropzoneContent.innerHTML = this.renderDropzoneContent();
    }

    const fileInput = this.querySelector("#firmwareFileInput");
    if (fileInput) {
      fileInput.disabled = this.isFlashing || this.isFetchingBinary;
    }

    const preserveCheck = this.querySelector("#preserveContentCheck");
    if (preserveCheck) {
      preserveCheck.disabled = this.isFlashing;
      preserveCheck.checked = this.preserveContent;
    }

    const eraseCheck = this.querySelector("#eraseBeforeFlashCheck");
    if (eraseCheck) {
      eraseCheck.disabled = this.isFlashing;
      eraseCheck.checked = this.eraseBeforeFlash;
    }

    // Flash Action Button
    const startFlashBtn = this.querySelector("#startFlashBtn");
    if (startFlashBtn) {
      const slot = this.fileSlots && this.fileSlots[0];
      const hasLocal = Boolean(
        slot &&
        slot.fileData &&
        slot.fileData.length > 0 &&
        !this.selectedRelease,
      );
      const canFlash = Boolean(
        hasLocal || (this.selectedRelease && this.selectedAsset),
      );

      startFlashBtn.disabled =
        this.isFlashing ||
        this.isFetchingBinary ||
        this.isFetchingReleasesList ||
        !canFlash;
      const flashLabel = this.querySelector("#startFlashBtnText");
      if (flashLabel) {
        flashLabel.textContent = this.renderFlashButtonText();
      }
    }

    // Progress Bar Container
    const progressContainer = this.querySelector("#flasherProgressContainer");
    if (progressContainer) {
      progressContainer.style.display =
        this.isFlashing || this.flashProgress > 0 ? "block" : "none";
    }
  }

  render() {
    if (!this.querySelector(".flasher-container")) {
      this.renderTemplate();
      this.checkWebSerialSupport();
      this.initEventListeners();
    }
    this.updateUI();
  }

  renderTemplate() {
    const isSupported = "serial" in navigator;

    this.innerHTML = `
      <div class="flasher-container">

        <!-- Browser Compatibility Notice -->
        <div id="serialCompatAlert" class="alert-banner alert-warning" style="display: ${isSupported ? "none" : "flex"};" role="alert">
          <div>
            <strong>Web Serial not supported:</strong> Use Chrome, Edge, or Opera on desktop to flash firmware over USB or Bluetooth.
          </div>
        </div>

        <!-- 1 Unified Main Card -->
        <div class="card flasher-card">

          <!-- Section 1: Connection -->
          <div class="flasher-section flasher-section-connection">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.75rem;">
              <span style="font-weight: 600; font-size: 0.92rem;">Serial connection</span>
              <span id="flasherStatusBadge" class="card-badge ${this.isDeviceConnected ? "badge-success" : "badge-muted"}">
                ${this.isDeviceConnected ? "Connected" : this.isConnecting ? "Connecting..." : "Disconnected"}
              </span>
            </div>

            <div style="display: flex; gap: 0.5rem; align-items: center; flex-wrap: wrap;">
              <button id="flasherConnectBtn" type="button" class="btn-connect ${this.isDeviceConnected ? "connected" : ""}" ${this.isConnecting || this.isFlashing ? "disabled" : ""} aria-label="Toggle serial port connection">
                <span class="status-dot ${this.isDeviceConnected ? "connected" : this.isConnecting ? "connecting" : "disconnected"}"></span>
                <span id="flasherConnectBtnText">${this.isDeviceConnected ? "Disconnect" : this.isConnecting ? "Connecting..." : "Connect"}</span>
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
              <button id="refreshReleasesBtn" type="button" class="btn-sm btn-secondary" style="font-size: 0.75rem; cursor: pointer;" title="Refresh releases from GitHub" ${this.isFetchingReleasesList || this.isFlashing ? "disabled" : ""}>
                Refresh
              </button>
            </div>

            <!-- Release Dropdown -->
            <div class="release-select-group">
              <label for="releaseSelect" class="release-select-label">
                Release
              </label>
              <select id="releaseSelect" class="control-select" style="width: 100%; padding: 0.45rem 0.65rem; font-size: 0.82rem; background: var(--bg-input); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); outline: none;" ${this.isFetchingReleasesList || this.isFlashing ? "disabled" : ""} aria-label="Firmware release version">
                ${this.renderReleaseOptions()}
              </select>
            </div>

            <!-- Board Target Dropdown (shown when multiple board binaries exist) -->
            <div id="flasherDeviceSelectContainer" style="display: ${this.availableReleaseAssets && this.availableReleaseAssets.length > 1 ? "block" : "none"}; margin-bottom: 0.85rem;">
              <label for="flasherDeviceSelect" class="release-select-label">
                Board target
              </label>
              <select id="flasherDeviceSelect" class="control-select" style="width: 100%; padding: 0.45rem 0.65rem; font-size: 0.82rem; background: var(--bg-input); color: var(--text-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); outline: none;" ${this.isFlashing ? "disabled" : ""} aria-label="Board hardware variant">
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
                  <input type="file" id="firmwareFileInput" class="file-picker-input" accept=".bin,application/octet-stream" ${this.isFlashing ? "disabled" : ""} aria-label="Select local binary file">
                  <div id="fileDropzoneContent">
                    ${this.renderDropzoneContent()}
                  </div>
                </div>
              </div>
            </details>

            <!-- Preserve & Erase Checkboxes -->
            <div style="margin-top: 0.75rem; display: flex; justify-content: space-between; align-items: center; gap: 0.75rem; flex-wrap: wrap;">
              <label class="checkbox-label" title="Extracts books and bookmarks before layout changes or erase, and preserves settings">
                <input type="checkbox" id="preserveContentCheck" ${this.preserveContent ? "checked" : ""} ${this.isFlashing ? "disabled" : ""}>
                <span>Preserve books, bookmarks &amp; settings</span>
              </label>
              <label class="checkbox-label">
                <input type="checkbox" id="eraseBeforeFlashCheck" ${this.eraseBeforeFlash ? "checked" : ""} ${this.isFlashing ? "disabled" : ""}>
                <span>Erase entire flash before write</span>
              </label>
            </div>

            <!-- Primary Action Button -->
            <div style="margin-top: 0.85rem;">
              <button id="startFlashBtn" type="button" class="btn-primary btn-flash" style="width: 100%; justify-content: center; height: 42px; font-weight: 600;" ${this.isFlashing || this.isFetchingBinary || this.isFetchingReleasesList ? "disabled" : ""}>
                <span id="startFlashBtnText">${this.renderFlashButtonText()}</span>
              </button>
            </div>

            <!-- Progress Bar -->
            <div id="flasherProgressContainer" class="flasher-progress-container" style="display: ${this.isFlashing || this.flashProgress > 0 ? "block" : "none"}; margin-top: 0.75rem;">
              <div class="progress-meta">
                <span id="flasherProgressStatusText" class="progress-status-text">${this.flashStatusText}</span>
                <span id="flasherProgressPctText" class="progress-pct-text">${this.flashProgress.toFixed(1)}%</span>
              </div>
              <div class="meter-bar flasher-meter-bar" role="progressbar" aria-valuenow="${this.flashProgress.toFixed(0)}" aria-valuemin="0" aria-valuemax="100">
                <div id="flasherProgressBarFill" class="meter-fill meter-flasher" style="width: ${this.flashProgress}%;"></div>
              </div>
              <div class="progress-bytes-meta" id="flasherProgressBytesText">
                ${this.progressDetails.total > 0 ? `${this.formatBytes(this.progressDetails.written)} / ${this.formatBytes(this.progressDetails.total)}` : ""}
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
    const connectBtn = this.querySelector("#flasherConnectBtn");
    if (connectBtn) {
      connectBtn.addEventListener("click", () => {
        if (this.isDeviceConnected) {
          this.disconnect();
        } else {
          this.connect();
        }
      });
    }

    // Refresh releases button
    const refreshBtn = this.querySelector("#refreshReleasesBtn");
    if (refreshBtn) {
      refreshBtn.addEventListener("click", () => this.fetchReleasesList());
    }

    // Release select dropdown
    const releaseSelect = this.querySelector("#releaseSelect");
    if (releaseSelect) {
      releaseSelect.addEventListener("change", (e) => {
        const tag = e.target.value;
        const rel = this.releasesList.find((r) => r.tag_name === tag);
        if (rel) {
          this.selectedRelease = rel;
          this.releaseTag = rel.tag_name;
          this.resolveReleaseAssets(rel);

          // Clear previously loaded buffer so selected release binary is used
          this.fileSlots[0].fileData = null;
          this.fileSlots[0].fileName = null;
          this.fileSlots[0].fileSize = 0;

          this.appendTerminal(`Selected release ${rel.tag_name}\n`, "info");
          this.updateUI();
        }
      });
    }

    // Device / Board binary dropdown
    const deviceSelect = this.querySelector("#flasherDeviceSelect");
    if (deviceSelect) {
      deviceSelect.addEventListener("change", (e) => {
        const val = e.target.value;
        const asset = this.availableReleaseAssets.find(
          (a) => a.browser_download_url === val || a.name === val,
        );
        if (asset) {
          this.selectedAsset = asset;
          this.selectedAssetUrl = asset.browser_download_url;

          this.fileSlots[0].fileData = null;
          this.fileSlots[0].fileName = null;
          this.fileSlots[0].fileSize = 0;

          this.appendTerminal(`Selected board target: ${asset.name}\n`, "info");
          this.updateUI();
        }
      });
    }

    // Preserve content checkbox
    const preserveCheck = this.querySelector("#preserveContentCheck");
    if (preserveCheck) {
      preserveCheck.addEventListener("change", (e) => {
        this.preserveContent = e.target.checked;
      });
    }

    // Erase before flash checkbox
    const eraseCheck = this.querySelector("#eraseBeforeFlashCheck");
    if (eraseCheck) {
      eraseCheck.addEventListener("change", (e) => {
        this.eraseBeforeFlash = e.target.checked;
      });
    }

    // Start flash button
    const startFlashBtn = this.querySelector("#startFlashBtn");
    if (startFlashBtn) {
      startFlashBtn.addEventListener("click", () => this.handleInstallAction());
    }

    // Terminal clear button
    const clearTerminalBtn = this.querySelector("#clearTerminalBtn");
    if (clearTerminalBtn) {
      clearTerminalBtn.addEventListener("click", () => this.clearTerminal());
    }

    // Dropzone & File Input delegation
    const fileInput = this.querySelector("#firmwareFileInput");
    if (fileInput) {
      fileInput.addEventListener("change", async (e) => {
        const file = e.target.files && e.target.files[0];
        if (file) {
          await this.loadBinaryFile(file);
        }
      });
    }

    const dropzone = this.querySelector("#fileDropzone");
    if (dropzone) {
      dropzone.addEventListener("dragover", (e) => {
        e.preventDefault();
        dropzone.classList.add("dragover");
      });

      dropzone.addEventListener("dragleave", (e) => {
        e.preventDefault();
        dropzone.classList.remove("dragover");
      });

      dropzone.addEventListener("drop", async (e) => {
        e.preventDefault();
        dropzone.classList.remove("dragover");
        if (this.isFlashing || this.isFetchingBinary) return;

        const file =
          e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
        if (file) {
          await this.loadBinaryFile(file);
        }
      });
    }

    // Listen for unexpected device disconnect
    if ("serial" in navigator) {
      navigator.serial.addEventListener("disconnect", (event) => {
        if (this.port && event.target === this.port) {
          this.appendTerminal(
            "Serial cable disconnected from computer.\n",
            "warning",
          );
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
        id: "slot_factory",
        offset: "0x0",
        fileData: new Uint8Array(buffer),
        fileName: file.name,
        fileSize: file.size,
      };
      this.selectedRelease = null;
      this.selectedAsset = null;
      this.selectedAssetUrl = null;
      this.appendTerminal(
        `Selected custom binary [${file.name}] (${this.formatBytes(file.size)}) for flash at 0x0\n`,
        "info",
      );
      this.updateUI();
    } catch (err) {
      this.appendTerminal(
        `Failed to read file ${file.name}: ${err.message}\n`,
        "error",
      );
    }
  }

  // ==========================================================================
  // GitHub Release Firmware Operations
  // ==========================================================================
  async fetchReleasesList(repo = this.releaseRepo) {
    const cfg =
      (typeof window !== "undefined" &&
        (window.CONFIG || window.BACA_CONFIG)) ||
      null;
    const fallbackRepo =
      cfg?.repoSlug ||
      (cfg?.repo
        ? cfg.repo.replace(/^https?:\/\/github\.com\//i, "").replace(/\/+$/, "")
        : "Kha-dr/BacaIn");
    const targetRepo = (repo && repo.trim()) || fallbackRepo;
    this.isFetchingReleasesList = true;
    this.updateUI();

    try {
      this.appendTerminal(
        `Fetching releases from github.com/${targetRepo}\n`,
        "info",
      );

      const res = await fetch(
        `https://api.github.com/repos/${targetRepo}/releases?per_page=30`,
      );

      if (!res.ok) {
        if (res.status === 403) {
          throw new Error(
            "GitHub API rate limit exceeded (HTTP 403). Select a local .bin file instead.",
          );
        } else if (res.status === 404) {
          throw new Error(`Repository '${targetRepo}' not found.`);
        } else {
          throw new Error(
            `Failed to fetch releases (HTTP ${res.status}: ${res.statusText})`,
          );
        }
      }

      const list = await res.json();
      if (!Array.isArray(list) || list.length === 0) {
        throw new Error(`No releases found for ${targetRepo}.`);
      }

      const published = list.filter((r) => !r.draft);
      const withBin = published.filter(
        (r) =>
          Array.isArray(r.assets) &&
          r.assets.some(
            (a) =>
              a &&
              typeof a.name === "string" &&
              a.name.toLowerCase().endsWith(".bin"),
          ),
      );

      this.releasesList = withBin.length > 0 ? withBin : published;

      if (this.releasesList.length === 0) {
        throw new Error(
          "No published releases with firmware binary (.bin) assets found.",
        );
      }

      // Default to latest release
      this.selectedRelease = this.releasesList[0];
      this.releaseTag = this.selectedRelease.tag_name;
      this.resolveReleaseAssets(this.selectedRelease);

      this.appendTerminal(
        `Loaded ${this.releasesList.length} release(s). Selected ${this.selectedRelease.tag_name}.\n`,
        "success",
      );
    } catch (err) {
      this.appendTerminal(`[Releases Error] ${err.message}\n`, "error");
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

    const binAssets = release.assets.filter(
      (a) =>
        a &&
        typeof a.name === "string" &&
        a.name.toLowerCase().endsWith(".bin") &&
        Boolean(a.browser_download_url),
    );

    const factoryAssets = binAssets.filter((a) =>
      /^bacain_.*\.bin$/i.test(a.name),
    );
    this.availableReleaseAssets =
      factoryAssets.length > 0 ? factoryAssets : binAssets;

    if (this.availableReleaseAssets.length > 0) {
      this.selectedAsset = this.availableReleaseAssets[0];
      this.selectedAssetUrl = this.selectedAsset.browser_download_url;
    } else {
      this.selectedAsset = null;
      this.selectedAssetUrl = null;
    }
  }

  async handleInstallAction() {
    if (this.isFlashing || this.isFetchingBinary || this.isFetchingReleasesList)
      return;

    const slot = this.fileSlots && this.fileSlots[0];
    const hasLocalFile = Boolean(
      slot &&
      slot.fileData &&
      slot.fileData.length > 0 &&
      !this.selectedRelease,
    );

    // If using GitHub release and binary not yet loaded in buffer, download it
    if (!hasLocalFile) {
      if (!this.selectedAsset) {
        this.appendTerminal(
          "No firmware release binary selected.\n",
          "warning",
        );
        return;
      }

      const needDownload =
        !slot || !slot.fileData || slot.fileName !== this.selectedAsset.name;
      if (needDownload) {
        try {
          await this.downloadReleaseAsset(this.selectedAsset);
        } catch (err) {
          return;
        }
      }
    }

    if (!this.hasValidFirmware()) {
      this.appendTerminal("No valid firmware loaded to flash.\n", "warning");
      return;
    }

    if (!this.isDeviceConnected || !this.esploader) {
      this.appendTerminal("Opening serial port selection...\n", "info");
      await this.connect();
      if (!this.isDeviceConnected || !this.esploader) {
        this.appendTerminal(
          "Flash cancelled: device not connected.\n",
          "warning",
        );
        return;
      }
    }

    await this.startFlashing();
  }

  async fetchLatestFirmwareFromRelease(repo = this.releaseRepo, tag = null) {
    const cfg =
      (typeof window !== "undefined" &&
        (window.CONFIG || window.BACA_CONFIG)) ||
      null;
    const fallbackRepo =
      cfg?.repoSlug ||
      (cfg?.repo
        ? cfg.repo.replace(/^https?:\/\/github\.com\//i, "").replace(/\/+$/, "")
        : "Kha-dr/BacaIn");
    const targetRepo = (repo && repo.trim()) || fallbackRepo;
    const targetTag =
      typeof tag === "string" && tag.trim().length > 0 ? tag.trim() : null;

    this.isFetchingBinary = true;
    this.isFetchingRelease = true;
    this.updateUI();

    let targetAssetToDownload = null;

    try {
      this.appendTerminal(
        `Fetching release ${targetTag || "latest"} from GitHub (${targetRepo})\n`,
        "info",
      );

      const releaseUrl = targetTag
        ? `https://api.github.com/repos/${targetRepo}/releases/tags/${encodeURIComponent(targetTag)}`
        : `https://api.github.com/repos/${targetRepo}/releases/latest`;

      const releaseRes = await fetch(releaseUrl);
      if (!releaseRes.ok) {
        if (releaseRes.status === 404) {
          throw new Error(`Release not found for repository '${targetRepo}'.`);
        } else if (releaseRes.status === 403) {
          throw new Error("GitHub API rate limit exceeded (HTTP 403).");
        } else {
          throw new Error(
            `Failed to fetch release (HTTP ${releaseRes.status}: ${releaseRes.statusText}).`,
          );
        }
      }

      const releaseData = await releaseRes.json();
      this.selectedRelease = releaseData;
      this.releaseTag = releaseData.tag_name;
      this.resolveReleaseAssets(releaseData);

      if (this.availableReleaseAssets.length === 0) {
        throw new Error("No .bin firmware assets found in release.");
      }

      if (this.availableReleaseAssets.length === 1) {
        targetAssetToDownload = this.availableReleaseAssets[0];
      } else {
        this.appendTerminal(
          `Found ${this.availableReleaseAssets.length} board binaries in this release. Select board target.\n`,
          "info",
        );
        return this.availableReleaseAssets;
      }
    } catch (err) {
      this.appendTerminal(`[GitHub Release Error] ${err.message}\n`, "error");
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
      const err = new Error("Invalid release asset: download URL missing");
      this.appendTerminal(`[Download Error] ${err.message}\n`, "error");
      throw err;
    }

    this.isFetchingBinary = true;
    this.isFetchingRelease = true;
    this.selectedAsset = asset;
    this.selectedAssetUrl = asset.browser_download_url;
    this.updateUI();

    try {
      this.appendTerminal(
        `Downloading ${asset.name} (${this.formatBytes(asset.size || 0)})...\n`,
        "info",
      );

      const downloadRes = await fetch(asset.browser_download_url);
      if (!downloadRes.ok) {
        throw new Error(
          `Download failed (HTTP ${downloadRes.status}: ${downloadRes.statusText})`,
        );
      }

      const buffer = await downloadRes.arrayBuffer();

      this.fileSlots[0] = {
        id: "slot_factory",
        offset: "0x0",
        fileData: new Uint8Array(buffer),
        fileName: asset.name,
        fileSize: buffer.byteLength,
      };

      this.appendTerminal(
        `Firmware ${asset.name} (${this.formatBytes(buffer.byteLength)}) loaded. Ready to flash at 0x0.\n`,
        "success",
      );
      this.updateUI();
      return this.fileSlots[0];
    } catch (err) {
      this.appendTerminal(`[Download Error] ${err.message}\n`, "error");
      throw err;
    } finally {
      this.isFetchingBinary = false;
      this.isFetchingRelease = false;
      this.updateUI();
    }
  }

  async selectDevice(nameOrUrl) {
    if (!nameOrUrl) return;
    const asset = this.availableReleaseAssets.find(
      (a) =>
        a.browser_download_url === nameOrUrl ||
        a.name === nameOrUrl ||
        (a.browser_download_url &&
          a.browser_download_url.endsWith("/" + nameOrUrl)),
    );
    if (!asset) {
      throw new Error(`Device firmware '${nameOrUrl}' not found.`);
    }
    this.selectedAsset = asset;
    this.selectedAssetUrl = asset.browser_download_url;
    const selectEl = this.querySelector("#flasherDeviceSelect");
    if (selectEl) {
      selectEl.value = asset.browser_download_url;
    }
    return await this.downloadReleaseAsset(asset);
  }

  // ==========================================================================
  // Web Serial Connection Operations
  // ==========================================================================
  async connect() {
    if (!("serial" in navigator)) {
      this.appendTerminal(
        "Web Serial API is not supported in this browser.\n",
        "error",
      );
      return;
    }

    this.isConnecting = true;
    this.updateUI();

    try {
      this.appendTerminal(
        "Requesting serial port... Select your ESP32-S3 device.\n",
        "info",
      );

      let port;
      try {
        port = await navigator.serial.requestPort({
          filters: SERIAL_DEVICE_FILTERS,
          allowedBluetoothServiceClassIds: ["00001101-0000-1000-8000-00805f9b34fb"]
        });
      } catch (filterErr) {
        if (filterErr.name === "NotFoundError") {
          this.appendTerminal(
            "Serial port selection cancelled by user.\n",
            "warning",
          );
          this.isConnecting = false;
          this.updateUI();
          return;
        }
        port = await navigator.serial.requestPort({
          allowedBluetoothServiceClassIds: ["00001101-0000-1000-8000-00805f9b34fb"]
        });
      }

      this.port = port;
      this.appendTerminal("Loading esptool-js flashing engine...\n", "info");
      const { ESPLoader, Transport } = await loadEsptool();

      this.appendTerminal("Initializing WebSerial transport...\n", "info");
      this.transport = new Transport(this.port, true);

      const targetBaud = this.selectedBaudrate || 460800;
      this.appendTerminal(
        `Opening connection (Flash: ${targetBaud} bps)...\n`,
        "info",
      );

      const termObj = {
        clean: () => this.clearTerminal(),
        writeLine: (data) => this.appendTerminal(data + "\n"),
        write: (data) => this.appendTerminal(data),
      };

      this.esploader = new ESPLoader({
        transport: this.transport,
        baudrate: targetBaud,
        romBaudrate: 115200,
        terminal: termObj,
      });

      this.appendTerminal("Connecting to ROM bootloader...\n", "info");
      const chip = await this.esploader.main();

      this.chipName =
        chip ||
        (this.esploader.chip ? this.esploader.chip.CHIP_NAME : "ESP32-S3");
      this.appendTerminal(`Connected to ${this.chipName}\n`, "success");

      // Query MAC Address
      try {
        if (
          this.esploader.chip &&
          typeof this.esploader.chip.readMac === "function"
        ) {
          this.macAddress = await this.esploader.chip.readMac(this.esploader);
          this.appendTerminal(`MAC Address: ${this.macAddress}\n`, "info");
        }
      } catch (macErr) {
        // Non-fatal
      }

      // Query Chip Model / Description
      try {
        if (
          this.esploader.chip &&
          typeof this.esploader.chip.getChipDescription === "function"
        ) {
          const desc = await this.esploader.chip.getChipDescription(
            this.esploader,
          );
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
      this.appendTerminal(`Serial Connection Error: ${err.message}\n`, "error");
      await this.cleanUpConnection();
      this.updateUI();
    }
  }

  async disconnect() {
    this.appendTerminal("Disconnecting from serial port...\n", "info");
    await this.cleanUpConnection();
    this.isDeviceConnected = false;
    this.isConnecting = false;
    this.isFlashing = false;
    this.appendTerminal("Disconnected from device.\n", "info");
    this.updateUI();
  }

  async cleanUpConnection() {
    if (this.transport) {
      try {
        await this.transport.disconnect();
      } catch (e) {
        console.warn("[EspWebFlasher] Error during transport disconnect:", e);
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
      this.appendTerminal("Cannot reboot: device is not connected.\n", "error");
      return;
    }
    try {
      this.appendTerminal(
        "Triggering hardware reset via DTR/RTS signals...\n",
        "info",
      );
      await this.esploader.after("hard_reset");
      this.appendTerminal("Hard reset sent. Device is booting.\n", "success");
    } catch (err) {
      this.appendTerminal(`Failed to reset device: ${err.message}\n`, "error");
    }
  }

  async eraseEntireFlash() {
    if (!this.esploader) {
      this.appendTerminal("Cannot erase: device is not connected.\n", "error");
      return;
    }

    try {
      this.isFlashing = true;
      this.flashStatusText = "Erasing flash memory...";
      this.flashProgress = 0;
      this.updateUI();

      this.appendTerminal(
        "Erasing entire flash chip... Please wait.\n",
        "warning",
      );
      await this.esploader.eraseFlash();
      this.appendTerminal("Flash memory erased completely.\n", "success");
    } catch (err) {
      this.appendTerminal(`Erase flash failed: ${err.message}\n`, "error");
    } finally {
      this.isFlashing = false;
      this.flashStatusText = "Ready";
      this.updateUI();
    }
  }

  // ==========================================================================
  // Partition Table, Wear-Levelling & FAT Content Extraction Helpers
  // ==========================================================================
  parsePartitionTable(tableBytes) {
    if (!tableBytes || tableBytes.length < 32) return {};
    const view = new DataView(
      tableBytes.buffer,
      tableBytes.byteOffset,
      tableBytes.byteLength,
    );
    const parts = {};
    for (let off = 0; off + 32 <= tableBytes.length; off += 32) {
      const b0 = tableBytes[off];
      const b1 = tableBytes[off + 1];
      if (b0 === 0xeb && b1 === 0xeb) break;
      if (b0 !== 0xaa || b1 !== 0x50) continue;

      const type = tableBytes[off + 2];
      const subtype = tableBytes[off + 3];
      const pOffset = view.getUint32(off + 4, true);
      const pSize = view.getUint32(off + 8, true);

      let label = "";
      for (let i = 0; i < 16; i++) {
        const ch = tableBytes[off + 12 + i];
        if (ch === 0) break;
        label += String.fromCharCode(ch);
      }
      if (label) {
        parts[label] = { label, type, subtype, offset: pOffset, size: pSize };
      }
    }
    return parts;
  }

  async readFlashRange(address, size, statusLabel = "Reading flash...") {
    this.flashStatusText = statusLabel;
    this.updateProgressBarUI();
    const data = await this.esploader.readFlash(
      address,
      size,
      (packet, progress, totalSize) => {
        const pct =
          totalSize > 0
            ? Math.min(100, Math.max(0, (progress / totalSize) * 100))
            : 0;
        this.flashProgress = pct;
        this.progressDetails = {
          written: progress,
          total: totalSize,
          fileIndex: 1,
          totalFiles: 1,
        };
        this.updateProgressBarUI();
      },
    );
    return data instanceof Uint8Array ? data : new Uint8Array(data);
  }

  async extractDeviceStorageFiles(storagePart) {
    const SECTOR_SIZE = 4096;
    const partOffset = storagePart.offset;
    const partSize = storagePart.size;
    if (!partOffset || partSize < SECTOR_SIZE * 8) return [];

    // 1. Read ESP-IDF Wear-Levelling state at tail of partition (last 16 KB)
    const tailSize = SECTOR_SIZE * 4;
    const tailOffset = partOffset + partSize - tailSize;
    const tailBytes = await this.readFlashRange(
      tailOffset,
      tailSize,
      "Reading storage wear-levelling state...",
    );

    let wlPos = 0;
    let wlMaxPos = Math.floor(partSize / SECTOR_SIZE) - 4;
    let wlMoveCount = 0;
    let hasValidWl = false;

    for (const stateSecIdx of [0, 1, 2]) {
      const base = stateSecIdx * SECTOR_SIZE;
      const view = new DataView(
        tailBytes.buffer,
        tailBytes.byteOffset + base,
        64,
      );
      const pos = view.getUint32(0, true);
      const maxPos = view.getUint32(4, true);
      const moveCount = view.getUint32(8, true);
      const blockSize = view.getUint32(20, true);
      if (
        blockSize === SECTOR_SIZE &&
        maxPos > 0 &&
        maxPos <= Math.floor(partSize / SECTOR_SIZE) &&
        pos <= maxPos
      ) {
        wlPos = pos;
        wlMaxPos = maxPos;
        wlMoveCount = moveCount;
        hasValidWl = true;
        break;
      }
    }

    const logicalToPhysicalAddress = (logicalSector) => {
      if (!hasValidWl || wlMaxPos === 0) {
        return partOffset + logicalSector * SECTOR_SIZE;
      }
      const modMove = wlMoveCount % wlMaxPos;
      let physSector = (logicalSector + wlMaxPos - modMove) % wlMaxPos;
      if (physSector >= wlPos) {
        physSector += 1;
      }
      return partOffset + physSector * SECTOR_SIZE;
    };

    const sectorCache = new Map();
    const readLogicalSectors = async (startLogical, count, label) => {
      const out = new Uint8Array(count * SECTOR_SIZE);
      let i = 0;
      while (i < count) {
        const logSec = startLogical + i;
        if (sectorCache.has(logSec)) {
          out.set(sectorCache.get(logSec), i * SECTOR_SIZE);
          i++;
          continue;
        }
        // Coalesce contiguous physical sectors into a single serial read
        const firstPhys = logicalToPhysicalAddress(logSec);
        let run = 1;
        while (i + run < count && run < 16) {
          const nextLog = startLogical + i + run;
          if (sectorCache.has(nextLog)) break;
          const nextPhys = logicalToPhysicalAddress(nextLog);
          if (nextPhys !== firstPhys + run * SECTOR_SIZE) break;
          run++;
        }
        const chunk = await this.readFlashRange(
          firstPhys,
          run * SECTOR_SIZE,
          label,
        );
        for (let k = 0; k < run; k++) {
          const secSlice = chunk.slice(k * SECTOR_SIZE, (k + 1) * SECTOR_SIZE);
          sectorCache.set(startLogical + i + k, secSlice);
          out.set(secSlice, (i + k) * SECTOR_SIZE);
        }
        i += run;
      }
      return out;
    };

    // 2. Locate FAT Boot Sector (BPB) at logical sector 0 or via MBR partition entry 0
    const firstSectors = await readLogicalSectors(
      0,
      2,
      "Reading FAT boot sector...",
    );
    let bpbSectorIdx = -1;

    const isBpbSector = (buf, offset) => {
      if (buf[offset + 510] !== 0x55 || buf[offset + 511] !== 0xaa)
        return false;
      const bps = buf[offset + 11] | (buf[offset + 12] << 8);
      const spc = buf[offset + 13];
      const numFats = buf[offset + 16];
      return bps === SECTOR_SIZE && spc > 0 && (numFats === 1 || numFats === 2);
    };

    if (isBpbSector(firstSectors, 0)) {
      bpbSectorIdx = 0;
    } else if (isBpbSector(firstSectors, SECTOR_SIZE)) {
      bpbSectorIdx = 1;
    } else if (firstSectors[510] === 0x55 && firstSectors[511] === 0xaa) {
      const mbrView = new DataView(
        firstSectors.buffer,
        firstSectors.byteOffset,
        SECTOR_SIZE,
      );
      const lbaStart = mbrView.getUint32(446 + 8, true);
      if (lbaStart > 0 && lbaStart < 64) {
        const candidate = await readLogicalSectors(
          lbaStart,
          1,
          "Reading FAT partition boot sector...",
        );
        if (isBpbSector(candidate, 0)) {
          bpbSectorIdx = lbaStart;
        }
      }
    }

    if (bpbSectorIdx < 0) {
      return [];
    }

    const bpbSector = await readLogicalSectors(
      bpbSectorIdx,
      1,
      "Parsing FAT geometry...",
    );
    const bpbView = new DataView(
      bpbSector.buffer,
      bpbSector.byteOffset,
      SECTOR_SIZE,
    );
    const bytesPerSector = bpbView.getUint16(11, true);
    const sectorsPerCluster = bpbSector[13];
    const reservedSectors = bpbView.getUint16(14, true);
    const numFats = bpbSector[16];
    const rootEntryCount = bpbView.getUint16(17, true);
    const totalSectors16 = bpbView.getUint16(19, true);
    const sectorsPerFat = bpbView.getUint16(22, true);
    const totalSectors32 = bpbView.getUint32(32, true);
    const totalSectors = totalSectors16 !== 0 ? totalSectors16 : totalSectors32;

    if (
      bytesPerSector !== SECTOR_SIZE ||
      sectorsPerCluster === 0 ||
      sectorsPerFat === 0
    ) {
      return [];
    }

    const fatStartSector = bpbSectorIdx + reservedSectors;
    const rootDirSectors = Math.ceil((rootEntryCount * 32) / bytesPerSector);
    const rootDirStartSector = fatStartSector + numFats * sectorsPerFat;
    const dataStartSector = rootDirStartSector + rootDirSectors;
    const dataSectors =
      totalSectors -
      (reservedSectors + numFats * sectorsPerFat + rootDirSectors);
    const totalClusters = Math.floor(dataSectors / sectorsPerCluster);
    const isFat12 = totalClusters < 4085;

    const fatBytes = await readLogicalSectors(
      fatStartSector,
      sectorsPerFat,
      "Reading FAT allocation table...",
    );
    const fatView = new DataView(
      fatBytes.buffer,
      fatBytes.byteOffset,
      fatBytes.byteLength,
    );

    const getNextCluster = (cluster) => {
      if (isFat12) {
        const fatOffset = cluster + Math.floor(cluster / 2);
        if (fatOffset + 1 >= fatBytes.length) return 0xfff;
        const val = fatView.getUint16(fatOffset, true);
        return cluster & 1 ? val >> 4 : val & 0x0fff;
      }
      const fatOffset = cluster * 2;
      if (fatOffset + 1 >= fatBytes.length) return 0xffff;
      return fatView.getUint16(fatOffset, true);
    };

    const isEofCluster = (cluster) =>
      isFat12 ? cluster >= 0x0ff8 : cluster >= 0xfff8;

    const readClusterChain = async (firstCluster, byteLength, label) => {
      if (firstCluster < 2 || byteLength === 0) return new Uint8Array(0);
      const out = new Uint8Array(byteLength);
      let written = 0;
      let cur = firstCluster;
      let guard = 0;
      const clusterBytes = sectorsPerCluster * bytesPerSector;

      while (
        cur >= 2 &&
        !isEofCluster(cur) &&
        written < byteLength &&
        guard < 4096
      ) {
        guard++;
        const logSec = dataStartSector + (cur - 2) * sectorsPerCluster;
        const raw = await readLogicalSectors(logSec, sectorsPerCluster, label);
        const toCopy = Math.min(clusterBytes, byteLength - written);
        out.set(raw.subarray(0, toCopy), written);
        written += toCopy;
        cur = getNextCluster(cur);
      }
      return out;
    };

    const decodeLfnPart = (entry) => {
      const offsets = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30];
      let s = "";
      for (const o of offsets) {
        const code = entry[o] | (entry[o + 1] << 8);
        if (code === 0x0000 || code === 0xffff) break;
        s += String.fromCharCode(code);
      }
      return s;
    };

    const decodeShortName = (entry) => {
      let base = "";
      for (let i = 0; i < 8; i++) {
        const c = entry[i];
        if (c === 0x20) break;
        base += String.fromCharCode(i === 0 && c === 0x05 ? 0xe5 : c);
      }
      let ext = "";
      for (let i = 8; i < 11; i++) {
        const c = entry[i];
        if (c === 0x20) break;
        ext += String.fromCharCode(c);
      }
      const ntRes = entry[12];
      if (ntRes & 0x08) base = base.toLowerCase();
      if (ntRes & 0x10) ext = ext.toLowerCase();
      return ext ? `${base}.${ext}` : base;
    };

    const extractedFiles = [];

    const parseDirectoryBytes = async (dirBytes, parentPrefix, depth = 0) => {
      if (depth > 4) return;
      let lfnParts = [];
      const view = new DataView(
        dirBytes.buffer,
        dirBytes.byteOffset,
        dirBytes.byteLength,
      );

      for (let off = 0; off + 32 <= dirBytes.length; off += 32) {
        const firstByte = dirBytes[off];
        if (firstByte === 0x00) break;
        if (firstByte === 0xe5) {
          lfnParts = [];
          continue;
        }
        const attr = dirBytes[off + 11];
        const entry = dirBytes.subarray(off, off + 32);

        if (attr === 0x0f) {
          const seq = firstByte & 0x1f;
          if (firstByte & 0x40) lfnParts = [];
          lfnParts[seq - 1] = decodeLfnPart(entry);
          continue;
        }

        if (attr & 0x08) {
          lfnParts = [];
          continue;
        }

        const name =
          lfnParts.length > 0 ? lfnParts.join("") : decodeShortName(entry);
        lfnParts = [];
        if (!name || name === "." || name === "..") continue;

        const firstCluster = view.getUint16(off + 26, true);
        const fileSize = view.getUint32(off + 28, true);
        const relPath = parentPrefix ? `${parentPrefix}/${name}` : name;

        if (attr & 0x10) {
          // Directory: traverse up to 16 clusters
          if (firstCluster >= 2) {
            const subDirBytes = await readClusterChain(
              firstCluster,
              sectorsPerCluster * bytesPerSector * 4,
              `Scanning folder ${relPath}...`,
            );
            await parseDirectoryBytes(subDirBytes, relPath, depth + 1);
          }
        } else {
          const fileData = await readClusterChain(
            firstCluster,
            fileSize,
            `Backing up ${relPath} (${this.formatBytes(fileSize)})...`,
          );
          extractedFiles.push({ path: relPath, data: fileData });
          this.appendTerminal(
            `Backed up: ${relPath} (${this.formatBytes(fileSize)})\n`,
            "info",
          );
        }
      }
    };

    const rootDirBytes = await readLogicalSectors(
      rootDirStartSector,
      rootDirSectors,
      "Scanning device root directory...",
    );
    await parseDirectoryBytes(rootDirBytes, "", 0);
    return extractedFiles;
  }

  packMigrationBundle(files) {
    if (!Array.isArray(files) || files.length === 0) return null;
    const encoder = new TextEncoder();
    const encodedEntries = [];
    let totalPayloadSize = 8; // [magic:4][file_count:4]

    for (const f of files) {
      const pathBytes = encoder.encode(f.path);
      if (pathBytes.length === 0 || pathBytes.length >= 192) continue;
      const dataBytes =
        f.data instanceof Uint8Array ? f.data : new Uint8Array(f.data);
      encodedEntries.push({ pathBytes, dataBytes });
      totalPayloadSize += 2 + pathBytes.length + 4 + dataBytes.length;
    }

    if (encodedEntries.length === 0) return null;

    const bundle = new Uint8Array(totalPayloadSize);
    const view = new DataView(bundle.buffer);
    view.setUint32(0, 0x47494d42, true); // "BMIG"
    view.setUint32(4, encodedEntries.length, true);

    let cursor = 8;
    for (const entry of encodedEntries) {
      view.setUint16(cursor, entry.pathBytes.length, true);
      cursor += 2;
      bundle.set(entry.pathBytes, cursor);
      cursor += entry.pathBytes.length;
      view.setUint32(cursor, entry.dataBytes.length, true);
      cursor += 4;
      bundle.set(entry.dataBytes, cursor);
      cursor += entry.dataBytes.length;
    }
    return bundle;
  }

  // ==========================================================================
  // Flash Firmware Execution (Zero Mock - Content-Preserving Flash)
  // ==========================================================================
  async startFlashing() {
    if (!this.isDeviceConnected || !this.esploader) {
      this.appendTerminal(
        "Device not connected. Opening serial port selection...\n",
        "info",
      );
      await this.connect();
      if (!this.isDeviceConnected || !this.esploader) {
        this.appendTerminal(
          "Flashing cancelled: serial connection not established.\n",
          "warning",
        );
        return;
      }
    }

    const slot = this.fileSlots && this.fileSlots[0];
    if (!slot || !slot.fileData || slot.fileData.length === 0) {
      const msg =
        "No firmware binary loaded. Select a release or custom .bin file.";
      this.appendTerminal(msg + "\n", "warning");
      return;
    }

    this.isFlashing = true;
    this.flashProgress = 0;
    this.flashStatusText = "Inspecting partition layout...";
    this.updateUI();

    try {
      const fwBytes = slot.fileData;
      const fwName = slot.fileName || "bacain_factory.bin";
      const newParts =
        fwBytes.length >= 0x9000
          ? this.parsePartitionTable(fwBytes.subarray(0x8000, 0x9000))
          : {};
      const newStorage = newParts.storage || null;
      const newFactory = newParts.factory || null;
      const newNvs = newParts.nvs || null;

      let oldParts = {};
      let nvsBackup = null;
      let migrationBundle = null;

      if (this.preserveContent && newStorage) {
        try {
          this.appendTerminal(
            "Checking on-device partition table at 0x8000...\n",
            "info",
          );
          const devPartBytes = await this.readFlashRange(
            0x8000,
            0x1000,
            "Reading device partition table...",
          );
          oldParts = this.parsePartitionTable(devPartBytes);
        } catch (partErr) {
          this.appendTerminal(
            `Could not read existing partition table (${partErr.message}). Proceeding with standard flash.\n`,
            "warning",
          );
        }

        const oldStorage = oldParts.storage || null;
        const oldNvs = oldParts.nvs || null;

        if (
          this.eraseBeforeFlash &&
          oldNvs &&
          newNvs &&
          oldNvs.size === newNvs.size
        ) {
          this.appendTerminal(
            `Backing up NVS settings (${this.formatBytes(oldNvs.size)} at 0x${oldNvs.offset.toString(16)})...\n`,
            "info",
          );
          nvsBackup = await this.readFlashRange(
            oldNvs.offset,
            oldNvs.size,
            "Backing up NVS settings...",
          );
        }

        if (oldStorage) {
          const layoutChanged =
            oldStorage.offset !== newStorage.offset ||
            oldStorage.size !== newStorage.size;
          if (layoutChanged || this.eraseBeforeFlash) {
            this.appendTerminal(
              `Storage partition ${layoutChanged ? `changing (0x${oldStorage.offset.toString(16)} [${this.formatBytes(oldStorage.size)}] -> 0x${newStorage.offset.toString(16)} [${this.formatBytes(newStorage.size)}])` : "being erased"}. Extracting books and bookmarks...\n`,
              "info",
            );
            try {
              const extractedFiles =
                await this.extractDeviceStorageFiles(oldStorage);
              if (extractedFiles.length > 0) {
                migrationBundle = this.packMigrationBundle(extractedFiles);
                this.appendTerminal(
                  `Packed ${extractedFiles.length} file(s) (${this.formatBytes(migrationBundle.length)}) for automatic restoration at 0x${newStorage.offset.toString(16)}.\n`,
                  "success",
                );
              } else {
                this.appendTerminal(
                  "No existing books or bookmarks found in storage partition.\n",
                  "info",
                );
              }
            } catch (extErr) {
              this.appendTerminal(
                `Storage extraction notice: ${extErr.message}\n`,
                "warning",
              );
            }
          } else {
            this.appendTerminal(
              `Storage partition layout unchanged (0x${newStorage.offset.toString(16)}, ${this.formatBytes(newStorage.size)}). Books and bookmarks preserved in place.\n`,
              "info",
            );
          }
        }
      }

      const filesToFlash = [];

      // If preserving content and not erasing entire flash, skip the 0x9000..factory.offset gap
      // inside bacain_factory.bin so the 0xFF padding does not wipe NVS settings at 0x9000.
      if (
        this.preserveContent &&
        !this.eraseBeforeFlash &&
        newFactory &&
        newFactory.offset >= 0x10000 &&
        fwBytes.length > newFactory.offset
      ) {
        filesToFlash.push({
          data: fwBytes.subarray(0, 0x9000),
          address: 0x0,
          name: `${fwName} (bootloader + partition table)`,
        });
        filesToFlash.push({
          data: fwBytes.subarray(newFactory.offset),
          address: newFactory.offset,
          name: `${fwName} (application @ 0x${newFactory.offset.toString(16)})`,
        });
      } else {
        filesToFlash.push({
          data: fwBytes,
          address: 0x0,
          name: fwName,
        });
      }

      if (nvsBackup && newNvs) {
        filesToFlash.push({
          data: nvsBackup,
          address: newNvs.offset,
          name: "NVS settings restore",
        });
      }

      if (migrationBundle && newStorage) {
        // Ensure the start and WL tail of the new storage partition are blank (0xFF)
        // so ESP-IDF formats the new FAT+WL partition cleanly before unpacking BMIG at +0x20000.
        if (!this.eraseBeforeFlash) {
          const blankHead = new Uint8Array(0x2000);
          blankHead.fill(0xff);
          filesToFlash.push({
            data: blankHead,
            address: newStorage.offset,
            name: "Storage header reset",
          });
          const blankTail = new Uint8Array(0x4000);
          blankTail.fill(0xff);
          filesToFlash.push({
            data: blankTail,
            address: newStorage.offset + newStorage.size - 0x4000,
            name: "Storage WL tail reset",
          });
        }
        filesToFlash.push({
          data: migrationBundle,
          address: newStorage.offset + 0x20000,
          name: `Preserved books & bookmarks bundle (${this.formatBytes(migrationBundle.length)})`,
        });
      }

      const totalBytesAll = filesToFlash.reduce(
        (sum, f) => sum + f.data.length,
        0,
      );
      const completedBytesBefore = new Array(filesToFlash.length).fill(0);
      for (let i = 1; i < filesToFlash.length; i++) {
        completedBytesBefore[i] =
          completedBytesBefore[i - 1] + filesToFlash[i - 1].data.length;
      }

      this.flashProgress = 0;
      this.flashStatusText = "Preparing flash parameters...";
      this.progressDetails = {
        written: 0,
        total: totalBytesAll,
        fileIndex: 1,
        totalFiles: filesToFlash.length,
      };
      this.updateUI();

      this.appendTerminal(
        "\n-------------------------------------------\n",
        "info",
      );
      for (const f of filesToFlash) {
        this.appendTerminal(
          `Segment: ${f.name} (${this.formatBytes(f.data.length)}) @ 0x${f.address.toString(16)}\n`,
          "info",
        );
      }
      this.appendTerminal(
        "-------------------------------------------\n\n",
        "info",
      );

      const flashOptions = {
        fileArray: filesToFlash.map((f) => ({
          data: f.data,
          address: f.address,
        })),
        flashSize: "keep",
        flashMode: "keep",
        flashFreq: "keep",
        eraseAll: this.eraseBeforeFlash,
        compress: true,
        reportProgress: (fileIndex, written, total) => {
          const baseWritten = completedBytesBefore[fileIndex] || 0;
          const overallWritten = baseWritten + written;
          const pct =
            totalBytesAll > 0
              ? Math.min(
                  100,
                  Math.max(0, (overallWritten / totalBytesAll) * 100),
                )
              : 0;

          this.flashProgress = pct;
          const seg = filesToFlash[fileIndex] || filesToFlash[0];
          this.flashStatusText = `Writing ${seg.name}...`;
          this.progressDetails = {
            written: overallWritten,
            total: totalBytesAll,
            fileIndex: fileIndex + 1,
            totalFiles: filesToFlash.length,
          };

          this.updateProgressBarUI();
        },
      };

      await this.esploader.writeFlash(flashOptions);

      this.flashProgress = 100;
      this.flashStatusText = "Flashing complete";
      this.progressDetails.written = totalBytesAll;
      this.updateProgressBarUI();

      this.appendTerminal("\nFlash write completed successfully.\n", "success");
      this.appendTerminal(
        `Wrote ${this.formatBytes(totalBytesAll)} across ${filesToFlash.length} segment(s).\n`,
        "success",
      );
    } catch (flashErr) {
      this.appendTerminal(`Flash Error: ${flashErr.message}\n`, "error");
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
    const container = this.querySelector("#flasherProgressContainer");
    const fill = this.querySelector("#flasherProgressBarFill");
    const pctText = this.querySelector("#flasherProgressPctText");
    const statusText = this.querySelector("#flasherProgressStatusText");
    const bytesText = this.querySelector("#flasherProgressBytesText");

    if (container) container.style.display = "block";
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
  appendTerminal(rawText, type = "normal") {
    // Strip ANSI escape codes
    const cleanText = rawText.replace(/\x1B\[[0-9;]*[a-zA-Z]/g, "");

    this.terminalLogs.push({ text: cleanText, type });
    if (this.terminalLogs.length > 500) {
      this.terminalLogs.shift();
    }

    const termEl = this.querySelector("#flasherTerminalOutput");
    if (termEl) {
      const span = document.createElement("span");
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
    const termEl = this.querySelector("#flasherTerminalOutput");
    if (!termEl) return;

    termEl.innerHTML = "";
    const fragment = document.createDocumentFragment();
    for (const log of this.terminalLogs) {
      const span = document.createElement("span");
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
    const termEl = this.querySelector("#flasherTerminalOutput");
    if (termEl) termEl.innerHTML = "";
  }

  formatBytes(bytes) {
    if (isNaN(bytes) || bytes === null || bytes < 0) return "0 B";
    if (bytes === 0) return "0 B";
    const k = 1024;
    const sizes = ["B", "KB", "MB", "GB"];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i];
  }

  formatDate(isoStr) {
    if (!isoStr) return "";
    try {
      const d = new Date(isoStr);
      if (isNaN(d.getTime())) return "";
      return d.toISOString().split("T")[0];
    } catch {
      return "";
    }
  }

  escapeHtml(str) {
    if (!str) return "";
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }
}

// Register Custom Element
if (!customElements.get("esp-web-flasher")) {
  customElements.define("esp-web-flasher", EspWebFlasher);
}

export { EspWebFlasher };
