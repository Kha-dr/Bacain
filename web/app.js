/**
 * BacaIn Web Bluetooth PWA
 * Application Logic & BLE Synchronization Engine
 */

// ============================================================================
// BLE Configuration & UUIDs
// ============================================================================
const appConfig = (typeof window !== 'undefined' && (window.CONFIG || window.BACA_CONFIG)) ? (window.CONFIG || window.BACA_CONFIG) : null;
const SERVICE_UUID = appConfig?.ble?.service || '0000fe60-0000-1000-8000-00805f9b34fb';
const CONTROL_UUID = appConfig?.ble?.control || '0000fe61-0000-1000-8000-00805f9b34fb';
const DATA_UUID    = appConfig?.ble?.data    || '0000fe62-0000-1000-8000-00805f9b34fb';
const STATUS_UUID  = appConfig?.ble?.status  || '0000fe63-0000-1000-8000-00805f9b34fb';

const OPCODES = appConfig?.ble?.opcodes || {
  LIST_BOOKS: 0x01,
  START_UPLOAD: 0x02,
  FINISH_UPLOAD: 0x03,
  DELETE_BOOK: 0x04,
  ABORT_UPLOAD: 0x05,
  SET_STORAGE_TARGET: 0x06,
  GET_BOOKMARKS: 0x07,
  DELETE_BOOKMARK: 0x08,
  SET_FONT: 0x10,
  FORCE_REFRESH: 0x12,
  SET_SLEEP_TIMEOUT: 0x13,
  TRIGGER_SLEEP: 0x14
};

const CHUNK_SIZE = appConfig?.ble?.chunkSize || 506;
const CHUNK_DELAY_MS = appConfig?.ble?.chunkDelayMs || 6;

// ============================================================================
// State Variables
// ============================================================================
let bleDevice = null;
let gattServer = null;
let controlChar = null;
let dataChar = null;
let statusChar = null;

let isConnecting = false;
let isConnected = false;

// Upload State
let isUploading = false;
let cancelUploadRequested = false;
let uploadQueue = [];

// Books State
let remoteBooks = [];
let pendingBookList = [];
let isListingBooks = false;
let listBooksTimeout = null;

// Control Notification Listeners
const controlCallbacks = new Map();

// Pending Delete Book
let bookPendingDelete = null;

// ============================================================================
// DOM Elements
// ============================================================================
const el = {
  // Onboarding elements
  onboardingView: document.getElementById('onboardingView'),
  mainContent: document.getElementById('mainContent'),
  onboardingConnectBtn: document.getElementById('onboardingConnectBtn'),
  skipOnboardingBtn: document.getElementById('skipOnboardingBtn'),

  themeToggleBtn: document.getElementById('themeToggleBtn'),
  themeIconSun: document.getElementById('themeIconSun'),
  themeIconMoon: document.getElementById('themeIconMoon'),

  connectBtn: document.getElementById('connectBtn'),
  connectBtnText: document.getElementById('connectBtnText'),
  statusDot: document.getElementById('statusDot'),
  compatBanner: document.getElementById('compatBanner'),
  githubBtn: document.getElementById('githubBtn'),

  // Device status cards
  deviceStateBadge: document.getElementById('deviceStateBadge'),
  deviceNameText: document.getElementById('deviceNameText'),
  deviceUuidText: document.getElementById('deviceUuidText'),
  deviceMtuText: document.getElementById('deviceMtuText'),

  batteryPctText: document.getElementById('batteryPctText'),
  batteryVoltageText: document.getElementById('batteryVoltageText'),
  batteryStatusDesc: document.getElementById('batteryStatusDesc'),
  chargingBadge: document.getElementById('chargingBadge'),
  batteryMeterFill: document.getElementById('batteryMeterFill'),

  storageTitleText: document.getElementById('storageTitleText'),
  storageFreeText: document.getElementById('storageFreeText'),
  storageUsedText: document.getElementById('storageUsedText'),
  storageTotalText: document.getElementById('storageTotalText'),
  storageBadge: document.getElementById('storageBadge'),
  storageMeterFill: document.getElementById('storageMeterFill'),

  // Upload
  dropzone: document.getElementById('dropzone'),
  fileInput: document.getElementById('fileInput'),
  uploadActiveCard: document.getElementById('uploadActiveCard'),
  uploadingFileName: document.getElementById('uploadingFileName'),
  cancelUploadBtn: document.getElementById('cancelUploadBtn'),
  uploadProgressFill: document.getElementById('uploadProgressFill'),
  uploadPctStat: document.getElementById('uploadPctStat'),
  uploadSpeedStat: document.getElementById('uploadSpeedStat'),
  uploadSeqStat: document.getElementById('uploadSeqStat'),
  uploadEtaStat: document.getElementById('uploadEtaStat'),

  // Books
  booksCountBadge: document.getElementById('booksCountBadge'),
  bookSearchInput: document.getElementById('bookSearchInput'),
  refreshBooksBtn: document.getElementById('refreshBooksBtn'),
  refreshBooksIcon: document.getElementById('refreshBooksIcon'),
  refreshStatusBtn: document.getElementById('refreshStatusBtn'),
  refreshStatusIcon: document.getElementById('refreshStatusIcon'),
  booksList: document.getElementById('booksList'),

  // Modals
  settingsModal: document.getElementById('settingsModal'),
  settingsBtn: document.getElementById('settingsBtn'),
  closeSettingsBtn: document.getElementById('closeSettingsBtn'),

  deleteModal: document.getElementById('deleteModal'),
  deleteModalText: document.getElementById('deleteModalText'),
  cancelDeleteBtn: document.getElementById('cancelDeleteBtn'),
  confirmDeleteBtn: document.getElementById('confirmDeleteBtn'),

  // Log
  logHeader: document.getElementById('logHeader'),
  logBody: document.getElementById('logBody'),
  logToggleIcon: document.getElementById('logToggleIcon'),
  logCountBadge: document.getElementById('logCountBadge'),
  clearLogBtn: document.getElementById('clearLogBtn'),

  toastContainer: document.getElementById('toastContainer'),

  // Settings & Hardware Controls
  storageTargetSdBtn: document.getElementById('storageTargetSdBtn'),
  storageTargetFlashBtn: document.getElementById('storageTargetFlashBtn'),
  storageTargetDesc: document.getElementById('storageTargetDesc'),
  font16Btn: document.getElementById('font16Btn'),
  font12Btn: document.getElementById('font12Btn'),
  forceRefreshBtn: document.getElementById('forceRefreshBtn'),
  sleepTimeoutSelect: document.getElementById('sleepTimeoutSelect'),
  triggerSleepBtn: document.getElementById('triggerSleepBtn')
};

function refreshDomElements() {
  for (const key of Object.keys(el)) {
    el[key] = document.getElementById(key) || el[key];
  }
  if (!el.booksList) {
    el.booksList = document.getElementById('booksListContainer') || document.getElementById('booksList');
  }
  if (!el.logBody) {
    el.logBody = document.getElementById('activityLog') || document.getElementById('logBody');
  }
}

// ============================================================================
// Utilities: Formatting & Logging
// ============================================================================
function formatBytes(bytes) {
  if (isNaN(bytes) || bytes === null || bytes < 0) return '0 B';
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function formatKb(kb) {
  return formatBytes(kb * 1024);
}

let logCount = 0;
function log(msg, type = 'info') {
  const modalComponent = document.getElementById('settingsModal');
  if (modalComponent && typeof modalComponent.appendLog === 'function') {
    modalComponent.appendLog(msg, type);
    return;
  }

  const logBody = el.logBody || document.getElementById('activityLog') || document.getElementById('logBody');
  if (!logBody) return;

  const now = new Date();
  const timestamp = now.toTimeString().split(' ')[0] + '.' + String(now.getMilliseconds()).padStart(3, '0');

  const entry = document.createElement('div');
  entry.className = `log-entry ${type}`;

  const timeSpan = document.createElement('span');
  timeSpan.className = 'log-time';
  timeSpan.textContent = `[${timestamp}]`;

  const msgSpan = document.createElement('span');
  msgSpan.className = 'log-msg';
  msgSpan.textContent = msg;

  entry.appendChild(timeSpan);
  entry.appendChild(msgSpan);

  logBody.appendChild(entry);
  logCount++;
  const badge = el.logCountBadge || document.getElementById('logCountBadge');
  if (badge) badge.textContent = logCount;

  // Auto-scroll to bottom
  logBody.scrollTop = logBody.scrollHeight;

  // Limit entries
  while (logBody.childElementCount > 250) {
    logBody.removeChild(logBody.firstChild);
  }
}

function showToast(message, type = 'info', duration = 3500) {
  const toast = document.createElement('div');
  toast.className = `toast ${type}`;
  toast.textContent = message;

  el.toastContainer.appendChild(toast);

  setTimeout(() => {
    toast.style.opacity = '0';
    toast.style.transform = 'translateY(10px)';
    setTimeout(() => {
      if (toast.parentNode) {
        toast.parentNode.removeChild(toast);
      }
    }, 250);
  }, duration);
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// ============================================================================
// Theme Toggle & Persistence
// ============================================================================
function initTheme() {
  const savedTheme = localStorage.getItem('bacain_theme') || localStorage.getItem('ereader_pwa_theme');
  const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
  const theme = savedTheme || (prefersDark ? 'dark' : 'light');
  setTheme(theme);

  if (el.themeToggleBtn) {
    el.themeToggleBtn.addEventListener('click', () => {
      const currentTheme = document.documentElement.getAttribute('data-theme') || 'dark';
      const nextTheme = currentTheme === 'dark' ? 'light' : 'dark';
      setTheme(nextTheme);
    });
  }
}

function setTheme(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  try {
    localStorage.setItem('bacain_theme', theme);
    localStorage.setItem('ereader_pwa_theme', theme);
  } catch (_) {}

  if (el.themeIconSun && el.themeIconMoon) {
    if (theme === 'dark') {
      el.themeIconSun.style.display = 'block';
      el.themeIconMoon.style.display = 'none';
    } else {
      el.themeIconSun.style.display = 'none';
      el.themeIconMoon.style.display = 'block';
    }
  }
}

// ============================================================================
// Web Bluetooth Compatibility Check
// ============================================================================
function checkBluetoothCompatibility() {
  if (!navigator.bluetooth) {
    el.compatBanner.style.display = 'flex';
    log('Web Bluetooth API is not supported in this browser.', 'warning');
    return false;
  }
  return true;
}

// ============================================================================
// BLE Connection Management
// ============================================================================
async function handleConnectToggle() {
  if (isConnected) {
    disconnectDevice();
  } else {
    await connectDevice();
  }
}

async function connectDevice() {
  if (!checkBluetoothCompatibility()) {
    showToast('Web Bluetooth not supported on this browser', 'error');
    return;
  }

  try {
    isConnecting = true;
    updateConnectionUI('connecting');
    log('Requesting Bluetooth device filter [0000fe60]...', 'info');

    bleDevice = await navigator.bluetooth.requestDevice({
      filters: [
        { services: [SERVICE_UUID] },
        { name: 'BacaIn' },
        { namePrefix: 'BacaIn' },
        { namePrefix: 'Mini-E-Reader' },
        { namePrefix: 'Mini-E' }
      ],
      optionalServices: [SERVICE_UUID]
    });

    log(`Device selected: ${bleDevice.name || 'Unknown'} (${bleDevice.id})`, 'info');
    bleDevice.addEventListener('gattserverdisconnected', onDisconnected);

    log('Connecting to GATT server...', 'info');
    gattServer = await bleDevice.gatt.connect();

    log('Discovering BacaIn Sync service...', 'info');
    const service = await gattServer.getPrimaryService(SERVICE_UUID);

    log('Binding GATT characteristics...', 'info');
    controlChar = await service.getCharacteristic(CONTROL_UUID);
    dataChar = await service.getCharacteristic(DATA_UUID);
    statusChar = await service.getCharacteristic(STATUS_UUID);

    // Subscribe to notifications
    log('Subscribing to Control notifications (0xfe61)...', 'info');
    await controlChar.startNotifications();
    controlChar.addEventListener('characteristicvaluechanged', onControlNotification);

    log('Subscribing to Status notifications (0xfe63)...', 'info');
    await statusChar.startNotifications();
    statusChar.addEventListener('characteristicvaluechanged', onStatusNotification);

    isConnected = true;
    isConnecting = false;
    updateConnectionUI('connected');
    showToast(`Connected to ${bleDevice.name || 'BacaIn'}`, 'success');
    log('Connection established successfully.', 'success');

    // Read initial status
    await readDeviceStatus();

    // Query initial book list
    await queryBooksList();

  } catch (error) {
    isConnecting = false;
    isConnected = false;
    updateConnectionUI('disconnected');

    if (error.name === 'NotFoundError') {
      log('Device selection cancelled by user.', 'info');
    } else {
      log(`BLE Connection Error: ${error.message}`, 'error');
      showToast(`Connection failed: ${error.message}`, 'error');
    }
  }
}

function disconnectDevice() {
  if (bleDevice && bleDevice.gatt && bleDevice.gatt.connected) {
    log('Disconnecting from device...', 'info');
    bleDevice.gatt.disconnect();
  } else {
    onDisconnected();
  }
}

function onDisconnected() {
  log('Bluetooth device disconnected.', 'warning');
  showToast('Disconnected from BacaIn', 'warning');

  isConnected = false;
  isConnecting = false;
  gattServer = null;
  controlChar = null;
  dataChar = null;
  statusChar = null;

  if (isUploading) {
    cancelUploadRequested = true;
    hideUploadActiveCard();
  }

  updateConnectionUI('disconnected');
}

function switchToDashboard() {
  const onboardingView = el.onboardingView || document.getElementById('onboardingView');
  const mainContent = el.mainContent || document.getElementById('mainContent');
  if (onboardingView) onboardingView.style.display = 'none';
  if (mainContent) mainContent.style.display = 'flex';
}

function updateConnectionUI(state) {
  const statusDashboard = document.getElementById('statusDashboard');
  const bookLibrary = document.getElementById('bookLibrary');
  const onboardingConnectBtn = el.onboardingConnectBtn || document.getElementById('onboardingConnectBtn');
  const settingsModal = document.getElementById('deviceSettingsModal') || document.getElementById('settingsModal');

  if (state !== 'connected') {
    if (settingsModal && typeof settingsModal.setSdCardAvailable === 'function') {
      settingsModal.setSdCardAvailable(false);
    }
  }

  if (state === 'connected') {
    switchToDashboard();
    if (onboardingConnectBtn) {
      onboardingConnectBtn.disabled = false;
      const span = onboardingConnectBtn.querySelector('span');
      if (span) span.textContent = 'Connected';
    }
    if (el.statusDot) el.statusDot.className = 'status-dot connected';
    if (el.connectBtn) {
      el.connectBtn.className = 'btn-connect connected';
      if (el.connectBtnText) el.connectBtnText.textContent = 'Disconnect';
    }
    if (el.deviceStateBadge) {
      el.deviceStateBadge.className = 'card-badge badge-success';
      el.deviceStateBadge.textContent = 'Connected';
    }
    if (el.deviceNameText) el.deviceNameText.textContent = bleDevice?.name || 'BacaIn';
    if (el.refreshBooksBtn) el.refreshBooksBtn.disabled = false;
    if (el.refreshStatusBtn) el.refreshStatusBtn.disabled = false;

    if (statusDashboard && typeof statusDashboard.updateDeviceStatus === 'function') {
      statusDashboard.updateDeviceStatus({
        state: true,
        name: bleDevice?.name || 'BacaIn'
      });
    }
    if (bookLibrary && typeof bookLibrary.setDeviceConnected === 'function') {
      bookLibrary.setDeviceConnected(true);
    }
  } else if (state === 'connecting') {
    if (onboardingConnectBtn) {
      onboardingConnectBtn.disabled = true;
      const span = onboardingConnectBtn.querySelector('span');
      if (span) span.textContent = 'Connecting...';
    }
    if (el.statusDot) el.statusDot.className = 'status-dot connecting';
    if (el.connectBtn) {
      el.connectBtn.className = 'btn-connect';
      if (el.connectBtnText) el.connectBtnText.textContent = 'Connecting...';
    }
    if (el.deviceStateBadge) {
      el.deviceStateBadge.className = 'card-badge badge-muted';
      el.deviceStateBadge.textContent = 'Connecting';
    }
    if (el.refreshBooksBtn) el.refreshBooksBtn.disabled = true;
    if (el.refreshStatusBtn) el.refreshStatusBtn.disabled = true;

    if (statusDashboard && typeof statusDashboard.updateDeviceStatus === 'function') {
      statusDashboard.updateDeviceStatus({ state: 'Connecting' });
    }
  } else {
    if (onboardingConnectBtn) {
      onboardingConnectBtn.disabled = false;
      const span = onboardingConnectBtn.querySelector('span');
      if (span) span.textContent = 'Connect Device';
    }
    if (el.statusDot) el.statusDot.className = 'status-dot disconnected';
    if (el.connectBtn) {
      el.connectBtn.className = 'btn-connect';
      if (el.connectBtnText) el.connectBtnText.textContent = 'Connect Device';
    }
    if (el.deviceStateBadge) {
      el.deviceStateBadge.className = 'card-badge badge-muted';
      el.deviceStateBadge.textContent = 'Disconnected';
    }
    if (el.refreshBooksBtn) el.refreshBooksBtn.disabled = true;
    if (el.refreshStatusBtn) el.refreshStatusBtn.disabled = true;

    // Reset status indicators to placeholder
    if (el.batteryPctText) el.batteryPctText.textContent = '—%';
    if (el.batteryVoltageText) el.batteryVoltageText.textContent = '— V';
    if (el.batteryStatusDesc) el.batteryStatusDesc.textContent = 'Not connected';
    if (el.chargingBadge) {
      el.chargingBadge.className = 'card-badge badge-muted';
      el.chargingBadge.textContent = '—';
    }
    if (el.batteryMeterFill) el.batteryMeterFill.style.width = '0%';

    if (el.storageFreeText) el.storageFreeText.textContent = '—';
    if (el.storageUsedText) el.storageUsedText.textContent = 'Used: —';
    if (el.storageTotalText) el.storageTotalText.textContent = 'Total: —';
    if (el.storageBadge) {
      el.storageBadge.className = 'card-badge badge-muted';
      el.storageBadge.textContent = currentStorageTarget === 0 ? 'MicroSD' : 'Internal Flash';
    }
    if (el.storageMeterFill) el.storageMeterFill.style.width = '0%';

    if (statusDashboard) {
      if (typeof statusDashboard.updateDeviceStatus === 'function') {
        statusDashboard.updateDeviceStatus({ state: false });
      }
      if (typeof statusDashboard.updateBattery === 'function') {
        statusDashboard.updateBattery({ pct: 0, isCharging: false, mv: 0, desc: 'Awaiting connection' });
      }
      if (typeof statusDashboard.updateFlashStorage === 'function') {
        statusDashboard.updateFlashStorage({ freeKb: 0, totalKb: 0, isActive: currentStorageTarget === 1 });
      }
      if (typeof statusDashboard.updateSdStorage === 'function') {
        statusDashboard.updateSdStorage({ freeKb: 0, totalKb: 0, isMounted: false, isActive: currentStorageTarget === 0 });
      }
    }

    if (bookLibrary && typeof bookLibrary.setDeviceConnected === 'function') {
      bookLibrary.setDeviceConnected(false);
    }

    remoteBooks = [];
    renderBooksList();
  }
}

// ============================================================================
// Status Characteristic Parsing & Updates
// ============================================================================
async function readDeviceStatus() {
  if (!statusChar) return;
  try {
    if (el.refreshStatusIcon) el.refreshStatusIcon.classList.add('spin');
    const val = await statusChar.readValue();
    parseStatusValue(val);
  } catch (err) {
    log(`Failed to read status: ${err.message}`, 'error');
  } finally {
    if (el.refreshStatusIcon) {
      setTimeout(() => el.refreshStatusIcon.classList.remove('spin'), 500);
    }
  }
}

function onStatusNotification(event) {
  parseStatusValue(event.target.value);
}

function parseStatusValue(dataView) {
  if (!dataView || dataView.byteLength < 12) {
    log(`Received invalid status packet (length ${dataView?.byteLength || 0})`, 'warning');
    return;
  }

  // 12-byte packed struct:
  // offset 0: uint8_t battery_pct (0-100)
  // offset 1: uint8_t is_charging (0 or 1)
  // offset 2: uint16_le battery_mv
  // offset 4: uint32_le free_kb
  // offset 8: uint32_le total_kb
  const batteryPct = dataView.getUint8(0);
  const isCharging = dataView.getUint8(1) !== 0;
  const batteryMv = dataView.getUint16(2, true);
  const freeKb = dataView.getUint32(4, true);
  const totalKb = dataView.getUint32(8, true);

  // Update Battery UI
  if (el.batteryPctText) el.batteryPctText.textContent = `${batteryPct}%`;
  if (el.batteryVoltageText) el.batteryVoltageText.textContent = `${(batteryMv / 1000).toFixed(2)} V`;
  if (el.batteryMeterFill) {
    el.batteryMeterFill.style.width = `${Math.min(100, Math.max(0, batteryPct))}%`;

    if (batteryPct > 50) {
      el.batteryMeterFill.style.backgroundColor = 'var(--success-color)';
    } else if (batteryPct > 20) {
      el.batteryMeterFill.style.backgroundColor = 'var(--warning-color)';
    } else {
      el.batteryMeterFill.style.backgroundColor = 'var(--danger-color)';
    }
  }

  if (el.chargingBadge) {
    if (isCharging) {
      el.chargingBadge.className = 'card-badge badge-charging';
      el.chargingBadge.innerHTML = '⚡ Charging';
      if (el.batteryStatusDesc) el.batteryStatusDesc.textContent = 'Charging via USB';
    } else {
      el.chargingBadge.className = 'card-badge badge-muted';
      el.chargingBadge.textContent = 'On Battery';
      if (el.batteryStatusDesc) el.batteryStatusDesc.textContent = 'Operating on battery';
    }
  }

  // Update Storage UI
  const usedKb = totalKb > freeKb ? totalKb - freeKb : 0;
  const usedPct = totalKb > 0 ? ((usedKb / totalKb) * 100).toFixed(1) : 0;

  if (el.storageFreeText) el.storageFreeText.textContent = `${formatKb(freeKb)} free`;
  if (el.storageUsedText) el.storageUsedText.textContent = `Used: ${formatKb(usedKb)} (${usedPct}%)`;
  if (el.storageTotalText) el.storageTotalText.textContent = `Total: ${formatKb(totalKb)}`;
  if (el.storageBadge) {
    el.storageBadge.className = 'card-badge badge-muted';
    el.storageBadge.textContent = `${usedPct}% used`;
  }
  if (el.storageMeterFill) {
    el.storageMeterFill.style.width = `${Math.min(100, Math.max(0, usedPct))}%`;
  }

  // Perbarui status dashboard untuk Flash dan MicroSD
  const statusDashboard = document.getElementById('statusDashboard');
  if (statusDashboard) {
    if (typeof statusDashboard.updateBattery === 'function') {
      statusDashboard.updateBattery({
        pct: batteryPct,
        isCharging,
        mv: batteryMv,
        desc: isCharging ? 'Charging via USB' : 'Operating on battery'
      });
    }

    if (currentStorageTarget === 1) {
      if (typeof statusDashboard.updateFlashStorage === 'function') {
        statusDashboard.updateFlashStorage({
          freeKb,
          totalKb,
          isActive: true
        });
      }
      if (typeof statusDashboard.updateSdStorage === 'function') {
        statusDashboard.updateSdStorage({
          isActive: false
        });
      }
    } else {
      if (typeof statusDashboard.updateSdStorage === 'function') {
        statusDashboard.updateSdStorage({
          freeKb,
          totalKb,
          isMounted: totalKb > 0,
          isActive: true
        });
      }
      if (typeof statusDashboard.updateFlashStorage === 'function') {
        statusDashboard.updateFlashStorage({
          isActive: false
        });
      }
    }
  }

  const settingsModal = document.getElementById('deviceSettingsModal') || document.getElementById('settingsModal');
  if (currentStorageTarget === 0) {
    if (totalKb > 0) {
      if (settingsModal && typeof settingsModal.setSdCardAvailable === 'function') {
        settingsModal.setSdCardAvailable(true);
      }
    } else if (totalKb === 0) {
      if (settingsModal && typeof settingsModal.setSdCardAvailable === 'function') {
        settingsModal.setSdCardAvailable(false);
      }
      updateStorageTargetUI(1);
    }
  }

  log(`Status Update: Battery ${batteryPct}% (${(batteryMv/1000).toFixed(2)}V), Storage Free: ${formatKb(freeKb)} / ${formatKb(totalKb)} (${usedPct}% used)`, 'info');
}

// ============================================================================
// Control Characteristic Notifications & Await Helpers
// ============================================================================
function onControlNotification(event) {
  const val = event.target.value;
  if (!val || val.byteLength < 1) return;

  const opcode = val.getUint8(0);

  // If there is an active Promise waiting for this opcode, notify it
  if (controlCallbacks.has(opcode)) {
    const callback = controlCallbacks.get(opcode);
    controlCallbacks.delete(opcode);
    callback(val);
    return;
  }

  // Opcode 0x01: LIST_BOOKS stream handler
  if (opcode === OPCODES.LIST_BOOKS) {
    handleListBooksPacket(val);
  }
}

function waitForControlResponse(opcode, timeoutMs = 8000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      controlCallbacks.delete(opcode);
      reject(new Error(`Timeout waiting for BLE opcode 0x${opcode.toString(16)} response`));
    }, timeoutMs);

    controlCallbacks.set(opcode, (val) => {
      clearTimeout(timer);
      const status = val.byteLength >= 2 ? val.getUint8(1) : 0;
      resolve({ status, dataView: val });
    });
  });
}

// ============================================================================
// Remote Books Management (Query & Delete)
// ============================================================================
async function queryBooksList() {
  if (!controlChar) {
    showToast('Not connected to BacaIn', 'warning');
    return;
  }

  try {
    el.refreshBooksIcon.classList.add('spin');
    isListingBooks = true;
    pendingBookList = [];

    // Fallback timer if device stream terminates unexpectedly
    clearTimeout(listBooksTimeout);
    listBooksTimeout = setTimeout(() => {
      if (isListingBooks) {
        log('Book listing stream finalized by timeout.', 'info');
        finalizeBookList();
      }
    }, 4000);

    log('Querying books on active storage (OpCode 0x01)...', 'info');
    const req = new Uint8Array([OPCODES.LIST_BOOKS]);
    await controlChar.writeValueWithResponse(req);

  } catch (err) {
    log(`Failed to request book list: ${err.message}`, 'error');
    showToast(`Error requesting books: ${err.message}`, 'error');
    finalizeBookList();
  }
}

function handleListBooksPacket(val) {
  // Packet layout: [0x01, status, is_last, uint32_le file_size, filename_bytes...]
  if (val.byteLength < 7) {
    log(`Malformed book list packet (length ${val.byteLength})`, 'warning');
    return;
  }

  const status = val.getUint8(1);
  const isLast = val.getUint8(2);
  const fileSize = val.getUint32(3, true);

  // Extract null-terminated filename string
  let filename = '';
  const nameBytes = [];
  for (let i = 7; i < val.byteLength; i++) {
    const b = val.getUint8(i);
    if (b === 0) break;
    nameBytes.push(b);
  }

  if (nameBytes.length > 0) {
    try {
      filename = new TextDecoder('utf-8').decode(new Uint8Array(nameBytes)).trim();
    } catch {
      filename = String.fromCharCode.apply(null, nameBytes).trim();
    }
  }

  if (status === 0 && filename.length > 0) {
    pendingBookList.push({ name: filename, size: fileSize });
  }

  if (isLast === 1) {
    clearTimeout(listBooksTimeout);
    finalizeBookList();
  }
}

function finalizeBookList() {
  isListingBooks = false;
  el.refreshBooksIcon.classList.remove('spin');

  // Sort books alphabetically
  remoteBooks = pendingBookList.sort((a, b) => a.name.localeCompare(b.name));
  log(`Found ${remoteBooks.length} book(s) on storage.`, 'success');
  renderBooksList();
}

function getBookReadingProgress(filename) {
  try {
    const saved = localStorage.getItem(`bacain_progress_${filename}`);
    if (saved !== null) {
      const num = parseInt(saved, 10);
      if (!isNaN(num) && num >= 0 && num <= 100) return num;
    }
  } catch (_) {}
  return 0;
}

function setBookReadingProgress(filename, progressPct) {
  try {
    localStorage.setItem(`bacain_progress_${filename}`, String(Math.round(progressPct)));
  } catch (_) {}
}

function renderBooksList() {
  const searchTerm = el.bookSearchInput ? el.bookSearchInput.value.trim().toLowerCase() : '';
  const filtered = remoteBooks.filter(b => b.name.toLowerCase().includes(searchTerm));

  const bookLibrary = document.getElementById('bookLibrary');
  if (bookLibrary && typeof bookLibrary.renderBooks === 'function') {
    bookLibrary.setDeviceConnected(isConnected);
    const booksWithProgress = remoteBooks.map(b => ({
      ...b,
      progress: b.progress !== undefined ? b.progress : getBookReadingProgress(b.name)
    }));
    bookLibrary.renderBooks(booksWithProgress, currentStorageTarget === 0 ? '/sdcard/books/' : '/spiflash/books/');
  }

  if (el.booksCountBadge) {
    el.booksCountBadge.textContent = `${remoteBooks.length} ${remoteBooks.length === 1 ? 'book' : 'books'}`;
  }
  if (!el.booksList) return;
  el.booksList.innerHTML = '';

  if (!isConnected) {
    el.booksList.innerHTML = `
      <div class="empty-state">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
          <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
          <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
        </svg>
        <p>Connect your BacaIn to view and manage books.</p>
      </div>`;
    return;
  }

  if (filtered.length === 0) {
    el.booksList.innerHTML = `
      <div class="empty-state">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="10"></circle>
          <line x1="8" y1="12" x2="16" y2="12"></line>
        </svg>
        <p>${searchTerm ? 'No matching books found' : 'No books found on active storage. Drop a .txt file above to upload!'}</p>
      </div>`;
    return;
  }

  filtered.forEach(book => {
    const progress = book.progress !== undefined ? book.progress : getBookReadingProgress(book.name);

    const item = document.createElement('div');
    item.className = 'book-item';

    const info = document.createElement('div');
    info.className = 'book-info';

    const icon = document.createElement('div');
    icon.className = 'book-icon';
    icon.innerHTML = `
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
        <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
      </svg>`;

    const meta = document.createElement('div');
    meta.className = 'book-meta';

    const name = document.createElement('div');
    name.className = 'book-name book-title';
    name.textContent = book.name;
    name.title = book.name;

    const details = document.createElement('div');
    details.className = 'book-details';
    details.style.display = 'flex';
    details.style.alignItems = 'center';
    details.style.gap = '0.75rem';
    details.style.marginTop = '0.2rem';

    const size = document.createElement('span');
    size.className = 'book-size';
    size.textContent = formatBytes(book.size);

    const separator = document.createElement('span');
    separator.style.color = 'var(--text-muted)';
    separator.style.fontSize = '0.75rem';
    separator.textContent = '•';

    const progressEl = document.createElement('span');
    progressEl.className = 'book-progress';
    progressEl.style.fontSize = '0.78rem';
    progressEl.style.color = progress > 0 ? 'var(--accent-color)' : 'var(--text-muted)';
    progressEl.style.fontFamily = 'var(--font-mono)';
    progressEl.textContent = `${progress}% read`;

    details.appendChild(size);
    details.appendChild(separator);
    details.appendChild(progressEl);

    meta.appendChild(name);
    meta.appendChild(details);
    info.appendChild(icon);
    info.appendChild(meta);

    const deleteBtn = document.createElement('button');
    deleteBtn.className = 'btn-delete-book';
    deleteBtn.title = `Delete ${book.name}`;
    deleteBtn.setAttribute('aria-label', `Delete ${book.name}`);
    deleteBtn.innerHTML = `
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <polyline points="3 6 5 6 21 6"></polyline>
        <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
        <line x1="10" y1="11" x2="10" y2="17"></line>
        <line x1="14" y1="11" x2="14" y2="17"></line>
      </svg>`;

    deleteBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      openDeleteModal(book);
    });

    item.appendChild(info);
    item.appendChild(deleteBtn);
    el.booksList.appendChild(item);
  });
}

// Settings Modal Dialog Helpers
function openSettingsModal() {
  const modalComponent = document.getElementById('settingsModal');
  if (modalComponent && typeof modalComponent.open === 'function') {
    modalComponent.open();
  } else if (el.settingsModal) {
    el.settingsModal.classList.add('active');
    el.settingsModal.setAttribute('aria-hidden', 'false');
  }
}

function closeSettingsModal() {
  const modalComponent = document.getElementById('settingsModal');
  if (modalComponent && typeof modalComponent.close === 'function') {
    modalComponent.close();
  } else if (el.settingsModal) {
    el.settingsModal.classList.remove('active');
    el.settingsModal.setAttribute('aria-hidden', 'true');
  }
}

// Delete Book Dialog
function openDeleteModal(book) {
  bookPendingDelete = book;
  if (el.deleteModalText) {
    const storageName = currentStorageTarget === 0 ? 'MicroSD card' : 'Internal Flash';
    el.deleteModalText.textContent = `Are you sure you want to delete "${book.name}" (${formatBytes(book.size)}) from ${storageName}? This cannot be undone.`;
  }
  if (el.deleteModal) {
    el.deleteModal.classList.add('active');
    el.deleteModal.setAttribute('aria-hidden', 'false');
  }
}

function closeDeleteModal() {
  bookPendingDelete = null;
  if (el.deleteModal) {
    el.deleteModal.classList.remove('active');
    el.deleteModal.setAttribute('aria-hidden', 'true');
  }
}

async function confirmDeleteBook() {
  if (!bookPendingDelete || !controlChar) {
    closeDeleteModal();
    return;
  }

  const book = bookPendingDelete;
  closeDeleteModal();

  try {
    log(`Deleting book "${book.name}" (OpCode 0x04)...`, 'info');

    // Build packet: [0x04, filename_bytes..., 0x00]
    const enc = new TextEncoder();
    const nameBytes = enc.encode(book.name);
    const packet = new Uint8Array(1 + nameBytes.length + 1);
    packet[0] = OPCODES.DELETE_BOOK;
    packet.set(nameBytes, 1);
    packet[1 + nameBytes.length] = 0; // null terminator

    const respPromise = waitForControlResponse(OPCODES.DELETE_BOOK, 6000);
    await controlChar.writeValueWithResponse(packet);
    const { status } = await respPromise;

    if (status === 0) {
      log(`Deleted "${book.name}" successfully.`, 'success');
      showToast(`Deleted "${book.name}"`, 'success');
      try {
        localStorage.removeItem(`bacain_progress_${book.name}`);
      } catch (_) {}
      // Refresh list & status
      await queryBooksList();
      await readDeviceStatus();
    } else {
      throw new Error(`Device reported error code: ${status}`);
    }

  } catch (err) {
    log(`Delete failed: ${err.message}`, 'error');
    showToast(`Delete failed: ${err.message}`, 'error');
  }
}

// ============================================================================
// Chunked File Upload Engine
// ============================================================================
function handleFilesSelected(files) {
  if (!files || files.length === 0) return;

  if (!isConnected) {
    showToast('Please connect to your BacaIn before uploading files', 'warning');
    return;
  }

  for (let i = 0; i < files.length; i++) {
    const file = files[i];
    if (!file.name.toLowerCase().endsWith('.txt')) {
      showToast(`Warning: "${file.name}" is not a .txt file, attempting plain text upload`, 'warning');
    }
    uploadQueue.push(file);
  }

  processUploadQueue();
}

async function processUploadQueue() {
  if (isUploading) return;
  if (uploadQueue.length === 0) return;

  const file = uploadQueue.shift();
  await executeFileUpload(file);

  // If there are more files in queue, continue
  if (uploadQueue.length > 0 && !cancelUploadRequested) {
    processUploadQueue();
  }
}

async function executeFileUpload(file) {
  if (!controlChar || !dataChar) {
    showToast('Upload failed: Bluetooth connection lost', 'error');
    return;
  }

  isUploading = true;
  cancelUploadRequested = false;

  const fileName = file.name;
  const fileSize = file.size;

  log(`Starting upload: "${fileName}" (${formatBytes(fileSize)})`, 'info');
  showUploadActiveCard(fileName);

  try {
    // Read entire file as ArrayBuffer
    const fileBuffer = await file.arrayBuffer();
    const fileBytes = new Uint8Array(fileBuffer);

    // Step 1: Send START_UPLOAD (0x02)
    // Packet: [0x02, uint32_le file_size, filename_null_terminated]
    const enc = new TextEncoder();
    const nameBytes = enc.encode(fileName);
    const startPacket = new Uint8Array(1 + 4 + nameBytes.length + 1);
    startPacket[0] = OPCODES.START_UPLOAD;
    const view = new DataView(startPacket.buffer);
    view.setUint32(1, fileSize, true);
    startPacket.set(nameBytes, 5);
    startPacket[5 + nameBytes.length] = 0; // null terminator

    log(`Sending START_UPLOAD for "${fileName}" (size: ${fileSize} bytes)...`, 'info');
    const startRespPromise = waitForControlResponse(OPCODES.START_UPLOAD, 8000);
    await controlChar.writeValueWithResponse(startPacket);
    const startResp = await startRespPromise;

    if (startResp.status !== 0) {
      throw new Error(`Device rejected upload start (error code: ${startResp.status})`);
    }

    log(`Device accepted upload. Beginning chunk transfer (Chunk size: ${CHUNK_SIZE} B)...`, 'info');

    // Step 2: Stream Data Chunks
    const totalChunks = Math.ceil(fileBytes.length / CHUNK_SIZE);
    let seqId = 0;
    let bytesSent = 0;
    const startTime = performance.now();

    for (let offset = 0; offset < fileBytes.length; offset += CHUNK_SIZE) {
      if (cancelUploadRequested) {
        throw new Error('Upload cancelled by user');
      }

      if (!isConnected || !dataChar) {
        throw new Error('Connection lost during transmission');
      }

      const chunk = fileBytes.subarray(offset, Math.min(offset + CHUNK_SIZE, fileBytes.length));
      const packet = new Uint8Array(2 + chunk.length);
      const pktView = new DataView(packet.buffer);
      pktView.setUint16(0, seqId & 0xFFFF, true);
      packet.set(chunk, 2);

      // Write chunk without response
      if (dataChar.writeValueWithoutResponse) {
        await dataChar.writeValueWithoutResponse(packet);
      } else {
        await dataChar.writeValue(packet);
      }

      bytesSent += chunk.length;
      seqId = (seqId + 1) & 0xFFFF;

      // Update UI Progress Stats
      updateUploadStats(bytesSent, fileSize, seqId, totalChunks, startTime);

      // Pacing pause to allow ESP32 SDMMC/flash write
      if (CHUNK_DELAY_MS > 0) {
        await sleep(CHUNK_DELAY_MS);
      }
    }

    // Step 3: Send FINISH_UPLOAD (0x03)
    log('All chunks sent. Sending FINISH_UPLOAD (0x03)...', 'info');
    const finishPacket = new Uint8Array([OPCODES.FINISH_UPLOAD]);
    const finishRespPromise = waitForControlResponse(OPCODES.FINISH_UPLOAD, 10000);
    await controlChar.writeValueWithResponse(finishPacket);
    const finishResp = await finishRespPromise;

    if (finishResp.status !== 0) {
      throw new Error(`Device failed to finish upload (error code: ${finishResp.status})`);
    }

    const elapsedTotal = ((performance.now() - startTime) / 1000).toFixed(1);
    const avgSpeed = (fileSize / 1024 / (elapsedTotal || 1)).toFixed(1);
    log(`Upload complete: "${fileName}" in ${elapsedTotal}s (avg ${avgSpeed} KB/s)`, 'success');
    showToast(`Uploaded "${fileName}" successfully!`, 'success');

    // Refresh book list & storage stats
    await queryBooksList();
    await readDeviceStatus();

  } catch (err) {
    log(`Upload Error: ${err.message}`, 'error');
    showToast(`Upload failed: ${err.message}`, 'error');
    if (controlChar && isConnected) {
      try {
        const abortPacket = new Uint8Array([OPCODES.ABORT_UPLOAD]);
        await controlChar.writeValueWithResponse(abortPacket);
      } catch (_) {}
    }
  } finally {
    isUploading = false;
    hideUploadActiveCard();
  }
}

function showUploadActiveCard(fileName) {
  if (el.uploadingFileName) el.uploadingFileName.textContent = fileName;
  if (el.uploadProgressFill) el.uploadProgressFill.style.width = '0%';
  if (el.uploadPctStat) el.uploadPctStat.textContent = '0.0%';
  if (el.uploadSpeedStat) el.uploadSpeedStat.textContent = '0.0 KB/s';
  if (el.uploadSeqStat) el.uploadSeqStat.textContent = '#0 / 0';
  if (el.uploadEtaStat) el.uploadEtaStat.textContent = 'Calculating...';
  if (el.uploadActiveCard) el.uploadActiveCard.classList.add('visible');

  const bookUploader = document.getElementById('bookUploader');
  if (bookUploader && typeof bookUploader.showProgress === 'function') {
    bookUploader.showProgress(fileName);
  }
}

function hideUploadActiveCard() {
  if (el.uploadActiveCard) el.uploadActiveCard.classList.remove('visible');
  if (el.fileInput) el.fileInput.value = '';

  const bookUploader = document.getElementById('bookUploader');
  if (bookUploader && typeof bookUploader.reset === 'function') {
    bookUploader.reset();
  }
}

function updateUploadStats(bytesSent, totalBytes, seqId, totalChunks, startTime) {
  const pct = totalBytes > 0 ? ((bytesSent / totalBytes) * 100).toFixed(1) : 0;
  const elapsedSec = (performance.now() - startTime) / 1000;
  const speedKBps = elapsedSec > 0 ? (bytesSent / 1024 / elapsedSec) : 0;
  const remainingBytes = totalBytes - bytesSent;
  const etaSec = speedKBps > 0 ? Math.ceil((remainingBytes / 1024) / speedKBps) : 0;

  if (el.uploadProgressFill) el.uploadProgressFill.style.width = `${pct}%`;
  if (el.uploadPctStat) el.uploadPctStat.textContent = `${pct}%`;
  if (el.uploadSpeedStat) el.uploadSpeedStat.textContent = `${speedKBps.toFixed(1)} KB/s`;
  if (el.uploadSeqStat) el.uploadSeqStat.textContent = `#${seqId} / ${totalChunks}`;
  if (el.uploadEtaStat) el.uploadEtaStat.textContent = `${etaSec}s`;

  const bookUploader = document.getElementById('bookUploader');
  if (bookUploader && typeof bookUploader.setProgress === 'function') {
    bookUploader.setProgress({
      pct: Number(pct),
      speedKbps: speedKBps,
      currentSeq: seqId,
      totalSeq: totalChunks,
      etaSec
    });
  }
}

// ============================================================================
// Service Worker Registration
// ============================================================================
function registerServiceWorker() {
  if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register('./sw.js')
        .then((reg) => {
          log(`Service Worker registered with scope: ${reg.scope}`, 'info');
        })
        .catch((err) => {
          log(`Service Worker registration failed: ${err.message}`, 'warning');
        });
    });
  }
}

// ============================================================================
// Live Hardware Controls Logic (Storage, Typography, Refresh, Power)
// ============================================================================
let currentStorageTarget = 1; // 1: Internal Flash (/spiflash) default, 0: MicroSD (/sdcard)
let currentFontSize = 0; // 0: Font16, 1: Font12

function updateStorageTargetUI(target) {
  currentStorageTarget = target;
  if (el.storageTargetSdBtn && el.storageTargetFlashBtn) {
    el.storageTargetSdBtn.classList.toggle('active', target === 0);
    el.storageTargetFlashBtn.classList.toggle('active', target === 1);
  }
  if (el.storageTitleText) {
    el.storageTitleText.textContent = target === 0 ? 'MicroSD Storage' : 'Internal Flash';
  }
  if (el.storageTargetDesc) {
    el.storageTargetDesc.textContent = target === 0
      ? 'Books and reading progress are synced to MicroSD card (/sdcard).'
      : 'Books and reading progress are synced to Internal Flash (/spiflash).';
  }
  if (el.storageBadge && !isConnected) {
    el.storageBadge.textContent = target === 0 ? 'MicroSD' : 'Internal Flash';
    el.storageBadge.className = 'card-badge badge-muted';
  }

  // Update StatusDashboard Web Component
  const statusDashboard = document.getElementById('statusDashboard');
  if (statusDashboard) {
    if (typeof statusDashboard.updateFlashStorage === 'function') {
      statusDashboard.updateFlashStorage({ isActive: target === 1 });
    }
    if (typeof statusDashboard.updateSdStorage === 'function') {
      statusDashboard.updateSdStorage({ isActive: target === 0 });
    }
  }

  // Update BookLibrary Web Component
  const bookLibrary = document.getElementById('bookLibrary');
  if (bookLibrary && typeof bookLibrary.setStorageTargetBadge === 'function') {
    bookLibrary.setStorageTargetBadge(target);
  }
}

async function setStorageTarget(target) {
  updateStorageTargetUI(target);
  if (!isConnected || !controlChar) {
    showToast('Connect device first to switch storage', 'warning');
    return;
  }
  try {
    const targetName = target === 0 ? 'MicroSD Storage' : 'Internal Flash';
    log(`Setting storage target to ${targetName} (OpCode 0x06)...`, 'info');
    const packet = new Uint8Array([OPCODES.SET_STORAGE_TARGET, target]);
    const waitPromise = waitForControlResponse(OPCODES.SET_STORAGE_TARGET, 3500);
    await controlChar.writeValueWithResponse(packet);
    const resp = await waitPromise;
    let status = resp && resp.byteLength > 1 ? resp.getUint8(1) : 0;
    if (resp && typeof resp.status === 'number') {
      status = resp.status;
    }

    const settingsModal = document.getElementById('deviceSettingsModal') || document.getElementById('settingsModal');
    const statusDashboard = document.getElementById('statusDashboard');

    if (status !== 0) {
      showToast('MicroSD card not available or not mounted', 'warning');
      updateStorageTargetUI(1);
      if (settingsModal && typeof settingsModal.setSdCardAvailable === 'function') {
        settingsModal.setSdCardAvailable(false);
      }
      if (statusDashboard && typeof statusDashboard.updateSdStorage === 'function') {
        statusDashboard.updateSdStorage({ isMounted: false, totalKb: 0 });
      }
      return;
    }

    if (target === 0) {
      if (settingsModal && typeof settingsModal.setSdCardAvailable === 'function') {
        settingsModal.setSdCardAvailable(true);
      }
    }

    showToast(`Active storage: ${targetName}`, 'success');
    await readDeviceStatus();
    await queryBooksList();
  } catch (err) {
    log(`Failed to set storage target: ${err.message}`, 'error');
    showToast('Failed to switch storage target', 'error');
  }
}
const sendSetStorageTarget = setStorageTarget;

function updateFontUI(fontId) {
  currentFontSize = fontId;
  if (el.font16Btn && el.font12Btn) {
    el.font16Btn.classList.toggle('active', fontId === 0);
    el.font12Btn.classList.toggle('active', fontId === 1);
  }
}

async function setFont(fontId) {
  if (!isConnected || !controlChar) {
    showToast('Connect device first', 'warning');
    return;
  }
  try {
    log(`Setting display typography to Font${fontId === 0 ? '16' : '12'} (OpCode 0x10)...`, 'info');
    const packet = new Uint8Array([OPCODES.SET_FONT, fontId]);
    await controlChar.writeValueWithResponse(packet);
    updateFontUI(fontId);
    showToast(`Font set to Font${fontId === 0 ? '16' : '12'}`, 'success');
  } catch (err) {
    log(`Failed to set font: ${err.message}`, 'error');
    showToast('Failed to change typography', 'error');
  }
}
const sendSetFont = setFont;

async function forceRefresh() {
  if (!isConnected || !controlChar) {
    showToast('Connect device first', 'warning');
    return;
  }
  try {
    log('Sending screen clean / full refresh request (OpCode 0x12)...', 'info');
    const packet = new Uint8Array([OPCODES.FORCE_REFRESH]);
    await controlChar.writeValueWithResponse(packet);
    showToast('Display cleaned (ghosting cleared)', 'success');
  } catch (err) {
    log(`Failed to refresh screen: ${err.message}`, 'error');
    showToast('Refresh request failed', 'error');
  }
}
const sendForceRefresh = forceRefresh;

async function setSleepTimeout(seconds) {
  if (!isConnected || !controlChar) {
    showToast('Connect device first', 'warning');
    return;
  }
  try {
    log(`Setting inactivity sleep timeout to ${seconds}s (OpCode 0x13)...`, 'info');
    const packet = new Uint8Array([OPCODES.SET_SLEEP_TIMEOUT, seconds & 0xff, (seconds >> 8) & 0xff]);
    await controlChar.writeValueWithResponse(packet);
    showToast(`Inactivity timeout: ${seconds === 0 ? 'Disabled' : seconds + 's'}`, 'success');
  } catch (err) {
    log(`Failed to set sleep timeout: ${err.message}`, 'error');
    showToast('Failed to update timeout', 'error');
  }
}
const sendSetSleepTimeout = setSleepTimeout;

async function triggerSleep() {
  if (!isConnected || !controlChar) {
    showToast('Connect device first', 'warning');
    return;
  }
  try {
    log('Triggering instant deep sleep on device (OpCode 0x14)...', 'warning');
    const packet = new Uint8Array([OPCODES.TRIGGER_SLEEP]);
    await controlChar.writeValueWithResponse(packet);
    showToast('Device entering deep sleep. Disconnecting...', 'info');
    disconnectDevice();
  } catch (err) {
    log(`Failed to trigger sleep: ${err.message}`, 'error');
    showToast('Failed to enter sleep', 'error');
  }
}
const sendTriggerSleep = triggerSleep;

// ============================================================================
// Event Listeners Initialization
// ============================================================================
function initEventListeners() {
  // BLE Connection Button (Header & Onboarding)
  if (el.connectBtn) {
    el.connectBtn.addEventListener('click', handleConnectToggle);
  }
  if (el.onboardingConnectBtn) {
    el.onboardingConnectBtn.addEventListener('click', handleConnectToggle);
  }
  if (el.skipOnboardingBtn) {
    el.skipOnboardingBtn.addEventListener('click', switchToDashboard);
  }

  // Status & Books Refresh Buttons
  if (el.refreshStatusBtn) {
    el.refreshStatusBtn.addEventListener('click', readDeviceStatus);
  }
  if (el.refreshBooksBtn) {
    el.refreshBooksBtn.addEventListener('click', queryBooksList);
  }

  // Search filter input
  if (el.bookSearchInput) {
    el.bookSearchInput.addEventListener('input', renderBooksList);
  }

  // File Dropzone & Input
  if (el.dropzone && el.fileInput) {
    el.dropzone.addEventListener('click', () => el.fileInput.click());
    el.fileInput.addEventListener('change', (e) => handleFilesSelected(e.target.files));

    el.dropzone.addEventListener('dragover', (e) => {
      e.preventDefault();
      e.stopPropagation();
      el.dropzone.classList.add('dragover');
    });

    el.dropzone.addEventListener('dragleave', (e) => {
      e.preventDefault();
      e.stopPropagation();
      el.dropzone.classList.remove('dragover');
    });

    el.dropzone.addEventListener('drop', (e) => {
      e.preventDefault();
      e.stopPropagation();
      el.dropzone.classList.remove('dragover');
      if (e.dataTransfer && e.dataTransfer.files) {
        handleFilesSelected(e.dataTransfer.files);
      }
    });
  }

  // Cancel Upload
  if (el.cancelUploadBtn) {
    el.cancelUploadBtn.addEventListener('click', () => {
      if (isUploading) {
        cancelUploadRequested = true;
        log('User requested upload cancellation.', 'warning');
        showToast('Cancelling upload...', 'warning');
      }
    });
  }

  // Settings Modal Dialog
  if (el.settingsBtn) {
    el.settingsBtn.addEventListener('click', openSettingsModal);
  }
  if (el.closeSettingsBtn) {
    el.closeSettingsBtn.addEventListener('click', closeSettingsModal);
  }
  if (el.settingsModal) {
    el.settingsModal.addEventListener('click', (e) => {
      if (e.target === el.settingsModal) {
        closeSettingsModal();
      }
    });
  }

  // Delete Modal
  if (el.cancelDeleteBtn) {
    el.cancelDeleteBtn.addEventListener('click', closeDeleteModal);
  }
  if (el.confirmDeleteBtn) {
    el.confirmDeleteBtn.addEventListener('click', confirmDeleteBook);
  }
  if (el.deleteModal) {
    el.deleteModal.addEventListener('click', (e) => {
      if (e.target === el.deleteModal) closeDeleteModal();
    });
  }

  // Keyboard shortcut (Escape to close modals)
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      if (el.settingsModal && el.settingsModal.classList.contains('active')) {
        closeSettingsModal();
      }
      if (el.deleteModal && el.deleteModal.classList.contains('active')) {
        closeDeleteModal();
      }
    }
  });

  // Log drawer
  if (el.logHeader) {
    el.logHeader.addEventListener('click', (e) => {
      if (e.target === el.clearLogBtn) return;
      el.logBody.classList.toggle('collapsed');
      el.logToggleIcon.textContent = el.logBody.classList.contains('collapsed') ? '\u25B6' : '\u25BC';
    });
  }

  if (el.clearLogBtn) {
    el.clearLogBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      el.logBody.innerHTML = '';
      logCount = 0;
      el.logCountBadge.textContent = '0';
    });
  }

  // Settings Modal Hardware Control Listeners
  if (el.font16Btn) {
    el.font16Btn.addEventListener('click', () => setFont(0));
  }
  if (el.font12Btn) {
    el.font12Btn.addEventListener('click', () => setFont(1));
  }
  if (el.forceRefreshBtn) {
    el.forceRefreshBtn.addEventListener('click', () => forceRefresh());
  }
  if (el.sleepTimeoutSelect) {
    el.sleepTimeoutSelect.addEventListener('change', (e) => setSleepTimeout(parseInt(e.target.value, 10)));
  }
  if (el.triggerSleepBtn) {
    el.triggerSleepBtn.addEventListener('click', () => triggerSleep());
  }

  // Web Components Event Listeners
  document.addEventListener('storage-target-change', (e) => {
    if (e.detail && e.detail.target !== undefined) {
      setStorageTarget(e.detail.target);
    }
  });

  document.addEventListener('refresh-books', async () => {
    await readDeviceStatus();
    await queryBooksList();
  });

  document.addEventListener('delete-book', (e) => {
    if (e.detail) {
      openDeleteModal(e.detail.book || { name: e.detail.name, size: e.detail.size, path: e.detail.path });
    }
  });

  document.addEventListener('file-select', (e) => {
    if (e.detail && e.detail.files) {
      handleFilesSelected(e.detail.files);
    }
  });

  document.addEventListener('cancel-upload', () => {
    if (isUploading) {
      cancelUploadRequested = true;
      log('User requested upload cancellation.', 'warning');
      showToast('Cancelling upload...', 'warning');
    }
  });

  document.addEventListener('font-change', (e) => {
    if (e.detail && e.detail.font !== undefined) {
      setFont(e.detail.font);
    }
  });

  document.addEventListener('force-refresh', () => {
    forceRefresh();
  });

  document.addEventListener('sleep-timeout-change', (e) => {
    if (e.detail && e.detail.timeout !== undefined) {
      setSleepTimeout(e.detail.timeout);
    }
  });

  document.addEventListener('trigger-sleep', () => {
    triggerSleep();
  });
}

// ============================================================================
// App Bootstrap
// ============================================================================
function init() {
  refreshDomElements();
  const repoUrl = appConfig?.repo || appConfig?.GITHUB_REPO;
  if (repoUrl) {
    const githubBtn = el.githubBtn || document.getElementById('githubBtn');
    if (githubBtn) {
      githubBtn.href = repoUrl;
    }
  }
  initTheme();
  checkBluetoothCompatibility();
  initEventListeners();
  updateStorageTargetUI(1);
  registerServiceWorker();
  log('BacaIn Web Bluetooth PWA initialized.', 'info');
}

document.addEventListener('DOMContentLoaded', init);
