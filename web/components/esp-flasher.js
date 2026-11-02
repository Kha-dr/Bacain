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

    this.isConnected = false;
    this.isConnecting = false;
    this.isFlashing = false;
    this.isErasing = false;

    // Hardware Details
    this.chipName = null;
    this.chipDesc = null;
    this.macAddress = null;

    // Settings
    this.selectedBaudrate = 460800; // 921600, 460800, 230400, 115200
    this.eraseBeforeFlash = false;
    this.autoScroll = true;

    // Binary file slots: { id, label, offset, fileData (Uint8Array), fileName, fileSize }
    this.fileSlots = [
      {
        id: 'slot_1',
        label: 'Combined Firmware / Bootloader',
        offset: '0x0',
        fileData: null,
        fileName: '',
        fileSize: 0
      }
    ];

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
    this.loadBundledFactoryFirmware({ auto: true });
  }

  disconnectedCallback() {
    this.cleanUpConnection();
  }

  hasValidFirmware() {
    return Array.isArray(this.fileSlots) && this.fileSlots.some(s => s.fileData && s.fileData.length > 0);
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
    if (!this.isConnected) return '';
    return `
      <div class="flasher-device-info-grid">
        <div class="info-pill">
          <span class="info-label">Chip Target:</span>
          <span class="info-value highlight">${this.chipName || 'ESP32-S3'}</span>
        </div>
        <div class="info-pill">
          <span class="info-label">MAC Address:</span>
          <span class="info-value mono">${this.macAddress || 'Reading...'}</span>
        </div>
        <div class="info-pill">
          <span class="info-label">Handshake Baud:</span>
          <span class="info-value mono">115,200 ROM</span>
        </div>
        <div class="info-pill">
          <span class="info-label">Active Flash Baud:</span>
          <span class="info-value mono">${this.selectedBaudrate} bps</span>
        </div>
      </div>
    `;
  }

  updateUI() {
    // Status Badge
    const badge = this.querySelector('#flasherStatusBadge');
    if (badge) {
      badge.className = `card-badge ${this.isConnected ? 'badge-success' : 'badge-muted'}`;
      badge.textContent = this.isConnected ? 'Connected' : (this.isConnecting ? 'Connecting...' : 'Disconnected');
    }

    // Connect Button
    const connectBtn = this.querySelector('#flasherConnectBtn');
    if (connectBtn) {
      connectBtn.className = `btn-connect ${this.isConnected ? 'connected' : ''}`;
      connectBtn.disabled = !('serial' in navigator) || this.isConnecting || this.isFlashing;
      const dot = connectBtn.querySelector('.status-dot');
      if (dot) {
        dot.className = `status-dot ${this.isConnected ? 'connected' : (this.isConnecting ? 'connecting' : 'disconnected')}`;
      }
      const label = this.querySelector('#flasherConnectBtnText');
      if (label) {
        label.textContent = this.isConnected ? 'Disconnect Serial' : (this.isConnecting ? 'Handshaking...' : 'Connect Serial Port');
      }
    }

    // Secondary Action Buttons
    const resetBtn = this.querySelector('#flasherResetBtn');
    if (resetBtn) resetBtn.disabled = !this.isConnected || this.isFlashing;

    const eraseBtn = this.querySelector('#flasherEraseBtn');
    if (eraseBtn) eraseBtn.disabled = !this.isConnected || this.isFlashing;

    const baudSelect = this.querySelector('#flasherBaudSelect');
    if (baudSelect) baudSelect.disabled = this.isFlashing;

    const addSlotBtn = this.querySelector('#addSlotBtn');
    if (addSlotBtn) addSlotBtn.disabled = this.isFlashing;

    const eraseCheck = this.querySelector('#eraseBeforeFlashCheck');
    if (eraseCheck) eraseCheck.disabled = this.isFlashing;

    // Flash Action Button
    const startFlashBtn = this.querySelector('#startFlashBtn');
    if (startFlashBtn) {
      const hasFiles = this.hasValidFirmware();
      startFlashBtn.disabled = this.isFlashing || !hasFiles;
      const flashLabel = this.querySelector('#startFlashBtnText');
      if (flashLabel) {
        if (this.isFlashing) {
          flashLabel.textContent = 'Writing to ESP32 Flash...';
        } else if (!this.isConnected) {
          flashLabel.textContent = 'Connect & Flash Firmware';
        } else {
          flashLabel.textContent = 'Flash Firmware Now';
        }
      }
    }

    // Device Info Grid
    const infoContainer = this.querySelector('#flasherDeviceInfoContainer');
    if (infoContainer) {
      infoContainer.innerHTML = this.renderDeviceInfo();
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
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="10"></circle>
            <line x1="12" y1="8" x2="12" y2="12"></line>
            <line x1="12" y1="16" x2="12.01" y2="16"></line>
          </svg>
          <div>
            <strong>Web Serial API Not Supported:</strong> Your current browser cannot communicate directly with serial devices.
            Please open this page in <strong>Google Chrome</strong>, <strong>Microsoft Edge</strong>, or <strong>Opera</strong> on Windows, macOS, Linux, or ChromeOS.
          </div>
        </div>

        <!-- Connection & Hardware Header Card -->
        <div class="card flasher-header-card">
          <div class="card-header">
            <span class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect>
                <rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect>
                <line x1="6" y1="6" x2="6.01" y2="6"></line>
                <line x1="6" y1="18" x2="6.01" y2="18"></line>
              </svg>
              Serial Connection (ESP32-S3)
            </span>
            <span id="flasherStatusBadge" class="card-badge ${this.isConnected ? 'badge-success' : 'badge-muted'}">
              ${this.isConnected ? 'Connected' : (this.isConnecting ? 'Connecting...' : 'Disconnected')}
            </span>
          </div>

          <div class="flasher-connection-row">
            <div class="connection-actions">
              <button id="flasherConnectBtn" class="btn-connect ${this.isConnected ? 'connected' : ''}" ${this.isConnecting || this.isFlashing ? 'disabled' : ''}>
                <span class="status-dot ${this.isConnected ? 'connected' : (this.isConnecting ? 'connecting' : 'disconnected')}"></span>
                <span id="flasherConnectBtnText">${this.isConnected ? 'Disconnect Serial' : (this.isConnecting ? 'Handshaking...' : 'Connect Serial Port')}</span>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path>
                  <polyline points="15 3 21 3 21 9"></polyline>
                  <line x1="10" y1="14" x2="21" y2="3"></line>
                </svg>
              </button>

              <button id="flasherResetBtn" class="btn-secondary" ${!this.isConnected || this.isFlashing ? 'disabled' : ''} title="Trigger hardware reset to reboot device into app">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <polyline points="23 4 23 10 17 10"></polyline>
                  <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"></path>
                </svg>
                <span>Reboot Device</span>
              </button>

              <button id="flasherEraseBtn" class="btn-secondary btn-danger-text" ${!this.isConnected || this.isFlashing ? 'disabled' : ''} title="Completely erase SPI flash memory">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <polyline points="3 6 5 6 21 6"></polyline>
                  <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
                </svg>
                <span>Erase Entire Flash</span>
              </button>
            </div>

            <!-- Baudrate Selector -->
            <div class="flasher-baud-group">
              <label for="flasherBaudSelect" class="input-label">Flash Baudrate:</label>
              <select id="flasherBaudSelect" class="select-input" ${this.isFlashing ? 'disabled' : ''}>
                <option value="921600" ${this.selectedBaudrate === 921600 ? 'selected' : ''}>921,600 bps (Fastest)</option>
                <option value="460800" ${this.selectedBaudrate === 460800 ? 'selected' : ''}>460,800 bps (Standard Recommended)</option>
                <option value="230400" ${this.selectedBaudrate === 230400 ? 'selected' : ''}>230,400 bps (Stable)</option>
                <option value="115200" ${this.selectedBaudrate === 115200 ? 'selected' : ''}>115,200 bps (Safe Mode)</option>
              </select>
            </div>
          </div>

          <!-- Connected Device Telemetry -->
          <div id="flasherDeviceInfoContainer">
            ${this.renderDeviceInfo()}
          </div>
        </div>

        <!-- Firmware Files & Offsets Section -->
        <div class="card flasher-files-card">
          <div class="card-header">
            <span class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path>
                <polyline points="14 2 14 8 20 8"></polyline>
                <line x1="16" y1="13" x2="8" y2="13"></line>
                <line x1="16" y1="17" x2="8" y2="17"></line>
                <polyline points="10 9 9 9 8 9"></polyline>
              </svg>
              Firmware Binary Images (.bin)
            </span>

            <!-- Preset Buttons -->
            <div class="preset-buttons">
              <button id="presetCombinedBtn" class="btn-sm btn-secondary" title="Single binary containing all partitions at 0x0">
                Single Combined (0x0)
              </button>
              <button id="presetMultipartBtn" class="btn-sm btn-secondary" title="Multi-part ESP-IDF: bootloader @ 0x0, partitions @ 0x8000, app @ 0x20000">
                ESP-IDF Multi-Part
              </button>
              <button id="loadFactoryBinBtn" class="btn-sm btn-secondary" title="Auto-load bundled 01_factory.bin from workspace">
                Use 01_factory.bin
              </button>
            </div>
          </div>

          <div class="file-slots-list" id="fileSlotsContainer">
            ${this.renderFileSlots()}
          </div>

          <div class="slots-footer">
            <button id="addSlotBtn" class="btn-sm btn-secondary" ${this.isFlashing ? 'disabled' : ''}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:14px;height:14px;">
                <line x1="12" y1="5" x2="12" y2="19"></line>
                <line x1="5" y1="12" x2="19" y2="12"></line>
              </svg>
              Add Another Binary Slot
            </button>

            <label class="checkbox-label">
              <input type="checkbox" id="eraseBeforeFlashCheck" ${this.eraseBeforeFlash ? 'checked' : ''} ${this.isFlashing ? 'disabled' : ''}>
              <span>Erase entire flash before writing</span>
            </label>
          </div>

          <!-- Real Progress Bar (Zero Mock) -->
          <div id="flasherProgressContainer" class="flasher-progress-container" style="display: ${this.isFlashing || this.flashProgress > 0 ? 'block' : 'none'};">
            <div class="progress-meta">
              <span id="flasherProgressStatusText" class="progress-status-text">${this.flashStatusText}</span>
              <span id="flasherProgressPctText" class="progress-pct-text">${this.flashProgress.toFixed(1)}%</span>
            </div>
            <div class="meter-bar flasher-meter-bar">
              <div id="flasherProgressBarFill" class="meter-fill meter-flasher" style="width: ${this.flashProgress}%;"></div>
            </div>
            <div class="progress-bytes-meta" id="flasherProgressBytesText">
              ${this.progressDetails.total > 0 ? `${this.formatBytes(this.progressDetails.written)} / ${this.formatBytes(this.progressDetails.total)} (${this.progressDetails.fileIndex}/${this.progressDetails.totalFiles} files)` : ''}
            </div>
          </div>

          <!-- Flash Action Button -->
          <div class="flash-actions-bar">
            <button id="startFlashBtn" class="btn-primary btn-flash" ${this.isFlashing || !this.hasValidFirmware() ? 'disabled' : ''}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path>
              </svg>
              <span id="startFlashBtnText">${this.isFlashing ? 'Writing to ESP32 Flash...' : 'Flash Firmware Now'}</span>
            </button>
          </div>
        </div>

        <!-- Serial Terminal / Console Output -->
        <div class="card flasher-terminal-card">
          <div class="card-header terminal-header">
            <div class="terminal-title-group">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <polyline points="4 17 10 11 4 5"></polyline>
                <line x1="12" y1="19" x2="20" y2="19"></line>
              </svg>
              <span class="card-title">Serial Terminal & Flash Console</span>
            </div>

            <div class="terminal-actions">
              <label class="terminal-autoscroll">
                <input type="checkbox" id="terminalAutoScrollCheck" ${this.autoScroll ? 'checked' : ''}>
                <span>Auto-scroll</span>
              </label>

              <button id="copyTerminalBtn" class="btn-sm btn-secondary" title="Copy console log to clipboard">Copy</button>
              <button id="clearTerminalBtn" class="btn-sm btn-secondary" title="Clear console output">Clear</button>
            </div>
          </div>

          <div id="flasherTerminalOutput" class="flasher-terminal-output" tabindex="0" role="region" aria-label="Serial Terminal Output"></div>
        </div>

      </div>
    `;

    this.hydrateTerminal();
  }

  renderFileSlots() {
    return this.fileSlots.map((slot, index) => {
      const hasFile = slot.fileData && slot.fileData.length > 0;
      return `
        <div class="file-slot-row" data-slot-id="${slot.id}">
          <div class="slot-header-mobile">
            <span class="slot-number">#${index + 1}</span>
            <span class="slot-label">${slot.label}</span>
          </div>

          <div class="slot-offset-col">
            <label class="input-label-sm">Offset (Hex):</label>
            <input type="text" class="offset-input mono" data-slot-id="${slot.id}" value="${slot.offset}" placeholder="0x0" ${this.isFlashing ? 'disabled' : ''}>
          </div>

          <div class="slot-file-col">
            <label class="input-label-sm">Binary File (.bin):</label>
            <div class="file-picker-wrapper">
              <input type="file" class="file-picker-input" data-slot-id="${slot.id}" accept=".bin,application/octet-stream" ${this.isFlashing ? 'disabled' : ''}>
              <div class="file-info-badge ${hasFile ? 'loaded' : 'empty'}">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:14px;height:14px;flex-shrink:0;">
                  <path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"></path>
                  <polyline points="13 2 13 9 20 9"></polyline>
                </svg>
                <span class="file-name-text">${hasFile ? `${slot.fileName} (${this.formatBytes(slot.fileSize)})` : 'Choose binary file...'}</span>
              </div>
            </div>
          </div>

          <div class="slot-actions-col">
            ${this.fileSlots.length > 1 ? `
              <button class="btn-icon btn-remove-slot" data-slot-id="${slot.id}" title="Remove file slot" ${this.isFlashing ? 'disabled' : ''}>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <line x1="18" y1="6" x2="6" y2="18"></line>
                  <line x1="6" y1="6" x2="18" y2="18"></line>
                </svg>
              </button>
            ` : ''}
          </div>
        </div>
      `;
    }).join('');
  }

  initEventListeners() {
    // Connect / Disconnect button
    const connectBtn = this.querySelector('#flasherConnectBtn');
    if (connectBtn) {
      connectBtn.addEventListener('click', () => {
        if (this.isConnected) {
          this.disconnect();
        } else {
          this.connect();
        }
      });
    }

    // Reboot device button
    const resetBtn = this.querySelector('#flasherResetBtn');
    if (resetBtn) {
      resetBtn.addEventListener('click', () => this.rebootDevice());
    }

    // Erase entire flash button
    const eraseBtn = this.querySelector('#flasherEraseBtn');
    if (eraseBtn) {
      eraseBtn.addEventListener('click', () => this.eraseEntireFlash());
    }

    // Baudrate selector
    const baudSelect = this.querySelector('#flasherBaudSelect');
    if (baudSelect) {
      baudSelect.addEventListener('change', (e) => {
        this.selectedBaudrate = parseInt(e.target.value, 10) || 460800;
        this.appendTerminal(`Flash baudrate set to ${this.selectedBaudrate} bps.\n`, 'info');
      });
    }

    // Presets
    const presetCombinedBtn = this.querySelector('#presetCombinedBtn');
    if (presetCombinedBtn) {
      presetCombinedBtn.addEventListener('click', () => this.applyPresetCombined());
    }

    const presetMultipartBtn = this.querySelector('#presetMultipartBtn');
    if (presetMultipartBtn) {
      presetMultipartBtn.addEventListener('click', () => this.applyPresetMultipart());
    }

    const loadFactoryBinBtn = this.querySelector('#loadFactoryBinBtn');
    if (loadFactoryBinBtn) {
      loadFactoryBinBtn.addEventListener('click', () => this.loadBundledFactoryFirmware());
    }

    // Add slot button
    const addSlotBtn = this.querySelector('#addSlotBtn');
    if (addSlotBtn) {
      addSlotBtn.addEventListener('click', () => this.addFileSlot());
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

    // Terminal controls
    const clearTerminalBtn = this.querySelector('#clearTerminalBtn');
    if (clearTerminalBtn) {
      clearTerminalBtn.addEventListener('click', () => this.clearTerminal());
    }

    const copyTerminalBtn = this.querySelector('#copyTerminalBtn');
    if (copyTerminalBtn) {
      copyTerminalBtn.addEventListener('click', () => this.copyTerminal());
    }

    const autoScrollCheck = this.querySelector('#terminalAutoScrollCheck');
    if (autoScrollCheck) {
      autoScrollCheck.addEventListener('change', (e) => {
        this.autoScroll = e.target.checked;
      });
    }

    // Delegate events for dynamic file slots
    this.initSlotDelegation();

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

  initSlotDelegation() {
    const container = this.querySelector('#fileSlotsContainer');
    if (!container) return;

    // Offset input changes
    container.addEventListener('input', (e) => {
      if (e.target.classList.contains('offset-input')) {
        const slotId = e.target.getAttribute('data-slot-id');
        const slot = this.fileSlots.find(s => s.id === slotId);
        if (slot) {
          slot.offset = e.target.value;
        }
      }
    });

    // File picker selection
    container.addEventListener('change', async (e) => {
      if (e.target.classList.contains('file-picker-input')) {
        const slotId = e.target.getAttribute('data-slot-id');
        const slot = this.fileSlots.find(s => s.id === slotId);
        const file = e.target.files && e.target.files[0];
        if (slot && file) {
          try {
            const buffer = await file.arrayBuffer();
            slot.fileData = new Uint8Array(buffer);
            slot.fileName = file.name;
            slot.fileSize = file.size;
            this.appendTerminal(`Selected [${file.name}] (${this.formatBytes(file.size)}) for ${slot.label} @ ${slot.offset}\n`, 'info');
            this.updateSlotsUI();
          } catch (err) {
            this.appendTerminal(`Failed to read file ${file.name}: ${err.message}\n`, 'error');
          }
        }
      }
    });

    // Remove slot button
    container.addEventListener('click', (e) => {
      const btn = e.target.closest('.btn-remove-slot');
      if (btn) {
        const slotId = btn.getAttribute('data-slot-id');
        this.removeFileSlot(slotId);
      }
    });
  }

  updateSlotsUI() {
    const container = this.querySelector('#fileSlotsContainer');
    if (container) {
      container.innerHTML = this.renderFileSlots();
    }
    this.updateUI();
  }

  addFileSlot(label = 'Application / Partition', offset = '0x20000') {
    const newId = `slot_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
    this.fileSlots.push({
      id: newId,
      label: label,
      offset: offset,
      fileData: null,
      fileName: '',
      fileSize: 0
    });
    this.updateSlotsUI();
  }

  removeFileSlot(slotId) {
    if (this.fileSlots.length <= 1) return;
    this.fileSlots = this.fileSlots.filter(s => s.id !== slotId);
    this.updateSlotsUI();
  }

  applyPresetCombined() {
    this.fileSlots = [
      {
        id: 'slot_1',
        label: 'Combined Factory Firmware',
        offset: '0x0',
        fileData: null,
        fileName: '',
        fileSize: 0
      }
    ];
    this.updateSlotsUI();
    this.appendTerminal('Applied preset: Single Combined Binary @ 0x0.\n', 'info');
  }

  applyPresetMultipart() {
    this.fileSlots = [
      {
        id: 'slot_bootloader',
        label: 'Bootloader (bootloader.bin)',
        offset: '0x0',
        fileData: null,
        fileName: '',
        fileSize: 0
      },
      {
        id: 'slot_partitions',
        label: 'Partition Table (partition-table.bin)',
        offset: '0x8000',
        fileData: null,
        fileName: '',
        fileSize: 0
      },
      {
        id: 'slot_app',
        label: 'Application Firmware (reader_firmware.bin)',
        offset: '0x20000',
        fileData: null,
        fileName: '',
        fileSize: 0
      }
    ];
    this.updateSlotsUI();
    this.appendTerminal('Applied preset: ESP-IDF Multi-Part (0x0, 0x8000, 0x20000).\n', 'info');
  }

  async loadBundledFactoryFirmware(options = {}) {
    const isAuto = Boolean(options && options.auto);
    if (!isAuto) {
      this.appendTerminal('Searching for bundled firmware (manifest.json or factory image)...\n', 'info');
    }

    let manifest = null;
    let manifestBaseUrl = '';

    // 1. Try reading manifest.json from candidate paths
    const manifestCandidates = [
      'build/manifest.json',
      '../build/manifest.json',
      './build/manifest.json',
      '/build/manifest.json',
      'firmware/build/manifest.json',
      '../firmware/build/manifest.json',
      'firmware/manifest.json',
      './firmware/manifest.json',
      '../firmware/manifest.json',
      '/firmware/manifest.json'
    ];

    for (const mPath of manifestCandidates) {
      try {
        const resp = await fetch(mPath);
        if (resp.ok) {
          const contentType = resp.headers.get('content-type') || '';
          if (contentType.includes('application/json') || mPath.endsWith('.json')) {
            const json = await resp.json();
            if (json && json.parts && Array.isArray(json.parts)) {
              manifest = json;
              manifestBaseUrl = mPath.substring(0, mPath.lastIndexOf('/') + 1);
              this.appendTerminal(`Found firmware manifest at "${mPath}" (Target: ${json.target || 'ESP32-S3'}, Version: ${json.version || '1.0.0'}).\n`, 'info');
              if (json.baudrate) {
                this.selectedBaudrate = json.baudrate;
                const baudEl = this.querySelector('#flasherBaudSelect');
                if (baudEl) baudEl.value = json.baudrate;
              }
              break;
            }
          }
        }
      } catch (_) {
        // try next candidate
      }
    }

    // 2. Prepare binary candidate paths
    const binaryCandidates = [];
    if (manifest) {
      if (manifest.factory_bin) {
        if (manifestBaseUrl) {
          binaryCandidates.push(manifestBaseUrl + manifest.factory_bin);
        }
        binaryCandidates.push(manifest.factory_bin);
      }
      if (manifest.parts && manifest.parts.length > 0) {
        for (const part of manifest.parts) {
          if (part.path) {
            if (manifestBaseUrl) {
              binaryCandidates.push(manifestBaseUrl + part.path);
            }
            binaryCandidates.push(part.path);
          }
        }
      }
    }

    // Add robust candidate paths covering various hosting structures
    binaryCandidates.push(
      'build/bacain_factory.bin',
      '../build/bacain_factory.bin',
      './build/bacain_factory.bin',
      '/build/bacain_factory.bin',
      'build/mini_e_reader_factory.bin',
      '../build/mini_e_reader_factory.bin',
      'firmware/build/bacain_factory.bin',
      '../firmware/build/bacain_factory.bin',
      'firmware/bacain_factory.bin',
      '../firmware/bacain_factory.bin'
    );

    const uniqueCandidates = [...new Set(binaryCandidates)];

    let found = false;
    for (const binPath of uniqueCandidates) {
      try {
        const resp = await fetch(binPath);
        if (resp.ok) {
          const buffer = await resp.arrayBuffer();
          const data = new Uint8Array(buffer);
          if (data.length > 0) {
            const fileName = binPath.split('/').pop();
            const desc = (manifest && manifest.parts && manifest.parts[0] && manifest.parts[0].description)
              ? manifest.parts[0].description
              : 'Factory Firmware';

            this.fileSlots = [
              {
                id: 'slot_factory',
                label: `${desc} (${fileName})`,
                offset: '0x0',
                fileData: data,
                fileName: fileName,
                fileSize: data.length
              }
            ];
            this.updateSlotsUI();
            this.appendTerminal(`Successfully loaded ${fileName} (${this.formatBytes(data.length)}) from "${binPath}" at offset 0x0!\n`, 'success');
            found = true;
            break;
          }
        }
      } catch (_) {
        // try next candidate
      }
    }

    if (!found) {
      if (!isAuto) {
        this.appendTerminal(
          'Could not fetch 01_factory.bin via HTTP. Please select firmware/01_factory.bin directly with the file picker.\n',
          'warning'
        );
      }
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
    this.render();

    try {
      this.appendTerminal('Requesting Serial Port... Please select your ESP32-S3 device in the browser dialog.\n', 'info');

      let port;
      try {
        port = await navigator.serial.requestPort({ filters: SERIAL_DEVICE_FILTERS });
      } catch (filterErr) {
        if (filterErr.name === 'NotFoundError') {
          this.appendTerminal('Serial port selection cancelled by user.\n', 'warning');
          this.isConnecting = false;
          this.render();
          return;
        }
        // Fallback: If filtered prompt fails, try unfiltered selection
        this.appendTerminal(`Filtered selection note: ${filterErr.message}. Showing all serial ports...\n`, 'info');
        port = await navigator.serial.requestPort();
      }

      this.port = port;
      this.appendTerminal('Loading esptool-js flashing engine...\n', 'info');
      const { ESPLoader, Transport } = await loadEsptool();

      this.appendTerminal('Initializing WebSerial Transport...\n', 'info');
      this.transport = new Transport(this.port, true);

      const targetBaud = this.selectedBaudrate || 460800;
      this.appendTerminal(`Opening connection (ROM Handshake: 115200, Flash: ${targetBaud} bps)...\n`, 'info');

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

      this.appendTerminal('Connecting to ROM bootloader (auto-reset sync)...\n', 'info');
      const chip = await this.esploader.main();

      this.chipName = chip || (this.esploader.chip ? this.esploader.chip.CHIP_NAME : 'ESP32-S3');
      this.appendTerminal(`\n=== Connected to ${this.chipName} ===\n`, 'success');

      // Query MAC Address
      try {
        if (this.esploader.chip && typeof this.esploader.chip.readMac === 'function') {
          this.macAddress = await this.esploader.chip.readMac(this.esploader);
          this.appendTerminal(`MAC Address: ${this.macAddress}\n`, 'success');
        }
      } catch (macErr) {
        this.appendTerminal(`MAC query returned: ${macErr.message}\n`, 'info');
      }

      // Query Chip Model / Description
      try {
        if (this.esploader.chip && typeof this.esploader.chip.getChipDescription === 'function') {
          const desc = await this.esploader.chip.getChipDescription(this.esploader);
          if (desc) {
            this.chipDesc = desc;
            this.appendTerminal(`Chip Model Description: ${desc}\n`, 'info');
          }
        }
      } catch (descErr) {
        // Non-fatal
      }

      this.isConnected = true;
      this.isConnecting = false;
      this.render();

    } catch (err) {
      this.isConnected = false;
      this.isConnecting = false;
      this.appendTerminal(`Serial Connection Error: ${err.message}\n`, 'error');
      await this.cleanUpConnection();
      this.render();
    }
  }

  async disconnect() {
    this.appendTerminal('Disconnecting from serial port...\n', 'info');
    await this.cleanUpConnection();
    this.isConnected = false;
    this.isConnecting = false;
    this.isFlashing = false;
    this.appendTerminal('Disconnected from device.\n', 'info');
    this.render();
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

    const confirmed = window.confirm(
      'WARNING: Are you sure you want to completely erase the flash memory on your ESP32-S3? All firmware and data will be erased.'
    );
    if (!confirmed) {
      this.appendTerminal('Flash erase aborted by user.\n', 'info');
      return;
    }

    try {
      this.isFlashing = true;
      this.flashStatusText = 'Erasing flash memory...';
      this.flashProgress = 0;
      this.render();

      this.appendTerminal('Erasing entire flash chip... This can take 10-30 seconds. Please wait.\n', 'warning');
      await this.esploader.eraseFlash();
      this.appendTerminal('Flash memory erased completely.\n', 'success');

    } catch (err) {
      this.appendTerminal(`Erase flash failed: ${err.message}\n`, 'error');
    } finally {
      this.isFlashing = false;
      this.flashStatusText = 'Ready';
      this.render();
    }
  }

  // ==========================================================================
  // Flash Firmware Execution (Zero Mock - Pure Real Progress)
  // ==========================================================================
  async startFlashProcess() {
    if (!this.isConnected || !this.esploader) {
      this.appendTerminal('Device not connected. Opening serial port selection dialog...\n', 'info');
      await this.connect();
      if (!this.isConnected || !this.esploader) {
        this.appendTerminal('Flashing cancelled: serial connection not established.\n', 'warning');
        return;
      }
    }

    // Validate selected files and hex offsets
    const filesToFlash = [];
    for (let i = 0; i < this.fileSlots.length; i++) {
      const slot = this.fileSlots[i];
      if (!slot.fileData || slot.fileData.length === 0) {
        continue;
      }

      const offsetStr = slot.offset.trim();
      let offsetNum;
      if (offsetStr.startsWith('0x') || offsetStr.startsWith('0X')) {
        offsetNum = parseInt(offsetStr, 16);
      } else {
        offsetNum = parseInt(offsetStr, 10);
      }

      if (isNaN(offsetNum) || offsetNum < 0) {
        const errorMsg = `Invalid hex offset for slot #${i + 1} (${slot.label}): "${slot.offset}". Format must be e.g. 0x0 or 0x20000.`;
        this.appendTerminal(errorMsg + '\n', 'error');
        alert(errorMsg);
        return;
      }

      filesToFlash.push({
        data: slot.fileData, // Uint8Array
        address: offsetNum,
        name: slot.fileName || `file_${i + 1}.bin`
      });
    }

    if (filesToFlash.length === 0) {
      const msg = 'Please select at least one valid firmware binary (.bin) file before flashing.';
      this.appendTerminal(msg + '\n', 'warning');
      alert(msg);
      return;
    }

    const totalBytesAll = filesToFlash.reduce((acc, f) => acc + f.data.length, 0);

    this.isFlashing = true;
    this.flashProgress = 0;
    this.flashStatusText = 'Preparing flash parameters...';
    this.progressDetails = {
      written: 0,
      total: totalBytesAll,
      fileIndex: 1,
      totalFiles: filesToFlash.length
    };
    this.render();

    this.appendTerminal('\n===========================================\n', 'info');
    this.appendTerminal(`Starting firmware flash: ${filesToFlash.length} binary file(s), Total: ${this.formatBytes(totalBytesAll)}\n`, 'info');
    for (const f of filesToFlash) {
      this.appendTerminal(`  • Offset 0x${f.address.toString(16).toUpperCase().padStart(5, '0')}: ${f.name} (${this.formatBytes(f.data.length)})\n`, 'info');
    }
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
          let completedPrev = 0;
          for (let j = 0; j < fileIndex; j++) {
            completedPrev += filesToFlash[j].data.length;
          }
          const overallWritten = completedPrev + written;
          const pct = Math.min(100, Math.max(0, (overallWritten / totalBytesAll) * 100));

          this.flashProgress = pct;
          this.flashStatusText = `Writing file ${fileIndex + 1} of ${filesToFlash.length}: ${filesToFlash[fileIndex].name}...`;
          this.progressDetails = {
            written: overallWritten,
            total: totalBytesAll,
            fileIndex: fileIndex + 1,
            totalFiles: filesToFlash.length
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

      this.appendTerminal('\n*** FLASH WRITE COMPLETED SUCCESSFULLY! ***\n', 'success');
      this.appendTerminal(`Wrote ${this.formatBytes(totalBytesAll)} across ${filesToFlash.length} partition(s).\n`, 'success');
      this.appendTerminal('Click "Reboot Device" to restart your Mini E-Reader into new firmware.\n', 'info');

    } catch (flashErr) {
      this.appendTerminal(`Flash Error: ${flashErr.message}\n`, 'error');
      this.flashStatusText = `Error: ${flashErr.message}`;
    } finally {
      this.isFlashing = false;
      this.render();
    }
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
      bytesText.textContent = `${this.formatBytes(this.progressDetails.written)} / ${this.formatBytes(this.progressDetails.total)} (${this.progressDetails.fileIndex}/${this.progressDetails.totalFiles} files)`;
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

  async copyTerminal() {
    const allText = this.terminalLogs.map(l => l.text).join('');
    try {
      await navigator.clipboard.writeText(allText);
      this.appendTerminal('[System] Console log copied to clipboard.\n', 'info');
    } catch (e) {
      this.appendTerminal(`[System] Clipboard copy failed: ${e.message}\n`, 'warning');
    }
  }

  formatBytes(bytes) {
    if (isNaN(bytes) || bytes === null || bytes < 0) return '0 B';
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  }
}

// Register Custom Element
customElements.define('esp-web-flasher', EspWebFlasher);

export { EspWebFlasher };
