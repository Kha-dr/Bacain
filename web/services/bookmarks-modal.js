/**
 * BacaIn Bookmarks & Reading Position Modal Controller
 */

let el = null;
let activeBook = null;
let isInitialized = false;

function ensureElements() {
  if (isInitialized) return el;
  if (typeof document === "undefined") return null;

  el = {
    bookmarksModal: document.getElementById("bookmarksModal"),
    bookmarksModalBookName: document.getElementById("bookmarksModalBookName"),
    bookmarksCurrentPosText: document.getElementById("bookmarksCurrentPosText"),
    bookmarksList: document.getElementById("bookmarksList"),
    bookmarkPositionPctInput: document.getElementById("bookmarkPositionPctInput"),
    bookmarkCurrentPosBtn: document.getElementById("bookmarkCurrentPosBtn"),
    jumpToPositionBtn: document.getElementById("jumpToPositionBtn"),
    addBookmarkAtPctBtn: document.getElementById("addBookmarkAtPctBtn"),
    closeBookmarksModalBtn: document.getElementById("closeBookmarksModalBtn"),
    dismissBookmarksModalBtn: document.getElementById("dismissBookmarksModalBtn"),
  };

  wireEvents();
  isInitialized = true;
  return el;
}

function wireEvents() {
  if (!el) return;

  if (el.closeBookmarksModalBtn) {
    el.closeBookmarksModalBtn.addEventListener("click", close);
  }
  if (el.dismissBookmarksModalBtn) {
    el.dismissBookmarksModalBtn.addEventListener("click", close);
  }
  if (el.bookmarksModal) {
    el.bookmarksModal.addEventListener("click", (e) => {
      if (e.target === el.bookmarksModal) {
        close();
      }
    });
  }

  if (el.bookmarkCurrentPosBtn) {
    el.bookmarkCurrentPosBtn.addEventListener("click", async () => {
      if (!activeBook) return;
      try {
        await window.BleClient.addBookBookmark(
          activeBook.name,
          activeBook.byteOffset || 0,
        );
        if (typeof window.showToast === "function") {
          window.showToast("Bookmarked current reading position", "success");
        }
        await refresh();
      } catch (err) {
        console.error("[BookmarksModal] Add bookmark failed:", err);
        if (typeof window.showToast === "function") {
          window.showToast(`Add bookmark failed: ${err.message}`, "error");
        }
      }
    });
  }

  if (el.jumpToPositionBtn) {
    el.jumpToPositionBtn.addEventListener("click", async () => {
      if (!activeBook) return;
      const pct = Math.max(
        0,
        Math.min(100, Number(el.bookmarkPositionPctInput?.value) || 0),
      );
      const targetOffset = Math.floor(((activeBook.size || 0) * pct) / 100);
      try {
        await window.BleClient.setBookReadingOffset(activeBook.name, targetOffset);
        if (typeof window.showToast === "function") {
          window.showToast(`Set reading position to ${pct}%`, "success");
        }
        await refresh();
      } catch (err) {
        console.error("[BookmarksModal] Set reading position failed:", err);
        if (typeof window.showToast === "function") {
          window.showToast(`Failed to set position: ${err.message}`, "error");
        }
      }
    });
  }

  if (el.addBookmarkAtPctBtn) {
    el.addBookmarkAtPctBtn.addEventListener("click", async () => {
      if (!activeBook) return;
      const pct = Math.max(
        0,
        Math.min(100, Number(el.bookmarkPositionPctInput?.value) || 0),
      );
      const targetOffset = Math.floor(((activeBook.size || 0) * pct) / 100);
      try {
        await window.BleClient.addBookBookmark(activeBook.name, targetOffset);
        if (typeof window.showToast === "function") {
          window.showToast(`Added bookmark at ${pct}%`, "success");
        }
        await refresh();
      } catch (err) {
        console.error("[BookmarksModal] Add bookmark failed:", err);
        if (typeof window.showToast === "function") {
          window.showToast(`Add bookmark failed: ${err.message}`, "error");
        }
      }
    });
  }

  if (typeof document !== "undefined") {
    document.addEventListener("keydown", (e) => {
      if (
        e.key === "Escape" &&
        el.bookmarksModal &&
        el.bookmarksModal.classList.contains("active")
      ) {
        close();
      }
    });
  }
}

function syncLocalBookMetadata(filename, byteOffset, totalBytes, bookmarkCount) {
  const size = totalBytes || 0;
  const progress = size > 0 ? Math.min(100, Math.floor((byteOffset * 100) / size)) : 0;

  try {
    if (byteOffset > 0) {
      localStorage.setItem(`bacain_progress_${filename}`, String(progress));
    } else {
      localStorage.removeItem(`bacain_progress_${filename}`);
    }
  } catch (storageErr) {
    console.warn(`[BookmarksModal] Failed to update localStorage: ${storageErr.message}`);
  }

  if (typeof window !== "undefined") {
    window.dispatchEvent(
      new CustomEvent("book-metadata-synced", {
        detail: { filename, byteOffset, totalBytes, bookmarkCount, progress },
      }),
    );
  }
}

function renderState(filename, state) {
  if (!el) return;

  const totalBytes = Number(state.totalBytes) || 0;
  const byteOffset = Number(state.byteOffset) || 0;
  const bookmarks = Array.isArray(state.bookmarks) ? state.bookmarks : [];
  const pct =
    totalBytes > 0
      ? Math.min(100, Math.floor((byteOffset * 100) / totalBytes))
      : 0;

  if (el.bookmarksModalBookName) {
    el.bookmarksModalBookName.textContent = filename;
  }
  if (el.bookmarksCurrentPosText) {
    if (byteOffset > 0) {
      el.bookmarksCurrentPosText.textContent = `Position: ${pct}% (Byte ${byteOffset.toLocaleString()} / ${totalBytes.toLocaleString()})`;
    } else {
      el.bookmarksCurrentPosText.textContent = `Not started (0 / ${totalBytes.toLocaleString()} B)`;
    }
  }
  if (el.bookmarkPositionPctInput) {
    el.bookmarkPositionPctInput.value = String(pct);
  }

  if (!el.bookmarksList) return;
  el.bookmarksList.innerHTML = "";

  if (bookmarks.length === 0) {
    el.bookmarksList.innerHTML = `<p style="font-size: 0.85rem; color: var(--text-muted); text-align: center; padding: 1rem 0;">No bookmarks saved for this book</p>`;
    return;
  }

  bookmarks.forEach((bmOffset, idx) => {
    const bmPct =
      totalBytes > 0
        ? Math.min(100, Math.floor((bmOffset * 100) / totalBytes))
        : 0;
    const row = document.createElement("div");
    row.className = "controls-item";
    row.style.display = "flex";
    row.style.alignItems = "center";
    row.style.justifyContent = "space-between";
    row.style.gap = "0.5rem";

    const label = document.createElement("div");
    label.style.fontFamily = "var(--font-mono)";
    label.style.fontSize = "0.82rem";
    label.textContent = `#${idx + 1} • ${bmPct}% (${bmOffset.toLocaleString()} B)`;

    const btnGroup = document.createElement("div");
    btnGroup.style.display = "flex";
    btnGroup.style.alignItems = "center";
    btnGroup.style.gap = "0.35rem";

    const jumpBtn = document.createElement("button");
    jumpBtn.className = "btn-secondary btn-jump-bookmark";
    jumpBtn.style.fontSize = "0.76rem";
    jumpBtn.style.padding = "0.28rem 0.55rem";
    jumpBtn.textContent = "Jump";
    jumpBtn.addEventListener("click", async () => {
      try {
        await window.BleClient.setBookReadingOffset(filename, bmOffset);
        if (typeof window.showToast === "function") {
          window.showToast(`Jumped "${filename}" to ${bmPct}%`, "success");
        }
        await refresh();
      } catch (err) {
        console.error("[BookmarksModal] Jump to bookmark failed:", err);
        if (typeof window.showToast === "function") {
          window.showToast(`Jump failed: ${err.message}`, "error");
        }
      }
    });

    const delBtn = document.createElement("button");
    delBtn.className = "btn-danger btn-delete-bookmark";
    delBtn.style.fontSize = "0.76rem";
    delBtn.style.padding = "0.28rem 0.55rem";
    delBtn.textContent = "Delete";
    delBtn.addEventListener("click", async () => {
      try {
        await window.BleClient.deleteBookBookmark(filename, bmOffset);
        if (typeof window.showToast === "function") {
          window.showToast(`Removed bookmark #${idx + 1}`, "info");
        }
        await refresh();
      } catch (err) {
        console.error("[BookmarksModal] Delete bookmark failed:", err);
        if (typeof window.showToast === "function") {
          window.showToast(`Delete bookmark failed: ${err.message}`, "error");
        }
      }
    });

    btnGroup.appendChild(jumpBtn);
    btnGroup.appendChild(delBtn);
    row.appendChild(label);
    row.appendChild(btnGroup);
    el.bookmarksList.appendChild(row);
  });
}

async function refresh() {
  ensureElements();
  if (!activeBook) return null;
  if (typeof window === "undefined" || !window.BleClient) {
    throw new Error("BleClient is not available");
  }

  const { name, size } = activeBook;
  const state = await window.BleClient.queryBookBookmarks(name);

  activeBook.byteOffset = state.byteOffset;
  activeBook.size = state.totalBytes || size;

  syncLocalBookMetadata(
    name,
    state.byteOffset,
    state.totalBytes || size,
    state.bookmarkCount,
  );

  renderState(name, state);
  return state;
}

async function open(book) {
  ensureElements();
  if (!el || !el.bookmarksModal) {
    throw new Error("Bookmarks modal elements not found in DOM");
  }

  if (typeof window !== "undefined" && window.BleClient && !window.BleClient.isConnected()) {
    if (typeof window.showToast === "function") {
      window.showToast("Connect to BacaIn to manage bookmarks", "warning");
    }
    return;
  }

  const name = typeof book === "string" ? book : book?.name;
  const size = (typeof book === "object" && book ? book.size : 0) || 0;
  const byteOffset = (typeof book === "object" && book ? book.byteOffset : 0) || 0;

  if (!name) {
    throw new Error("Book name is required to open bookmarks modal");
  }

  activeBook = { name, size, byteOffset };

  if (el.bookmarksModalBookName) {
    el.bookmarksModalBookName.textContent = name;
  }
  if (el.bookmarksCurrentPosText) {
    el.bookmarksCurrentPosText.textContent = "Loading device state...";
  }
  if (el.bookmarksList) {
    el.bookmarksList.innerHTML = `<p style="font-size: 0.85rem; color: var(--text-muted); text-align: center; padding: 1rem 0;">Loading bookmarks...</p>`;
  }

  el.bookmarksModal.classList.add("active");
  el.bookmarksModal.setAttribute("aria-hidden", "false");

  try {
    await refresh();
  } catch (err) {
    console.error(`[BookmarksModal] Error loading bookmarks for "${name}":`, err);
    if (typeof window !== "undefined" && typeof window.showToast === "function") {
      window.showToast(`Failed to load bookmarks: ${err.message}`, "error");
    }
    throw err;
  }
}

function close() {
  ensureElements();
  activeBook = null;
  if (el && el.bookmarksModal) {
    el.bookmarksModal.classList.remove("active");
    el.bookmarksModal.setAttribute("aria-hidden", "true");
  }
}

if (typeof document !== "undefined") {
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", ensureElements);
  } else {
    ensureElements();
  }
}

const BookmarksModal = {
  open,
  close,
  refresh,
};

if (typeof window !== "undefined") {
  window.BookmarksModal = BookmarksModal;
}
if (typeof module !== "undefined" && module.exports) {
  module.exports = { BookmarksModal };
}
