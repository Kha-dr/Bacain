/**
 * device-settings-modal.js
 * BacaIn PWA - Device Settings Modal Custom Element (<device-settings-modal>)
 * Light DOM Web Component rendering:
 * 1. Appearance / Theme toggle
 * 2. Storage Target Destination (Flash vs MicroSD)
 * 3. E-Paper Hardware Controls (Font, Refresh, Auto-Sleep)
 * 4. BLE Technical Specifications
 * 5. Activity Log Console
 */

function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

class DeviceSettingsModal extends HTMLElement {
  constructor() {
    super();
    this._logCount = 0;
    this._handleKeyDown = this._handleKeyDown.bind(this);
  }

  connectedCallback() {
    if (!this.querySelector('#settingsModal')) {
      this.render();
    }
    this.bindEvents();
    document.addEventListener('keydown', this._handleKeyDown);
  }

  disconnectedCallback() {
    document.removeEventListener('keydown', this._handleKeyDown);
  }

  _handleKeyDown(e) {
    if (e.key === 'Escape') {
      const modal = this.querySelector('#settingsModal');
      if (modal && modal.classList.contains('active')) {
        this.close();
      }
    }
  }

  render() {
    this.innerHTML = `
      <div id="settingsModal" class="modal-backdrop" aria-hidden="true">
        <div class="modal-card" style="max-width: 640px; max-height: 88vh; display: flex; flex-direction: column; padding: 1.25rem;">
          
          <!-- Modal Header -->
          <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 1rem; padding-bottom: 0.75rem; border-bottom: 1px solid var(--border-color);">
            <h3 class="modal-title" style="margin-bottom: 0; display: flex; align-items: center; gap: 0.5rem;">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width: 20px; height: 20px;">
                <circle cx="12" cy="12" r="3"></circle>
                <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
              </svg>
              Settings
            </h3>
            <button id="closeSettingsBtn" class="btn-icon" aria-label="Close Settings" title="Close Settings">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <line x1="18" y1="6" x2="6" y2="18"></line>
                <line x1="6" y1="6" x2="18" y2="18"></line>
              </svg>
            </button>
          </div>

          <!-- Modal Body Content -->
          <div class="modal-settings-body" style="overflow-y: auto; padding-right: 4px; display: flex; flex-direction: column; gap: 1rem;">
            
            <!-- Bagian 1: Preferensi Tampilan & Tema -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <circle cx="12" cy="12" r="5"></circle>
                  <line x1="12" y1="1" x2="12" y2="3"></line>
                  <line x1="12" y1="21" x2="12" y2="23"></line>
                  <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line>
                  <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line>
                  <line x1="1" y1="12" x2="3" y2="12"></line>
                  <line x1="21" y1="12" x2="23" y2="12"></line>
                  <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line>
                  <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line>
                </svg>
                Appearance
              </div>
              <div class="control-inline-row" style="justify-content: space-between;">
                <span class="control-label">Interface Theme</span>
                <button id="themeToggleBtn" class="btn-icon" aria-label="Toggle Theme" title="Toggle Dark/Light Mode">
                  <svg id="themeIconSun" class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <circle cx="12" cy="12" r="5"></circle>
                    <line x1="12" y1="1" x2="12" y2="3"></line>
                    <line x1="12" y1="21" x2="12" y2="23"></line>
                    <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line>
                    <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line>
                    <line x1="1" y1="12" x2="3" y2="12"></line>
                    <line x1="21" y1="12" x2="23" y2="12"></line>
                    <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line>
                    <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line>
                  </svg>
                  <svg id="themeIconMoon" class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="display: none;">
                    <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>
                  </svg>
                </button>
              </div>
            </div>

            <!-- Bagian 2: Target Storage Sinkronisasi -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect>
                  <rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect>
                  <line x1="6" y1="6" x2="6.01" y2="6"></line>
                  <line x1="6" y1="18" x2="6.01" y2="18"></line>
                </svg>
                Storage Target Destination
              </div>
              <div class="control-btn-group">
                <button id="storageTargetFlashBtn" class="btn-control" data-target="1" type="button">
                  <span>⚡ Internal Flash (/spiflash)</span>
                </button>
                <button id="storageTargetSdBtn" class="btn-control active" data-target="0" type="button">
                  <span>📁 MicroSD (/sdcard)</span>
                </button>
              </div>
              <div id="storageTargetDesc" class="control-hint">Books and reading progress are synced to MicroSD card.</div>
            </div>

            <!-- Bagian 3: Kontrol Hardware E-Paper -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <polyline points="4 7 4 4 20 4 20 7"></polyline>
                  <line x1="9" y1="20" x2="15" y2="20"></line>
                  <line x1="12" y1="4" x2="12" y2="20"></line>
                </svg>
                Display & Typography
              </div>
              <div class="control-btn-group">
                <button id="font16Btn" class="btn-control active" data-font="0" type="button">
                  <span>Font 16 (Standard - 8 lines)</span>
                </button>
                <button id="font12Btn" class="btn-control" data-font="1" type="button">
                  <span>Font 12 (Compact - 11 lines)</span>
                </button>
                <button id="forceRefreshBtn" class="btn-control-secondary" type="button">
                  <span>🔄 Clean Screen (Clear Ghosting)</span>
                </button>
              </div>
            </div>

            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <path d="M18.36 6.64a9 9 0 1 1-12.73 0"></path>
                  <line x1="12" y1="2" x2="12" y2="12"></line>
                </svg>
                Power Management
              </div>
              <div class="control-inline-row">
                <label for="sleepTimeoutSelect" class="control-label">Auto-Sleep Timeout:</label>
                <select id="sleepTimeoutSelect" class="control-select">
                  <option value="30">30 seconds</option>
                  <option value="60" selected>60 seconds (Default)</option>
                  <option value="120">2 minutes</option>
                  <option value="300">5 minutes</option>
                  <option value="0">Disabled (Stay Awake)</option>
                </select>
              </div>
              <div class="control-actions-row" style="margin-top: 0.5rem;">
                <button id="triggerSleepBtn" class="btn-danger-outline" type="button">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:14px;height:14px;">
                    <path d="M18.36 6.64a9 9 0 1 1-12.73 0"></path>
                    <line x1="12" y1="2" x2="12" y2="12"></line>
                  </svg>
                  <span>Put BacaIn to Sleep</span>
                </button>
              </div>
            </div>

            <!-- Bagian 4: Spesifikasi Teknis BLE -->
            <div class="telemetry-specs">
              <div class="spec-row">
                <span class="spec-label">Service UUID</span>
                <span class="spec-value mono">0000fe60-0000-1000-8000-00805f9b34fb</span>
              </div>
              <div class="spec-row">
                <span class="spec-label">Control Characteristic</span>
                <span class="spec-value mono">0000fe61 (Notify / Write)</span>
              </div>
              <div class="spec-row">
                <span class="spec-label">Data Characteristic</span>
                <span class="spec-value mono">0000fe62 (Write Without Response)</span>
              </div>
              <div class="spec-row">
                <span class="spec-label">Status Characteristic</span>
                <span class="spec-value mono">0000fe63 (Read / Notify - 12B payload)</span>
              </div>
              <div class="spec-row">
                <span class="spec-label">Chunk Payload</span>
                <span class="spec-value mono">506B Data + 2B Seq = 508B (512B MTU)</span>
              </div>
              <div class="spec-row">
                <span class="spec-label">Target Directory</span>
                <span class="spec-value mono">/spiflash/books/ atau /sdcard/books/</span>
              </div>
            </div>

            <!-- Bagian 5: Activity Log console (#activityLog, #clearLogBtn) -->
            <section id="logSection" class="log-section" style="margin-top: 0.5rem;">
              <div id="logHeader" class="log-header" style="cursor: pointer;">
                <div class="log-header-left">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <polyline points="4 17 10 11 4 5"></polyline>
                    <line x1="12" y1="19" x2="20" y2="19"></line>
                  </svg>
                  <span>Activity Log</span>
                  <span id="logCountBadge" class="log-badge">0</span>
                </div>
                <div class="log-actions">
                  <button id="clearLogBtn" class="btn-clear-log" title="Clear log" type="button">Clear</button>
                  <span id="logToggleIcon">&#x25BC;</span>
                </div>
              </div>
              <div id="activityLog" class="log-body">
                <!-- Log entries appear here -->
              </div>
            </section>

          </div>
        </div>
      </div>
    `;
  }

  bindEvents() {
    const modal = this.querySelector('#settingsModal');
    const closeBtn = this.querySelector('#closeSettingsBtn');

    if (closeBtn) {
      closeBtn.addEventListener('click', () => this.close());
    }

    if (modal) {
      modal.addEventListener('click', (e) => {
        if (e.target === modal) {
          this.close();
        }
      });
    }

    // Appearance Theme Toggle
    const themeBtn = this.querySelector('#themeToggleBtn');
    if (themeBtn) {
      themeBtn.addEventListener('click', () => {
        this.toggleTheme();
      });
    }

    // Storage Destination Targets
    const flashBtn = this.querySelector('#storageTargetFlashBtn');
    const sdBtn = this.querySelector('#storageTargetSdBtn');

    if (flashBtn) {
      flashBtn.addEventListener('click', () => {
        this.setStorageTarget(1);
      });
    }
    if (sdBtn) {
      sdBtn.addEventListener('click', () => {
        this.setStorageTarget(0);
      });
    }

    // Typography
    const font16Btn = this.querySelector('#font16Btn');
    const font12Btn = this.querySelector('#font12Btn');
    const forceRefreshBtn = this.querySelector('#forceRefreshBtn');

    if (font16Btn && font12Btn) {
      font16Btn.addEventListener('click', () => {
        font16Btn.classList.add('active');
        font12Btn.classList.remove('active');
        this.dispatchEvent(new CustomEvent('font-change', {
          detail: { font: 0 },
          bubbles: true,
          composed: true
        }));
      });

      font12Btn.addEventListener('click', () => {
        font12Btn.classList.add('active');
        font16Btn.classList.remove('active');
        this.dispatchEvent(new CustomEvent('font-change', {
          detail: { font: 1 },
          bubbles: true,
          composed: true
        }));
      });
    }

    if (forceRefreshBtn) {
      forceRefreshBtn.addEventListener('click', () => {
        this.dispatchEvent(new CustomEvent('force-refresh', {
          bubbles: true,
          composed: true
        }));
      });
    }

    // Power Management
    const sleepTimeoutSelect = this.querySelector('#sleepTimeoutSelect');
    if (sleepTimeoutSelect) {
      sleepTimeoutSelect.addEventListener('change', () => {
        this.dispatchEvent(new CustomEvent('sleep-timeout-change', {
          detail: { timeout: Number(sleepTimeoutSelect.value) },
          bubbles: true,
          composed: true
        }));
      });
    }

    const triggerSleepBtn = this.querySelector('#triggerSleepBtn');
    if (triggerSleepBtn) {
      triggerSleepBtn.addEventListener('click', () => {
        this.dispatchEvent(new CustomEvent('trigger-sleep', {
          bubbles: true,
          composed: true
        }));
      });
    }

    // Activity Log Controls
    const logHeader = this.querySelector('#logHeader');
    const activityLog = this.querySelector('#activityLog');
    const logToggleIcon = this.querySelector('#logToggleIcon');
    const clearLogBtn = this.querySelector('#clearLogBtn');

    if (logHeader && activityLog) {
      logHeader.addEventListener('click', (e) => {
        if (e.target === clearLogBtn || (clearLogBtn && clearLogBtn.contains(e.target))) {
          return;
        }
        const isCollapsed = activityLog.style.display === 'none';
        activityLog.style.display = isCollapsed ? 'block' : 'none';
        if (logToggleIcon) {
          logToggleIcon.textContent = isCollapsed ? '\u25BC' : '\u25B6';
        }
      });
    }

    if (clearLogBtn) {
      clearLogBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        if (activityLog) activityLog.innerHTML = '';
        this._logCount = 0;
        const countBadge = this.querySelector('#logCountBadge');
        if (countBadge) countBadge.textContent = '0';
        this.dispatchEvent(new CustomEvent('clear-log', {
          bubbles: true,
          composed: true
        }));
      });
    }
  }

  /**
   * Open Modal
   */
  open() {
    const modal = this.querySelector('#settingsModal');
    if (modal) {
      modal.classList.add('active');
      modal.setAttribute('aria-hidden', 'false');
      this.dispatchEvent(new CustomEvent('modal-opened', { bubbles: true, composed: true }));
    }
  }

  /**
   * Close Modal
   */
  close() {
    const modal = this.querySelector('#settingsModal');
    if (modal) {
      modal.classList.remove('active');
      modal.setAttribute('aria-hidden', 'true');
      this.dispatchEvent(new CustomEvent('modal-closed', { bubbles: true, composed: true }));
    }
  }

  /**
   * Toggle Light/Dark Theme
   */
  toggleTheme() {
    const current = document.documentElement.getAttribute('data-theme') || 'dark';
    const next = current === 'dark' ? 'light' : 'dark';
    this.setTheme(next);
  }

  /**
   * Set interface theme
   * @param {'dark'|'light'} theme
   */
  setTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    try {
      localStorage.setItem('bacain_theme', theme);
    } catch (_) {}

    const sun = this.querySelector('#themeIconSun');
    const moon = this.querySelector('#themeIconMoon');
    if (sun && moon) {
      if (theme === 'dark') {
        sun.style.display = 'block';
        moon.style.display = 'none';
      } else {
        sun.style.display = 'none';
        moon.style.display = 'block';
      }
    }

    this.dispatchEvent(new CustomEvent('theme-change', {
      detail: { theme },
      bubbles: true,
      composed: true
    }));
  }

  /**
   * Set Storage Target (0 = MicroSD, 1 = Flash)
   * @param {number} target
   */
  setStorageTarget(target) {
    const flashBtn = this.querySelector('#storageTargetFlashBtn');
    const sdBtn = this.querySelector('#storageTargetSdBtn');
    const desc = this.querySelector('#storageTargetDesc');

    const numTarget = Number(target) === 1 ? 1 : 0;

    if (sdBtn) sdBtn.classList.toggle('active', numTarget === 0);
    if (flashBtn) flashBtn.classList.toggle('active', numTarget === 1);

    if (desc) {
      desc.textContent = numTarget === 0
        ? 'Books and reading progress are synced to MicroSD card.'
        : 'Books and reading progress are synced to ESP32 internal flash memory (/spiflash).';
    }

    this.dispatchEvent(new CustomEvent('storage-target-change', {
      detail: { target: numTarget },
      bubbles: true,
      composed: true
    }));
  }

  /**
   * Append log message to Activity Log console
   * @param {string} msg
   * @param {'info'|'success'|'warning'|'error'} type
   */
  appendLog(msg, type = 'info') {
    const logBody = this.querySelector('#activityLog');
    if (!logBody) return;

    this._logCount++;
    const countBadge = this.querySelector('#logCountBadge');
    if (countBadge) countBadge.textContent = String(this._logCount);

    const now = new Date();
    const timeStr = [
      String(now.getHours()).padStart(2, '0'),
      String(now.getMinutes()).padStart(2, '0'),
      String(now.getSeconds()).padStart(2, '0')
    ].join(':');

    const entry = document.createElement('div');
    entry.className = `log-entry log-${type}`;
    entry.style.fontFamily = 'var(--font-mono)';
    entry.style.fontSize = '0.78rem';
    entry.style.padding = '0.2rem 0';
    entry.style.borderBottom = '1px solid var(--border-subtle)';
    entry.style.lineHeight = '1.4';

    let colorStyle = 'var(--text-secondary)';
    if (type === 'success') colorStyle = 'var(--success-color)';
    else if (type === 'warning') colorStyle = 'var(--warning-color)';
    else if (type === 'error') colorStyle = 'var(--danger-color)';

    entry.innerHTML = `
      <span style="color: var(--text-muted); margin-right: 0.4rem;">[${timeStr}]</span>
      <span style="color: ${colorStyle};">${escapeHtml(msg)}</span>
    `;

    logBody.appendChild(entry);

    // Limit log memory to prevent browser slowdown (keep last 200 items)
    while (logBody.children.length > 200) {
      logBody.removeChild(logBody.firstElementChild);
    }

    logBody.scrollTop = logBody.scrollHeight;
  }
}

if (!customElements.get('device-settings-modal')) {
  customElements.define('device-settings-modal', DeviceSettingsModal);
}
if (typeof window !== 'undefined') {
  window.DeviceSettingsModal = DeviceSettingsModal;
}
