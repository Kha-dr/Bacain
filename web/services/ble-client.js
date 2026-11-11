/**
 * BacaIn Web Bluetooth Client & Protocol Engine
 * Strictly typed against device protocol opcodes and packet specifications.
 */

if (typeof window === "undefined" || !window.CONFIG || !window.CONFIG.ble) {
  throw new Error("window.CONFIG.ble is missing");
}

const {
  service: SERVICE_UUID,
  control: CONTROL_UUID,
  data: DATA_UUID,
  status: STATUS_UUID,
  chunkSize: CHUNK_SIZE,
  chunkDelayMs: CHUNK_DELAY_MS,
  opcodes: OPCODES,
} = window.CONFIG.ble;

// Transport State
let bleDevice = null;
let gattServer = null;
let controlChar = null;
let dataChar = null;
let statusChar = null;

let isConnecting = false;
let isConnected = false;

// Response Awaiting Callbacks
const controlCallbacks = new Map();

// Books Listing Stream State
let activeBookStream = null;
let listBooksTimeout = null;

function getDeviceName() {
  return bleDevice ? bleDevice.name : null;
}

function dispatchConnectionChange() {
  if (typeof window !== "undefined") {
    window.dispatchEvent(
      new CustomEvent("ble-connection-changed", {
        detail: {
          isConnected,
          isConnecting,
          name: getDeviceName(),
        },
      }),
    );
  }
}

function parseStatusValue(dataView) {
  if (!dataView || dataView.byteLength < 12) {
    return null;
  }

  const batteryPct = dataView.getUint8(0);
  const isCharging = dataView.getUint8(1) !== 0;
  const batteryMv = dataView.getUint16(2, true);
  const freeKb = dataView.getUint32(4, true);
  const totalKb = dataView.getUint32(8, true);

  const usedKb = totalKb > freeKb ? totalKb - freeKb : 0;
  const usedPct = totalKb > 0 ? parseFloat(((usedKb / totalKb) * 100).toFixed(1)) : 0;

  const detail = {
    batteryPct,
    isCharging,
    batteryMv,
    freeKb,
    totalKb,
    usedKb,
    usedPct,
  };

  if (typeof window !== "undefined") {
    window.dispatchEvent(new CustomEvent("ble-status-changed", { detail }));
  }

  return detail;
}

function onStatusNotification(event) {
  parseStatusValue(event.target.value);
}

function parseConfigPayload(val) {
  if (!val || val.byteLength < 2) return null;
  const status = val.getUint8(1);
  if (status !== 0x00) return null;

  const font = val.byteLength > 2 ? val.getUint8(2) : 1;
  const navHints = val.byteLength > 3 ? val.getUint8(3) !== 0 : true;
  const rotation = val.byteLength > 4 ? val.getUint8(4) : 0;
  const buttonInvert = val.byteLength > 5 ? val.getUint8(5) !== 0 : false;
  const statusLed = val.byteLength > 6 ? val.getUint8(6) !== 0 : true;

  const configObj = {
    font,
    navHints,
    rotation,
    buttonInvert,
    statusLed,
  };

  if (val.byteLength > 7) {
    configObj.storageTarget = val.getUint8(7);
  }
  if (val.byteLength >= 10) {
    configObj.sleepTimeout = val.getUint16(8, true);
  }

  const caps = val.byteLength >= 11 ? val.getUint8(10) : 0;
  const rawW = val.byteLength >= 13 ? val.getUint16(11, true) : 200;
  const rawH = val.byteLength >= 15 ? val.getUint16(13, true) : 200;
  const screenW = rotation % 2 === 1 ? rawH : rawW;
  const screenH = rotation % 2 === 1 ? rawW : rawH;
  const mushafSupported = (caps & OPCODES.CAP_MUSHAF) !== 0;
  const refreshIntervalSupported = val.byteLength >= 16 && (caps & OPCODES.CAP_REFRESH_INTERVAL) !== 0;

  configObj.caps = caps;
  configObj.screenW = screenW;
  configObj.screenH = screenH;
  configObj.mushafSupported = mushafSupported;
  configObj.refreshIntervalSupported = refreshIntervalSupported;
  if (refreshIntervalSupported) {
    configObj.refreshInterval = val.getUint8(15);
  }

  return configObj;
}

function parseMushafStatePayload(val) {
  if (!val || val.byteLength < 8) {
    return {
      started: false,
      surah: 1,
      ayah: 1,
      layer: 0,
      bookmarks: [],
    };
  }

  const started = val.getUint8(2) !== 0;
  const surah = val.getUint8(3) || 1;
  const ayah = val.getUint16(4, true) || 1;
  const layer = val.getUint8(6);
  const bookmarkCount = Math.min(val.getUint8(7), 8);

  const bookmarks = [];
  for (let i = 0; i < bookmarkCount; i++) {
    const offset = 8 + i * 4;
    if (offset + 4 <= val.byteLength) {
      const bmSurah = val.getUint8(offset);
      const bmAyah = val.getUint16(offset + 1, true);
      const bmJuz = val.getUint8(offset + 3);
      if (bmSurah > 0) {
        bookmarks.push({ surah: bmSurah, ayah: bmAyah, juz: bmJuz });
      }
    }
  }

  return {
    started,
    surah,
    ayah,
    layer,
    bookmarks,
  };
}

function handleListBooksPacket(val) {
  if (val.byteLength < 7) {
    return;
  }

  const status = val.getUint8(1);
  const isLast = val.getUint8(2);
  const fileSize = val.getUint32(3, true);

  let filename = "";
  const nameBytes = [];
  for (let i = 7; i < val.byteLength; i++) {
    const b = val.getUint8(i);
    if (b === 0) break;
    nameBytes.push(b);
  }

  if (nameBytes.length > 0) {
    try {
      filename = new TextDecoder("utf-8").decode(new Uint8Array(nameBytes)).trim();
    } catch (_) {
      filename = String.fromCharCode.apply(null, nameBytes).trim();
    }
  }

  const nullIdx = 7 + nameBytes.length;
  let byteOffset = 0;
  let bookmarkCount = 0;
  if (val.byteLength >= nullIdx + 6) {
    byteOffset = val.getUint32(nullIdx + 1, true);
    bookmarkCount = val.getUint8(nullIdx + 5);
  }
  const progress = fileSize > 0 ? Math.min(100, Math.floor((byteOffset * 100) / fileSize)) : 0;

  if (status === 0 && filename.length > 0) {
    if (activeBookStream && typeof activeBookStream.onBookReceived === "function") {
      activeBookStream.onBookReceived({
        name: filename,
        size: fileSize,
        byteOffset,
        bookmarkCount,
        progress,
      });
    }
  }

  if (isLast === 1) {
    if (listBooksTimeout) {
      clearTimeout(listBooksTimeout);
      listBooksTimeout = null;
    }
    if (activeBookStream && typeof activeBookStream.onComplete === "function") {
      const stream = activeBookStream;
      activeBookStream = null;
      stream.onComplete();
    }
  }
}

function onControlNotification(event) {
  const val = event.target.value;
  if (!val || val.byteLength < 1) return;

  const opcode = val.getUint8(0);

  if (controlCallbacks.has(opcode)) {
    const callback = controlCallbacks.get(opcode);
    controlCallbacks.delete(opcode);
    callback(val);
    return;
  }

  if (opcode === OPCODES.LIST_BOOKS) {
    handleListBooksPacket(val);
    return;
  }

  if (opcode === OPCODES.GET_CONFIG) {
    const config = parseConfigPayload(val);
    if (config && typeof window !== "undefined") {
      window.dispatchEvent(new CustomEvent("ble-config-changed", { detail: config }));
    }
    return;
  }

  if (opcode === OPCODES.GET_MUSHAF_STATE) {
    const mushafState = parseMushafStatePayload(val);
    if (typeof window !== "undefined") {
      window.dispatchEvent(new CustomEvent("ble-mushaf-state-changed", { detail: mushafState }));
    }
    return;
  }
}

function waitForControlResponse(opcode, timeoutMs = 8000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      controlCallbacks.delete(opcode);
      reject(
        new Error(`Timeout waiting for BLE opcode 0x${opcode.toString(16)} response`),
      );
    }, timeoutMs);

    controlCallbacks.set(opcode, (val) => {
      clearTimeout(timer);
      const status = val.byteLength >= 2 ? val.getUint8(1) : 0;
      resolve({ status, dataView: val });
    });
  });
}

function onDisconnected() {
  isConnected = false;
  isConnecting = false;
  gattServer = null;
  controlChar = null;
  dataChar = null;
  statusChar = null;

  if (listBooksTimeout) {
    clearTimeout(listBooksTimeout);
    listBooksTimeout = null;
  }
  if (activeBookStream && typeof activeBookStream.onError === "function") {
    activeBookStream.onError(new Error("Device disconnected"));
  }
  activeBookStream = null;

  for (const [, callback] of controlCallbacks.entries()) {
    try {
      callback(new DataView(new Uint8Array([0, 0xff]).buffer));
    } catch (cbErr) {
      console.error("Error invoking control callback on disconnect:", cbErr);
    }
  }
  controlCallbacks.clear();

  dispatchConnectionChange();
}

async function connect() {
  if (typeof navigator === "undefined" || !navigator.bluetooth) {
    throw new Error("Web Bluetooth API is not supported in this browser.");
  }

  try {
    isConnecting = true;
    dispatchConnectionChange();

    bleDevice = await navigator.bluetooth.requestDevice({
      filters: [
        { services: [SERVICE_UUID] },
        { name: "BacaIn" },
        { namePrefix: "BacaIn" },
        { namePrefix: "Mini-E-Reader" },
        { namePrefix: "Mini-E" },
      ],
      optionalServices: [SERVICE_UUID],
    });

    bleDevice.addEventListener("gattserverdisconnected", onDisconnected);

    gattServer = await bleDevice.gatt.connect();
    const service = await gattServer.getPrimaryService(SERVICE_UUID);

    controlChar = await service.getCharacteristic(CONTROL_UUID);
    dataChar = await service.getCharacteristic(DATA_UUID);
    statusChar = await service.getCharacteristic(STATUS_UUID);

    await controlChar.startNotifications();
    controlChar.addEventListener("characteristicvaluechanged", onControlNotification);

    await statusChar.startNotifications();
    statusChar.addEventListener("characteristicvaluechanged", onStatusNotification);

    isConnected = true;
    isConnecting = false;
    dispatchConnectionChange();

    // Query active hardware ground truth (NVS config & initial status)
    try {
      await readDeviceStatus();
    } catch (e) {
      console.warn("Initial status read error:", e);
    }
    try {
      await requestDeviceConfig();
    } catch (e) {
      console.warn("Initial config read error:", e);
    }

    return bleDevice;
  } catch (error) {
    isConnecting = false;
    isConnected = false;
    dispatchConnectionChange();
    throw error;
  }
}

function disconnect() {
  if (bleDevice && bleDevice.gatt && bleDevice.gatt.connected) {
    bleDevice.gatt.disconnect();
  } else {
    onDisconnected();
  }
}

async function readDeviceStatus() {
  if (!statusChar) {
    throw new Error("Not connected to BacaIn");
  }
  const val = await statusChar.readValue();
  return parseStatusValue(val);
}

async function requestDeviceConfig() {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const respPromise = waitForControlResponse(OPCODES.GET_CONFIG, 5000);
  const packet = new Uint8Array([OPCODES.GET_CONFIG]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }

  const { status, dataView: val } = await respPromise;
  if (status === 0x00 && val) {
    const config = parseConfigPayload(val);
    if (config && typeof window !== "undefined") {
      window.dispatchEvent(new CustomEvent("ble-config-changed", { detail: config }));
    }
    return config;
  }
  throw new Error(`Device configuration request returned status: 0x${status.toString(16)}`);
}

function queryBooksList(onBookReceived, onComplete, onError) {
  return new Promise((resolve, reject) => {
    if (!controlChar || !isConnected) {
      const err = new Error("Not connected to BacaIn");
      if (typeof onError === "function") onError(err);
      reject(err);
      return;
    }

    if (listBooksTimeout) {
      clearTimeout(listBooksTimeout);
      listBooksTimeout = null;
    }

    const books = [];
    activeBookStream = {
      onBookReceived: (book) => {
        books.push(book);
        if (typeof onBookReceived === "function") {
          onBookReceived(book);
        }
      },
      onComplete: () => {
        if (typeof onComplete === "function") {
          onComplete(books);
        }
        resolve(books);
      },
      onError: (err) => {
        if (typeof onError === "function") {
          onError(err);
        }
        reject(err);
      },
    };

    listBooksTimeout = setTimeout(() => {
      if (activeBookStream) {
        const stream = activeBookStream;
        activeBookStream = null;
        stream.onComplete();
      }
    }, 4000);

    const req = new Uint8Array([OPCODES.LIST_BOOKS]);
    const writePromise = controlChar.writeValueWithResponse
      ? controlChar.writeValueWithResponse(req)
      : controlChar.writeValue(req);

    writePromise.catch((err) => {
      if (listBooksTimeout) {
        clearTimeout(listBooksTimeout);
        listBooksTimeout = null;
      }
      if (activeBookStream) {
        const stream = activeBookStream;
        activeBookStream = null;
        stream.onError(err);
      }
    });
  });
}

async function deleteBook(filename) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }

  const enc = new TextEncoder();
  const nameBytes = enc.encode(filename);
  const packet = new Uint8Array(1 + nameBytes.length + 1);
  packet[0] = OPCODES.DELETE_BOOK;
  packet.set(nameBytes, 1);
  packet[1 + nameBytes.length] = 0;

  const respPromise = waitForControlResponse(OPCODES.DELETE_BOOK, 6000);
  await controlChar.writeValueWithResponse(packet);
  const { status } = await respPromise;
  if (status !== 0) {
    throw new Error(`Device reported error code: ${status}`);
  }
  return { status };
}

async function queryBookBookmarks(filename) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }

  const enc = new TextEncoder();
  const nameBytes = enc.encode(filename);
  const packet = new Uint8Array(1 + nameBytes.length + 1);
  packet[0] = OPCODES.GET_BOOKMARKS;
  packet.set(nameBytes, 1);
  packet[1 + nameBytes.length] = 0;

  const respPromise = waitForControlResponse(OPCODES.GET_BOOKMARKS, 4000);
  await controlChar.writeValueWithResponse(packet);
  const { status, dataView: val } = await respPromise;

  if (status !== 0 || !val || val.byteLength < 11) {
    throw new Error(`Device failed to return bookmarks for "${filename}" (status ${status})`);
  }

  const byteOffset = val.getUint32(2, true);
  const totalBytes = val.getUint32(6, true);
  const bookmarkCount = val.getUint8(10);
  const bookmarks = [];

  for (let i = 0; i < bookmarkCount && 11 + (i + 1) * 4 <= val.byteLength; i++) {
    bookmarks.push(val.getUint32(11 + i * 4, true));
  }

  return { status, byteOffset, totalBytes, bookmarkCount, bookmarks };
}

function buildOffsetAndFilenamePacket(opcode, byteOffset, filename) {
  const enc = new TextEncoder();
  const nameBytes = enc.encode(filename);
  const packet = new Uint8Array(1 + 4 + nameBytes.length + 1);
  const view = new DataView(packet.buffer);
  view.setUint8(0, opcode);
  view.setUint32(1, Math.max(0, Math.floor(Number(byteOffset) || 0)), true);
  packet.set(nameBytes, 5);
  packet[5 + nameBytes.length] = 0;
  return packet;
}

async function addBookBookmark(filename, byteOffset) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = buildOffsetAndFilenamePacket(OPCODES.ADD_BOOKMARK, byteOffset, filename);
  const respPromise = waitForControlResponse(OPCODES.ADD_BOOKMARK, 4000);
  await controlChar.writeValueWithResponse(packet);
  const { status } = await respPromise;
  if (status !== 0) {
    throw new Error(`Failed to add bookmark (status ${status})`);
  }
  return status;
}

async function deleteBookBookmark(filename, byteOffset) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = buildOffsetAndFilenamePacket(OPCODES.DELETE_BOOKMARK, byteOffset, filename);
  const respPromise = waitForControlResponse(OPCODES.DELETE_BOOKMARK, 4000);
  await controlChar.writeValueWithResponse(packet);
  const { status } = await respPromise;
  if (status !== 0) {
    throw new Error(`Failed to delete bookmark (status ${status})`);
  }
  return status;
}

async function setBookReadingOffset(filename, byteOffset) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = buildOffsetAndFilenamePacket(OPCODES.SET_BOOK_OFFSET, byteOffset, filename);
  const respPromise = waitForControlResponse(OPCODES.SET_BOOK_OFFSET, 4000);
  await controlChar.writeValueWithResponse(packet);
  const { status } = await respPromise;
  if (status !== 0) {
    throw new Error(`Failed to set reading offset (status ${status})`);
  }
  return status;
}

function describeUploadErrorStatus(status, rxBytes, expectedBytes) {
  switch (status) {
    case 0x02:
    case 0x07:
      return "Target storage not mounted or unavailable (0x07)";
    case 0x03:
      return "BLE chunk sequence mismatch / dropped packet (0x03)";
    case 0x04:
      return "Device ringbuffer overflow / storage write backpressure (0x04)";
    case 0x05:
      return "Flash/SD write failed or storage full (0x05)";
    case 0x06:
      if (!rxBytes) {
        return "File size exceeds available device storage or size mismatch (0x06)";
      }
      return `File size mismatch: device received ${rxBytes} of ${expectedBytes} bytes (0x06)`;
    case 0x09:
      return "Invalid mushaf.bqm header or display geometry mismatch (0x09)";
    default:
      return `Device error status 0x${Number(status).toString(16).padStart(2, "0")}`;
  }
}

async function executeFileUpload(file, options = {}) {
  const { onProgress, onStatus, checkCancelled } = options;
  if (!isConnected || !controlChar || !dataChar) {
    throw new Error("Not connected to BacaIn");
  }

  const fileName = file.name;
  const fileSize = file.size;

  if (typeof onStatus === "function") {
    onStatus(`Starting upload: "${fileName}" (${fileSize} bytes)`);
  }

  const fileBuffer = await file.arrayBuffer();
  const fileBytes = new Uint8Array(fileBuffer);

  const enc = new TextEncoder();
  const nameBytes = enc.encode(fileName);
  const startPacket = new Uint8Array(1 + 4 + nameBytes.length + 1);
  startPacket[0] = OPCODES.START_UPLOAD;
  const view = new DataView(startPacket.buffer);
  view.setUint32(1, fileSize, true);
  startPacket.set(nameBytes, 5);
  startPacket[5 + nameBytes.length] = 0;

  const startRespPromise = waitForControlResponse(OPCODES.START_UPLOAD, 10000);
  await controlChar.writeValueWithResponse(startPacket);
  const startResp = await startRespPromise;

  if (startResp.status !== 0) {
    throw new Error(
      `Device rejected upload start: ${describeUploadErrorStatus(startResp.status, 0, fileSize)}`,
    );
  }

  const totalChunks = Math.ceil(fileBytes.length / CHUNK_SIZE);
  let seqId = 0;
  let bytesSent = 0;
  const startTime = performance.now();

  try {
    for (let offset = 0; offset < fileBytes.length; offset += CHUNK_SIZE) {
      if (checkCancelled && checkCancelled()) {
        throw new Error("Upload cancelled by user");
      }
      if (!isConnected || !dataChar) {
        throw new Error("Connection lost during transmission");
      }

      const chunk = fileBytes.subarray(
        offset,
        Math.min(offset + CHUNK_SIZE, fileBytes.length),
      );
      const packet = new Uint8Array(2 + chunk.length);
      const pktView = new DataView(packet.buffer);
      pktView.setUint16(0, seqId & 0xffff, true);
      packet.set(chunk, 2);

      const isSyncChunk = (seqId & 7) === 7 || offset + CHUNK_SIZE >= fileBytes.length;
      if (isSyncChunk && dataChar.writeValueWithResponse) {
        await dataChar.writeValueWithResponse(packet);
      } else if (dataChar.writeValueWithoutResponse) {
        await dataChar.writeValueWithoutResponse(packet);
      } else {
        await dataChar.writeValue(packet);
      }

      bytesSent += chunk.length;
      seqId = (seqId + 1) & 0xffff;

      if (typeof onProgress === "function") {
        onProgress({
          bytesSent,
          totalBytes: fileSize,
          seqId,
          totalChunks,
          startTime,
        });
      }

      if (CHUNK_DELAY_MS > 0) {
        await new Promise((resolve) => setTimeout(resolve, CHUNK_DELAY_MS));
      }
    }

    const finishPacket = new Uint8Array([OPCODES.FINISH_UPLOAD]);
    const finishRespPromise = waitForControlResponse(OPCODES.FINISH_UPLOAD, 30000);
    await controlChar.writeValueWithResponse(finishPacket);
    const finishResp = await finishRespPromise;

    if (finishResp.status !== 0) {
      const rxBytes =
        finishResp.dataView && finishResp.dataView.byteLength >= 6
          ? finishResp.dataView.getUint32(2, true)
          : null;
      throw new Error(
        `Device failed to finish upload: ${describeUploadErrorStatus(finishResp.status, rxBytes, fileSize)}`,
      );
    }

    return { ok: true, fileName, fileSize };
  } catch (err) {
    if (controlChar && isConnected) {
      try {
        const abortPacket = new Uint8Array([OPCODES.ABORT_UPLOAD]);
        await controlChar.writeValueWithResponse(abortPacket);
      } catch (abortErr) {
        console.error("Failed to send abort packet:", abortErr);
      }
    }
    throw err;
  }
}

async function sendSetStorageTarget(target) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([OPCODES.SET_STORAGE_TARGET, target]);
  const waitPromise = waitForControlResponse(OPCODES.SET_STORAGE_TARGET, 3500);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  const resp = await waitPromise;
  let status = 0;
  if (resp && typeof resp.status === "number") {
    status = resp.status;
  } else if (resp && resp.dataView && resp.dataView.byteLength > 1) {
    status = resp.dataView.getUint8(1);
  }
  return { status };
}

async function sendSetFont(fontId) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([OPCODES.SET_FONT, fontId]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  return true;
}

async function sendSetNavHints(enabled) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([OPCODES.SET_NAV_HINTS, enabled ? 1 : 0]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  return true;
}

async function sendSetRotation(rot) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([OPCODES.SET_ROTATION, rot]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  return true;
}

async function sendSetButtonInvert(inverted) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([OPCODES.SET_BUTTON_INVERT, inverted ? 1 : 0]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  return true;
}

async function sendSetStatusLed(enabled) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const respPromise = waitForControlResponse(OPCODES.SET_STATUS_LED, 5000);
  const packet = new Uint8Array([OPCODES.SET_STATUS_LED, enabled ? 1 : 0]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  const { status } = await respPromise;
  return status;
}

async function sendSetSleepTimeout(seconds) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([
    OPCODES.SET_SLEEP_TIMEOUT,
    seconds & 0xff,
    (seconds >> 8) & 0xff,
  ]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  return true;
}

async function sendSetRefreshInterval(interval) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const respPromise = waitForControlResponse(OPCODES.SET_REFRESH_INTERVAL, 5000);
  const packet = new Uint8Array([OPCODES.SET_REFRESH_INTERVAL, interval]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  const { status } = await respPromise;
  if (status !== 0x00) {
    throw new Error(`Device replied with error status: 0x${status.toString(16)}`);
  }
  return true;
}

async function sendTriggerSleep() {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([OPCODES.TRIGGER_SLEEP]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  return true;
}

async function sendTriggerRefresh() {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const packet = new Uint8Array([OPCODES.FORCE_REFRESH]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  return true;
}

async function requestMushafState() {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const respPromise = waitForControlResponse(OPCODES.GET_MUSHAF_STATE, 5000);
  const packet = new Uint8Array([OPCODES.GET_MUSHAF_STATE]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  const { status, dataView: val } = await respPromise;
  if (status !== 0x00 || !val) {
    throw new Error(`Mushaf state request returned status: 0x${(status || 0).toString(16)}`);
  }
  const state = parseMushafStatePayload(val);
  if (typeof window !== "undefined") {
    window.dispatchEvent(new CustomEvent("ble-mushaf-state-changed", { detail: state }));
  }
  return state;
}

async function setMushafCursor(surah, ayah, layer = 0) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const s = Math.max(1, Math.min(114, parseInt(surah, 10) || 1));
  const a = Math.max(1, parseInt(ayah, 10) || 1);
  const l = Math.max(0, Math.min(3, parseInt(layer, 10) || 0));

  const respPromise = waitForControlResponse(OPCODES.SET_MUSHAF_STATE, 5000);
  const packet = new Uint8Array([
    OPCODES.SET_MUSHAF_STATE,
    0x01,
    s,
    a & 0xff,
    (a >> 8) & 0xff,
    l,
  ]);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  const { status } = await respPromise;
  if (status !== 0x00) {
    throw new Error(`Device returned status 0x${status.toString(16)} setting cursor`);
  }
  return status;
}

async function setMushafBookmarks(bookmarks) {
  if (!isConnected || !controlChar) {
    throw new Error("Not connected to BacaIn");
  }
  const list = Array.isArray(bookmarks) ? bookmarks.slice(0, 8) : [];
  const count = list.length;
  const packet = new Uint8Array(3 + count * 4);
  packet[0] = OPCODES.SET_MUSHAF_STATE;
  packet[1] = 0x02;
  packet[2] = count;

  for (let i = 0; i < count; i++) {
    const bm = list[i];
    const offset = 3 + i * 4;
    packet[offset] = bm.surah & 0xff;
    packet[offset + 1] = bm.ayah & 0xff;
    packet[offset + 2] = (bm.ayah >> 8) & 0xff;
    packet[offset + 3] = bm.juz & 0xff;
  }

  const respPromise = waitForControlResponse(OPCODES.SET_MUSHAF_STATE, 5000);
  if (controlChar.writeValueWithResponse) {
    await controlChar.writeValueWithResponse(packet);
  } else {
    await controlChar.writeValue(packet);
  }
  const { status } = await respPromise;
  if (status !== 0x00) {
    throw new Error(`Device returned status 0x${status.toString(16)} setting bookmarks`);
  }
  return status;
}

const BleClient = {
  isConnected: () => isConnected,
  isConnecting: () => isConnecting,
  getDeviceName,
  connect,
  disconnect,
  waitForControlResponse,
  readDeviceStatus,
  requestDeviceConfig,
  queryBooksList,
  deleteBook,
  queryBookBookmarks,
  addBookBookmark,
  deleteBookBookmark,
  setBookReadingOffset,
  executeFileUpload,
  sendSetStorageTarget,
  sendSetFont,
  sendSetNavHints,
  sendSetRotation,
  sendSetButtonInvert,
  sendSetStatusLed,
  sendSetSleepTimeout,
  sendSetRefreshInterval,
  sendTriggerSleep,
  sendTriggerRefresh,
  requestMushafState,
  setMushafCursor,
  setMushafBookmarks,
};

if (typeof window !== "undefined") {
  window.BleClient = BleClient;
}
if (typeof module !== "undefined" && module.exports) {
  module.exports = { BleClient };
}
