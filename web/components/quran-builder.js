/**
 * quran-builder.js
 * BacaIn PWA - Al-Quran Reader & BQM Manager Custom Element (<quran-builder>)
 * Light DOM Web Component providing:
 * 1. Package Builder (quran.bqm): Script selection, layers, font size, live 1-bit e-paper canvas preview, compiler and BLE uploader
 * 2. Tilawah Cursor & Sunnah Quick-Jump: Authoritative NVS cursor sync, Sunnah preset jumps
 * 3. Bookmarks Manager: 0..8 slots in device hardware NVS
 */

import {
  buildBqmPackage,
  renderAyahPreviewToCanvas,
  ensureQuranFont,
  fetchQuranDatasets,
  fetchWithCache,
  QURAN_FONTS,
  QURAN_DATA_ENDPOINTS,
  QURAN_CACHE_NAME,
  SURAHS,
  CANONICAL_SURAHS,
  JUZ_BOUNDARIES,
  JUZ_STARTS,
  getJuzForSurahAyah,
  getJuzForAyah,
  LAYER_TAGS,
  LAYER_NAMES,
  SUNNAH_PRESETS,
  QURAN_LAYERS
} from '../processors/bqm-builder.js';

function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

const BaseElement = typeof HTMLElement !== 'undefined' ? HTMLElement : class {};

class QuranBuilder extends BaseElement {
  constructor() {
    super();
    this._screenW = 200;
    this._screenH = 200;
    this._supported = false;
    this._isConnected = false;
    this._isBuilding = false;
    this._assetsReady = false;
    this._assetsLoading = false;
    this._loadedEditionsKey = '';
    this._cachedQuranBase = null;
    this._cachedEditionsData = null;
    this._deviceState = {
      started: false,
      surah: 1,
      ayah: 1,
      layer: 0,
      bookmarks: []
    };
    this._onUploadStatus = this._onUploadStatus.bind(this);
  }

  setStatusText(text, isError = false) {
    const statusEl = this.querySelector('#quranStatusText') || this.querySelector('#statusText');
    if (statusEl) {
      statusEl.textContent = text;
      if (isError) {
        statusEl.style.color = 'var(--danger-color, #ef4444)';
        statusEl.style.fontWeight = '500';
      } else {
        statusEl.style.color = 'var(--text-secondary)';
        statusEl.style.fontWeight = 'normal';
      }
    }
    const legacyStatus = this.querySelector('#statusText');
    if (legacyStatus && legacyStatus !== statusEl) {
      legacyStatus.textContent = text;
    }
    const progressText = this.querySelector('#quranBuildProgressText');
    if (progressText) progressText.textContent = text;
    const previewStatus = this.querySelector('#previewStatusText');
    if (previewStatus && !this._isBuilding) previewStatus.textContent = text;

    if (isError) {
      console.error('[QuranBuilder]', text);
    }
  }

  connectedCallback() {
    document.addEventListener('quran-upload-status', this._onUploadStatus);
    if (!this.querySelector('.quran-section')) {
      this.render();
      this.bindEvents();
      this.updateGeometryBadge();
      this.updateSizeEstimate();
      this.setBuilderControlsDisabled(true);
      this.preloadAssets();
    }
  }

  disconnectedCallback() {
    document.removeEventListener('quran-upload-status', this._onUploadStatus);
  }

  _onUploadStatus(e) {
    if (!e || !e.detail) return;
    if (e.detail.ok === false) {
      this.setStatusText(`Upload failed: ${e.detail.error}`, true);
    } else if (e.detail.ok === true && e.detail.fileName === 'quran.bqm') {
      this.setStatusText('quran.bqm uploaded and verified on device.');
    }
  }

  setBuilderControlsDisabled(disabled) {
    const selector = [
      '#quranScriptSelect',
      '#quranFontSizeSelect',
      '#quranRangeSelect',
      '#customStartSurah',
      '#customEndSurah',
      '#layerLatinCheckbox',
      '#layerTranslationCheckbox',
      '#layerTafsirCheckbox',
      '#quranTranslationSelect',
      '#quranTafsirSelect',
      '#previewSurahSelect',
      '#previewAyahInput',
      '#previewLayerSelect',
      '#previewRefreshBtn'
    ].join(', ');

    const controls = this.querySelectorAll(selector);
    controls.forEach((el) => {
      el.disabled = Boolean(disabled);
    });

    const uploadBtn = this.querySelector('#quranBuildUploadBtn');
    const downloadBtn = this.querySelector('#quranDownloadBtn');

    if (disabled) {
      if (uploadBtn) uploadBtn.disabled = true;
      if (downloadBtn) downloadBtn.disabled = true;
    } else {
      if (uploadBtn) uploadBtn.disabled = !this._isConnected || !this._assetsReady;
      if (downloadBtn) downloadBtn.disabled = !this._assetsReady;
    }
  }

  async preloadAssets() {
    if (this._assetsLoading) return;
    this._assetsLoading = true;
    this._assetsReady = false;

    this.setBuilderControlsDisabled(true);

    const retryBtn = this.querySelector('#quranRetryAssetsBtn');
    if (retryBtn) retryBtn.style.display = 'none';

    const progressContainer = this.querySelector('#quranBuildProgressContainer');
    if (progressContainer) progressContainer.style.display = 'block';

    this.setStatusText('Loading Quran calligraphy fonts & corpora from cache/upstream...');

    try {
      await ensureQuranFont(0, (msg) => this.setStatusText(msg));
      await ensureQuranFont(1, (msg) => this.setStatusText(msg));

      const translationEdition = this.querySelector('#quranTranslationSelect')?.value || 'id_kemenag';
      const tafsirEdition = this.querySelector('#quranTafsirSelect')?.value || 'id_kemenag_ringkas';
      const datasets = await fetchQuranDatasets({
        scriptStyle: 0,
        translationEdition,
        tafsirEdition,
        layers: { mushaf: true, latin: true, translation: true, tafsir: true }
      }, (msg) => this.setStatusText(msg));

      this._cachedQuranBase = datasets.quranBase;
      this._cachedEditionsData = datasets.editionsData;
      this._loadedEditionsKey = `${translationEdition}|${tafsirEdition}`;

      this._assetsReady = true;
      this.setBuilderControlsDisabled(false);
      this.setStatusText('Quran fonts & 6,236-ayah corpora ready (cached).');
      await this.renderAyahPreview();
    } catch (err) {
      this._assetsReady = false;
      console.error('[QuranBuilder] Failed to preload Quran assets:', err);
      this.setBuilderControlsDisabled(true);
      this.setStatusText(`Assets unavailable (check connection or cache): ${err.message}`, true);
      if (retryBtn) retryBtn.style.display = 'inline-flex';
    } finally {
      this._assetsLoading = false;
    }
  }

  /**
   * Set device capability parameters on connection.
   * @param {{ supported: boolean, screenW: number, screenH: number, isConnected: boolean }} caps
   */
  setDeviceCapabilities(caps) {
    if (!caps) return;
    this._supported = Boolean(caps.supported);
    if (typeof caps.screenW === 'number' && caps.screenW > 0) {
      this._screenW = caps.screenW;
    }
    if (typeof caps.screenH === 'number' && caps.screenH > 0) {
      this._screenH = caps.screenH;
    }
    if (caps.isConnected !== undefined) {
      this._isConnected = Boolean(caps.isConnected);
    }

    const canvas = this.querySelector('#quranPreviewCanvas');
    if (canvas) {
      canvas.width = this._screenW;
      canvas.height = this._screenH;
    }

    this.updateGeometryBadge();
    this.updateConnectionStateUI();
    this.renderAyahPreview();
  }

  /**
   * Set screen geometry and update preview canvas.
   * @param {number} width
   * @param {number} height
   */
  setDisplayGeometry(width, height) {
    if (typeof width === 'number' && width > 0) this._screenW = width;
    if (typeof height === 'number' && height > 0) this._screenH = height;

    const canvas = this.querySelector('#quranPreviewCanvas');
    if (canvas) {
      canvas.width = this._screenW;
      canvas.height = this._screenH;
    }

    this.updateGeometryBadge();
    this.renderAyahPreview();
  }

  /**
   * Update device Quran state retrieved via 0x20.
   * @param {{ started: boolean, surah: number, ayah: number, layer: number, bookmarks: Array }} state
   */
  updateDeviceState(state) {
    if (!state) return;
    this._deviceState = {
      started: Boolean(state.started),
      surah: Math.max(1, Math.min(114, Number(state.surah) || 1)),
      ayah: Math.max(1, Number(state.ayah) || 1),
      layer: Math.max(0, Math.min(3, Number(state.layer) || 0)),
      bookmarks: Array.isArray(state.bookmarks) ? state.bookmarks.slice(0, 8) : []
    };

    this.renderDeviceStateUI();
  }

  updateQuranState(state) {
    this.updateDeviceState(state);
  }

  updateGeometryBadge() {
    const badge = this.querySelector('#quranGeometryBadge');
    if (badge) {
      badge.textContent = `${this._screenW} x ${this._screenH} px`;
    }
  }

  updateConnectionStateUI() {
    const syncBtn = this.querySelector('#quranSyncCursorBtn');
    const uploadBtn = this.querySelector('#quranBuildUploadBtn');
    const addBmBtn = this.querySelector('#quranAddBookmarkBtn');

    if (syncBtn) syncBtn.disabled = !this._isConnected;
    if (uploadBtn) uploadBtn.disabled = !this._isConnected || !this._assetsReady || this._isBuilding;
    if (addBmBtn) addBmBtn.disabled = !this._isConnected;
  }

  renderDeviceStateUI() {
    const state = this._deviceState;
    const badge = this.querySelector('#quranTilawahBadge');
    const infoText = this.querySelector('#quranActiveTilawahInfo');

    const surahInfo = SURAHS[state.surah - 1] || SURAHS[0];
    const layerName = QURAN_LAYERS[state.layer]?.name || 'Mushaf';
    const juz = getJuzForAyah(state.surah, state.ayah);

    if (state.started) {
      if (badge) {
        badge.className = 'card-badge badge-success';
        badge.textContent = `Active: ${state.surah}:${state.ayah}`;
      }
      if (infoText) {
        infoText.textContent = `Active reading: Surah ${state.surah}:${state.ayah} (${surahInfo.name}) - Layer: ${layerName} - Juz ${juz}`;
      }
    } else {
      // Zero-Metric Clutter: Suppress 0% progress, show calm ready state
      if (badge) {
        badge.className = 'card-badge badge-muted';
        badge.textContent = 'Ready to begin Tilawah (1:1)';
      }
      if (infoText) {
        infoText.textContent = 'Device state: Ready to begin Tilawah (1:1)';
      }
    }

    // Update Section 2 inputs if not currently focused
    const curSurahSelect = this.querySelector('#cursorSurahSelect');
    const curAyahInput = this.querySelector('#cursorAyahInput');
    const curLayerSelect = this.querySelector('#cursorLayerSelect');

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

    this.renderBookmarksList();
  }

  renderBookmarksList() {
    const container = this.querySelector('#quranBookmarksList');
    const countBadge = this.querySelector('#quranBookmarkCountBadge');
    if (!container) return;

    const bookmarks = this._deviceState.bookmarks || [];
    if (countBadge) {
      countBadge.textContent = `${bookmarks.length} / 8 slots`;
      countBadge.className = bookmarks.length > 0 ? 'card-badge badge-flash' : 'card-badge badge-muted';
    }

    if (bookmarks.length === 0) {
      container.innerHTML = `<p style="font-size: 0.82rem; color: var(--text-muted); padding: 0.5rem 0;">No bookmarks stored in device NVS.</p>`;
      return;
    }

    let html = '';
    bookmarks.forEach((bm, idx) => {
      const sInfo = SURAHS[bm.surah - 1] || { name: 'Unknown' };
      html += `
        <div class="book-item" style="padding: 0.5rem 0.75rem;">
          <div class="book-info">
            <div class="book-icon" style="width: 28px; height: 28px;">
              <span style="font-size: 0.75rem; font-weight: 600;">${idx + 1}</span>
            </div>
            <div class="book-meta">
              <span class="book-name" style="font-size: 0.82rem;">${escapeHtml(sInfo.name)} ${bm.surah}:${bm.ayah}</span>
              <span class="book-size">Juz ${bm.juz}</span>
            </div>
          </div>
          <div style="display: flex; align-items: center; gap: 0.4rem; flex-shrink: 0;">
            <button class="btn-control quran-bm-jump-btn" type="button" data-index="${idx}" style="padding: 0.25rem 0.55rem; font-size: 0.75rem;" title="Jump cursor to this bookmark">Jump</button>
            <button class="btn-control btn-danger-text quran-bm-del-btn" type="button" data-index="${idx}" style="padding: 0.25rem 0.55rem; font-size: 0.75rem;" title="Delete bookmark">Delete</button>
          </div>
        </div>
      `;
    });
    container.innerHTML = html;
  }

  render() {
    let surahOptionsHtml = '';
    SURAHS.forEach((s) => {
      surahOptionsHtml += `<option value="${s.number}">${s.number}. ${escapeHtml(s.name)} (${s.ayahs})</option>`;
    });

    const presetButtonsHtml = SUNNAH_PRESETS.map((p) =>
      `<button class="btn-control quran-preset-btn" type="button" data-surah="${p.surah}" data-ayah="${p.ayah}">${escapeHtml(p.label)}</button>`
    ).join('');

    this.innerHTML = `
      <section class="upload-section quran-section">
        <!-- Section Title -->
        <div class="section-title">
          <div class="section-title-left">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
              <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
              <path d="M12 6v8"></path>
              <path d="M9 10l3-3 3 3"></path>
            </svg>
            <h2>Al-Quran Reader & BQM Manager</h2>
          </div>
          <div id="quranGeometryBadge" class="card-badge badge-flash">200 x 200 px</div>
        </div>

        <!-- SECTION 1: Package Builder (quran.bqm) -->
        <div class="card" style="margin-bottom: 1rem;">
          <div class="card-header">
            <div class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
                <polyline points="2 17 12 22 22 17"></polyline>
                <polyline points="2 12 12 17 22 12"></polyline>
              </svg>
              <span>Package Builder (quran.bqm)</span>
            </div>
            <div id="quranSizeEstimate" class="card-badge badge-muted">Est. Size: ~2.4 MB</div>
          </div>

          <!-- Script & Font Options Grid -->
          <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 0.75rem; margin-bottom: 0.75rem;">
            <div>
              <label for="quranScriptSelect" class="input-label-sm">Mushaf Script</label>
              <select id="quranScriptSelect" class="select-input" style="width: 100%;">
                <option value="0" selected>Madinah Uthmani Hafs (KFGQPC)</option>
                <option value="1">IndoPak 15-Line Naskh</option>
              </select>
            </div>

            <div>
              <label for="quranFontSizeSelect" class="input-label-sm">Mushaf Font Size</label>
              <select id="quranFontSizeSelect" class="select-input" style="width: 100%;">
                <option value="20">20px (Compact)</option>
                <option value="24">24px (Normal)</option>
                <option value="28" selected>28px (Standard)</option>
                <option value="32">32px (Large)</option>
              </select>
            </div>

            <div>
              <label for="quranRangeSelect" class="input-label-sm">Content Range</label>
              <select id="quranRangeSelect" class="select-input" style="width: 100%;">
                <option value="full" selected>Full 30 Juz (Complete)</option>
                <option value="juz30">Juz 30 ('Amma) Only</option>
                <option value="custom">Custom Surah Range</option>
              </select>
            </div>
          </div>

          <!-- Custom Range Pickers (hidden unless custom selected) -->
          <div id="quranCustomRangeRow" style="display: none; grid-template-columns: 1fr 1fr; gap: 0.75rem; margin-bottom: 0.75rem;">
            <div>
              <label for="customStartSurah" class="input-label-sm">Start Surah</label>
              <select id="customStartSurah" class="select-input" style="width: 100%;">${surahOptionsHtml}</select>
            </div>
            <div>
              <label for="customEndSurah" class="input-label-sm">End Surah</label>
              <select id="customEndSurah" class="select-input" style="width: 100%;">${surahOptionsHtml}</select>
            </div>
          </div>

          <!-- Layers Toggle & Edition Row -->
          <div style="background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: var(--radius-md); padding: 0.75rem; margin-bottom: 0.75rem;">
            <div style="font-size: 0.8rem; font-weight: 600; color: var(--text-secondary); margin-bottom: 0.5rem; text-transform: uppercase; letter-spacing: 0.04em;">Content Layers</div>
            <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 0.65rem;">
              <label style="display: flex; align-items: center; gap: 0.5rem; font-size: 0.82rem; color: var(--text-primary); cursor: pointer;">
                <input type="checkbox" id="layerMushafCheckbox" checked disabled>
                <span>Mushaf (Word-Sprite Atlas)</span>
              </label>
              <label style="display: flex; align-items: center; gap: 0.5rem; font-size: 0.82rem; color: var(--text-primary); cursor: pointer;">
                <input type="checkbox" id="layerLatinCheckbox" checked>
                <span>Latin Transliteration</span>
              </label>
              <div style="display: flex; flex-direction: column; gap: 0.25rem;">
                <label style="display: flex; align-items: center; gap: 0.5rem; font-size: 0.82rem; color: var(--text-primary); cursor: pointer;">
                  <input type="checkbox" id="layerTranslationCheckbox" checked>
                  <span>Translation</span>
                </label>
                <select id="quranTranslationSelect" class="select-input" style="font-size: 0.78rem; padding: 0.25rem 0.5rem;">
                  <option value="id_kemenag" selected>Bahasa Indonesia - Kemenag</option>
                  <option value="en_saheeh">English - Sahih International</option>
                </select>
              </div>
              <div style="display: flex; flex-direction: column; gap: 0.25rem;">
                <label style="display: flex; align-items: center; gap: 0.5rem; font-size: 0.82rem; color: var(--text-primary); cursor: pointer;">
                  <input type="checkbox" id="layerTafsirCheckbox" checked>
                  <span>Tafsir</span>
                </label>
                <select id="quranTafsirSelect" class="select-input" style="font-size: 0.78rem; padding: 0.25rem 0.5rem;">
                  <option value="id_kemenag_ringkas" selected>Tafsir Ringkas Kemenag (ID)</option>
                  <option value="jalalayn">Tafsir Al-Jalalayn (EN)</option>
                </select>
              </div>
            </div>
          </div>

          <!-- Live 1-Bit E-Paper Preview Canvas -->
          <div style="background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: var(--radius-md); padding: 0.85rem; margin-bottom: 0.75rem;">
            <div style="display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 0.5rem; margin-bottom: 0.65rem;">
              <div style="display: flex; align-items: center; gap: 0.5rem;">
                <span style="font-size: 0.8rem; font-weight: 600; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.04em;">Live 1-Bit E-Paper Preview</span>
                <span id="previewStatusText" style="font-size: 0.75rem; color: var(--text-muted); font-style: italic;"></span>
              </div>
              <div style="display: flex; flex-wrap: wrap; align-items: center; gap: 0.4rem;">
                <select id="previewSurahSelect" class="select-input" style="font-size: 0.78rem; padding: 0.25rem 0.5rem;">${surahOptionsHtml}</select>
                <input type="number" id="previewAyahInput" class="select-input" style="width: 60px; font-size: 0.78rem; padding: 0.25rem 0.4rem;" min="1" max="7" value="1">
                <select id="previewLayerSelect" class="select-input" style="font-size: 0.78rem; padding: 0.25rem 0.5rem;">
                  <option value="0" selected>Mushaf</option>
                  <option value="1">Latin</option>
                  <option value="2">Translation</option>
                  <option value="3">Tafsir</option>
                </select>
                <button id="previewRefreshBtn" class="btn-control" type="button" title="Refresh Preview">Refresh</button>
              </div>
            </div>
            <div style="display: flex; justify-content: center; align-items: center; padding: 0.5rem; background: var(--bg-input); border-radius: var(--radius-sm); overflow: auto;">
              <canvas id="quranPreviewCanvas" width="${this._screenW}" height="${this._screenH}" style="background: #ffffff; image-rendering: pixelated; border: 1px solid var(--border-color); border-radius: 4px; box-shadow: var(--shadow-sm); max-width: 100%; height: auto;"></canvas>
            </div>
          </div>

          <!-- Progress bar & Action buttons -->
          <div id="quranBuildProgressContainer" style="display: none; margin-bottom: 0.75rem;">
            <div class="upload-progress-container" style="margin-bottom: 0.35rem;">
              <div id="quranBuildProgressBar" class="upload-progress-fill" style="width: 0%;"></div>
            </div>
            <div style="display: flex; align-items: center; justify-content: space-between; gap: 0.5rem;">
              <div id="quranStatusText" class="status-text" style="font-size: 0.78rem; color: var(--text-secondary); font-family: var(--font-mono);">Preparing build...</div>
              <button id="quranRetryAssetsBtn" class="btn-control" type="button" style="display: none; font-size: 0.75rem; padding: 0.2rem 0.5rem;">Retry Load Assets</button>
            </div>
            <div id="statusText" style="display: none;"></div>
            <div id="quranBuildProgressText" style="display: none;"></div>
          </div>

          <div style="display: flex; flex-wrap: wrap; gap: 0.6rem; justify-content: flex-end;">
            <button id="quranDownloadBtn" class="btn-secondary" type="button">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                <polyline points="7 10 12 15 17 10"></polyline>
                <line x1="12" y1="15" x2="12" y2="3"></line>
              </svg>
              <span>Download quran.bqm</span>
            </button>
            <button id="quranBuildUploadBtn" class="btn-connect" type="button">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                <polyline points="17 8 12 3 7 8"></polyline>
                <line x1="12" y1="3" x2="12" y2="15"></line>
              </svg>
              <span>Build & Upload quran.bqm</span>
            </button>
          </div>
        </div>

        <!-- SECTION 2: Tilawah Cursor & Sunnah Quick-Jump -->
        <div class="card" style="margin-bottom: 1rem;">
          <div class="card-header">
            <div class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="10"></circle>
                <polygon points="12 8 8 12 12 16 12 8"></polygon>
              </svg>
              <span>Tilawah Cursor & Sunnah Quick-Jump</span>
            </div>
            <div id="quranTilawahBadge" class="card-badge badge-muted">Ready to begin Tilawah (1:1)</div>
          </div>

          <!-- Active status display -->
          <div id="quranActiveTilawahInfo" style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 0.75rem;">
            Device state: Ready to begin Tilawah (1:1)
          </div>

          <!-- Cursor selection controls -->
          <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)) auto; gap: 0.6rem; align-items: flex-end; margin-bottom: 0.75rem;">
            <div>
              <label for="cursorSurahSelect" class="input-label-sm">Surah</label>
              <select id="cursorSurahSelect" class="select-input" style="width: 100%;">${surahOptionsHtml}</select>
            </div>
            <div>
              <label for="cursorAyahInput" class="input-label-sm">Ayah</label>
              <input type="number" id="cursorAyahInput" class="select-input" style="width: 100%;" min="1" max="7" value="1">
            </div>
            <div>
              <label for="cursorLayerSelect" class="input-label-sm">Layer</label>
              <select id="cursorLayerSelect" class="select-input" style="width: 100%;">
                <option value="0" selected>Mushaf</option>
                <option value="1">Latin</option>
                <option value="2">Translation</option>
                <option value="3">Tafsir</option>
              </select>
            </div>
            <div>
              <button id="quranSyncCursorBtn" class="btn-secondary" type="button" style="height: 36px;">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <polyline points="23 4 23 10 17 10"></polyline>
                  <polyline points="1 20 1 14 7 14"></polyline>
                  <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
                </svg>
                <span>Sync Cursor to Device</span>
              </button>
            </div>
          </div>

          <!-- Sunnah Quick-Preset Buttons -->
          <div>
            <div style="font-size: 0.75rem; font-weight: 500; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.04em; margin-bottom: 0.4rem;">Sunnah Quick Presets</div>
            <div class="control-btn-group">
              ${presetButtonsHtml}
            </div>
          </div>
        </div>

        <!-- SECTION 3: Bookmarks Manager (0..8 slots) -->
        <div class="card">
          <div class="card-header">
            <div class="card-title">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"></path>
              </svg>
              <span>Bookmarks Manager</span>
            </div>
            <div id="quranBookmarkCountBadge" class="card-badge badge-muted">0 / 8 slots</div>
          </div>

          <!-- Bookmarks List -->
          <div id="quranBookmarksList" class="books-list" style="margin-bottom: 0.85rem;">
            <p style="font-size: 0.82rem; color: var(--text-muted); padding: 0.5rem 0;">No bookmarks stored in device NVS.</p>
          </div>

          <!-- Add Bookmark Row -->
          <div id="quranAddBookmarkRow" style="display: flex; flex-wrap: wrap; align-items: flex-end; gap: 0.6rem; padding-top: 0.6rem; border-top: 1px solid var(--border-color);">
            <div style="flex: 1; min-width: 140px;">
              <label for="bmSurahSelect" class="input-label-sm">Bookmark Surah</label>
              <select id="bmSurahSelect" class="select-input" style="width: 100%;">${surahOptionsHtml}</select>
            </div>
            <div style="width: 80px;">
              <label for="bmAyahInput" class="input-label-sm">Ayah</label>
              <input type="number" id="bmAyahInput" class="select-input" style="width: 100%;" min="1" max="7" value="1">
            </div>
            <div>
              <button id="quranAddBookmarkBtn" class="btn-secondary" type="button" style="height: 36px;">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <line x1="12" y1="5" x2="12" y2="19"></line>
                  <line x1="5" y1="12" x2="19" y2="12"></line>
                </svg>
                <span>Add Bookmark</span>
              </button>
            </div>
          </div>
        </div>
      </section>
    `;
  }

  bindEvents() {
    // Retry load assets
    const retryBtn = this.querySelector('#quranRetryAssetsBtn');
    if (retryBtn) {
      retryBtn.addEventListener('click', () => this.preloadAssets());
    }

    // Range selector change
    const rangeSelect = this.querySelector('#quranRangeSelect');
    const customRangeRow = this.querySelector('#quranCustomRangeRow');
    if (rangeSelect && customRangeRow) {
      rangeSelect.addEventListener('change', () => {
        customRangeRow.style.display = rangeSelect.value === 'custom' ? 'grid' : 'none';
        this.updateSizeEstimate();
      });
    }

    // Layer checkboxes & dropdowns change
    const layerInputs = this.querySelectorAll('#layerLatinCheckbox, #layerTranslationCheckbox, #layerTafsirCheckbox, #customStartSurah, #customEndSurah');
    layerInputs.forEach((input) => {
      input.addEventListener('change', () => this.updateSizeEstimate());
    });

    // Edition selectors change
    const transSelect = this.querySelector('#quranTranslationSelect');
    const tafsirSelect = this.querySelector('#quranTafsirSelect');
    const onEditionChange = () => {
      this.updateSizeEstimate();
      const translationEdition = transSelect?.value || 'id_kemenag';
      const tafsirEdition = tafsirSelect?.value || 'id_kemenag_ringkas';
      const key = `${translationEdition}|${tafsirEdition}`;
      if (key !== this._loadedEditionsKey) {
        this.preloadAssets();
      }
    };
    if (transSelect) transSelect.addEventListener('change', onEditionChange);
    if (tafsirSelect) tafsirSelect.addEventListener('change', onEditionChange);

    // Preview picker inputs
    const prevSurahSelect = this.querySelector('#previewSurahSelect');
    const prevAyahInput = this.querySelector('#previewAyahInput');
    const prevLayerSelect = this.querySelector('#previewLayerSelect');
    const prevRefreshBtn = this.querySelector('#previewRefreshBtn');

    if (prevSurahSelect && prevAyahInput) {
      prevSurahSelect.addEventListener('change', () => {
        const sNum = parseInt(prevSurahSelect.value, 10);
        const sInfo = SURAHS[sNum - 1] || SURAHS[0];
        prevAyahInput.max = String(sInfo.ayahs);
        if (parseInt(prevAyahInput.value, 10) > sInfo.ayahs) {
          prevAyahInput.value = '1';
        }
        this.renderAyahPreview();
      });
      prevAyahInput.addEventListener('change', () => this.renderAyahPreview());
    }
    if (prevLayerSelect) {
      prevLayerSelect.addEventListener('change', () => this.renderAyahPreview());
    }
    if (prevRefreshBtn) {
      prevRefreshBtn.addEventListener('click', () => this.renderAyahPreview());
    }

    // Script and font size change
    const scriptSelect = this.querySelector('#quranScriptSelect');
    const fontSelect = this.querySelector('#quranFontSizeSelect');
    if (scriptSelect) scriptSelect.addEventListener('change', () => this.renderAyahPreview());
    if (fontSelect) fontSelect.addEventListener('change', () => this.renderAyahPreview());

    // Build buttons
    const uploadBtn = this.querySelector('#quranBuildUploadBtn');
    const downloadBtn = this.querySelector('#quranDownloadBtn');
    if (uploadBtn) {
      uploadBtn.addEventListener('click', () => this.buildAndUpload(true));
    }
    if (downloadBtn) {
      downloadBtn.addEventListener('click', () => this.buildAndUpload(false));
    }

    // Section 2: Cursor selection
    const curSurahSelect = this.querySelector('#cursorSurahSelect');
    const curAyahInput = this.querySelector('#cursorAyahInput');
    const syncCursorBtn = this.querySelector('#quranSyncCursorBtn');

    if (curSurahSelect && curAyahInput) {
      curSurahSelect.addEventListener('change', () => {
        const sNum = parseInt(curSurahSelect.value, 10);
        const sInfo = SURAHS[sNum - 1] || SURAHS[0];
        curAyahInput.max = String(sInfo.ayahs);
        if (parseInt(curAyahInput.value, 10) > sInfo.ayahs) {
          curAyahInput.value = '1';
        }
      });
    }

    if (syncCursorBtn) {
      syncCursorBtn.addEventListener('click', () => {
        const surah = parseInt(curSurahSelect?.value || '1', 10);
        const ayah = parseInt(curAyahInput?.value || '1', 10);
        const layer = parseInt(this.querySelector('#cursorLayerSelect')?.value || '0', 10);
        this.dispatchCursorSync(surah, ayah, layer);
      });
    }

    // Sunnah Quick-Presets
    const presetBtns = this.querySelectorAll('.quran-preset-btn');
    presetBtns.forEach((btn) => {
      btn.addEventListener('click', () => {
        const surah = parseInt(btn.getAttribute('data-surah'), 10);
        const ayah = parseInt(btn.getAttribute('data-ayah'), 10);
        const layer = parseInt(this.querySelector('#cursorLayerSelect')?.value || '0', 10);

        if (curSurahSelect) curSurahSelect.value = String(surah);
        if (curAyahInput) {
          const sInfo = SURAHS[surah - 1] || SURAHS[0];
          curAyahInput.max = String(sInfo.ayahs);
          curAyahInput.value = String(ayah);
        }

        this.dispatchCursorSync(surah, ayah, layer);
      });
    });

    // Section 3: Add Bookmark
    const bmSurahSelect = this.querySelector('#bmSurahSelect');
    const bmAyahInput = this.querySelector('#bmAyahInput');
    const addBmBtn = this.querySelector('#quranAddBookmarkBtn');

    if (bmSurahSelect && bmAyahInput) {
      bmSurahSelect.addEventListener('change', () => {
        const sNum = parseInt(bmSurahSelect.value, 10);
        const sInfo = SURAHS[sNum - 1] || SURAHS[0];
        bmAyahInput.max = String(sInfo.ayahs);
        if (parseInt(bmAyahInput.value, 10) > sInfo.ayahs) {
          bmAyahInput.value = '1';
        }
      });
    }

    if (addBmBtn) {
      addBmBtn.addEventListener('click', () => {
        const bookmarks = [...(this._deviceState.bookmarks || [])];
        if (bookmarks.length >= 8) {
          alert('Maximum 8 bookmark slots supported by device hardware.');
          return;
        }

        const surah = parseInt(bmSurahSelect?.value || '1', 10);
        const ayah = parseInt(bmAyahInput?.value || '1', 10);
        const juz = getJuzForAyah(surah, ayah);

        bookmarks.push({ surah, ayah, juz });
        this.dispatchEvent(new CustomEvent('quran-set-bookmarks', {
          detail: { bookmarks },
          bubbles: true,
          composed: true
        }));
      });
    }

    // Bookmarks Jump / Delete delegation
    const bmList = this.querySelector('#quranBookmarksList');
    if (bmList) {
      bmList.addEventListener('click', (e) => {
        const jumpBtn = e.target.closest('.quran-bm-jump-btn');
        if (jumpBtn) {
          const idx = parseInt(jumpBtn.getAttribute('data-index'), 10);
          const bm = this._deviceState.bookmarks[idx];
          if (bm) {
            const layer = parseInt(this.querySelector('#cursorLayerSelect')?.value || '0', 10);
            this.dispatchCursorSync(bm.surah, bm.ayah, layer);
          }
          return;
        }

        const delBtn = e.target.closest('.quran-bm-del-btn');
        if (delBtn) {
          const idx = parseInt(delBtn.getAttribute('data-index'), 10);
          const bookmarks = [...(this._deviceState.bookmarks || [])];
          if (idx >= 0 && idx < bookmarks.length) {
            bookmarks.splice(idx, 1);
            this.dispatchEvent(new CustomEvent('quran-set-bookmarks', {
              detail: { bookmarks },
              bubbles: true,
              composed: true
            }));
          }
        }
      });
    }
  }

  dispatchCursorSync(surah, ayah, layer) {
    this.dispatchEvent(new CustomEvent('quran-set-cursor', {
      detail: { surah, ayah, layer },
      bubbles: true,
      composed: true
    }));

    // Compatible alias
    this.dispatchEvent(new CustomEvent('quran-set-tilawah', {
      detail: { surah, ayah, layer },
      bubbles: true,
      composed: true
    }));
  }

  updateSizeEstimate() {
    const rangeType = this.querySelector('#quranRangeSelect')?.value || 'full';
    const customStart = parseInt(this.querySelector('#customStartSurah')?.value || '1', 10);
    const customEnd = parseInt(this.querySelector('#customEndSurah')?.value || '114', 10);
    const latin = Boolean(this.querySelector('#layerLatinCheckbox')?.checked);
    const trans = Boolean(this.querySelector('#layerTranslationCheckbox')?.checked);
    const tafsir = Boolean(this.querySelector('#layerTafsirCheckbox')?.checked);

    let totalAyahs = 6236;
    if (rangeType === 'juz30') {
      totalAyahs = 564;
    } else if (rangeType === 'custom') {
      totalAyahs = 0;
      const start = Math.min(customStart, customEnd);
      const end = Math.max(customStart, customEnd);
      for (let s = start; s <= end; s++) {
        const info = SURAHS[s - 1];
        if (info) totalAyahs += info.ayahs;
      }
    }

    let bytesPerAyah = 180;
    if (latin) bytesPerAyah += 40;
    if (trans) bytesPerAyah += 100;
    if (tafsir) bytesPerAyah += 150;

    const totalBytes = 64 + totalAyahs * bytesPerAyah;
    let formatted = '';
    if (totalBytes < 1024 * 1024) {
      formatted = `Est. Size: ~${Math.round(totalBytes / 1024)} KB`;
    } else {
      formatted = `Est. Size: ~${(totalBytes / (1024 * 1024)).toFixed(1)} MB`;
    }

    const badge = this.querySelector('#quranSizeEstimate');
    if (badge) badge.textContent = formatted;
  }

  async renderAyahPreview() {
    if (!this._assetsReady) return;
    const canvas = this.querySelector('#quranPreviewCanvas');
    if (!canvas) return;

    const surah = parseInt(this.querySelector('#previewSurahSelect')?.value || '1', 10);
    const ayah = parseInt(this.querySelector('#previewAyahInput')?.value || '1', 10);
    const layer = parseInt(this.querySelector('#previewLayerSelect')?.value || '0', 10);
    const scriptStyle = parseInt(this.querySelector('#quranScriptSelect')?.value || '0', 10);
    const mushafFontSize = parseInt(this.querySelector('#quranFontSizeSelect')?.value || '28', 10);
    const translationEdition = this.querySelector('#quranTranslationSelect')?.value || 'id_kemenag';
    const tafsirEdition = this.querySelector('#quranTafsirSelect')?.value || 'id_kemenag_ringkas';

    const previewOptions = {
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
      quranBase: this._cachedQuranBase,
      editionsData: this._cachedEditionsData
    };

    try {
      if (typeof ensureQuranFont === 'function') {
        if (!this._assetsReady) {
          this.setStatusText('Loading Quran calligraphy font (bacain-quran-v1)...');
          await ensureQuranFont(scriptStyle, (msg) => this.setStatusText(msg));
          this.setStatusText('');
        } else {
          await ensureQuranFont(scriptStyle);
        }
      }

      if (typeof renderAyahPreviewToCanvas === 'function') {
        await renderAyahPreviewToCanvas(canvas, previewOptions);
      } else {
        this.drawMonochromePreview(canvas, previewOptions);
      }
    } catch (err) {
      console.warn('Preview render fallback:', err.message);
      this.drawMonochromePreview(canvas, previewOptions, err.message);
      this.setStatusText(`Preview note: ${err.message}`);
    }
  }

  drawMonochromePreview(canvas, options, errorMsg = null) {
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = this._screenW;
    const h = this._screenH;
    canvas.width = w;
    canvas.height = h;

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, w, h);

    // Inverted Header Bar (y = 0..15)
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, w, 16);

    ctx.fillStyle = '#ffffff';
    ctx.font = 'bold 11px monospace';
    ctx.textBaseline = 'middle';
    const surahInfo = SURAHS[options.surah - 1] || { name: 'Al-Fatihah', ayahs: 7 };
    const tag = QURAN_LAYERS[options.layer]?.tag || 'M';
    const headerLeft = `${String(options.surah).padStart(2, '0')}:${String(options.ayah).padStart(3, '0')} ${surahInfo.name}`;
    ctx.fillText(headerLeft.slice(0, Math.floor(w / 8)), 4, 8);

    const headerRight = `[${tag}]`;
    const hrWidth = ctx.measureText(headerRight).width;
    ctx.fillText(headerRight, w - hrWidth - 4, 8);

    // Body text (y = 20 .. h - 20)
    ctx.fillStyle = '#000000';
    ctx.font = '12px system-ui, sans-serif';
    ctx.textBaseline = 'top';

    if (errorMsg) {
      ctx.fillText('1-Bit Preview', 8, 24);
      ctx.font = '10px monospace';
      ctx.fillText(errorMsg.slice(0, Math.floor(w / 7)), 8, 44);
    } else {
      const layerDesc = QURAN_LAYERS[options.layer]?.title || 'Mushaf';
      ctx.fillText(`${surahInfo.name} - Ayah ${options.ayah}`, 8, 24);
      ctx.font = '11px monospace';
      ctx.fillText(`Layer: ${layerDesc}`, 8, 44);
      ctx.fillText(`Script: ${options.scriptStyle === 0 ? 'Madinah Uthmani' : 'IndoPak 15-Line'}`, 8, 60);
      ctx.fillText(`Target: ${w}x${h} px`, 8, 76);
    }

    // Footer bar (y = h - 16 .. h)
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, h - 16, w, 1);
    ctx.font = '10px monospace';
    ctx.textBaseline = 'middle';
    const juz = getJuzForAyah(options.surah, options.ayah);
    ctx.fillText(`Juz ${juz}`, 4, h - 8);
    const footerRight = `${w}x${h}`;
    const frWidth = ctx.measureText(footerRight).width;
    ctx.fillText(footerRight, w - frWidth - 4, h - 8);
  }

  async buildAndUpload(isUpload = true) {
    if (this._isBuilding) return;
    this._isBuilding = true;

    const progressContainer = this.querySelector('#quranBuildProgressContainer');
    const progressBar = this.querySelector('#quranBuildProgressBar');
    const uploadBtn = this.querySelector('#quranBuildUploadBtn');
    const downloadBtn = this.querySelector('#quranDownloadBtn');

    if (progressContainer) progressContainer.style.display = 'block';
    if (progressBar) progressBar.style.width = '0%';
    if (uploadBtn) uploadBtn.disabled = true;
    if (downloadBtn) downloadBtn.disabled = true;

    const scriptStyle = parseInt(this.querySelector('#quranScriptSelect')?.value || '0', 10);
    const mushafFontSize = parseInt(this.querySelector('#quranFontSizeSelect')?.value || '28', 10);
    const rangeType = this.querySelector('#quranRangeSelect')?.value || 'full';
    const customStart = parseInt(this.querySelector('#customStartSurah')?.value || '1', 10);
    const customEnd = parseInt(this.querySelector('#customEndSurah')?.value || '114', 10);

    const layerMushaf = true;
    const layerLatin = Boolean(this.querySelector('#layerLatinCheckbox')?.checked);
    const layerTranslation = Boolean(this.querySelector('#layerTranslationCheckbox')?.checked);
    const layerTafsir = Boolean(this.querySelector('#layerTafsirCheckbox')?.checked);

    const translationEdition = this.querySelector('#quranTranslationSelect')?.value || 'id_kemenag';
    const tafsirEdition = this.querySelector('#quranTafsirSelect')?.value || 'id_kemenag_ringkas';

    const buildOptions = {
      scriptStyle,
      scriptId: scriptStyle,
      mushafFontSize,
      rangeType,
      startSurah: rangeType === 'custom' ? Math.min(customStart, customEnd) : (rangeType === 'juz30' ? 78 : 1),
      endSurah: rangeType === 'custom' ? Math.max(customStart, customEnd) : 114,
      layers: {
        mushaf: layerMushaf,
        latin: layerLatin,
        translation: layerTranslation,
        tafsir: layerTafsir
      },
      translationEdition,
      tafsirEdition,
      displayWidth: this._screenW,
      displayHeight: this._screenH
    };

    try {
      if (typeof buildBqmPackage !== 'function') {
        throw new Error('BQM package compiler (buildBqmPackage) is not available.');
      }

      this.setStatusText('Initializing fonts & corpora in CacheStorage (bacain-quran-v1)...');

      if (typeof ensureQuranFont === 'function') {
        await ensureQuranFont(scriptStyle, (msg) => this.setStatusText(msg));
      }

      if (!this._cachedQuranBase || !this._cachedEditionsData) {
        this.setStatusText('Fetching Quran datasets from upstream CORS endpoints...');
        const datasets = await fetchQuranDatasets(buildOptions, (msg) => this.setStatusText(msg));
        this._cachedQuranBase = datasets.quranBase;
        this._cachedEditionsData = datasets.editionsData;
      }

      buildOptions.quranBase = this._cachedQuranBase;
      buildOptions.editionsData = this._cachedEditionsData;

      this.setStatusText('Compiling Al-Quran BQM package...');

      const bqmArrayBuffer = await buildBqmPackage(buildOptions, (progress) => {
        if (!progress) return;
        const total = progress.total || 100;
        const current = progress.current ?? (progress.percent || 0);
        const pct = Math.max(0, Math.min(100, Math.round((current / total) * 100)));
        if (progressBar) progressBar.style.width = `${pct}%`;
        this.setStatusText(`${progress.stage || 'Building'}: ${pct}%`);
      });

      const file = new File([bqmArrayBuffer], 'quran.bqm', { type: 'application/octet-stream' });

      if (isUpload) {
        this.setStatusText('Queueing quran.bqm for BLE upload...');
        this.dispatchEvent(new CustomEvent('quran-upload-bqm', {
          detail: { file },
          bubbles: true,
          composed: true
        }));
      } else {
        const blobUrl = URL.createObjectURL(file);
        const a = document.createElement('a');
        a.href = blobUrl;
        a.download = 'quran.bqm';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(blobUrl);
        this.setStatusText('quran.bqm downloaded successfully.');
      }
    } catch (err) {
      console.error('[QuranBuilder] Build failed:', err);
      this.setStatusText(`Build failed: ${err.message}`, true);
    } finally {
      this._isBuilding = false;
      if (uploadBtn) uploadBtn.disabled = !this._isConnected || !this._assetsReady;
      if (downloadBtn) downloadBtn.disabled = !this._assetsReady;
    }
  }
}

if (typeof customElements !== 'undefined' && !customElements.get('quran-builder')) {
  customElements.define('quran-builder', QuranBuilder);
}

export { QuranBuilder };
