/**
 * status-dashboard.js
 * BacaIn PWA - Status Dashboard Custom Element (<status-dashboard>)
 * Light DOM Web Component:
 * - Device connection status bar
 * - 3 status cards: Battery, Internal Flash, MicroSD
 */

function formatKb(kb) {
  if (isNaN(kb) || kb === null || kb < 0) return '0 KB';
  if (kb < 1024) return `${kb} KB`;
  if (kb < 1024 * 1024) return `${(kb / 1024).toFixed(1)} MB`;
  return `${(kb / (1024 * 1024)).toFixed(2)} GB`;
}

class StatusDashboard extends HTMLElement {
  constructor() {
    super();
    this.isDeviceConnected = false;
    this.currentStorageTarget = 1; // 0: SD, 1: Flash (Default to Flash)
    this._flashFreeKb = 0;
    this._flashTotalKb = 0;
    this._sdMounted = false;
    this._sdFreeKb = 0;
    this._sdTotalKb = 0;
  }

  connectedCallback() {
    if (!this.querySelector('.dashboard-grid')) {
      this.render();
    }
    this.bindEvents();
  }

  render() {
    this.innerHTML = `
      <div class="device-status-line" style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 0.75rem; padding: 0.5rem 0.75rem; background: var(--bg-card); border: 1px solid var(--border-color); border-radius: var(--radius-md);">
        <div style="display: flex; align-items: center; gap: 0.5rem;">
          <span style="color: var(--text-muted); font-size: 0.875rem;">Device:</span>
          <strong id="deviceNameText">Device</strong>
        </div>
        <div style="display: flex; align-items: center; gap: 0.5rem;">
          <button id="controlsBtn" class="btn-device-controls" type="button" aria-label="Device Controls Guide" title="Device Controls Guide">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <line x1="6" y1="12" x2="10" y2="12"></line>
              <line x1="8" y1="10" x2="8" y2="14"></line>
              <line x1="15" y1="13" x2="15.01" y2="13"></line>
              <line x1="18" y1="11" x2="18.01" y2="11"></line>
              <rect x="2" y="6" width="20" height="12" rx="2"></rect>
            </svg>
            <span>Controls</span>
          </button>
          <span id="deviceStateBadge" class="card-badge badge-muted">Disconnected</span>
        </div>
      </div>

      <section class="dashboard-grid" style="grid-template-columns: repeat(auto-fit, minmax(210px, 1fr));">
        <!-- 1. Battery Status Card -->
        <div class="card" id="batteryStatusCard">
          <div class="card-header">
            <span class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="1" y="6" width="18" height="12" rx="2" ry="2"></rect>
                <line x1="23" y1="11" x2="23" y2="13"></line>
              </svg>
              Battery
            </span>
            <span id="chargingBadge" class="card-badge badge-muted">—</span>
          </div>
          <div id="batteryPctText" class="stat-value">—%</div>
          <div class="stat-sub">
            <span id="batteryVoltageText">— V</span>
            <span id="batteryStatusDesc">Awaiting connection</span>
          </div>
          <div class="meter-bar">
            <div id="batteryMeterFill" class="meter-fill meter-battery" style="width: 0%;"></div>
          </div>
        </div>

        <!-- 2. Internal Flash Storage Card -->
        <div class="card storage-card" id="flashStorageCard">
          <div class="card-header">
            <span class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path>
              </svg>
              Internal Flash
            </span>
            <span id="flashStorageBadge" class="card-badge badge-muted">—</span>
          </div>
          <div id="flashStorageFreeText" class="stat-value">—</div>
          <div class="stat-sub">
            <span id="flashStorageUsedText">Used: —</span>
            <span id="flashStorageTotalText">Total: —</span>
          </div>
          <div class="meter-bar">
            <div id="flashStorageMeterFill" class="meter-fill meter-storage" style="width: 0%;"></div>
          </div>
        </div>

        <!-- 3. MicroSD Storage Card -->
        <div class="card storage-card" id="sdStorageCard">
          <div class="card-header">
            <span class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect>
                <rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect>
                <line x1="6" y1="6" x2="6.01" y2="6"></line>
                <line x1="6" y1="18" x2="6.01" y2="18"></line>
              </svg>
              MicroSD Card
            </span>
            <span id="sdStorageBadge" class="card-badge badge-muted">No Card</span>
          </div>
          <div id="sdStorageFreeText" class="stat-value">No Card</div>
          <div class="stat-sub">
            <span id="sdStorageUsedText">Used: —</span>
            <span id="sdStorageTotalText">Total: —</span>
          </div>
          <div class="meter-bar">
            <div id="sdStorageMeterFill" class="meter-fill meter-storage" style="width: 0%;"></div>
          </div>
        </div>
      </section>
    `;
  }

  bindEvents() {
    // Interaksi pemilihan target storage dipusatkan pada modal pengaturan
  }

  /**
   * Update Connection Status Alias
   * @param {boolean} connected
   */
  updateConnectionStatus(connected) {
    this.updateDeviceStatus({ state: !!connected });
  }

  /**
   * Update Device Status
   * @param {{ state?: string|boolean, name?: string, uuid?: string, mtu?: number|string }} status
   */
  updateDeviceStatus({ state, name, uuid, mtu } = {}) {
    const badge = this.querySelector('#deviceStateBadge');
    const nameEl = this.querySelector('#deviceNameText');
    const uuidEl = this.querySelector('#deviceUuidText');
    const mtuEl = this.querySelector('#deviceMtuText');

    if (state !== undefined && badge) {
      if (typeof state === 'boolean') {
        this.isDeviceConnected = state;
        badge.textContent = state ? 'Connected' : 'Disconnected';
        badge.className = `card-badge ${state ? 'badge-success' : 'badge-muted'}`;
      } else {
        badge.textContent = String(state);
        const s = String(state).toLowerCase();
        if (s.includes('connect') && !s.includes('dis')) {
          this.isDeviceConnected = true;
          badge.className = 'card-badge badge-success';
        } else if (s.includes('wait') || s.includes('ing')) {
          this.isDeviceConnected = false;
          badge.className = 'card-badge badge-charging';
        } else {
          this.isDeviceConnected = false;
          badge.className = 'card-badge badge-muted';
        }
      }
    }

    if (name !== undefined && nameEl) nameEl.textContent = name;
    if (uuid !== undefined && uuidEl) uuidEl.textContent = `UUID: ${uuid}`;
    if (mtu !== undefined && mtuEl) mtuEl.textContent = `MTU: ${mtu}`;
  }

  /**
   * Update Battery Status Card
   * @param {{ pct?: number, isCharging?: boolean, mv?: number, desc?: string }} battery
   */
  updateBattery({ pct, isCharging, mv, desc } = {}) {
    const pctEl = this.querySelector('#batteryPctText');
    const voltEl = this.querySelector('#batteryVoltageText');
    const descEl = this.querySelector('#batteryStatusDesc');
    const badgeEl = this.querySelector('#chargingBadge');
    const meterEl = this.querySelector('#batteryMeterFill');

    if (pct !== undefined && pctEl) {
      pctEl.textContent = `${pct}%`;
    }
    if (mv !== undefined && voltEl) {
      voltEl.textContent = `${(mv / 1000).toFixed(2)} V`;
    }
    if (desc !== undefined && descEl) {
      descEl.textContent = desc;
    }

    if (meterEl && pct !== undefined) {
      const clampedPct = Math.min(100, Math.max(0, pct));
      meterEl.style.width = `${clampedPct}%`;
      if (clampedPct > 50) {
        meterEl.style.backgroundColor = 'var(--success-color)';
      } else if (clampedPct > 20) {
        meterEl.style.backgroundColor = 'var(--warning-color)';
      } else {
        meterEl.style.backgroundColor = 'var(--danger-color)';
      }
    }

    if (isCharging !== undefined && badgeEl) {
      if (isCharging) {
        badgeEl.className = 'card-badge badge-charging';
        badgeEl.textContent = '⚡ Charging';
        if (!desc && descEl) descEl.textContent = 'Charging via USB';
      } else {
        badgeEl.className = 'card-badge badge-muted';
        badgeEl.textContent = 'On Battery';
        if (!desc && descEl) descEl.textContent = 'Operating on battery';
      }
    }
  }

  /**
   * Route storage update to the active storage card (Flash or SD)
   * @param {{ freeKb?: number, totalKb?: number, usedKb?: number, usedPct?: number }} storage
   */
  updateStorage({ freeKb, totalKb, usedKb, usedPct } = {}) {
    this._lastStorageData = { freeKb, totalKb, usedKb, usedPct };
    if (this.currentStorageTarget === 1) {
      this.updateFlashStorage({ freeKb, totalKb, isActive: true });
    } else {
      this.updateSdStorage({ freeKb, totalKb, isMounted: (totalKb || 0) > 0, isActive: true });
    }
  }

  /**
   * Set active storage target and refresh card presentation
   * @param {number} target - 0: SD, 1: Flash
   */
  setStorageTarget(target) {
    this.currentStorageTarget = Number(target) || 0;
    if (this._lastStorageData) {
      this.updateStorage(this._lastStorageData);
    }
  }

  /**
   * Update Internal Flash Storage Card
   * @param {{ freeKb?: number, totalKb?: number, isActive?: boolean }} flash
   */
  updateFlashStorage({ freeKb, totalKb, isActive } = {}) {
    const badge = this.querySelector('#flashStorageBadge');
    const freeEl = this.querySelector('#flashStorageFreeText');
    const usedEl = this.querySelector('#flashStorageUsedText');
    const totalEl = this.querySelector('#flashStorageTotalText');
    const meter = this.querySelector('#flashStorageMeterFill');

    if (freeKb === undefined && totalKb === undefined) {
      return;
    }

    if (freeKb !== undefined) {
      this._flashFreeKb = Number(freeKb) || 0;
    }
    if (totalKb !== undefined) {
      this._flashTotalKb = Number(totalKb) || 0;
    }

    const free = this._flashFreeKb || 0;
    const total = this._flashTotalKb || 0;

    if (total > 0) {
      const used = total > free ? total - free : 0;
      const usedPct = ((used / total) * 100).toFixed(1);

      if (badge) {
        badge.className = 'card-badge badge-muted';
        badge.textContent = 'Ready';
      }
      if (freeEl) freeEl.textContent = `${formatKb(free)} free`;
      if (usedEl) usedEl.textContent = `Used: ${formatKb(used)} (${usedPct}%)`;
      if (totalEl) totalEl.textContent = `Total: ${formatKb(total)}`;
      if (meter) meter.style.width = `${Math.min(100, Math.max(0, usedPct))}%`;
    } else {
      if (badge) {
        badge.className = 'card-badge badge-muted';
        badge.textContent = '—';
      }
      if (freeEl) freeEl.textContent = '—';
      if (usedEl) usedEl.textContent = 'Used: —';
      if (totalEl) totalEl.textContent = 'Total: —';
      if (meter) meter.style.width = '0%';
    }
  }

  /**
   * Update MicroSD Storage Card
   * @param {{ freeKb?: number, totalKb?: number, isMounted?: boolean, isActive?: boolean }} sd
   */
  updateSdStorage({ freeKb, totalKb, isMounted, isActive } = {}) {
    const badge = this.querySelector('#sdStorageBadge');
    const freeEl = this.querySelector('#sdStorageFreeText');
    const usedEl = this.querySelector('#sdStorageUsedText');
    const totalEl = this.querySelector('#sdStorageTotalText');
    const meter = this.querySelector('#sdStorageMeterFill');

    if (freeKb === undefined && totalKb === undefined && isMounted === undefined) {
      return;
    }

    if (isMounted !== undefined) {
      this._sdMounted = Boolean(isMounted);
    } else if (totalKb !== undefined) {
      this._sdMounted = Number(totalKb) > 0;
    }

    if (freeKb !== undefined) {
      this._sdFreeKb = Number(freeKb) || 0;
    }
    if (totalKb !== undefined) {
      this._sdTotalKb = Number(totalKb) || 0;
    }

    const mounted = this._sdMounted;
    const total = this._sdTotalKb || 0;
    const free = this._sdFreeKb || 0;

    if (!mounted || total === 0) {
      if (badge) {
        badge.className = 'card-badge badge-muted';
        badge.textContent = 'No Card';
      }
      if (freeEl) freeEl.textContent = 'No Card';
      if (usedEl) usedEl.textContent = 'Used: —';
      if (totalEl) totalEl.textContent = 'Total: —';
      if (meter) meter.style.width = '0%';
      return;
    }

    const used = total > free ? total - free : 0;
    const usedPct = ((used / total) * 100).toFixed(1);

    if (badge) {
      badge.className = 'card-badge badge-muted';
      badge.textContent = 'Ready';
    }
    if (freeEl) freeEl.textContent = `${formatKb(free)} free`;
    if (usedEl) usedEl.textContent = `Used: ${formatKb(used)} (${usedPct}%)`;
    if (totalEl) totalEl.textContent = `Total: ${formatKb(total)}`;
    if (meter) meter.style.width = `${Math.min(100, Math.max(0, usedPct))}%`;
  }
}

if (!customElements.get('status-dashboard')) {
  customElements.define('status-dashboard', StatusDashboard);
}
if (typeof window !== 'undefined') {
  window.StatusDashboard = StatusDashboard;
}
