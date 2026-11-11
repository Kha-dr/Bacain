/**
 * book-uploader.js
 * BacaIn PWA - Book Uploader Custom Element (<book-uploader>)
 * Light DOM Web Component providing drag-and-drop book upload zone
 * with native EPUB/FB2-to-TXT conversion pipeline and active transfer progress telemetry.
 */

class BookUploader extends HTMLElement {
  constructor() {
    super();
    this._isUploading = false;
  }

  connectedCallback() {
    if (!this.querySelector('.upload-section')) {
      this.render();
    }
    this.bindEvents();
  }

  render() {
    this.innerHTML = `
      <section class="upload-section">
        <div class="section-title">
          <div class="section-title-left">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
              <polyline points="17 8 12 3 7 8"></polyline>
              <line x1="12" y1="3" x2="12" y2="15"></line>
            </svg>
            <h2>Upload Book</h2>
          </div>
        </div>

        <!-- 1. Drag & Drop Zone -->
        <div id="dropzone" class="dropzone" tabindex="0" role="button" aria-label="Drop files here or click to browse">
          <svg class="dropzone-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
            <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
            <line x1="12" y1="7" x2="12" y2="13"></line>
            <polyline points="9 10 12 7 15 10"></polyline>
          </svg>
          <div class="dropzone-title">Drop books (.txt, .epub, .fb2) here, or click to browse</div>
          <div class="dropzone-hint">Supports UTF-8 Text (.txt), EPUB (.epub), and FictionBook (.fb2)</div>
          <input type="file" id="fileInput" class="file-input" accept=".txt,.epub,.fb2,text/plain" multiple>
        </div>

        <!-- Conversion / Upload Error Message Banner -->
        <div id="uploaderError" class="alert-banner alert-danger" style="display: none; margin-top: 0.75rem;">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="10"></circle>
            <line x1="12" y1="8" x2="12" y2="12"></line>
            <line x1="12" y1="16" x2="12.01" y2="16"></line>
          </svg>
          <span id="uploaderErrorText"></span>
        </div>

        <!-- 2. Active Upload Card & Transfer Progress -->
        <div id="uploadActiveCard" class="upload-active-card" style="display: none;">
          <div class="upload-header">
            <div id="uploadingFileName" class="upload-filename">uploading_book.txt</div>
            <button id="cancelUploadBtn" class="btn-cancel-upload" type="button">Cancel Upload</button>
          </div>

          <div id="uploadProgressContainer" class="upload-progress-container">
            <div id="uploadProgressBar" class="upload-progress-fill" style="width: 0%;"></div>
          </div>

          <div id="uploadStatusText" class="upload-status-text" style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 0.5rem; display: none;"></div>
          <div id="uploadMetricsText" class="upload-metrics-text" style="font-size: 0.75rem; color: var(--text-muted); font-family: var(--font-mono); margin-bottom: 0.65rem; display: none;"></div>

          <div class="upload-stats-grid">
            <div class="upload-stat-item">
              <div class="upload-stat-label">Progress</div>
              <div id="uploadPctStat" class="upload-stat-val">0.0%</div>
            </div>
            <div class="upload-stat-item">
              <div class="upload-stat-label">Transfer Speed</div>
              <div id="uploadSpeedStat" class="upload-stat-val">0.0 KB/s</div>
            </div>
            <div class="upload-stat-item">
              <div class="upload-stat-label">Sequence ID</div>
              <div id="uploadSeqStat" class="upload-stat-val">#0 / 0</div>
            </div>
            <div class="upload-stat-item">
              <div class="upload-stat-label">Estimated Time</div>
              <div id="uploadEtaStat" class="upload-stat-val">—</div>
            </div>
          </div>
        </div>
      </section>
    `;
  }

  bindEvents() {
    const dropzone = this.querySelector('#dropzone');
    const fileInput = this.querySelector('#fileInput');
    const cancelBtn = this.querySelector('#cancelUploadBtn');

    if (dropzone && fileInput) {
      dropzone.addEventListener('click', () => {
        fileInput.click();
      });

      dropzone.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          fileInput.click();
        }
      });

      fileInput.addEventListener('change', async () => {
        if (fileInput.files && fileInput.files.length > 0) {
          const files = Array.from(fileInput.files);
          fileInput.value = '';
          await this.handleFileSelect(files);
        }
      });

      // Drag and Drop Events
      ['dragenter', 'dragover'].forEach(eventName => {
        dropzone.addEventListener(eventName, (e) => {
          e.preventDefault();
          e.stopPropagation();
          dropzone.classList.add('dragover');
        });
      });

      ['dragleave', 'dragend'].forEach(eventName => {
        dropzone.addEventListener(eventName, (e) => {
          e.preventDefault();
          e.stopPropagation();
          dropzone.classList.remove('dragover');
        });
      });

      dropzone.addEventListener('drop', async (e) => {
        e.preventDefault();
        e.stopPropagation();
        dropzone.classList.remove('dragover');

        if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length > 0) {
          const files = Array.from(e.dataTransfer.files);
          await this.handleFileSelect(files);
        }
      });
    }

    if (cancelBtn) {
      cancelBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        this.dispatchEvent(new CustomEvent('cancel-upload', {
          bubbles: true,
          composed: true
        }));
      });
    }
  }

  /**
   * Process selected or dropped file(s), converting .epub and .fb2 to plain text if necessary.
   * Dispatches 'file-select' event with array of clean .txt File objects.
   * @param {File|File[]|FileList} fileOrFiles
   */
  async handleFileSelect(fileOrFiles) {
    if (!fileOrFiles) return;

    const files = Array.isArray(fileOrFiles)
      ? fileOrFiles
      : (typeof FileList !== 'undefined' && fileOrFiles instanceof FileList)
        ? Array.from(fileOrFiles)
        : [fileOrFiles];

    if (files.length === 0) return;
    this.clearError();

    const convertedFiles = [];

    for (const file of files) {
      if (!file || !file.name) continue;

      const lowerName = file.name.toLowerCase();
      const isTxt = lowerName.endsWith('.txt') || file.type === 'text/plain';
      const isEpub = lowerName.endsWith('.epub');
      const isFb2 = lowerName.endsWith('.fb2');

      if (!isTxt && !isEpub && !isFb2) {
        this.reset();
        this.showError(`Unsupported file format: "${file.name}". Only .txt, .epub, and .fb2 files are supported.`);
        return;
      }

      if (isEpub) {
        try {
          this.showProgress(file.name);
          this.setProgress({
            fileName: file.name,
            statusText: 'Converting EPUB to plain text with chapter breaks...',
            pct: 0
          });

          const arrayBuffer = typeof file.arrayBuffer === 'function'
            ? await file.arrayBuffer()
            : (file.buffer || file);

          const converter = (typeof window !== 'undefined' && typeof window.convertEpubToTxt === 'function')
            ? window.convertEpubToTxt
            : (typeof convertEpubToTxt === 'function' ? convertEpubToTxt : null);

          if (!converter) {
            throw new Error('EPUB converter (window.convertEpubToTxt) is not loaded.');
          }

          const cleanTxt = await converter(arrayBuffer);
          const baseName = file.name.replace(/\.epub$/i, '');
          const convertedFile = new File([cleanTxt], `${baseName}.txt`, { type: 'text/plain' });
          convertedFiles.push(convertedFile);
        } catch (err) {
          console.error(`EPUB conversion failed for "${file.name}":`, err);
          this.reset();
          this.showError(`Failed to convert "${file.name}": ${err.message || err}`);
          return;
        } finally {
          this.hideProgress();
        }
      } else if (isFb2) {
        try {
          this.showProgress(file.name);
          this.setProgress({
            fileName: file.name,
            statusText: 'Converting FB2 to plain text with chapter breaks...',
            pct: 0
          });

          const arrayBuffer = typeof file.arrayBuffer === 'function'
            ? await file.arrayBuffer()
            : (file.buffer || file);

          const converter = (typeof window !== 'undefined' && typeof window.convertFb2ToTxt === 'function')
            ? window.convertFb2ToTxt
            : (typeof convertFb2ToTxt === 'function' ? convertFb2ToTxt : null);

          if (!converter) {
            throw new Error('FB2 converter (window.convertFb2ToTxt) is not loaded.');
          }

          const cleanTxt = await converter(arrayBuffer);
          const baseName = file.name.replace(/\.fb2$/i, '');
          const convertedFile = new File([cleanTxt], `${baseName}.txt`, { type: 'text/plain' });
          convertedFiles.push(convertedFile);
        } catch (err) {
          console.error(`FB2 conversion failed for "${file.name}":`, err);
          this.reset();
          this.showError(`Failed to convert "${file.name}": ${err.message || err}`);
          return;
        } finally {
          this.hideProgress();
        }
      } else if (isTxt) {
        try {
          const rawText = typeof file.text === 'function' ? await file.text() : await new Response(file).text();
          const cleaner = (typeof window !== 'undefined' && typeof window.cleanPlainText === 'function')
            ? window.cleanPlainText
            : (typeof cleanPlainText === 'function' ? cleanPlainText : null);
          const cleanTxt = cleaner ? cleaner(rawText) : rawText;
          const convertedFile = new File([cleanTxt], file.name, { type: 'text/plain' });
          convertedFiles.push(convertedFile);
        } catch (err) {
          console.warn(`Text cleaner fallback for "${file.name}":`, err);
          convertedFiles.push(file);
        }
      }
    }

    if (convertedFiles.length > 0) {
      this.dispatchEvent(new CustomEvent('file-select', {
        detail: { files: convertedFiles },
        bubbles: true,
        composed: true
      }));
    }
  }

  /**
   * Display an error message banner on the uploader UI
   * @param {string} message
   */
  showError(message) {
    const errorBanner = this.querySelector('#uploaderError');
    const errorText = this.querySelector('#uploaderErrorText');
    if (errorBanner && errorText) {
      errorText.textContent = message;
      errorBanner.style.display = 'flex';
    }
    if (typeof window !== 'undefined' && typeof window.showToast === 'function') {
      window.showToast(message, 'error');
    }
  }

  /**
   * Clear error message banner
   */
  clearError() {
    const errorBanner = this.querySelector('#uploaderError');
    const errorText = this.querySelector('#uploaderErrorText');
    if (errorBanner) {
      errorBanner.style.display = 'none';
    }
    if (errorText) {
      errorText.textContent = '';
    }
  }

  /**
   * Show transfer progress card
   * @param {string} fileName
   */
  showProgress(fileName = 'uploading_book.txt') {
    this._isUploading = true;
    const card = this.querySelector('#uploadActiveCard');
    const nameEl = this.querySelector('#uploadingFileName');
    if (card) {
      card.style.display = 'block';
      card.classList.add('visible');
    }
    if (nameEl) {
      nameEl.textContent = fileName;
    }
  }

  /**
   * Hide transfer progress card
   */
  hideProgress() {
    this._isUploading = false;
    const card = this.querySelector('#uploadActiveCard');
    if (card) {
      card.style.display = 'none';
      card.classList.remove('visible');
    }
  }

  /**
   * Update transfer telemetry
   * @param {{ pct?: number, speedKbps?: number, currentSeq?: number, totalSeq?: number, etaSec?: number, fileName?: string, statusText?: string, metricsText?: string }} telemetry
   */
  setProgress({ pct, speedKbps, currentSeq, totalSeq, etaSec, fileName, statusText, metricsText } = {}) {
    if (!this._isUploading) {
      this.showProgress(fileName);
    }

    if (fileName) {
      const nameEl = this.querySelector('#uploadingFileName');
      if (nameEl) nameEl.textContent = fileName;
    }

    const clampedPct = Math.min(100, Math.max(0, Number(pct) || 0));
    const progressBar = this.querySelector('#uploadProgressBar');
    if (progressBar) {
      progressBar.style.width = `${clampedPct.toFixed(1)}%`;
    }

    const pctStat = this.querySelector('#uploadPctStat');
    if (pctStat) {
      pctStat.textContent = `${clampedPct.toFixed(1)}%`;
    }

    if (speedKbps !== undefined) {
      const speedStat = this.querySelector('#uploadSpeedStat');
      if (speedStat) {
        speedStat.textContent = `${(Number(speedKbps) || 0).toFixed(1)} KB/s`;
      }
    }

    if (currentSeq !== undefined && totalSeq !== undefined) {
      const seqStat = this.querySelector('#uploadSeqStat');
      if (seqStat) {
        seqStat.textContent = `#${currentSeq} / ${totalSeq}`;
      }
    }

    if (etaSec !== undefined) {
      const etaStat = this.querySelector('#uploadEtaStat');
      if (etaStat) {
        if (etaSec < 0 || isNaN(etaSec) || !isFinite(etaSec)) {
          etaStat.textContent = '—';
        } else if (etaSec < 60) {
          etaStat.textContent = `${Math.ceil(etaSec)}s`;
        } else {
          const mins = Math.floor(etaSec / 60);
          const secs = Math.ceil(etaSec % 60);
          etaStat.textContent = `${mins}m ${secs}s`;
        }
      }
    }

    const statusEl = this.querySelector('#uploadStatusText');
    if (statusEl) {
      if (statusText) {
        statusEl.textContent = statusText;
        statusEl.style.display = 'block';
      } else {
        statusEl.style.display = 'none';
      }
    }

    const metricsEl = this.querySelector('#uploadMetricsText');
    if (metricsEl) {
      if (metricsText) {
        metricsEl.textContent = metricsText;
        metricsEl.style.display = 'block';
      } else {
        metricsEl.style.display = 'none';
      }
    }
  }

  /**
   * Reset uploader states and file inputs
   */
  reset() {
    this.hideProgress();
    this.clearError();
    const fileInput = this.querySelector('#fileInput');
    if (fileInput) fileInput.value = '';

    const progressBar = this.querySelector('#uploadProgressBar');
    if (progressBar) progressBar.style.width = '0%';

    const pctStat = this.querySelector('#uploadPctStat');
    if (pctStat) pctStat.textContent = '0.0%';

    const speedStat = this.querySelector('#uploadSpeedStat');
    if (speedStat) speedStat.textContent = '0.0 KB/s';

    const seqStat = this.querySelector('#uploadSeqStat');
    if (seqStat) seqStat.textContent = '#0 / 0';

    const etaStat = this.querySelector('#uploadEtaStat');
    if (etaStat) etaStat.textContent = '—';
  }
}

if (typeof customElements !== 'undefined' && !customElements.get('book-uploader')) {
  customElements.define('book-uploader', BookUploader);
}
if (typeof window !== 'undefined') {
  window.BookUploader = BookUploader;
}
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { BookUploader };
}
