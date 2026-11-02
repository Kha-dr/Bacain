/**
 * book-library.js
 * Mini E-Reader - Remote Books Library Custom Element (<book-library>)
 * Light DOM Web Component managing the book list, search filtering,
 * storage target badge, subfolder breadcrumb, and deletion triggers.
 */

function formatBytes(bytes) {
  if (isNaN(bytes) || bytes === null || bytes < 0) return '0 B';
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(1))} ${sizes[i]}`;
}

function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

class BookLibrary extends HTMLElement {
  constructor() {
    super();
    this._books = [];
    this._currentPath = '/';
    this._isDeviceConnected = false;
  }

  connectedCallback() {
    if (!this.querySelector('.books-section')) {
      this.render();
    }
    this.bindEvents();
  }

  render() {
    this.innerHTML = `
      <section class="books-section">
        <!-- 1. Header Bar: Title, Storage Target Badge & Books Count -->
        <div class="section-title">
          <div class="section-title-left">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
              <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
            </svg>
            <h2>Books</h2>
            <span id="storageTargetBadge" class="card-badge badge-muted">Storage: —</span>
            <span id="booksCountBadge" class="card-badge badge-muted">0 books</span>
          </div>
        </div>

        <!-- 2. Controls: Search, Refresh Buttons & Subfolder Breadcrumb -->
        <div class="books-controls">
          <div class="search-input-wrapper">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <circle cx="11" cy="11" r="8"></circle>
              <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
            </svg>
            <input type="text" id="bookSearchInput" class="search-input" placeholder="Search books by name...">
          </div>

          <button id="refreshBooksBtn" class="btn-secondary" title="Refresh books and device status">
            <svg id="refreshBooksIcon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
              <polyline points="23 4 23 10 17 10"></polyline>
              <polyline points="1 20 1 14 7 14"></polyline>
              <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
            </svg>
            <span>Refresh</span>
          </button>
        </div>

        <!-- 3. Subfolder / Breadcrumb Path Indicator -->
        <div id="booksBreadcrumb" class="books-breadcrumb" style="display: flex; align-items: center; gap: 0.4rem; margin-bottom: 0.85rem; font-size: 0.8rem; color: var(--text-muted); font-family: var(--font-mono);">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width: 14px; height: 14px; flex-shrink: 0;">
            <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
          </svg>
          <span>Path:</span>
          <span id="booksCurrentPath" style="color: var(--text-primary); font-weight: 500;">/</span>
        </div>

        <!-- 4. Books List Container -->
        <div id="booksListContainer" class="books-list">
          <div class="empty-state">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
              <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
            </svg>
            <p>Connect device to view and manage books.</p>
          </div>
        </div>
      </section>
    `;
  }

  bindEvents() {
    const searchInput = this.querySelector('#bookSearchInput');
    if (searchInput) {
      searchInput.addEventListener('input', () => {
        this.filterAndRender();
        this.dispatchEvent(new CustomEvent('search-change', {
          detail: { query: searchInput.value },
          bubbles: true,
          composed: true
        }));
      });
    }

    const refreshBooksBtn = this.querySelector('#refreshBooksBtn');
    if (refreshBooksBtn) {
      refreshBooksBtn.addEventListener('click', () => {
        this.dispatchEvent(new CustomEvent('refresh-books', {
          bubbles: true,
          composed: true
        }));
      });
    }
  }

  /**
   * Set Storage Target Badge in Header
   * @param {number|string} target - 0 for MicroSD, 1 for Flash, or string
   */
  setStorageTargetBadge(target) {
    const badge = this.querySelector('#storageTargetBadge');
    if (!badge) return;

    if (target === 1 || target === '1' || String(target).toLowerCase().includes('flash')) {
      badge.className = 'card-badge badge-flash';
      badge.textContent = 'Internal Flash';
    } else if (target === 0 || target === '0' || String(target).toLowerCase().includes('sd')) {
      badge.className = 'card-badge badge-sd';
      badge.textContent = 'MicroSD';
    } else {
      badge.className = 'card-badge badge-muted';
      badge.textContent = String(target);
    }
  }

  /**
   * Set device connected state for empty state rendering
   * @param {boolean} connected
   */
  setDeviceConnected(connected) {
    this._isDeviceConnected = !!connected;
    this.filterAndRender();
  }

  /**
   * Render books list
   * @param {Array<{name: string, size: number, progress?: number, path?: string}>} booksArray
   * @param {string} currentPath
   */
  renderBooks(booksArray = [], currentPath = '/') {
    this._books = Array.isArray(booksArray) ? booksArray : [];
    this._currentPath = currentPath || '/';

    const pathEl = this.querySelector('#booksCurrentPath');
    if (pathEl) {
      pathEl.textContent = this._currentPath;
    }

    const countBadge = this.querySelector('#booksCountBadge');
    if (countBadge) {
      const len = this._books.length;
      countBadge.textContent = `${len} ${len === 1 ? 'book' : 'books'}`;
    }

    this.filterAndRender();
  }

  filterAndRender() {
    const container = this.querySelector('#booksListContainer');
    if (!container) return;

    const searchInput = this.querySelector('#bookSearchInput');
    const searchTerm = searchInput ? searchInput.value.trim().toLowerCase() : '';
    const filtered = this._books.filter(b => (b.name || '').toLowerCase().includes(searchTerm));

    container.innerHTML = '';

    if (this._books.length === 0) {
      container.innerHTML = `
        <div class="empty-state">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
            <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
          </svg>
          <p>No books found on active storage. Drop a .txt file below to upload!</p>
        </div>
      `;
      return;
    }

    if (filtered.length === 0) {
      container.innerHTML = `
        <div class="empty-state">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="10"></circle>
            <line x1="8" y1="12" x2="16" y2="12"></line>
          </svg>
          <p>No matching books found for "${escapeHtml(searchTerm)}"</p>
        </div>
      `;
      return;
    }

    filtered.forEach(book => {
      const progress = Number(book.progress) || 0;
      const item = document.createElement('div');
      item.className = 'book-item';

      item.innerHTML = `
        <div class="book-info">
          <div class="book-icon">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"></path>
              <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"></path>
            </svg>
          </div>
          <div class="book-meta">
            <div class="book-name book-title" title="${escapeHtml(book.name)}">${escapeHtml(book.name)}</div>
            <div class="book-details" style="display:flex;align-items:center;gap:0.75rem;margin-top:0.2rem;">
              <span class="book-size">${formatBytes(book.size)}</span>
              <span style="color:var(--text-muted);font-size:0.75rem;">•</span>
              <span class="book-progress" style="font-size:0.78rem;color:${progress > 0 ? 'var(--accent-color)' : 'var(--text-muted)'};font-family:var(--font-mono);">${progress}% read</span>
            </div>
          </div>
        </div>
        <button class="btn-delete-book" title="Delete ${escapeHtml(book.name)}" aria-label="Delete ${escapeHtml(book.name)}" data-book-name="${escapeHtml(book.name)}">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="3 6 5 6 21 6"></polyline>
            <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
            <line x1="10" y1="11" x2="10" y2="17"></line>
            <line x1="14" y1="11" x2="14" y2="17"></line>
          </svg>
        </button>
      `;

      const deleteBtn = item.querySelector('.btn-delete-book');
      if (deleteBtn) {
        deleteBtn.addEventListener('click', (e) => {
          e.stopPropagation();
          this.dispatchEvent(new CustomEvent('delete-book', {
            detail: {
              book,
              name: book.name,
              size: book.size,
              path: book.path || `${this._currentPath}${book.name}`
            },
            bubbles: true,
            composed: true
          }));
        });
      }

      container.appendChild(item);
    });
  }
}

if (!customElements.get('book-library')) {
  customElements.define('book-library', BookLibrary);
}
if (typeof window !== 'undefined') {
  window.BookLibrary = BookLibrary;
}
