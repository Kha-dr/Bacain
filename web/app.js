/**
 * BacaIn Web Bluetooth PWA
 * Application Orchestrator & UI Controller
 */

(function () {
  "use strict";

  // DOM Elements Cache
  const el = {
    // Header & Connection
    connectBtn: document.getElementById("connectBtn"),
    connectBtnText: document.getElementById("connectBtnText"),
    statusDot: document.getElementById("statusDot"),
    settingsBtn: document.getElementById("settingsBtn"),
    githubBtn: document.getElementById("githubBtn"),
    installBtn: document.getElementById("installBtn"),

    // Views
    onboardingView: document.getElementById("onboardingView"),
    mainContent: document.getElementById("mainContent"),
    onboardingConnectBtn: document.getElementById("onboardingConnectBtn"),
    skipOnboardingBtn: document.getElementById("skipOnboardingBtn"),
    compatBanner: document.getElementById("compatBanner"),

    // Web Components
    statusDashboard: document.getElementById("statusDashboard"),
    bookLibrary: document.getElementById("bookLibrary"),
    bookUploader: document.getElementById("bookUploader"),
    deviceSettingsModal:
      document.getElementById("deviceSettingsModal") ||
      document.getElementById("settingsModal"),
    mushafBuilder: document.getElementById("mushafBuilder"),

    // Modals
    deleteModal: document.getElementById("deleteModal"),
    deleteModalText: document.getElementById("deleteModalText"),
    cancelDeleteBtn: document.getElementById("cancelDeleteBtn"),
    confirmDeleteBtn: document.getElementById("confirmDeleteBtn"),

    controlsModal: document.getElementById("controlsModal"),
    controlsBtn: document.getElementById("controlsBtn"),
    closeControlsBtn: document.getElementById("closeControlsBtn"),
    dismissControlsBtn: document.getElementById("dismissControlsBtn"),

    // Toast Container
    toastContainer: document.getElementById("toastContainer"),

    // Upload Dropzone Fallback (only used when <book-uploader> is absent)
    dropzone: document.getElementById("dropzone"),
    fileInput: document.getElementById("fileInput"),
    uploadActiveCard: document.getElementById("uploadActiveCard"),
    uploadFileName: document.getElementById("uploadFileName"),
    uploadProgressBar: document.getElementById("uploadProgressBar"),
    uploadPercentText: document.getElementById("uploadPercentText"),
    uploadBytesText: document.getElementById("uploadBytesText"),
    uploadSpeedText: document.getElementById("uploadSpeedText"),
    cancelUploadBtn: document.getElementById("cancelUploadBtn"),
  };

  // State
  let uploadQueue = [];
  let isUploading = false;
  let cancelUploadRequested = false;
  let bookPendingDelete = null;
  let remoteBooks = [];
  let deferredInstallPrompt = null;

  // ============================================================================
  // Utilities & Feedback
  // ============================================================================
  function showToast(message, type = "info", duration = 3500) {
    if (!el.toastContainer) return;
    const toast = document.createElement("div");
    toast.className = `toast toast-${type}`;

    let icon = "ℹ️";
    if (type === "success") icon = "✓";
    else if (type === "error") icon = "✕";
    else if (type === "warning") icon = "⚠";

    toast.innerHTML = `<span class="toast-icon">${icon}</span><span class="toast-msg">${escapeHtml(message)}</span>`;
    el.toastContainer.appendChild(toast);

    setTimeout(() => {
      toast.classList.add("fade-out");
      setTimeout(() => toast.remove(), 300);
    }, duration);
  }
  window.showToast = showToast;

  function log(message, level = "info") {
    const timestamp = new Date().toLocaleTimeString();
    const formatted = `[${timestamp}] [${level.toUpperCase()}] ${message}`;
    if (level === "error") console.error(formatted);
    else if (level === "warning") console.warn(formatted);
    else console.log(formatted);

    const modal = el.deviceSettingsModal;
    if (modal && typeof modal.appendLog === "function") {
      modal.appendLog(message, level);
    }
  }
  window.log = log;

  function escapeHtml(str) {
    if (!str) return "";
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }

  function formatBytes(bytes) {
    if (!bytes || bytes === 0) return "0 B";
    const k = 1024;
    const sizes = ["B", "KB", "MB", "GB"];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(1))} ${sizes[i]}`;
  }

  // ============================================================================
  // Web Bluetooth Compatibility
  // ============================================================================
  function checkBluetoothCompatibility() {
    const supported = Boolean(navigator && navigator.bluetooth);
    if (!supported && el.compatBanner) {
      el.compatBanner.style.display = "flex";
    }
    return supported;
  }

  // ============================================================================
  // Theme Management
  // ============================================================================
  function initTheme() {
    try {
      const saved = localStorage.getItem("bacain_theme") || "dark";
      document.documentElement.setAttribute("data-theme", saved);
    } catch (_) {}
  }

  function setTheme(theme) {
    try {
      document.documentElement.setAttribute("data-theme", theme);
      localStorage.setItem("bacain_theme", theme);
    } catch (_) {}
  }

  // ============================================================================
  // Connection UI Updates
  // ============================================================================
  function updateConnectionUI(isConnected, isConnecting, deviceName) {
    if (isConnecting) {
      if (el.connectBtnText) el.connectBtnText.textContent = "Connecting...";
      if (el.statusDot) el.statusDot.className = "status-dot connecting";
      if (el.connectBtn) el.connectBtn.disabled = true;
      return;
    }

    if (el.connectBtn) el.connectBtn.disabled = false;

    if (isConnected) {
      if (el.connectBtnText) el.connectBtnText.textContent = "Disconnect";
      if (el.statusDot) el.statusDot.className = "status-dot connected";

      if (el.onboardingView) el.onboardingView.style.display = "none";
      if (el.mainContent) el.mainContent.style.display = "block";

      if (
        el.statusDashboard &&
        typeof el.statusDashboard.updateDeviceStatus === "function"
      ) {
        el.statusDashboard.updateDeviceStatus({
          state: true,
          name:
            deviceName ||
            (window.BleClient && window.BleClient.getDeviceName()) ||
            "BacaIn",
        });
      }
      if (
        el.bookLibrary &&
        typeof el.bookLibrary.setDeviceConnected === "function"
      ) {
        el.bookLibrary.setDeviceConnected(true);
      }
    } else {
      if (el.connectBtnText) el.connectBtnText.textContent = "Connect Device";
      if (el.statusDot) el.statusDot.className = "status-dot disconnected";

      try {
        const skipped = sessionStorage.getItem("bacain_onboarding_skipped");
        if (!skipped && el.onboardingView) {
          el.onboardingView.style.display = "flex";
          if (el.mainContent) el.mainContent.style.display = "none";
        }
      } catch (_) {}

      if (
        el.statusDashboard &&
        typeof el.statusDashboard.updateDeviceStatus === "function"
      ) {
        el.statusDashboard.updateDeviceStatus({ state: false });
      }
      if (
        el.bookLibrary &&
        typeof el.bookLibrary.setDeviceConnected === "function"
      ) {
        el.bookLibrary.setDeviceConnected(false);
      }
      unmountMushafBuilder();
    }
  }

  async function handleConnectToggle() {
    if (!checkBluetoothCompatibility()) {
      showToast("Web Bluetooth is not supported in this browser", "error");
      return;
    }

    if (window.BleClient.isConnected()) {
      window.BleClient.disconnect();
    } else {
      try {
        await window.BleClient.connect();
        showToast(
          `Connected to ${window.BleClient.getDeviceName() || "BacaIn"}`,
          "success",
        );
        await refreshBooksList();
      } catch (err) {
        log(`Connection failed: ${err.message}`, "error");
        showToast(`Connection failed: ${err.message}`, "error");
      }
    }
  }

  // ============================================================================
  // Books Management
  // ============================================================================
  async function refreshBooksList() {
    if (!window.BleClient.isConnected()) return;

    if (el.bookLibrary && typeof el.bookLibrary.setLoading === "function") {
      el.bookLibrary.setLoading(true);
    }

    remoteBooks = [];
    try {
      await window.BleClient.queryBooksList(
        (book) => {
          remoteBooks.push(book);
          if (el.bookLibrary && typeof el.bookLibrary.setBooks === "function") {
            el.bookLibrary.setBooks(remoteBooks, "/spiflash/books/");
          }
        },
        () => {
          if (
            el.bookLibrary &&
            typeof el.bookLibrary.setLoading === "function"
          ) {
            el.bookLibrary.setLoading(false);
          }
          if (
            el.bookLibrary &&
            typeof el.bookLibrary.setDeviceConnected === "function"
          ) {
            el.bookLibrary.setDeviceConnected(true);
          }
          if (
            el.bookLibrary &&
            typeof el.bookLibrary.renderBooks === "function"
          ) {
            el.bookLibrary.renderBooks(remoteBooks, "/spiflash/books/");
          }
          log(`Book list updated: ${remoteBooks.length} items.`, "info");
        },
        (err) => {
          if (
            el.bookLibrary &&
            typeof el.bookLibrary.setLoading === "function"
          ) {
            el.bookLibrary.setLoading(false);
          }
          log(`Book list error: ${err.message}`, "error");
        },
      );
    } catch (err) {
      if (el.bookLibrary && typeof el.bookLibrary.setLoading === "function") {
        el.bookLibrary.setLoading(false);
      }
      log(`Failed to query books: ${err.message}`, "error");
    }
  }

  function openDeleteModal(book) {
    if (!book) return;
    bookPendingDelete = book;
    if (el.deleteModalText) {
      el.deleteModalText.textContent = `Are you sure you want to delete "${book.name}" from your device?`;
    }
    if (el.deleteModal) el.deleteModal.classList.add("active");
  }

  function closeDeleteModal() {
    bookPendingDelete = null;
    if (el.deleteModal) el.deleteModal.classList.remove("active");
  }

  async function confirmDeleteBook() {
    if (!bookPendingDelete) return;
    const book = bookPendingDelete;
    closeDeleteModal();

    try {
      showToast(`Deleting "${book.name}"...`, "info");
      await window.BleClient.deleteBook(book.name);
      showToast(`Deleted "${book.name}"`, "success");
      log(`Deleted "${book.name}" successfully.`, "success");
      await refreshBooksList();
    } catch (err) {
      log(`Failed to delete "${book.name}": ${err.message}`, "error");
      showToast(`Delete failed: ${err.message}`, "error");
    }
  }

  // ============================================================================
  // Upload Processing
  // ============================================================================

  // Adapter over <book-uploader>'s real API: showProgress / setProgress / reset / showError.
  // Falls back to plain DOM nodes only when the component is absent.
  const uploaderUI = {
    hasComponent() {
      return (
        el.bookUploader && typeof el.bookUploader.setProgress === "function"
      );
    },
    start(fileName) {
      if (this.hasComponent()) {
        el.bookUploader.showProgress(fileName);
        el.bookUploader.setProgress({
          fileName,
          pct: 0,
          statusText: "Starting transfer...",
        });
        return;
      }
      if (el.uploadActiveCard) el.uploadActiveCard.style.display = "block";
      if (el.uploadFileName) el.uploadFileName.textContent = fileName;
    },
    progress(data) {
      if (this.hasComponent()) {
        el.bookUploader.setProgress(data);
        return;
      }
      const pct = Math.min(100, Math.max(0, data.pct || 0)).toFixed(1);
      if (el.uploadProgressBar) el.uploadProgressBar.style.width = `${pct}%`;
      if (el.uploadPercentText) el.uploadPercentText.textContent = `${pct}%`;
      if (el.uploadBytesText && data.metricsText)
        el.uploadBytesText.textContent = data.metricsText;
      if (el.uploadSpeedText)
        el.uploadSpeedText.textContent = `${(data.speedKbps || 0).toFixed(1)} KB/s`;
    },
    finish() {
      if (el.bookUploader && typeof el.bookUploader.reset === "function") {
        el.bookUploader.reset();
      } else if (el.uploadActiveCard) {
        el.uploadActiveCard.style.display = "none";
      }
    },
    error(message) {
      // BookUploader.showError() already raises a toast; avoid a second one.
      if (el.bookUploader && typeof el.bookUploader.showError === "function") {
        el.bookUploader.showError(message);
      } else {
        showToast(message, "error");
      }
    },
  };

  // BleClient.executeFileUpload calls onProgress with a single object:
  //   { bytesSent, totalBytes, seqId, totalChunks, startTime }
  // Positional (pct, sent, total, speed) is still accepted as a fallback.
  function normalizeProgress(args) {
    const a = args[0];
    if (a && typeof a === "object") {
      return {
        pct: Number(a.pct ?? a.percent ?? 0),
        sent: Number(a.bytesSent ?? a.sent ?? 0),
        total: Number(a.totalBytes ?? a.total ?? 0),
        speed: Number(a.speed ?? a.bytesPerSec ?? 0),
        totalSeq: a.totalChunks ?? a.totalSeq,
        startTime: typeof a.startTime === "number" ? a.startTime : undefined,
      };
    }
    return {
      pct: Number(args[0]) || 0,
      sent: Number(args[1]) || 0,
      total: Number(args[2]) || 0,
      speed: Number(args[3]) || 0,
      totalSeq: undefined,
      startTime: undefined,
    };
  }

  function isConnected() {
    return Boolean(
      window.BleClient &&
      typeof window.BleClient.isConnected === "function" &&
      window.BleClient.isConnected(),
    );
  }

  // Entry point for files that are ALREADY converted (from <book-uploader>).
  function enqueueFiles(files) {
    if (!files || files.length === 0) return;
    if (!isConnected()) {
      showToast("Connect device before uploading books", "warning");
      return;
    }
    for (const f of files) uploadQueue.push(f);
    if (isUploading) {
      showToast(`Queued ${files.length} file(s)`, "info");
    }
    processUploadQueue();
  }

  // Entry point for RAW files (fallback dropzone, mushaf .bqm, etc.).
  async function handleFilesSelected(files) {
    if (!files || files.length === 0) return;
    if (!isConnected()) {
      showToast("Connect device before uploading books", "warning");
      return;
    }

    const processed = [];
    for (const file of Array.from(files)) {
      let out = file;
      try {
        const lower = file.name.toLowerCase();
        if (lower.endsWith(".epub") && window.EpubProcessor) {
          showToast(`Converting EPUB: "${file.name}"...`, "info");
          out = await window.EpubProcessor.process(file);
        } else if (lower.endsWith(".fb2") && window.Fb2Processor) {
          showToast(`Converting FB2: "${file.name}"...`, "info");
          out = await window.Fb2Processor.process(file);
        } else if (lower.endsWith(".txt") && window.TextCleaner) {
          out = await window.TextCleaner.clean(file);
        }
      } catch (err) {
        log(
          `Pre-processing failed for "${file.name}": ${err.message}`,
          "warning",
        );
      }
      processed.push(out);
    }

    enqueueFiles(processed);
  }

  async function processUploadQueue() {
    if (isUploading || uploadQueue.length === 0) return;
    if (!isConnected()) {
      uploadQueue.length = 0;
      showToast("Device disconnected — upload queue cleared", "warning");
      return;
    }

    isUploading = true;
    cancelUploadRequested = false;

    const file = uploadQueue.shift();
    const startedAt = performance.now();
    let lastStatus = "";
    let failureMessage = null;
    let chunksSent = 0;

    uploaderUI.start(file.name);

    try {
      showToast(`Uploading "${file.name}"...`, "info");

      await window.BleClient.executeFileUpload(file, {
        onProgress: (...args) => {
          chunksSent += 1; // one callback per chunk; avoids seqId 0xFFFF wrap
          const p = normalizeProgress(args);
          const total = p.total > 0 ? p.total : file.size;

          // Prefer byte counts over a reported pct (avoids 0–1 vs 0–100 ambiguity).
          const pct = total > 0 && p.sent > 0 ? (p.sent / total) * 100 : p.pct;

          // BleClient reports no speed; derive it from its startTime (same clock).
          let speed = p.speed;
          if (!(speed > 0)) {
            const t0 = p.startTime ?? startedAt;
            const elapsed = (performance.now() - t0) / 1000;
            speed = elapsed > 0 ? p.sent / elapsed : 0;
          }
          const etaSec = speed > 0 && total > 0 ? (total - p.sent) / speed : -1;

          uploaderUI.progress({
            fileName: file.name,
            pct,
            speedKbps: speed / 1024,
            etaSec,
            currentSeq: chunksSent,
            totalSeq: p.totalSeq,
            statusText: lastStatus || undefined,
            metricsText:
              total > 0
                ? `${formatBytes(p.sent)} / ${formatBytes(total)}`
                : undefined,
          });
        },
        onStatus: (msg) => {
          lastStatus = msg;
          log(msg, "info");
        },
        checkCancelled: () => cancelUploadRequested,
      });

      if (cancelUploadRequested) {
        showToast(`Upload cancelled: "${file.name}"`, "warning");
        log(`Upload cancelled for "${file.name}".`, "warning");
      } else {
        showToast(`Upload complete: "${file.name}"`, "success");
        log(`Uploaded "${file.name}" successfully.`, "success");
      }
      await refreshBooksList();
    } catch (err) {
      if (cancelUploadRequested) {
        showToast(`Upload cancelled: "${file.name}"`, "warning");
        log(`Upload cancelled for "${file.name}": ${err.message}`, "warning");
        await refreshBooksList(); // a partial file may remain on the device
      } else {
        failureMessage = `Upload failed for "${file.name}": ${err.message}`;
        log(failureMessage, "error");
      }
    } finally {
      isUploading = false;
      uploaderUI.finish(); // reset() clears errors, so show errors AFTER this

      if (failureMessage) uploaderUI.error(failureMessage);

      if (cancelUploadRequested) {
        if (uploadQueue.length > 0) {
          log(
            `Cleared ${uploadQueue.length} queued file(s) after cancel.`,
            "warning",
          );
        }
        uploadQueue.length = 0;
        cancelUploadRequested = false;
      } else if (uploadQueue.length > 0) {
        processUploadQueue();
      }
    }
  }

  function requestCancelUpload() {
    if (isUploading && !cancelUploadRequested) {
      cancelUploadRequested = true;
      showToast("Cancelling upload...", "warning");
    }
  }

  // ============================================================================
  // Mushaf Builder Mounting
  // ============================================================================
  async function mountMushafBuilder(width = 200, height = 200) {
    try {
      let qb = el.mushafBuilder || document.querySelector("mushaf-builder");
      if (!qb) {
        await import("./components/mushaf-builder.js");
        qb = el.mushafBuilder || document.querySelector("mushaf-builder");
        if (!qb) {
          qb = document.createElement("mushaf-builder");
          qb.id = "mushafBuilder";
          const target =
            el.mainContent || document.querySelector(".app-container");
          if (target) target.appendChild(qb);
        }
      }

      if (qb) {
        qb.style.display = "block";
        const isConn =
          window.BleClient && typeof window.BleClient.isConnected === "function"
            ? window.BleClient.isConnected()
            : false;
        if (typeof qb.setDisplayGeometry === "function") {
          qb.setDisplayGeometry(width, height);
        }
        if (typeof qb.setDeviceCapabilities === "function") {
          qb.setDeviceCapabilities({
            supported: true,
            isConnected: isConn,
            screenW: width,
            screenH: height,
          });
        }
      }
    } catch (err) {
      log(`Failed to mount Mushaf builder: ${err.message}`, "warning");
    }
  }

  function unmountMushafBuilder() {
    const qb = el.mushafBuilder || document.querySelector("mushaf-builder");
    if (qb) qb.style.display = "none";
  }

  // ============================================================================
  // Modals & Controls Guide
  // ============================================================================
  function openControlsModal() {
    if (el.controlsModal) el.controlsModal.classList.add("active");
  }

  function closeControlsModal() {
    if (el.controlsModal) el.controlsModal.classList.remove("active");
  }

  function openSettingsModal() {
    if (
      el.deviceSettingsModal &&
      typeof el.deviceSettingsModal.open === "function"
    ) {
      el.deviceSettingsModal.open();
    } else if (el.deviceSettingsModal) {
      el.deviceSettingsModal.classList.add("active");
    }
  }

  function closeSettingsModal() {
    if (
      el.deviceSettingsModal &&
      typeof el.deviceSettingsModal.close === "function"
    ) {
      el.deviceSettingsModal.close();
    } else if (el.deviceSettingsModal) {
      el.deviceSettingsModal.classList.remove("active");
    }
  }

  // ============================================================================
  // Service Worker
  // ============================================================================
  function registerServiceWorker() {
    if (
      "serviceWorker" in navigator &&
      window.location.protocol.startsWith("http")
    ) {
      navigator.serviceWorker.register("./sw.js").catch((err) => {
        console.warn("[SW] Registration failed:", err);
      });
    }
  }

  // ============================================================================
  // PWA Installation
  // ============================================================================
  function setupPwaInstall() {
    if (window.matchMedia("(display-mode: standalone)").matches) {
      return;
    }

    window.addEventListener("beforeinstallprompt", (e) => {
      // Allow the browser's native mini-infobar while keeping prompt reference
      deferredInstallPrompt = e;
      if (el.installBtn) el.installBtn.style.display = "inline-flex";
    });

    if (el.installBtn) {
      el.installBtn.addEventListener("click", async () => {
        if (!deferredInstallPrompt) return;
        deferredInstallPrompt.prompt();
        await deferredInstallPrompt.userChoice;
        deferredInstallPrompt = null;
        el.installBtn.style.display = "none";
      });
    }

    window.addEventListener("appinstalled", () => {
      deferredInstallPrompt = null;
      if (el.installBtn) el.installBtn.style.display = "none";
    });
  }

  // ============================================================================
  // Event Bindings
  // ============================================================================
  function setupEventListeners() {
    // Header & Onboarding
    if (el.connectBtn)
      el.connectBtn.addEventListener("click", handleConnectToggle);
    if (el.onboardingConnectBtn)
      el.onboardingConnectBtn.addEventListener("click", handleConnectToggle);
    if (el.skipOnboardingBtn) {
      el.skipOnboardingBtn.addEventListener("click", () => {
        try {
          sessionStorage.setItem("bacain_onboarding_skipped", "1");
        } catch (_) {}
        if (el.onboardingView) el.onboardingView.style.display = "none";
        if (el.mainContent) el.mainContent.style.display = "block";
      });
    }

    if (el.settingsBtn)
      el.settingsBtn.addEventListener("click", openSettingsModal);
    if (el.controlsBtn)
      el.controlsBtn.addEventListener("click", openControlsModal);
    if (el.closeControlsBtn)
      el.closeControlsBtn.addEventListener("click", closeControlsModal);
    if (el.dismissControlsBtn)
      el.dismissControlsBtn.addEventListener("click", closeControlsModal);

    // Delete Modal
    if (el.cancelDeleteBtn)
      el.cancelDeleteBtn.addEventListener("click", closeDeleteModal);
    if (el.confirmDeleteBtn)
      el.confirmDeleteBtn.addEventListener("click", confirmDeleteBook);

    // Cancel Upload (fallback only — <book-uploader> dispatches "cancel-upload" itself)
    if (el.cancelUploadBtn && !el.bookUploader) {
      el.cancelUploadBtn.addEventListener("click", requestCancelUpload);
    }

    // Component Events: <book-library>
    if (el.bookLibrary) {
      el.bookLibrary.addEventListener("refresh-books", refreshBooksList);
      el.bookLibrary.addEventListener("delete-book", (e) =>
        openDeleteModal(e.detail?.book),
      );
      el.bookLibrary.addEventListener("manage-bookmarks", (e) => {
        if (
          window.BookmarksModal &&
          typeof window.BookmarksModal.open === "function"
        ) {
          window.BookmarksModal.open(e.detail?.book);
        }
      });
    }

    // Component Events: <book-uploader>
    if (el.bookUploader) {
      // Files arrive already converted + cleaned by the component.
      el.bookUploader.addEventListener("file-select", (e) =>
        enqueueFiles(e.detail?.files),
      );
      el.bookUploader.addEventListener("cancel-upload", requestCancelUpload);
    }

    // Dropzone fallback — only when <book-uploader> is NOT present,
    // otherwise these IDs belong to the component and get double-bound.
    if (!el.bookUploader && el.dropzone && el.fileInput) {
      el.dropzone.addEventListener("click", (e) => {
        if (e.target === el.fileInput) return; // input sits inside dropzone
        el.fileInput.click();
      });
      el.fileInput.addEventListener("change", (e) => {
        const files = Array.from(e.target.files || []);
        e.target.value = "";
        handleFilesSelected(files);
      });
      el.dropzone.addEventListener("dragover", (e) => {
        e.preventDefault();
        el.dropzone.classList.add("dragover");
      });
      el.dropzone.addEventListener("dragleave", () =>
        el.dropzone.classList.remove("dragover"),
      );
      el.dropzone.addEventListener("drop", (e) => {
        e.preventDefault();
        el.dropzone.classList.remove("dragover");
        if (e.dataTransfer?.files) handleFilesSelected(e.dataTransfer.files);
      });
    }

    // Component Events: <device-settings-modal>
    if (el.deviceSettingsModal) {
      el.deviceSettingsModal.addEventListener("font-change", async (e) => {
        try {
          await window.BleClient.sendSetFont(e.detail?.fontId);
          showToast("Font updated", "success");
        } catch (err) {
          showToast(`Font update failed: ${err.message}`, "error");
        }
      });

      el.deviceSettingsModal.addEventListener("force-refresh", async () => {
        try {
          await window.BleClient.sendTriggerRefresh();
          showToast("Triggered full display refresh", "success");
        } catch (err) {
          showToast(`Refresh failed: ${err.message}`, "error");
        }
      });

      el.deviceSettingsModal.addEventListener("nav-hints-change", async (e) => {
        try {
          await window.BleClient.sendSetNavHints(e.detail?.enabled);
          showToast("Navigation hints updated", "success");
        } catch (err) {
          showToast(`Nav hints update failed: ${err.message}`, "error");
        }
      });

      el.deviceSettingsModal.addEventListener(
        "orientation-change",
        async (e) => {
          try {
            await window.BleClient.sendSetRotation(e.detail?.orientation);
            showToast("Orientation updated", "success");
          } catch (err) {
            showToast(`Orientation update failed: ${err.message}`, "error");
          }
        },
      );

      el.deviceSettingsModal.addEventListener(
        "button-invert-change",
        async (e) => {
          try {
            await window.BleClient.sendSetButtonInvert(e.detail?.inverted);
            showToast("Button mapping updated", "success");
          } catch (err) {
            showToast(`Button invert update failed: ${err.message}`, "error");
          }
        },
      );

      el.deviceSettingsModal.addEventListener("setting-changed", async (e) => {
        if (e.detail?.key === "status-led" || e.detail?.key === "status_led") {
          try {
            await window.BleClient.sendSetStatusLed(e.detail.value);
            showToast("Status LED updated", "success");
          } catch (err) {
            showToast(`Status LED failed: ${err.message}`, "error");
          }
        } else if (
          e.detail?.key === "refresh_interval" ||
          e.detail?.key === "refresh-interval"
        ) {
          try {
            await window.BleClient.sendSetRefreshInterval(e.detail.value);
            showToast("Full refresh interval updated", "success");
          } catch (err) {
            showToast(`Full refresh interval failed: ${err.message}`, "error");
          }
        }
      });

      el.deviceSettingsModal.addEventListener(
        "sleep-timeout-change",
        async (e) => {
          try {
            await window.BleClient.sendSetSleepTimeout(e.detail?.timeout);
            showToast("Sleep timeout updated", "success");
          } catch (err) {
            showToast(`Sleep timeout failed: ${err.message}`, "error");
          }
        },
      );

      el.deviceSettingsModal.addEventListener(
        "storage-target-change",
        async (e) => {
          try {
            await window.BleClient.sendSetStorageTarget(e.detail?.target);
            showToast("Storage target updated", "success");
            await refreshBooksList();
          } catch (err) {
            showToast(`Storage target failed: ${err.message}`, "error");
          }
        },
      );

      el.deviceSettingsModal.addEventListener("theme-change", (e) => {
        setTheme(e.detail?.theme);
      });
    }

    // Mushaf Component Events
    window.addEventListener("mushaf-request-state", async () => {
      try {
        await window.BleClient.requestMushafState();
      } catch (err) {
        log(`Mushaf state request failed: ${err.message}`, "error");
      }
    });

    window.addEventListener("mushaf-set-cursor", async (e) => {
      try {
        await window.BleClient.setMushafCursor(e.detail?.surah, e.detail?.ayah);
        showToast("Mushaf reading position updated", "success");
      } catch (err) {
        showToast(`Failed to update position: ${err.message}`, "error");
      }
    });

    window.addEventListener("mushaf-set-bookmarks", async (e) => {
      try {
        await window.BleClient.setMushafBookmarks(e.detail?.bookmarks);
        showToast("Mushaf bookmarks updated", "success");
      } catch (err) {
        showToast(`Failed to update bookmarks: ${err.message}`, "error");
      }
    });

    window.addEventListener("mushaf-upload-bqm", (e) => {
      if (e.detail?.file) {
        handleFilesSelected([e.detail.file]);
      }
    });

    // Window BLE Events from BleClient
    window.addEventListener("ble-connection-changed", (e) => {
      const { isConnected, isConnecting, name } = e.detail;
      updateConnectionUI(isConnected, isConnecting, name);
      const qb = el.mushafBuilder || document.querySelector("mushaf-builder");
      if (qb && typeof qb.setDeviceCapabilities === "function") {
        qb.setDeviceCapabilities({ isConnected });
      }
    });

    window.addEventListener("ble-status-changed", (e) => {
      const status = e.detail;
      if (el.statusDashboard) {
        if (typeof el.statusDashboard.updateBattery === "function") {
          el.statusDashboard.updateBattery({
            pct: status.batteryPct,
            isCharging: status.isCharging,
            mv: status.batteryMv,
          });
        }
        if (typeof el.statusDashboard.updateStorage === "function") {
          el.statusDashboard.updateStorage({
            freeKb: status.freeKb,
            totalKb: status.totalKb,
            usedKb: status.usedKb,
            usedPct: status.usedPct,
          });
        }
      }
    });

    // Field names match BleClient.parseConfigPayload():
    //   font, navHints(bool), rotation, buttonInvert(bool), statusLed(bool),
    //   storageTarget, sleepTimeout, caps, screenW, screenH,
    //   mushafSupported, refreshIntervalSupported, refreshInterval
    window.addEventListener("ble-config-changed", (e) => {
      const cfg = e.detail;
      if (cfg.storageTarget !== undefined) {
        if (
          el.statusDashboard &&
          typeof el.statusDashboard.setStorageTarget === "function"
        ) {
          el.statusDashboard.setStorageTarget(cfg.storageTarget);
        } else if (el.statusDashboard) {
          el.statusDashboard.currentStorageTarget = cfg.storageTarget;
        }
        if (
          el.bookLibrary &&
          typeof el.bookLibrary.setStorageTargetBadge === "function"
        ) {
          el.bookLibrary.setStorageTargetBadge(cfg.storageTarget);
        }
      }

      if (
        el.deviceSettingsModal &&
        typeof el.deviceSettingsModal.updateConfig === "function"
      ) {
        el.deviceSettingsModal.updateConfig({
          font: cfg.font,
          navHints: Boolean(cfg.navHints),
          orientation: cfg.rotation,
          buttonInvert: Boolean(cfg.buttonInvert),
          statusLed: Boolean(cfg.statusLed),
          storageTarget: cfg.storageTarget,
          sleepTimeout: cfg.sleepTimeout,
          refreshInterval: cfg.refreshInterval,
          refreshIntervalSupported: cfg.refreshIntervalSupported,
        });
      }

      if (cfg.mushafSupported) {
        mountMushafBuilder(cfg.screenW, cfg.screenH);
      } else {
        unmountMushafBuilder();
      }
    });

    window.addEventListener("ble-mushaf-state-changed", (e) => {
      const qb = el.mushafBuilder || document.querySelector("mushaf-builder");
      if (qb && typeof qb.updateMushafState === "function") {
        qb.updateMushafState(e.detail);
      }
    });
  }

  // ============================================================================
  // Initialization
  // ============================================================================
  function init() {
    initTheme();
    checkBluetoothCompatibility();
    setupEventListeners();
    if (
      el.statusDashboard &&
      typeof el.statusDashboard.setStorageTarget === "function"
    ) {
      el.statusDashboard.setStorageTarget(1);
    }
    if (
      el.bookLibrary &&
      typeof el.bookLibrary.setStorageTargetBadge === "function"
    ) {
      el.bookLibrary.setStorageTargetBadge(1);
    }
    registerServiceWorker();
    setupPwaInstall();
    log("BacaIn Web Client initialized.", "info");
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
