/**
 * mushaf-builder.js
 * BacaIn PWA - Mushaf Reader & BQM Manager Custom Element (<mushaf-builder>)
 */

import {
  buildBqmPackage,
  renderAyahPreviewToCanvas,
  ensureMushafFont,
  fetchMushafDatasets,
  fetchWithCache,
  MUSHAF_FONTS,
  MUSHAF_DATA_ENDPOINTS,
  SURAHS,
  CANONICAL_SURAHS,
  JUZ_BOUNDARIES,
  JUZ_STARTS,
  getJuzForSurahAyah,
  getJuzForAyah,
  LAYER_TAGS,
  LAYER_NAMES,
  SUNNAH_PRESETS,
  MUSHAF_LAYERS,
} from "../processors/bqm-builder.js";

export const MUSHAF_CACHE_NAME = "bacain-mushaf-assets-v1";

function escapeHtml(str) {
  if (!str) return "";
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

const BaseElement = typeof HTMLElement !== "undefined" ? HTMLElement : class {};

class MushafBuilder extends BaseElement {
  constructor() {
    super();
    this._screenW = 200;
    this._screenH = 200;
    this._supported = false;
    this._isConnected = false;
    this._isBuilding = false;
    this._isUploadingBqm = false;
    this._assetsReady = false;
    this._assetsLoading = false;
    this._loadedEditionsKey = "";
    this._cachedMushafBase = null;
    this._cachedEditionsData = null;
    this._deviceState = {
      started: false,
      surah: 1,
      ayah: 1,
      layer: 0,
      bookmarks: [],
    };
    this._onUploadStatus = this._onUploadStatus.bind(this);
  }

  setStatusText(text, isError = false) {
    const statusEl = this.querySelector("#mushafStatusText");
    if (statusEl) {
      statusEl.textContent = text;
      statusEl.style.color = isError
        ? "var(--danger-color, #ef4444)"
        : "var(--text-secondary)";
      statusEl.style.fontWeight = isError ? "500" : "normal";
    }
    if (isError) {
      console.error("[MushafBuilder]", text);
    }
    this.dispatchEvent(
      new CustomEvent("mushaf-status", {
        detail: { text, isError },
        bubbles: true,
        composed: true,
      }),
    );
  }

  connectedCallback() {
    document.addEventListener("mushaf-upload-status", this._onUploadStatus);
    document.addEventListener("mushaf-status", this._onUploadStatus);
    this._onBleConnectionChange = (e) => {
      const isConn = Boolean(e && e.detail && e.detail.isConnected);
      this._isConnected = isConn;
      this.updateConnectionStateUI();
    };
    window.addEventListener("ble-connection-changed", this._onBleConnectionChange);
    if (typeof window !== "undefined" && window.BleClient && typeof window.BleClient.isConnected === "function") {
      this._isConnected = window.BleClient.isConnected();
    }
    if (!this.querySelector(".mushaf-section")) {
      this.render();
      this.bindEvents();
      this.updateGeometryBadge();
      this.updateSizeEstimate();
      this.setBuilderControlsDisabled(true);
      this.preloadAssets();
    } else {
      this.updateConnectionStateUI();
    }
  }

  disconnectedCallback() {
    document.removeEventListener("mushaf-upload-status", this._onUploadStatus);
    document.removeEventListener("mushaf-status", this._onUploadStatus);
    if (this._onBleConnectionChange) {
      window.removeEventListener("ble-connection-changed", this._onBleConnectionChange);
    }
  }

  _onUploadStatus(e) {
    if (!e || !e.detail) return;
    this._isUploadingBqm = false;
    this.updateConnectionStateUI();
    const progressWrap = this.querySelector("#mushafBuildProgressWrap");
    if (progressWrap) progressWrap.style.display = "none";
    if (e.detail.ok === false) {
      this.setStatusText(`Upload failed: ${e.detail.error}`, true);
    } else if (
      e.detail.ok === true &&
      e.detail.fileName === "mushaf.bqm"
    ) {
      this.setStatusText(
        `${e.detail.fileName} uploaded and verified on device.`,
      );
    }
  }

  setBuilderControlsDisabled(disabled) {
    const selector = [
      "#mushafScriptSelect",
      "#mushafFontSizeSelect",
      "#mushafRangeSelect",
      "#customStartSurah",
      "#customEndSurah",
      "#layerLatinCheckbox",
      "#layerTranslationCheckbox",
      "#layerTafsirCheckbox",
      "#mushafTranslationSelect",
      "#mushafTafsirSelect",
      "#cursorSurahSelect",
      "#cursorAyahInput",
      "#cursorLayerSelect",
    ].join(", ");

    this.querySelectorAll(selector).forEach((el) => {
      el.disabled = Boolean(disabled);
    });

    const uploadBtn = this.querySelector("#mushafBuildUploadBtn");
    const downloadBtn = this.querySelector("#mushafDownloadBtn");

    if (disabled) {
      if (uploadBtn) uploadBtn.disabled = true;
      if (downloadBtn) downloadBtn.disabled = true;
    } else {
      if (uploadBtn)
        uploadBtn.disabled =
          !this._isConnected ||
          !this._assetsReady ||
          Boolean(this._isUploadingBqm);
      if (downloadBtn) downloadBtn.disabled = !this._assetsReady;
    }
  }

  async preloadAssets() {
    if (this._assetsLoading) return;
    this._assetsLoading = true;
    this._assetsReady = false;

    this.setBuilderControlsDisabled(true);

    const retryBtn = this.querySelector("#mushafRetryAssetsBtn");
    if (retryBtn) retryBtn.style.display = "none";

    const progressWrap = this.querySelector("#mushafBuildProgressWrap");
    if (progressWrap) progressWrap.style.display = "block";

    this.setStatusText("Loading mushaf fonts and corpora.");

    try {
      await ensureMushafFont(0, (msg) => this.setStatusText(msg));
      await ensureMushafFont(1, (msg) => this.setStatusText(msg));

      const translationEdition =
        this.querySelector("#mushafTranslationSelect")?.value || "id_kemenag";
      const tafsirEdition =
        this.querySelector("#mushafTafsirSelect")?.value ||
        "id_kemenag_ringkas";
      const datasets = await fetchMushafDatasets(
        {
          scriptStyle: 0,
          translationEdition,
          tafsirEdition,
          layers: {
            mushaf: true,
            latin: true,
            translation: true,
            tafsir: true,
          },
        },
        (msg) => this.setStatusText(msg),
      );

      this._cachedMushafBase = datasets.mushafBase;
      this._cachedEditionsData = datasets.editionsData;
      this._loadedEditionsKey = `${translationEdition}|${tafsirEdition}`;

      this._assetsReady = true;
      if (progressWrap) progressWrap.style.display = "none";
      this.setBuilderControlsDisabled(false);
      this.setStatusText("Assets ready (cached).");
      this.updateSizeEstimate();
      await this.renderAyahPreview();
    } catch (err) {
      this._assetsReady = false;
      console.error("[MushafBuilder] Failed to preload mushaf assets:", err);
      this.setBuilderControlsDisabled(true);
      this.setStatusText(`Assets unavailable: ${err.message}`, true);
      if (retryBtn) retryBtn.style.display = "inline-flex";
    } finally {
      this._assetsLoading = false;
    }
  }

  setDeviceCapabilities(caps) {
    if (!caps) return;
    this._supported = Boolean(caps.supported);
    if (typeof caps.screenW === "number" && caps.screenW > 0)
      this._screenW = caps.screenW;
    if (typeof caps.screenH === "number" && caps.screenH > 0)
      this._screenH = caps.screenH;
    if (caps.isConnected !== undefined)
      this._isConnected = Boolean(caps.isConnected);

    const canvas = this.querySelector("#mushafPreviewCanvas");
    if (canvas) {
      canvas.width = this._screenW;
      canvas.height = this._screenH;
    }

    this.updateGeometryBadge();
    this.updateConnectionStateUI();
    this.renderAyahPreview();
  }

  configureGeometry(width, height) {
    this.setDisplayGeometry(width, height);
  }

  setDisplayGeometry(width, height) {
    if (typeof width === "number" && width > 0) this._screenW = width;
    if (typeof height === "number" && height > 0) this._screenH = height;

    const canvas = this.querySelector("#mushafPreviewCanvas");
    if (canvas) {
      canvas.width = this._screenW;
      canvas.height = this._screenH;
    }

    this.updateGeometryBadge();
    this.renderAyahPreview();
  }

  updateDeviceState(state) {
    if (!state) return;
    this._deviceState = {
      started: Boolean(state.started),
      surah: Math.max(1, Math.min(114, Number(state.surah) || 1)),
      ayah: Math.max(1, Number(state.ayah) || 1),
      layer: Math.max(0, Math.min(3, Number(state.layer) || 0)),
      bookmarks: Array.isArray(state.bookmarks)
        ? state.bookmarks.slice(0, 8)
        : [],
    };

    this.renderDeviceStateUI();
  }

  updateMushafState(state) {
    this.updateDeviceState(state);
  }

  updateGeometryBadge() {
    const badge = this.querySelector("#mushafGeometryBadge");
    if (badge) {
      badge.textContent = `${this._screenW} × ${this._screenH} px`;
    }
  }

  updateConnectionStateUI() {
    const syncBtn = this.querySelector("#mushafSyncCursorBtn");
    const uploadBtn = this.querySelector("#mushafBuildUploadBtn");
    const addBmBtn = this.querySelector("#mushafAddBookmarkBtn");

    if (syncBtn) syncBtn.disabled = !this._isConnected;
    if (uploadBtn)
      uploadBtn.disabled =
        !this._isConnected ||
        !this._assetsReady ||
        this._isBuilding ||
        Boolean(this._isUploadingBqm);
    if (addBmBtn) addBmBtn.disabled = !this._isConnected;
  }

  renderDeviceStateUI() {
    const state = this._deviceState;
    const badge = this.querySelector("#mushafTilawahBadge");
    const surahInfo = SURAHS[state.surah - 1] || SURAHS[0];
    const layerName = MUSHAF_LAYERS[state.layer]?.name || "Mushaf";
    const juz = getJuzForAyah(state.surah, state.ayah);

    if (state.started) {
      if (badge) {
        badge.className = "card-badge badge-success";
        badge.textContent = `S.${state.surah}:${state.ayah} (${surahInfo.name}) (Juz ${juz}) [${layerName}]`;
      }
    } else if (badge) {
      badge.className = "card-badge badge-muted";
      badge.textContent = "Ready (1:1)";
    }

    const curSurahSelect = this.querySelector("#cursorSurahSelect");
    const curAyahInput = this.querySelector("#cursorAyahInput");
    const curLayerSelect = this.querySelector("#cursorLayerSelect");

    if (curSurahSelect && document.activeElement !== curSurahSelect) {
      curSurahSelect.value = String(state.surah);
    }
    if (curAyahInput && document.activeElement !== curAyahInput) {
      curAyahInput.max = String(surahInfo.ayahs);
      curAyahInput.value = String(state.ayah);
    }
    if (curLayerSelect && document.activeElement !== curLayerSelect) {
      curLayerSelect.value = String(state.layer);
    }

    this.renderAyahPreview();
    this.renderBookmarksList();
  }

  renderBookmarksList() {
    const container = this.querySelector("#mushafBookmarksList");
    const countBadge = this.querySelector("#mushafBookmarkCountBadge");
    if (!container) return;

    const bookmarks = this._deviceState.bookmarks || [];
    if (countBadge) {
      countBadge.textContent = `${bookmarks.length} / 8 Bookmarks`;
      countBadge.className =
        bookmarks.length > 0
          ? "card-badge badge-flash"
          : "card-badge badge-muted";
    }

    if (bookmarks.length === 0) {
      container.innerHTML = `<p style="font-size: 0.78rem; color: var(--text-muted); margin: 0;">No bookmarks saved on device.</p>`;
      return;
    }

    let html = "";
    bookmarks.forEach((bm, idx) => {
      const sInfo = SURAHS[bm.surah - 1] || { name: "Surah" };
      html += `
        <div class="book-item" style="padding: 0.4rem 0.65rem;">
          <div class="book-info">
            <span style="font-size: 0.75rem; font-weight: 600; color: var(--text-muted); min-width: 18px;">#${idx + 1}</span>
            <div class="book-meta">
              <span class="book-name" style="font-size: 0.8rem;">${escapeHtml(sInfo.name)} ${bm.surah}:${bm.ayah}</span>
              <span class="book-size">Juz ${bm.juz}</span>
            </div>
          </div>
          <div style="display: flex; align-items: center; gap: 0.35rem;">
            <button class="btn-control mushaf-bm-jump-btn" type="button" data-index="${idx}" style="padding: 0.2rem 0.5rem; font-size: 0.74rem;">Jump</button>
            <button class="btn-control btn-danger-text mushaf-bm-del-btn" type="button" data-index="${idx}" style="padding: 0.2rem 0.5rem; font-size: 0.74rem;">Delete</button>
          </div>
        </div>
      `;
    });
    container.innerHTML = html;
  }

  render() {
    let surahOptionsHtml = "";
    SURAHS.forEach((s) => {
      surahOptionsHtml += `<option value="${s.number}">${s.number}. ${escapeHtml(s.name)} (${s.ayahs})</option>`;
    });

    const presetButtonsHtml = SUNNAH_PRESETS.map(
      (p) =>
        `<button class="btn-control mushaf-preset-btn" type="button" data-surah="${p.surah}" data-ayah="${p.ayah}" style="font-size: 0.75rem; padding: 0.25rem 0.55rem;">${escapeHtml(p.label)}</button>`,
    ).join("");

    this.innerHTML = `
      <section class="upload-section mushaf-section">
        <div class="section-title">
          <div class="section-title-left">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
              <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
            </svg>
            <h2>Al-Quran Reader</h2>
          </div>
          <div id="mushafGeometryBadge" class="card-badge badge-flash">200 × 200 px</div>
        </div>

        <!-- CARD 1: Mushaf Package Builder -->
        <div class="card" style="margin-bottom: 0.85rem;">
          <div class="card-header">
            <div class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
                <polyline points="2 17 12 22 22 17"></polyline>
                <polyline points="2 12 12 17 22 12"></polyline>
              </svg>
              <span>Package Builder</span>
            </div>
            <div id="mushafSizeEstimate" class="card-badge badge-muted">Est. Size: ~8.7 MB</div>
          </div>

          <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(170px, 1fr)); gap: 0.65rem; margin-bottom: 0.65rem;">
            <div>
              <label for="mushafScriptSelect" class="input-label-sm">Mushaf Script</label>
              <select id="mushafScriptSelect" class="select-input" style="width: 100%;">
                <option value="0" selected>Madinah Uthmani (KFGQPC)</option>
                <option value="1">IndoPak 15-Line Naskh</option>
              </select>
            </div>
            <div>
              <label for="mushafFontSizeSelect" class="input-label-sm">Font Size</label>
              <select id="mushafFontSizeSelect" class="select-input" style="width: 100%;">
                <option value="20">20px (Compact)</option>
                <option value="24">24px (Normal)</option>
                <option value="28" selected>28px (Standard)</option>
                <option value="32">32px (Large)</option>
              </select>
            </div>
            <div>
              <label for="mushafRangeSelect" class="input-label-sm">Content Range</label>
              <select id="mushafRangeSelect" class="select-input" style="width: 100%;">
                <option value="full" selected>Full 30 Juz (1–114)</option>
                <option value="juz30">Juz 30 ('Amma, 78–114)</option>
                <option value="custom">Custom Surah Range</option>
              </select>
            </div>
          </div>

          <div id="mushafCustomRangeRow" style="display: none; grid-template-columns: 1fr 1fr; gap: 0.65rem; margin-bottom: 0.65rem;">
            <div>
              <label for="customStartSurah" class="input-label-sm">From Surah</label>
              <select id="customStartSurah" class="select-input" style="width: 100%;">${surahOptionsHtml}</select>
            </div>
            <div>
              <label for="customEndSurah" class="input-label-sm">To Surah</label>
              <select id="customEndSurah" class="select-input" style="width: 100%;">${surahOptionsHtml}</select>
            </div>
          </div>

          <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 0.6rem; align-items: center; background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: var(--radius-md); padding: 0.6rem 0.75rem; margin-bottom: 0.75rem;">
            <label style="display: flex; align-items: center; gap: 0.45rem; font-size: 0.8rem; color: var(--text-primary); cursor: pointer;">
              <input type="checkbox" id="layerLatinCheckbox" checked>
              <span>Latin Transliteration</span>
            </label>
            <div style="display: flex; align-items: center; gap: 0.4rem;">
              <label style="display: flex; align-items: center; gap: 0.4rem; font-size: 0.8rem; color: var(--text-primary); cursor: pointer; white-space: nowrap;">
                <input type="checkbox" id="layerTranslationCheckbox" checked>
                <span>Translation:</span>
              </label>
              <select id="mushafTranslationSelect" class="select-input" style="font-size: 0.76rem; padding: 0.2rem 0.4rem; flex: 1; min-width: 0;">
                <option value="id_kemenag" selected>ID - Kemenag</option>
                <option value="en_saheeh">EN - Sahih Intl</option>
              </select>
            </div>
            <div style="display: flex; align-items: center; gap: 0.4rem;">
              <label style="display: flex; align-items: center; gap: 0.4rem; font-size: 0.8rem; color: var(--text-primary); cursor: pointer; white-space: nowrap;">
                <input type="checkbox" id="layerTafsirCheckbox" checked>
                <span>Tafsir:</span>
              </label>
              <select id="mushafTafsirSelect" class="select-input" style="font-size: 0.76rem; padding: 0.2rem 0.4rem; flex: 1; min-width: 0;">
                <option value="id_kemenag_ringkas" selected>ID - Kemenag Ringkas</option>
                <option value="jalalayn">EN - Al-Jalalayn</option>
              </select>
            </div>
          </div>

          <div id="mushafBuildProgressWrap" style="display: none; margin-bottom: 0.6rem;">
            <div class="upload-progress-container">
              <div id="mushafBuildProgressBar" class="upload-progress-fill" style="width: 0%;"></div>
            </div>
          </div>

          <div style="display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 0.6rem;">
            <div style="display: flex; align-items: center; gap: 0.5rem; min-width: 0;">
              <span id="mushafStatusText" class="status-text" style="font-size: 0.78rem; color: var(--text-secondary); font-family: var(--font-mono);">Loading assets.</span>
              <button id="mushafRetryAssetsBtn" class="btn-control" type="button" style="display: none; font-size: 0.74rem; padding: 0.2rem 0.5rem;">Retry</button>
            </div>
            <div style="display: flex; flex-wrap: wrap; gap: 0.5rem;">
              <button id="mushafDownloadBtn" class="btn-secondary" type="button">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                  <polyline points="7 10 12 15 17 10"></polyline>
                  <line x1="12" y1="15" x2="12" y2="3"></line>
                </svg>
                <span>Download Package(.bqm)</span>
              </button>
              <button id="mushafBuildUploadBtn" class="btn-connect" type="button">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                  <polyline points="17 8 12 3 7 8"></polyline>
                  <line x1="12" y1="3" x2="12" y2="15"></line>
                </svg>
                <span>Build & Upload</span>
              </button>
            </div>
          </div>
        </div>

        <!-- CARD 2: Unified Preview, Tilawah Cursor & Bookmarks -->
        <div class="card">
          <div class="card-header">
            <div class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z"></path>
                <path d="M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z"></path>
              </svg>
              <span>Preview, Tilawah & Bookmarks</span>
            </div>
            <div style="display: flex; align-items: center; gap: 0.4rem; flex-wrap: wrap;">
              <div id="mushafTilawahBadge" class="card-badge badge-muted">Ready (1:1)</div>
              <div id="mushafBookmarkCountBadge" class="card-badge badge-muted">0 / 8 Bookmarks</div>
            </div>
          </div>

          <div style="display: grid; grid-template-columns: auto 1fr; gap: 1rem; align-items: start;">
            <!-- Left: 1-Bit E-Paper Preview Canvas -->
            <div style="display: flex; flex-direction: column; align-items: center; gap: 0.35rem; background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: var(--radius-md); padding: 0.6rem;">
              <canvas id="mushafPreviewCanvas" width="${this._screenW}" height="${this._screenH}" style="background: #ffffff; image-rendering: pixelated; border: 1px solid var(--border-color); border-radius: 4px; max-width: 100%; height: auto;"></canvas>
              <span style="font-size: 0.7rem; color: var(--text-muted);">1-Bit E-Paper Preview</span>
            </div>

            <!-- Right: Unified Selector, Sunnah Presets & Bookmarks -->
            <div style="display: flex; flex-direction: column; gap: 0.75rem; min-width: 220px;">
              <!-- Unified Surah / Ayah / Layer Selector & Actions -->
              <div style="display: flex; flex-wrap: wrap; gap: 0.5rem; align-items: flex-end;">
                <div style="flex: 1 1 150px;">
                  <label for="cursorSurahSelect" class="input-label-sm">Surah</label>
                  <select id="cursorSurahSelect" class="select-input" style="width: 100%;">${surahOptionsHtml}</select>
                </div>
                <div style="width: 68px;">
                  <label for="cursorAyahInput" class="input-label-sm">Ayah</label>
                  <input type="number" id="cursorAyahInput" class="select-input" style="width: 100%;" min="1" max="7" value="1">
                </div>
                <div style="width: 110px;">
                  <label for="cursorLayerSelect" class="input-label-sm">Layer</label>
                  <select id="cursorLayerSelect" class="select-input" style="width: 100%;">
                    <option value="0" selected>Mushaf</option>
                    <option value="1">Latin</option>
                    <option value="2">Translation</option>
                    <option value="3">Tafsir</option>
                  </select>
                </div>
                <button id="mushafSyncCursorBtn" class="btn-secondary" type="button" style="height: 36px; padding: 0 0.7rem;" title="Jump device to selected Surah, Ayah & Layer">
                  <span>Sync Cursor</span>
                </button>
                <button id="mushafAddBookmarkBtn" class="btn-secondary" type="button" style="height: 36px; padding: 0 0.7rem;" title="Bookmark selected Surah & Ayah on device">
                  <span>+ Bookmark</span>
                </button>
              </div>

              <!-- Sunnah Quick Presets -->
              <div>
                <div style="font-size: 0.72rem; font-weight: 600; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.04em; margin-bottom: 0.35rem;">Sunnah Quick Jump</div>
                <div class="control-btn-group" style="gap: 0.35rem;">
                  ${presetButtonsHtml}
                </div>
              </div>

              <!-- Saved Bookmarks List -->
              <div style="border-top: 1px solid var(--border-color); padding-top: 0.6rem;">
                <div id="mushafBookmarksList" class="books-list">
                  <p style="font-size: 0.78rem; color: var(--text-muted); margin: 0;">No bookmarks saved on device.</p>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>
    `;
  }

  bindEvents() {
    const retryBtn = this.querySelector("#mushafRetryAssetsBtn");
    if (retryBtn) {
      retryBtn.addEventListener("click", () => this.preloadAssets());
    }

    const rangeSelect = this.querySelector("#mushafRangeSelect");
    const customRangeRow = this.querySelector("#mushafCustomRangeRow");
    if (rangeSelect && customRangeRow) {
      rangeSelect.addEventListener("change", () => {
        customRangeRow.style.display =
          rangeSelect.value === "custom" ? "grid" : "none";
        this.updateSizeEstimate();
      });
    }

    const builderInputs = this.querySelectorAll(
      "#layerLatinCheckbox, #layerTranslationCheckbox, #layerTafsirCheckbox, #customStartSurah, #customEndSurah, #mushafScriptSelect, #mushafFontSizeSelect",
    );
    builderInputs.forEach((input) => {
      input.addEventListener("change", () => {
        this.updateSizeEstimate();
        if (
          input.id === "mushafScriptSelect" ||
          input.id === "mushafFontSizeSelect"
        ) {
          this.renderAyahPreview();
        }
      });
    });

    const transSelect = this.querySelector("#mushafTranslationSelect");
    const tafsirSelect = this.querySelector("#mushafTafsirSelect");
    const onEditionChange = () => {
      this.updateSizeEstimate();
      const translationEdition = transSelect?.value || "id_kemenag";
      const tafsirEdition = tafsirSelect?.value || "id_kemenag_ringkas";
      const key = `${translationEdition}|${tafsirEdition}`;
      if (key !== this._loadedEditionsKey) {
        this.preloadAssets();
      } else {
        this.renderAyahPreview();
      }
    };
    if (transSelect) transSelect.addEventListener("change", onEditionChange);
    if (tafsirSelect) tafsirSelect.addEventListener("change", onEditionChange);

    // Build & Download buttons
    const uploadBtn = this.querySelector("#mushafBuildUploadBtn");
    const downloadBtn = this.querySelector("#mushafDownloadBtn");
    if (uploadBtn)
      uploadBtn.addEventListener("click", () => this.buildAndUpload(true));
    if (downloadBtn)
      downloadBtn.addEventListener("click", () => this.buildAndUpload(false));

    // Unified Surah / Ayah / Layer selector drives Live Preview, Cursor Sync, Add Bookmark
    const curSurahSelect = this.querySelector("#cursorSurahSelect");
    const curAyahInput = this.querySelector("#cursorAyahInput");
    const curLayerSelect = this.querySelector("#cursorLayerSelect");
    const syncCursorBtn = this.querySelector("#mushafSyncCursorBtn");
    const addBmBtn = this.querySelector("#mushafAddBookmarkBtn");

    if (curSurahSelect && curAyahInput) {
      curSurahSelect.addEventListener("change", () => {
        const sNum = parseInt(curSurahSelect.value, 10);
        const sInfo = SURAHS[sNum - 1] || SURAHS[0];
        curAyahInput.max = String(sInfo.ayahs);
        if (parseInt(curAyahInput.value, 10) > sInfo.ayahs) {
          curAyahInput.value = "1";
        }
        this.renderAyahPreview();
      });
      curAyahInput.addEventListener("change", () => this.renderAyahPreview());
    }
    if (curLayerSelect) {
      curLayerSelect.addEventListener("change", () => this.renderAyahPreview());
    }

    if (syncCursorBtn) {
      syncCursorBtn.addEventListener("click", () => {
        const surah = parseInt(curSurahSelect?.value || "1", 10);
        const ayah = parseInt(curAyahInput?.value || "1", 10);
        const layer = parseInt(curLayerSelect?.value || "0", 10);
        this.dispatchCursorSync(surah, ayah, layer);
      });
    }

    if (addBmBtn) {
      addBmBtn.addEventListener("click", () => {
        const bookmarks = [...(this._deviceState.bookmarks || [])];
        if (bookmarks.length >= 8) {
          this.setStatusText(
            "Maximum 8 bookmark slots supported by device.",
            true,
          );
          return;
        }
        const surah = parseInt(curSurahSelect?.value || "1", 10);
        const ayah = parseInt(curAyahInput?.value || "1", 10);
        const juz = getJuzForAyah(surah, ayah);
        bookmarks.push({ surah, ayah, juz });
        this.dispatchEvent(
          new CustomEvent("mushaf-set-bookmarks", {
            detail: { bookmarks },
            bubbles: true,
            composed: true,
          }),
        );
      });
    }

    // Sunnah Quick Presets update preview AND sync cursor when connected
    this.querySelectorAll(".mushaf-preset-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        const surah = parseInt(btn.getAttribute("data-surah"), 10);
        const ayah = parseInt(btn.getAttribute("data-ayah"), 10);
        const layer = parseInt(curLayerSelect?.value || "0", 10);

        if (curSurahSelect) curSurahSelect.value = String(surah);
        if (curAyahInput) {
          const sInfo = SURAHS[surah - 1] || SURAHS[0];
          curAyahInput.max = String(sInfo.ayahs);
          curAyahInput.value = String(ayah);
        }
        this.renderAyahPreview();
        if (this._isConnected) {
          this.dispatchCursorSync(surah, ayah, layer);
        }
      });
    });

    // Bookmarks Jump / Delete delegation
    const bmList = this.querySelector("#mushafBookmarksList");
    if (bmList) {
      bmList.addEventListener("click", (e) => {
        const jumpBtn = e.target.closest(".mushaf-bm-jump-btn");
        if (jumpBtn) {
          const idx = parseInt(jumpBtn.getAttribute("data-index"), 10);
          const bm = this._deviceState.bookmarks[idx];
          if (bm) {
            const layer = parseInt(curLayerSelect?.value || "0", 10);
            if (curSurahSelect) curSurahSelect.value = String(bm.surah);
            if (curAyahInput) {
              const sInfo = SURAHS[bm.surah - 1] || SURAHS[0];
              curAyahInput.max = String(sInfo.ayahs);
              curAyahInput.value = String(bm.ayah);
            }
            this.renderAyahPreview();
            if (this._isConnected) {
              this.dispatchCursorSync(bm.surah, bm.ayah, layer);
            }
          }
          return;
        }

        const delBtn = e.target.closest(".mushaf-bm-del-btn");
        if (delBtn) {
          const idx = parseInt(delBtn.getAttribute("data-index"), 10);
          const bookmarks = [...(this._deviceState.bookmarks || [])];
          if (idx >= 0 && idx < bookmarks.length) {
            bookmarks.splice(idx, 1);
            this.dispatchEvent(
              new CustomEvent("mushaf-set-bookmarks", {
                detail: { bookmarks },
                bubbles: true,
                composed: true,
              }),
            );
          }
        }
      });
    }
  }

  dispatchCursorSync(surah, ayah, layer) {
    this.dispatchEvent(
      new CustomEvent("mushaf-set-cursor", {
        detail: { surah, ayah, layer },
        bubbles: true,
        composed: true,
      }),
    );
  }

  updateSizeEstimate() {
    const rangeType =
      this.querySelector("#mushafRangeSelect")?.value || "full";
    const customStart = parseInt(
      this.querySelector("#customStartSurah")?.value || "1",
      10,
    );
    const customEnd = parseInt(
      this.querySelector("#customEndSurah")?.value || "114",
      10,
    );
    const scriptId = parseInt(
      this.querySelector("#mushafScriptSelect")?.value || "0",
      10,
    );
    const fontSize = parseInt(
      this.querySelector("#mushafFontSizeSelect")?.value || "24",
      10,
    );
    const latin = Boolean(this.querySelector("#layerLatinCheckbox")?.checked);
    const trans = Boolean(
      this.querySelector("#layerTranslationCheckbox")?.checked,
    );
    const tafsir = Boolean(this.querySelector("#layerTafsirCheckbox")?.checked);
    const translationEdition =
      this.querySelector("#mushafTranslationSelect")?.value || "id_kemenag";
    const tafsirEdition =
      this.querySelector("#mushafTafsirSelect")?.value || "id_kemenag_ringkas";

    const startSurah =
      rangeType === "custom"
        ? Math.min(customStart, customEnd)
        : rangeType === "juz30"
          ? 78
          : 1;
    const endSurah =
      rangeType === "custom" ? Math.max(customStart, customEnd) : 114;

    let totalBytes = 64 + 114 * 24 + 6236 * 24;

    if (this._cachedMushafBase && Array.isArray(this._cachedMushafBase.ayahs)) {
      const uniqueWords = new Set();
      const trArr =
        this._cachedEditionsData?.translations?.[translationEdition];
      const tafArr = this._cachedEditionsData?.tafsirs?.[tafsirEdition];

      for (let i = 0; i < this._cachedMushafBase.ayahs.length; i++) {
        const a = this._cachedMushafBase.ayahs[i];
        if (a.surah < startSurah || a.surah > endSurah) continue;

        const words =
          scriptId === 1
            ? a.indopakWords || a.words || []
            : a.uthmaniWords || a.words || [];
        for (let w = 0; w < words.length; w++) {
          uniqueWords.add(words[w]);
        }
        totalBytes += 4 + words.length * 6;

        if (latin && a.latin) {
          totalBytes += a.latin.length + 12;
        }
        if (trans) {
          const trStr =
            trArr?.[a.globalAyahIndex ?? i] ||
            a.translationId ||
            a.translationEn ||
            "";
          totalBytes += trStr.length + 16;
        }
        if (tafsir) {
          const tafStr =
            tafArr?.[a.globalAyahIndex ?? i] || a.tafsirId || a.tafsirEn || "";
          totalBytes += tafStr.length + 24;
        }
      }

      const avgSpriteBytes = Math.round(148 * Math.pow(fontSize / 24, 1.55));
      totalBytes += uniqueWords.size * avgSpriteBytes;
    } else {
      let totalAyahs = 0;
      for (let s = startSurah; s <= endSurah; s++) {
        const info = SURAHS[s - 1];
        if (info) totalAyahs += info.ayahs;
      }
      let bytesPerAyah = 490;
      if (latin) bytesPerAyah += 120;
      if (trans) bytesPerAyah += 165;
      if (tafsir) bytesPerAyah += 720;
      totalBytes += totalAyahs * bytesPerAyah;
    }

    const formatted =
      totalBytes < 1024 * 1024
        ? `Est. Size: ~${Math.round(totalBytes / 1024)} KB`
        : `Est. Size: ~${(totalBytes / (1024 * 1024)).toFixed(1)} MB`;

    const badge = this.querySelector("#mushafSizeEstimate");
    if (badge) badge.textContent = formatted;
  }

  async renderAyahPreview() {
    if (!this._assetsReady) return;
    const canvas = this.querySelector("#mushafPreviewCanvas");
    if (!canvas) return;

    const surah = parseInt(
      this.querySelector("#cursorSurahSelect")?.value || "1",
      10,
    );
    const ayah = parseInt(
      this.querySelector("#cursorAyahInput")?.value || "1",
      10,
    );
    const layer = parseInt(
      this.querySelector("#cursorLayerSelect")?.value || "0",
      10,
    );
    const scriptStyle = parseInt(
      this.querySelector("#mushafScriptSelect")?.value || "0",
      10,
    );
    const mushafFontSize = parseInt(
      this.querySelector("#mushafFontSizeSelect")?.value || "28",
      10,
    );
    const translationEdition =
      this.querySelector("#mushafTranslationSelect")?.value || "id_kemenag";
    const tafsirEdition =
      this.querySelector("#mushafTafsirSelect")?.value || "id_kemenag_ringkas";

    try {
      await ensureMushafFont(scriptStyle);
      await renderAyahPreviewToCanvas(canvas, {
        surah,
        ayah,
        layer,
        scriptStyle,
        scriptId: scriptStyle,
        mushafFontSize,
        displayWidth: this._screenW,
        displayHeight: this._screenH,
        translationEdition,
        tafsirEdition,
        mushafBase: this._cachedMushafBase,
        editionsData: this._cachedEditionsData,
      });
    } catch (err) {
      console.error("[MushafBuilder] Preview render error:", err);
      this.setStatusText(`Preview error: ${err.message}`, true);
    }
  }

  async buildAndUpload(isUpload = true) {
    if (this._isBuilding) return;
    this._isBuilding = true;

    const progressWrap = this.querySelector("#mushafBuildProgressWrap");
    const progressBar = this.querySelector("#mushafBuildProgressBar");
    const uploadBtn = this.querySelector("#mushafBuildUploadBtn");
    const downloadBtn = this.querySelector("#mushafDownloadBtn");

    if (progressWrap) progressWrap.style.display = "block";
    if (progressBar) progressBar.style.width = "0%";
    if (uploadBtn) uploadBtn.disabled = true;
    if (downloadBtn) downloadBtn.disabled = true;

    const scriptStyle = parseInt(
      this.querySelector("#mushafScriptSelect")?.value || "0",
      10,
    );
    const mushafFontSize = parseInt(
      this.querySelector("#mushafFontSizeSelect")?.value || "28",
      10,
    );
    const rangeType =
      this.querySelector("#mushafRangeSelect")?.value || "full";
    const customStart = parseInt(
      this.querySelector("#customStartSurah")?.value || "1",
      10,
    );
    const customEnd = parseInt(
      this.querySelector("#customEndSurah")?.value || "114",
      10,
    );

    const layerLatin = Boolean(
      this.querySelector("#layerLatinCheckbox")?.checked,
    );
    const layerTranslation = Boolean(
      this.querySelector("#layerTranslationCheckbox")?.checked,
    );
    const layerTafsir = Boolean(
      this.querySelector("#layerTafsirCheckbox")?.checked,
    );

    const translationEdition =
      this.querySelector("#mushafTranslationSelect")?.value || "id_kemenag";
    const tafsirEdition =
      this.querySelector("#mushafTafsirSelect")?.value || "id_kemenag_ringkas";

    const buildOptions = {
      scriptStyle,
      scriptId: scriptStyle,
      mushafFontSize,
      rangeType,
      startSurah:
        rangeType === "custom"
          ? Math.min(customStart, customEnd)
          : rangeType === "juz30"
            ? 78
            : 1,
      endSurah: rangeType === "custom" ? Math.max(customStart, customEnd) : 114,
      layers: {
        mushaf: true,
        latin: layerLatin,
        translation: layerTranslation,
        tafsir: layerTafsir,
      },
      translationEdition,
      tafsirEdition,
      displayWidth: this._screenW,
      displayHeight: this._screenH,
      mushafBase: this._cachedMushafBase,
      editionsData: this._cachedEditionsData,
    };

    try {
      await ensureMushafFont(scriptStyle, (msg) => this.setStatusText(msg));

      if (!this._cachedMushafBase || !this._cachedEditionsData) {
        const datasets = await fetchMushafDatasets(buildOptions, (msg) =>
          this.setStatusText(msg),
        );
        this._cachedMushafBase = datasets.mushafBase;
        this._cachedEditionsData = datasets.editionsData;
        buildOptions.mushafBase = this._cachedMushafBase;
        buildOptions.editionsData = this._cachedEditionsData;
      }

      this.setStatusText("Compiling mushaf.bqm.");

      const bqmArrayBuffer = await buildBqmPackage(buildOptions, (progress) => {
        if (!progress || typeof progress !== "object") return;
        const pct = Math.max(
          0,
          Math.min(100, Math.round(progress.percent ?? progress.current ?? 0)),
        );
        if (progressBar) progressBar.style.width = `${pct}%`;
        this.setStatusText(`${progress.stage || "Building"}: ${pct}%`);
      });

      const file = new File([bqmArrayBuffer], "mushaf.bqm", {
        type: "application/octet-stream",
      });

      if (isUpload) {
        this._isUploadingBqm = true;
        this.setStatusText(
          `Uploading mushaf.bqm (${(file.size / 1024).toFixed(0)} KB).`,
        );
        this.dispatchEvent(
          new CustomEvent("mushaf-upload-bqm", {
            detail: { file },
            bubbles: true,
            composed: true,
          }),
        );
      } else {
        const blobUrl = URL.createObjectURL(file);
        const a = document.createElement("a");
        a.href = blobUrl;
        a.download = "mushaf.bqm";
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(blobUrl);
        if (progressWrap) progressWrap.style.display = "none";
        this.setStatusText(
          `mushaf.bqm downloaded (${(file.size / 1024).toFixed(0)} KB).`,
        );
      }
    } catch (err) {
      this._isUploadingBqm = false;
      if (progressWrap) progressWrap.style.display = "none";
      console.error("[MushafBuilder] Build failed:", err);
      this.setStatusText(`Build failed: ${err.message}`, true);
    } finally {
      this._isBuilding = false;
      if (uploadBtn)
        uploadBtn.disabled =
          !this._isConnected ||
          !this._assetsReady ||
          Boolean(this._isUploadingBqm);
      if (downloadBtn) downloadBtn.disabled = !this._assetsReady;
    }
  }
}

if (
  typeof customElements !== "undefined" &&
  !customElements.get("mushaf-builder")
) {
  customElements.define("mushaf-builder", MushafBuilder);
}

export { MushafBuilder };
