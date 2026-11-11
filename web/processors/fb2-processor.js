/**
 * fb2-processor.js
 * BacaIn E-Book Ingestion Pipeline - Zero-Dependency Native FB2 to TXT Converter
 *
 * Decodes UTF-8/windows-1251 FB2 buffers, parses FictionBook2 XML schema with DOMParser,
 * extracts metadata (<book-title>, <author>), traverses all <body> and <section> elements,
 * handles <p>, <poem>, <epigraph>, and injects e-paper hardware page-breaks (\f)
 * before running cleanPlainText().
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

function extractTitleFromElement(el) {
  for (let i = 0; i < el.childNodes.length; i++) {
    const child = el.childNodes[i];
    if (child.nodeType === 1 && child.tagName.toLowerCase() === 'title') {
      const pList = child.getElementsByTagName('p');
      if (pList.length > 0) {
        const lines = [];
        for (let j = 0; j < pList.length; j++) {
          const t = pList[j].textContent.trim();
          if (t) lines.push(t);
        }
        return lines.join(' - ');
      }
      return child.textContent.trim();
    }
  }
  return '';
}

function extractBlockContent(el) {
  const pieces = [];

  for (let i = 0; i < el.childNodes.length; i++) {
    const child = el.childNodes[i];
    if (child.nodeType !== 1) continue;

    const tag = child.tagName.toLowerCase();
    if (tag === 'title' || tag === 'section') {
      continue;
    }

    if (tag === 'p') {
      const txt = child.textContent.trim();
      if (txt) pieces.push(txt);
    } else if (tag === 'poem') {
      const stanzas = child.getElementsByTagName('stanza');
      for (let s = 0; s < stanzas.length; s++) {
        const verses = stanzas[s].getElementsByTagName('v');
        const vLines = [];
        for (let v = 0; v < verses.length; v++) {
          const vt = verses[v].textContent.trim();
          if (vt) vLines.push(vt);
        }
        if (vLines.length > 0) {
          pieces.push(vLines.join('\n'));
        }
      }
      const poemTitle = child.getElementsByTagName('title');
      if (poemTitle.length > 0 && poemTitle[0].textContent.trim()) {
        pieces.push(poemTitle[0].textContent.trim());
      }
    } else if (tag === 'epigraph') {
      const pList = child.getElementsByTagName('p');
      const epLines = [];
      for (let p = 0; p < pList.length; p++) {
        const pt = pList[p].textContent.trim();
        if (pt) epLines.push(pt);
      }
      const authorList = child.getElementsByTagName('text-author');
      for (let a = 0; a < authorList.length; a++) {
        const at = authorList[a].textContent.trim();
        if (at) epLines.push(`- ${at}`);
      }
      if (epLines.length > 0) {
        pieces.push(epLines.join('\n'));
      }
    } else if (tag === 'cite' || tag === 'annotation') {
      const pList = child.getElementsByTagName('p');
      const citeLines = [];
      for (let p = 0; p < pList.length; p++) {
        const pt = pList[p].textContent.trim();
        if (pt) citeLines.push(pt);
      }
      if (citeLines.length > 0) {
        pieces.push(citeLines.join('\n\n'));
      }
    } else if (tag === 'subtitle') {
      const sub = child.textContent.trim();
      if (sub) pieces.push(`--- ${sub} ---`);
    } else if (tag === 'empty-line') {
      // blank line preserved by block joining
    } else {
      const txt = child.textContent.trim();
      if (txt) pieces.push(txt);
    }
  }

  return pieces.join('\n\n');
}

async function convertFb2ToTxt(textOrBuffer, options = {}) {
  let xmlString;

  if (typeof textOrBuffer === 'string') {
    xmlString = textOrBuffer;
  } else {
    let uint8Array;
    if (textOrBuffer instanceof Uint8Array) {
      uint8Array = textOrBuffer;
    } else if (textOrBuffer instanceof ArrayBuffer) {
      uint8Array = new Uint8Array(textOrBuffer);
    } else if (textOrBuffer && textOrBuffer.buffer instanceof ArrayBuffer) {
      uint8Array = new Uint8Array(textOrBuffer.buffer, textOrBuffer.byteOffset, textOrBuffer.byteLength);
    } else {
      throw new Error('Input berkas FB2 harus berupa string, ArrayBuffer, atau Uint8Array.');
    }

    // Periksa encoding deklarasi XML jika ada
    const peekLen = Math.min(uint8Array.length, 256);
    let peekAscii = '';
    for (let i = 0; i < peekLen; i++) {
      peekAscii += String.fromCharCode(uint8Array[i]);
    }
    const encMatch = peekAscii.match(/encoding=["']([^"']+)["']/i);
    const encoding = encMatch ? encMatch[1].toLowerCase() : 'utf-8';

    try {
      xmlString = new TextDecoder(encoding).decode(uint8Array);
    } catch (e) {
      xmlString = new TextDecoder('utf-8').decode(uint8Array);
    }
  }

  const parser = getDOMParserInstance();
  let doc;

  try {
    doc = parser.parseFromString(xmlString, 'text/xml');
  } catch (e) {
    const sanitized = xmlString.replace(/&(?!(amp|lt|gt|quot|apos|#\d+|#x[0-9a-fA-F]+);)/g, '&amp;');
    doc = parser.parseFromString(sanitized, 'text/xml');
  }

  const errEl = doc.querySelector ? doc.querySelector('parsererror') : null;
  if (errEl) {
    const sanitized = xmlString.replace(/&(?!(amp|lt|gt|quot|apos|#\d+|#x[0-9a-fA-F]+);)/g, '&amp;');
    const retryDoc = parser.parseFromString(sanitized, 'text/xml');
    if (!retryDoc.querySelector || !retryDoc.querySelector('parsererror')) {
      doc = retryDoc;
    }
  }

  // 1. Ekstrak Metadata (<book-title>, <author>)
  let bookTitle = '';
  const bookAuthors = [];

  let titleInfo = doc.querySelector ? doc.querySelector('description > title-info, title-info') : null;
  if (!titleInfo) {
    const list = doc.getElementsByTagName('title-info');
    if (list.length > 0) titleInfo = list[0];
  }

  if (titleInfo) {
    let bt = titleInfo.querySelector ? titleInfo.querySelector('book-title') : null;
    if (!bt) {
      const list = titleInfo.getElementsByTagName('book-title');
      if (list.length > 0) bt = list[0];
    }
    if (bt && bt.textContent.trim()) {
      bookTitle = bt.textContent.trim();
    }

    const authorNodes = titleInfo.getElementsByTagName('author');
    for (let i = 0; i < authorNodes.length; i++) {
      const a = authorNodes[i];
      const fn = a.getElementsByTagName('first-name')[0];
      const mn = a.getElementsByTagName('middle-name')[0];
      const ln = a.getElementsByTagName('last-name')[0];
      const nn = a.getElementsByTagName('nickname')[0];

      const parts = [
        fn ? fn.textContent.trim() : '',
        mn ? mn.textContent.trim() : '',
        ln ? ln.textContent.trim() : ''
      ].filter(Boolean);

      if (parts.length > 0) {
        bookAuthors.push(parts.join(' '));
      } else if (nn && nn.textContent.trim()) {
        bookAuthors.push(nn.textContent.trim());
      } else if (a.textContent.trim()) {
        bookAuthors.push(a.textContent.trim());
      }
    }
  }

  // 2. Traversal Section & Sub-Sections
  let sectionCounter = 0;

  function traverseSection(secEl, defaultTitle) {
    const results = [];
    sectionCounter++;

    let title = extractTitleFromElement(secEl);
    if (!title) {
      title = defaultTitle || `Bagian ${sectionCounter}`;
    }
    title = title.replace(/\s+/g, ' ').trim();

    const content = extractBlockContent(secEl);
    if (content.length > 0 || title) {
      results.push(`\f\n\n=== ${title} ===\n\n${content}`);
    }

    const childSections = [];
    for (let i = 0; i < secEl.childNodes.length; i++) {
      const node = secEl.childNodes[i];
      if (node.nodeType === 1 && node.tagName.toLowerCase() === 'section') {
        childSections.push(node);
      }
    }

    for (let i = 0; i < childSections.length; i++) {
      const subResults = traverseSection(childSections[i], `${title}.${i + 1}`);
      results.push(...subResults);
    }

    return results;
  }

  // 3. Iterasi seluruh elemen <body>
  let bodies = doc.getElementsByTagName('body');
  if (bodies.length === 0) {
    bodies = [doc.documentElement];
  }

  const sectionChunks = [];

  for (let b = 0; b < bodies.length; b++) {
    const bodyEl = bodies[b];
    const bodyName = bodyEl.getAttribute ? bodyEl.getAttribute('name') : null;
    const bodyTitle = extractTitleFromElement(bodyEl);

    const directSections = [];
    for (let i = 0; i < bodyEl.childNodes.length; i++) {
      const node = bodyEl.childNodes[i];
      if (node.nodeType === 1 && node.tagName.toLowerCase() === 'section') {
        directSections.push(node);
      }
    }

    if (directSections.length > 0) {
      const preContent = extractBlockContent(bodyEl);
      if (preContent.length > 0 || bodyTitle) {
        const t = bodyTitle || (bodyName === 'notes' ? 'Catatan Kaki' : (bookTitle || `Bagian ${++sectionCounter}`));
        sectionChunks.push(`\f\n\n=== ${t} ===\n\n${preContent}`);
      }

      for (let s = 0; s < directSections.length; s++) {
        const secRes = traverseSection(directSections[s], `Bab ${s + 1}`);
        sectionChunks.push(...secRes);
      }
    } else {
      const bodyContent = extractBlockContent(bodyEl);
      let t = bodyTitle;
      if (!t && bodyName === 'notes') t = 'Catatan Kaki';
      if (!t && bookTitle) t = bookTitle;
      if (!t) t = `Buku`;

      if (bodyContent.length > 0 || t) {
        sectionChunks.push(`\f\n\n=== ${t} ===\n\n${bodyContent}`);
      }
    }
  }

  let metaPrefix = '';
  if (bookTitle) {
    metaPrefix = `=== ${bookTitle} ===\n`;
    if (bookAuthors.length > 0) {
      metaPrefix += `Penulis: ${bookAuthors.join(', ')}\n\n`;
    } else {
      metaPrefix += '\n';
    }
  }

  const fullRawText = metaPrefix + sectionChunks.join('\n\n');
  const cleaner = getCleanPlainTextFn();
  const cleanBookText = cleaner(fullRawText, options);

  return cleanBookText;
}

if (typeof window !== 'undefined') {
  window.convertFb2ToTxt = convertFb2ToTxt;
  window.Fb2Processor = {
    process: async (file, options = {}) => {
      const buf = typeof file.arrayBuffer === 'function' ? await file.arrayBuffer() : file;
      const cleanTxt = await convertFb2ToTxt(buf, options);
      const baseName = (file.name || 'book').replace(/\.fb2$/i, '');
      return new File([cleanTxt], `${baseName}.txt`, { type: 'text/plain' });
    }
  };
}
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { convertFb2ToTxt };
}
