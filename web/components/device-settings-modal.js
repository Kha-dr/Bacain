/**
 * device-settings-modal.js
 * Mini E-Reader - Device Settings Modal Custom Element (<device-settings-modal>)
 * Light DOM Web Component rendering:
 * 1. Appearance / Theme toggle
 * 2. Storage Target Destination (Internal Flash vs MicroSD)
 * 3. E-Paper Hardware Controls (Typography, Screen Refresh, Power)
 * 4. Firmware Flasher
 * 5. BLE Specifications
 * 6. Activity Log Console
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
    this._sdAvailable = false;
    this._logCount = 0;
    this._orientation = 0;
    this._buttonInvert = false;
    this._handleKeyDown = this._handleKeyDown.bind(this);
  }

  connectedCallback() {
    if (!this.querySelector('#settingsModal')) {
      this.render();
    }
    const currentTheme = document.documentElement.getAttribute('data-theme') || 'dark';
    this._updateThemeUI(currentTheme);

    try {
      const savedRot = localStorage.getItem('bacain_orientation');
      if (savedRot !== null) {
        this.setOrientation(parseInt(savedRot, 10));
      } else {
        this.setOrientation(this._orientation);
      }
      const savedBtnInv = localStorage.getItem('bacain_btn_invert');
      if (savedBtnInv !== null) {
        this.setButtonInvert(savedBtnInv === '1' || savedBtnInv === 'true');
      } else {
        this.setButtonInvert(this._buttonInvert);
      }
    } catch (_) {}

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
      <style>
        .btn-control:disabled { opacity: 0.45; cursor: not-allowed; }
      </style>
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
            
            <!-- Appearance / Theme -->
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
                <span class="control-label">Theme</span>
                <div class="control-btn-group">
                  <button id="themeLightBtn" class="btn-control" type="button">
                    <span>Light</span>
                  </button>
                  <button id="themeDarkBtn" class="btn-control active" type="button">
                    <span>Dark</span>
                  </button>
                </div>
              </div>
            </div>

            <!-- Storage Target -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect>
                  <rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect>
                  <line x1="6" y1="6" x2="6.01" y2="6"></line>
                  <line x1="6" y1="18" x2="6.01" y2="18"></line>
                </svg>
                Storage Target
              </div>
              <div class="control-btn-group">
                <button id="storageTargetFlashBtn" class="btn-control active" data-target="1" type="button">
                  <span>Internal Flash</span>
                </button>
                <button id="storageTargetSdBtn" class="btn-control" data-target="0" type="button" disabled title="MicroSD card not available">
                  <span>MicroSD Card</span>
                </button>
              </div>
              <div id="storageTargetDesc" class="control-hint">Target storage for books and progress.</div>
            </div>

            <!-- Typography -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <polyline points="4 7 4 4 20 4 20 7"></polyline>
                  <line x1="9" y1="20" x2="15" y2="20"></line>
                  <line x1="12" y1="4" x2="12" y2="20"></line>
                </svg>
                Typography
              </div>
              <div class="control-btn-group">
                <button id="font16Btn" class="btn-control active" data-font="0" type="button">
                  <span>Font 16 (8 lines)</span>
                </button>
                <button id="font12Btn" class="btn-control" data-font="1" type="button">
                  <span>Font 12 (11 lines)</span>
                </button>
              </div>
            </div>

            <!-- Screen Refresh -->
            <div class="control-card">
              <div class="control-inline-row" style="justify-content: space-between;">
                <span class="control-label">Screen</span>
                <button id="forceRefreshBtn" class="btn-secondary" type="button">Clean Screen</button>
              </div>
            </div>

            <!-- Navigation Hints -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <polygon points="3 11 22 2 13 21 11 13 3 11"></polygon>
                </svg>
                Navigation Hints
              </div>
              <div class="control-btn-group">
                <button id="navHintsShowBtn" class="btn-control active" type="button"><span>Show</span></button>
                <button id="navHintsHideBtn" class="btn-control" type="button"><span>Hide</span></button>
              </div>
              <div class="control-hint">Show or hide bottom navigation hints for more screen area.</div>
            </div>

            <!-- Screen Orientation -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"></path>
                </svg>
                Screen Orientation
              </div>
              <div class="control-inline-row" style="justify-content: space-between;">
                <label for="settingOrientation" class="control-label">Orientation</label>
                <select id="settingOrientation" class="control-select">
                  <option value="0">0° (Portrait Upright - Default)</option>
                  <option value="1">90° (Landscape Clockwise)</option>
                  <option value="2">180° (Inverted Portrait)</option>
                  <option value="3">270° (Landscape Counter-Clockwise)</option>
                </select>
              </div>
            </div>

            <!-- Button Navigation -->
            <div class="control-card">
              <div class="control-card-title">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                  <polyline points="7 13 12 18 17 13"></polyline>
                  <polyline points="7 6 12 11 17 6"></polyline>
                </svg>
                Button Navigation
              </div>
              <div class="control-inline-row">
                <label class="checkbox-label" for="settingButtonInvert">
                  <input type="checkbox" id="settingButtonInvert">
                  <span>Swap UP / DOWN buttons (UP=PWR / DOWN=BOOT)</span>
                </label>
              </div>
            </div>

            <!-- Power Management -->
            <div class="control-card">
              <div class="control-inline-row" style="justify-content: space-between;">
                <div style="display: flex; align-items: center; gap: 0.5rem;">
                  <label for="sleepTimeoutSelect" class="control-label">Auto-Sleep:</label>
                  <select id="sleepTimeoutSelect" class="control-select">
                    <option value="30">30s</option>
                    <option value="60" selected>60s</option>
                    <option value="120">2m</option>
                    <option value="300">5m</option>
                    <option value="0">Off</option>
                  </select>
                </div>
                <button id="triggerSleepBtn" class="btn-danger-outline" type="button">Sleep Now</button>
              </div>
            </div>

            <!-- Firmware Flasher -->
            <div class="control-card">
              <div class="control-inline-row" style="justify-content: space-between;">
                <span class="control-label">Firmware</span>
                <a href="flasher.html" class="btn-secondary" style="text-decoration: none;">Open Flasher</a>
              </div>
            </div>

            <!-- BLE Specifications -->
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
                <span class="spec-value mono">0000fe63 (Read / Notify)</span>
              </div>
              <div class="spec-row">
                <span class="spec-label">Chunk Payload</span>
                <span class="spec-value mono">506B Data + 2B Seq = 508B (512B MTU)</span>
              </div>
            </div>

            <!-- Activity Log Console -->
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
              <div id="activityLog" class="log-body"></div>
            </section>

          </div>
        </div>
      </div>
    `;
  }

  _updateThemeUI(theme) {
    const lightBtn = this.querySelector('#themeLightBtn');
    const darkBtn = this.querySelector('#themeDarkBtn');
    if (lightBtn) lightBtn.classList.toggle('active', theme === 'light');
    if (darkBtn) darkBtn.classList.toggle('active', theme === 'dark');

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

    // Appearance Theme
    const themeBtn = this.querySelector('#themeToggleBtn');
    if (themeBtn) {
      themeBtn.addEventListener('click', () => {
        this.toggleTheme();
      });
    }

    const themeLightBtn = this.querySelector('#themeLightBtn');
    if (themeLightBtn) {
      themeLightBtn.addEventListener('click', () => {
        this.setTheme('light');
      });
    }

    const themeDarkBtn = this.querySelector('#themeDarkBtn');
    if (themeDarkBtn) {
      themeDarkBtn.addEventListener('click', () => {
        this.setTheme('dark');
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
        if (sdBtn.disabled || !this._sdAvailable) return;
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

    // Navigation Hints
    const navHintsShowBtn = this.querySelector('#navHintsShowBtn');
    const navHintsHideBtn = this.querySelector('#navHintsHideBtn');

    if (navHintsShowBtn) {
      navHintsShowBtn.addEventListener('click', () => {
        this.dispatchEvent(new CustomEvent('nav-hints-change', {
          detail: { show: true },
          bubbles: true,
          composed: true
        }));
      });
    }

    if (navHintsHideBtn) {
      navHintsHideBtn.addEventListener('click', () => {
        this.dispatchEvent(new CustomEvent('nav-hints-change', {
          detail: { show: false },
          bubbles: true,
          composed: true
        }));
      });
    }

    // Screen Orientation
    const settingOrientation = this.querySelector('#settingOrientation');
    if (settingOrientation) {
      settingOrientation.addEventListener('change', () => {
        const rot = parseInt(settingOrientation.value, 10);
        this._orientation = rot;
        this.dispatchEvent(new CustomEvent('orientation-change', {
          detail: { rotation: rot },
          bubbles: true,
          composed: true
        }));
      });
    }

    // Button Navigation
    const settingButtonInvert = this.querySelector('#settingButtonInvert');
    if (settingButtonInvert) {
      settingButtonInvert.addEventListener('change', () => {
        this._buttonInvert = settingButtonInvert.checked;
        this.dispatchEvent(new CustomEvent('button-invert-change', {
          detail: { inverted: settingButtonInvert.checked },
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

    this._updateThemeUI(theme);

    this.dispatchEvent(new CustomEvent('theme-change', {
      detail: { theme },
      bubbles: true,
      composed: true
    }));
  }

  /**
   * Set MicroSD card availability status
   * @param {boolean} available
   */
  setSdCardAvailable(available) {
    this._sdAvailable = Boolean(available);
    const sdBtn = this.querySelector('#storageTargetSdBtn');
    const flashBtn = this.querySelector('#storageTargetFlashBtn');

    if (sdBtn) {
      if (this._sdAvailable) {
        sdBtn.removeAttribute('disabled');
        sdBtn.disabled = false;
        sdBtn.title = '';
      } else {
        sdBtn.setAttribute('disabled', '');
        sdBtn.disabled = true;
        sdBtn.title = 'MicroSD card not available';
        if (sdBtn.classList.contains('active')) {
          sdBtn.classList.remove('active');
          if (flashBtn) {
            flashBtn.classList.add('active');
          }
        }
      }
    }
  }

  /**
   * Set Storage Target (0 = MicroSD, 1 = Flash)
   * @param {number} target
   */
  setStorageTarget(target) {
    const numTarget = Number(target) === 1 ? 1 : 0;
    if (numTarget === 0 && !this._sdAvailable) {
      return;
    }

    const flashBtn = this.querySelector('#storageTargetFlashBtn');
    const sdBtn = this.querySelector('#storageTargetSdBtn');
    const desc = this.querySelector('#storageTargetDesc');

    if (sdBtn) sdBtn.classList.toggle('active', numTarget === 0);
    if (flashBtn) flashBtn.classList.toggle('active', numTarget === 1);

    if (desc) {
      desc.textContent = 'Target storage for books and progress.';
    }

    this.dispatchEvent(new CustomEvent('storage-target-change', {
      detail: { target: numTarget },
      bubbles: true,
      composed: true
    }));
  }

  /**
   * Set Storage Target Badge alias
   * @param {number} target
   */
  setStorageTargetBadge(target) {
    this.setStorageTarget(target);
  }

  /**
   * Set Navigation Hints display state
   * @param {boolean} show
   */
  setNavHints(show) {
    const isShow = Boolean(show);
    const showBtn = this.querySelector('#navHintsShowBtn');
    const hideBtn = this.querySelector('#navHintsHideBtn');

    if (showBtn) showBtn.classList.toggle('active', isShow);
    if (hideBtn) hideBtn.classList.toggle('active', !isShow);
  }

  /**
   * Set Screen Orientation
   * @param {number|string} rotation (0=0°, 1=90°, 2=180°, 3=270°)
   */
  setOrientation(rotation) {
    const val = parseInt(rotation, 10);
    this._orientation = isNaN(val) ? 0 : val;
    const select = this.querySelector('#settingOrientation');
    if (select) {
      select.value = String(this._orientation);
    }
  }

  /**
   * Get Screen Orientation
   * @returns {number}
   */
  getOrientation() {
    const select = this.querySelector('#settingOrientation');
    if (select) {
      return parseInt(select.value, 10);
    }
    return this._orientation;
  }

  get orientation() {
    return this.getOrientation();
  }

  set orientation(val) {
    this.setOrientation(val);
  }

  /**
   * Set Button Inversion
   * @param {boolean} inverted
   */
  setButtonInvert(inverted) {
    this._buttonInvert = Boolean(inverted);
    const checkbox = this.querySelector('#settingButtonInvert');
    if (checkbox) {
      checkbox.checked = this._buttonInvert;
    }
  }

  /**
   * Get Button Inversion
   * @returns {boolean}
   */
  getButtonInvert() {
    const checkbox = this.querySelector('#settingButtonInvert');
    if (checkbox) {
      return checkbox.checked;
    }
    return this._buttonInvert;
  }

  get buttonInvert() {
    return this.getButtonInvert();
  }

  set buttonInvert(val) {
    this.setButtonInvert(val);
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
