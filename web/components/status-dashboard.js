/**
 * status-dashboard.js
 * BacaIn PWA - Status Dashboard Custom Element (<status-dashboard>)
 * Light DOM Web Component displaying 4 status cards:
 * 1. Device Status
 * 2. Battery Status
 * 3. Internal Flash Storage
 * 4. MicroSD Storage
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
    this.currentStorageTarget = 0; // 0: SD, 1: Flash
  }

  connectedCallback() {
    if (!this.querySelector('.dashboard-grid')) {
      this.render();
    }
    this.bindEvents();
  }

  render() {
    this.innerHTML = `
      <section class="dashboard-grid" style="grid-template-columns: repeat(auto-fit, minmax(210px, 1fr));">
        <!-- 1. Device Status Card -->
        <div class="card" id="deviceStatusCard">
          <div class="card-header">
            <span class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="4" y="2" width="16" height="20" rx="2" ry="2"></rect>
                <line x1="12" y1="18" x2="12.01" y2="18"></line>
              </svg>
              Device
            </span>
            <span id="deviceStateBadge" class="card-badge badge-muted">Disconnected</span>
          </div>
          <div id="deviceNameText" class="stat-value">BacaIn</div>
          <div class="stat-sub">
            <span id="deviceUuidText">UUID: 0000fe60</span>
            <span id="deviceMtuText">MTU: 512B</span>
          </div>
        </div>

        <!-- 2. Battery Status Card -->
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

        <!-- 3. Internal Flash Storage Card -->
        <div class="card storage-card" id="flashStorageCard" data-storage="1" role="button" tabindex="0" title="Click to select Internal Flash storage" style="cursor: pointer; transition: border-color var(--transition-fast), background-color var(--transition-fast);">
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

        <!-- 4. MicroSD Storage Card -->
        <div class="card storage-card" id="sdStorageCard" data-storage="0" role="button" tabindex="0" title="Click to select MicroSD storage" style="cursor: pointer; transition: border-color var(--transition-fast), background-color var(--transition-fast);">
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
            <span id="sdStorageBadge" class="card-badge badge-muted">—</span>
          </div>
          <div id="sdStorageFreeText" class="stat-value">—</div>
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
    const flashCard = this.querySelector('#flashStorageCard');
    const sdCard = this.querySelector('#sdStorageCard');

    if (flashCard) {
      const selectFlash = () => {
        this.dispatchEvent(new CustomEvent('storage-target-change', {
          detail: { target: 1 },
          bubbles: true,
          composed: true
        }));
      };
      flashCard.addEventListener('click', selectFlash);
      flashCard.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          selectFlash();
        }
      });
    }

    if (sdCard) {
      const selectSd = () => {
        this.dispatchEvent(new CustomEvent('storage-target-change', {
          detail: { target: 0 },
          bubbles: true,
          composed: true
        }));
      };
      sdCard.addEventListener('click', selectSd);
      sdCard.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          selectSd();
        }
      });
    }
  }

  /**
   * Update Device Status Card
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
   * Update Internal Flash Storage Card
   * @param {{ freeKb?: number, totalKb?: number, isActive?: boolean }} flash
   */
  updateFlashStorage({ freeKb, totalKb, isActive } = {}) {
    const badge = this.querySelector('#flashStorageBadge');
    const freeEl = this.querySelector('#flashStorageFreeText');
    const usedEl = this.querySelector('#flashStorageUsedText');
    const totalEl = this.querySelector('#flashStorageTotalText');
    const meter = this.querySelector('#flashStorageMeterFill');
    const card = this.querySelector('#flashStorageCard');

    const free = Number(freeKb) || 0;
    const total = Number(totalKb) || 0;
    const used = total > free ? total - free : 0;
    const usedPct = total > 0 ? ((used / total) * 100).toFixed(1) : 0;

    if (freeEl) freeEl.textContent = `${formatKb(free)} free`;
    if (usedEl) usedEl.textContent = `Used: ${formatKb(used)} (${usedPct}%)`;
    if (totalEl) totalEl.textContent = `Total: ${formatKb(total)}`;
    if (meter) meter.style.width = `${Math.min(100, Math.max(0, usedPct))}%`;

    if (isActive !== undefined && badge) {
      if (isActive) {
        badge.className = 'card-badge badge-flash';
        badge.textContent = 'Active';
      } else {
        badge.className = 'card-badge badge-muted';
        badge.textContent = total > 0 ? 'Ready' : '—';
      }
    }

    if (card && isActive !== undefined) {
      card.classList.toggle('active-storage', !!isActive);
      card.style.borderColor = isActive ? 'var(--accent-color)' : '';
      card.style.boxShadow = isActive ? '0 0 0 1px var(--accent-color)' : '';
    }
  }

  /**
   * Update MicroSD Storage Card
   * @param {{ freeKb?: number, totalKb?: number, isMounted?: boolean, isActive?: boolean }} sd
   */
  updateSdStorage({ freeKb, totalKb, isMounted = true, isActive } = {}) {
    const badge = this.querySelector('#sdStorageBadge');
    const freeEl = this.querySelector('#sdStorageFreeText');
    const usedEl = this.querySelector('#sdStorageUsedText');
    const totalEl = this.querySelector('#sdStorageTotalText');
    const meter = this.querySelector('#sdStorageMeterFill');
    const card = this.querySelector('#sdStorageCard');

    if (!isMounted) {
      if (badge) {
        badge.className = 'card-badge badge-muted';
        badge.textContent = 'Unmounted';
      }
      if (freeEl) freeEl.textContent = 'No Card';
      if (usedEl) usedEl.textContent = 'Used: —';
      if (totalEl) totalEl.textContent = 'Total: —';
      if (meter) meter.style.width = '0%';
      if (card) {
        card.classList.remove('active-storage');
        card.style.borderColor = '';
        card.style.boxShadow = '';
      }
      return;
    }

    const free = Number(freeKb) || 0;
    const total = Number(totalKb) || 0;
    const used = total > free ? total - free : 0;
    const usedPct = total > 0 ? ((used / total) * 100).toFixed(1) : 0;

    if (freeEl) freeEl.textContent = `${formatKb(free)} free`;
    if (usedEl) usedEl.textContent = `Used: ${formatKb(used)} (${usedPct}%)`;
    if (totalEl) totalEl.textContent = `Total: ${formatKb(total)}`;
    if (meter) meter.style.width = `${Math.min(100, Math.max(0, usedPct))}%`;

    if (isActive !== undefined && badge) {
      if (isActive) {
        badge.className = 'card-badge badge-sd';
        badge.textContent = 'Active';
      } else {
        badge.className = 'card-badge badge-muted';
        badge.textContent = total > 0 ? 'Ready' : '—';
      }
    }

    if (card && isActive !== undefined) {
      card.classList.toggle('active-storage', !!isActive);
      card.style.borderColor = isActive ? 'var(--success-color)' : '';
      card.style.boxShadow = isActive ? '0 0 0 1px var(--success-color)' : '';
    }
  }
}

if (!customElements.get('status-dashboard')) {
  customElements.define('status-dashboard', StatusDashboard);
}
if (typeof window !== 'undefined') {
  window.StatusDashboard = StatusDashboard;
}
