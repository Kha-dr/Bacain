/**
 * epub-processor.js
 * BacaIn E-Book Ingestion Pipeline - Zero-Dependency Native EPUB to TXT Converter
 *
 * Extracts ZIP structure using native DecompressionStream('deflate-raw'),
 * parses META-INF/container.xml and OPF spine/manifest using DOMParser,
 * extracts chapter headings and paragraphs, and inserts e-paper hardware
 * page-breaks (\f) before running cleanPlainText().
 */

function getCleanPlainTextFn() {
  if (typeof cleanPlainText === 'function') {
    return cleanPlainText;
  }
  if (typeof window !== 'undefined' && typeof window.cleanPlainText === 'function') {
    return window.cleanPlainText;
  }
  if (typeof require !== 'undefined') {
    try {
      const cleaner = require('./text-cleaner.js');
      if (typeof cleaner.cleanPlainText === 'function') {
        return cleaner.cleanPlainText;
      }
    } catch (e) {}
  }
  return function(text) { return text; };
}

function getDOMParserInstance() {
  if (typeof DOMParser !== 'undefined') {
    return new DOMParser();
  }
  if (typeof globalThis !== 'undefined' && typeof globalThis.DOMParser !== 'undefined') {
    return new globalThis.DOMParser();
  }
  throw new Error('DOMParser tidak tersedia pada lingkungan ini.');
}

async function streamToUint8Array(readableStream) {
  if (typeof Response !== 'undefined') {
    try {
      const res = new Response(readableStream);
      const ab = await res.arrayBuffer();
      return new Uint8Array(ab);
    } catch (e) {
      // fallback to stream reader if Response failed
    }
  }

  const reader = readableStream.getReader();
  const chunks = [];
  let totalLength = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) {
      chunks.push(value);
      totalLength += value.length;
    }
  }

  const combined = new Uint8Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) {
    combined.set(chunk, offset);
    offset += chunk.length;
  }
  return combined;
}

async function decompressDeflateRaw(compressedBytes) {
  if (compressedBytes.length === 0) {
    return new Uint8Array(0);
  }

  if (typeof DecompressionStream !== 'undefined') {
    const ds = new DecompressionStream('deflate-raw');
    const writer = ds.writable.getWriter();
    writer.write(compressedBytes);
    writer.close();
    return await streamToUint8Array(ds.readable);
  }

  // Node.js zlib fallback for whitebox automated testing environments
  if (typeof require !== 'undefined') {
    try {
      const zlib = require('zlib');
      return new Uint8Array(zlib.inflateRawSync(compressedBytes));
    } catch (e) {
      // pass
    }
  }

  throw new Error('DecompressionStream (deflate-raw) tidak didukung pada lingkungan runtime ini.');
}

function normalizeZipPath(p) {
  if (!p) return '';
  return p.replace(/\\/g, '/').replace(/^\.\//, '').replace(/^\/+/, '');
}

function findZipEntry(entries, targetPath) {
  const norm = normalizeZipPath(targetPath);
  if (entries.has(norm)) return entries.get(norm);
  const lower = norm.toLowerCase();
  for (const [key, val] of entries.entries()) {
    if (normalizeZipPath(key).toLowerCase() === lower) {
      return val;
    }
  }
  return null;
}

function resolveRelativePath(baseDir, relativePath) {
  relativePath = relativePath.split('#')[0].split('?')[0];
  if (relativePath.startsWith('/')) {
    return normalizeZipPath(relativePath);
  }
  const combined = (baseDir ? baseDir + '/' : '') + relativePath;
  const parts = combined.replace(/\\/g, '/').split('/');
  const resolved = [];
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i];
    if (!p || p === '.') continue;
    if (p === '..') {
      if (resolved.length > 0) resolved.pop();
    } else {
      resolved.push(p);
    }
  }
  return resolved.join('/');
}

async function extractZipEntries(uint8Array) {
  const view = new DataView(uint8Array.buffer, uint8Array.byteOffset, uint8Array.byteLength);
  const entries = new Map();
  const decoder = new TextDecoder('utf-8');

  // Search for End of Central Directory (EOCD) signature: 0x06054b50
  let eocdOffset = -1;
  const maxSearch = Math.min(uint8Array.length, 65557 + 22);
  for (let i = uint8Array.length - 22; i >= uint8Array.length - maxSearch; i--) {
    if (view.getUint32(i, true) === 0x06054b50) {
      eocdOffset = i;
      break;
    }
  }

  if (eocdOffset !== -1) {
    const totalEntries = view.getUint16(eocdOffset + 10, true);
    const cdSize = view.getUint32(eocdOffset + 12, true);
    const cdOffset = view.getUint32(eocdOffset + 16, true);

    let cdCursor = cdOffset;
    for (let i = 0; i < totalEntries && cdCursor < cdOffset + cdSize; i++) {
      if (view.getUint32(cdCursor, true) !== 0x02014b50) {
        break;
      }
      const compressionMethod = view.getUint16(cdCursor + 10, true);
      const compressedSize = view.getUint32(cdCursor + 20, true);
      const fileNameLen = view.getUint16(cdCursor + 28, true);
      const extraLen = view.getUint16(cdCursor + 30, true);
      const commentLen = view.getUint16(cdCursor + 32, true);
      const localHeaderOffset = view.getUint32(cdCursor + 42, true);

      const fileNameBytes = uint8Array.subarray(cdCursor + 46, cdCursor + 46 + fileNameLen);
      const fileName = decoder.decode(fileNameBytes);

      if (view.getUint32(localHeaderOffset, true) === 0x04034b50) {
        const localNameLen = view.getUint16(localHeaderOffset + 26, true);
        const localExtraLen = view.getUint16(localHeaderOffset + 28, true);
        const dataStart = localHeaderOffset + 30 + localNameLen + localExtraLen;
        const compressedData = uint8Array.subarray(dataStart, dataStart + compressedSize);

        if (compressionMethod === 0) {
          // STORED
          entries.set(normalizeZipPath(fileName), compressedData);
        } else if (compressionMethod === 8) {
          // DEFLATED
          const decompressed = await decompressDeflateRaw(compressedData);
          entries.set(normalizeZipPath(fileName), decompressed);
        }
      }

      cdCursor += 46 + fileNameLen + extraLen + commentLen;
    }
  } else {
    // Fallback: sequential scan of local file headers
    let cursor = 0;
    while (cursor + 30 <= uint8Array.length) {
      if (view.getUint32(cursor, true) !== 0x04034b50) {
        cursor++;
        continue;
      }
      const flag = view.getUint16(cursor + 6, true);
      const method = view.getUint16(cursor + 8, true);
      const compSize = view.getUint32(cursor + 18, true);
      const nameLen = view.getUint16(cursor + 26, true);
      const extraLen = view.getUint16(cursor + 28, true);

      const fileNameBytes = uint8Array.subarray(cursor + 30, cursor + 30 + nameLen);
      const fileName = decoder.decode(fileNameBytes);
      const dataStart = cursor + 30 + nameLen + extraLen;

      if ((flag & 0x08) === 0 && compSize > 0) {
        const compressedData = uint8Array.subarray(dataStart, dataStart + compSize);
        if (method === 0) {
          entries.set(normalizeZipPath(fileName), compressedData);
        } else if (method === 8) {
          const decompressed = await decompressDeflateRaw(compressedData);
          entries.set(normalizeZipPath(fileName), decompressed);
        }
        cursor = dataStart + compSize;
      } else {
        cursor = dataStart;
      }
    }
  }

  return entries;
}

async function convertEpubToTxt(arrayBuffer, progressCallback) {
  let uint8Array;
  if (arrayBuffer instanceof Uint8Array) {
    uint8Array = arrayBuffer;
  } else if (arrayBuffer instanceof ArrayBuffer) {
    uint8Array = new Uint8Array(arrayBuffer);
  } else if (arrayBuffer && arrayBuffer.buffer instanceof ArrayBuffer) {
    uint8Array = new Uint8Array(arrayBuffer.buffer, arrayBuffer.byteOffset, arrayBuffer.byteLength);
  } else {
    throw new Error('Input berkas EPUB harus berupa ArrayBuffer atau Uint8Array.');
  }

  if (typeof progressCallback === 'function') {
    progressCallback(0.1, 'Mengekstrak struktur berkas EPUB...');
  }

  const entries = await extractZipEntries(uint8Array);
  if (entries.size === 0) {
    throw new Error('Arsip EPUB kosong atau struktur ZIP tidak dapat dibaca.');
  }

  if (typeof progressCallback === 'function') {
    progressCallback(0.3, 'Membaca manifest & struktur spine...');
  }

  const utf8Decoder = new TextDecoder('utf-8');
  const parser = getDOMParserInstance();

  // 1. Locate META-INF/container.xml
  const containerBytes = findZipEntry(entries, 'META-INF/container.xml');
  if (!containerBytes) {
    throw new Error('Format berkas EPUB tidak valid: META-INF/container.xml tidak ditemukan.');
  }

  const containerXml = utf8Decoder.decode(containerBytes);
  const containerDoc = parser.parseFromString(containerXml, 'text/xml');

  let rootfileEl = containerDoc.querySelector('rootfile');
  if (!rootfileEl) {
    const list = containerDoc.getElementsByTagName('rootfile');
    if (list.length > 0) rootfileEl = list[0];
    else {
      const nsList = containerDoc.getElementsByTagNameNS('*', 'rootfile');
      if (nsList.length > 0) rootfileEl = nsList[0];
    }
  }

  if (!rootfileEl) {
    throw new Error('Format EPUB tidak valid: elemen rootfile tidak ditemukan dalam META-INF/container.xml.');
  }

  const opfPath = rootfileEl.getAttribute('full-path');
  if (!opfPath) {
    throw new Error('Format EPUB tidak valid: atribut full-path tidak ditemukan dalam rootfile.');
  }

  // 2. Read OPF Package Document
  const opfBytes = findZipEntry(entries, opfPath);
  if (!opfBytes) {
    throw new Error(`Berkas OPF tidak ditemukan dalam arsip EPUB: ${opfPath}`);
  }

  const opfXml = utf8Decoder.decode(opfBytes);
  const opfDoc = parser.parseFromString(opfXml, 'text/xml');
  const normOpfPath = normalizeZipPath(opfPath);
  const opfDir = normOpfPath.includes('/') ? normOpfPath.substring(0, normOpfPath.lastIndexOf('/')) : '';

  // 3. Parse <manifest> items
  const manifest = new Map();
  let itemNodes = opfDoc.querySelectorAll('manifest > item, item');
  if (itemNodes.length === 0) {
    itemNodes = opfDoc.getElementsByTagName('item');
    if (itemNodes.length === 0) {
      itemNodes = opfDoc.getElementsByTagNameNS('*', 'item');
    }
  }

  for (let i = 0; i < itemNodes.length; i++) {
    const item = itemNodes[i];
    const id = item.getAttribute('id');
    const href = item.getAttribute('href');
    const mediaType = item.getAttribute('media-type');
    if (id && href) {
      let cleanHref = href;
      try { cleanHref = decodeURIComponent(href); } catch (e) {}
      manifest.set(id, { href: cleanHref, mediaType });
    }
  }

  // 4. Parse <spine> order
  let itemrefNodes = opfDoc.querySelectorAll('spine > itemref, itemref');
  if (itemrefNodes.length === 0) {
    itemrefNodes = opfDoc.getElementsByTagName('itemref');
    if (itemrefNodes.length === 0) {
      itemrefNodes = opfDoc.getElementsByTagNameNS('*', 'itemref');
    }
  }

  const spinePaths = [];
  for (let i = 0; i < itemrefNodes.length; i++) {
    const idref = itemrefNodes[i].getAttribute('idref');
    if (idref && manifest.has(idref)) {
      const item = manifest.get(idref);
      const fullItemPath = resolveRelativePath(opfDir, item.href);
      spinePaths.push(fullItemPath);
    }
  }

  // Fallback if spine was empty
  if (spinePaths.length === 0) {
    for (const [, item] of manifest.entries()) {
      if (item.mediaType && (item.mediaType.includes('xhtml') || item.mediaType.includes('html') || item.mediaType.includes('xml'))) {
        spinePaths.push(resolveRelativePath(opfDir, item.href));
      }
    }
  }

  if (spinePaths.length === 0) {
    throw new Error('Tidak ditemukan bab atau dokumen konten XHTML dalam spine EPUB.');
  }

  // 5. Extract chapters content
  const totalChapters = spinePaths.length;
  const chapters = [];

  for (let idx = 0; idx < totalChapters; idx++) {
    const chapterPath = spinePaths[idx];
    if (typeof progressCallback === 'function') {
      progressCallback(0.3 + 0.6 * ((idx + 1) / totalChapters), `Memproses bab ${idx + 1}/${totalChapters}...`);
    }

    const chapterBytes = findZipEntry(entries, chapterPath);
    if (!chapterBytes) {
      continue;
    }

    const chapterHtml = utf8Decoder.decode(chapterBytes);
    let chapterDoc;
    try {
      chapterDoc = parser.parseFromString(chapterHtml, 'text/html');
    } catch (e) {
      try {
        chapterDoc = parser.parseFromString(chapterHtml, 'application/xhtml+xml');
      } catch (e2) {
        continue;
      }
    }

    if (!chapterDoc) continue;

    // Ekstrak judul bab dari <h1>, <h2>, <h3>, atau <title>
    let chapterTitle = '';
    let headingElement = null;

    const candidateHeadings = ['h1', 'h2', 'h3', 'title'];
    for (const tag of candidateHeadings) {
      const el = chapterDoc.querySelector(tag);
      if (el && el.textContent.trim()) {
        chapterTitle = el.textContent.trim();
        if (tag !== 'title') {
          headingElement = el;
        }
        break;
      }
    }

    // Jika heading utama ditemukan, lepaskan dari DOM agar tidak terulang ganda di paragraf
    if (headingElement && headingElement.parentNode) {
      headingElement.parentNode.removeChild(headingElement);
    }

    chapterTitle = chapterTitle.replace(/\s+/g, ' ').trim();
    if (!chapterTitle) {
      chapterTitle = `Bab ${idx + 1}`;
    }

    // Bersihkan elemen yang tidak diperlukan
    const unneeded = chapterDoc.querySelectorAll('script, style, nav, svg');
    unneeded.forEach(el => {
      if (el.remove) el.remove();
      else if (el.parentNode) el.parentNode.removeChild(el);
    });

    // Ubah <br> menjadi newline
    const brs = chapterDoc.querySelectorAll('br');
    brs.forEach(br => {
      if (br.replaceWith) {
        br.replaceWith('\n');
      } else if (br.parentNode) {
        br.parentNode.removeChild(br);
      }
    });

    // Ekstrak teks paragraf (<p>, <div>, <blockquote>, <li>)
    const paragraphs = [];
    const body = chapterDoc.body || chapterDoc.documentElement;
    const blocks = body.querySelectorAll('p, div, blockquote, li, pre, h4, h5, h6');

    for (let b = 0; b < blocks.length; b++) {
      const block = blocks[b];
      // Jika blok memiliki blok anak, biarkan anak-anaknya diproses tersendiri
      const hasBlockChildren = block.querySelector('p, div, blockquote, li, pre');
      if (hasBlockChildren) {
        continue;
      }
      const text = block.textContent.trim();
      if (text) {
        paragraphs.push(text);
      }
    }

    // Fallback jika tidak ada elemen blok terpisah
    if (paragraphs.length === 0) {
      const rawBody = body.textContent.trim();
      if (rawBody) {
        paragraphs.push(rawBody);
      }
    }

    const chapterBody = paragraphs.join('\n\n');

    // Sisipkan hard page break docs \f sebelum setiap judul bab baru
    if (chapterBody.length > 0 || chapterTitle) {
      chapters.push(`\f\n\n=== ${chapterTitle} ===\n\n${chapterBody}`);
    }
  }

  const rawBookText = chapters.join('\n\n');
  const cleaner = getCleanPlainTextFn();
  const cleanBookText = cleaner(rawBookText);

  if (typeof progressCallback === 'function') {
    progressCallback(1.0, 'Konversi EPUB selesai');
  }

  return cleanBookText;
}

if (typeof window !== 'undefined') {
  window.convertEpubToTxt = convertEpubToTxt;
}
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { convertEpubToTxt };
}
