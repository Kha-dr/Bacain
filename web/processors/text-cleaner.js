/**
 * text-cleaner.js
 * BacaIn E-Book Ingestion Pipeline - Plain Text Cleaning & Typography Normalizer
 *
 * Normalizes line endings, cleans excess blank lines, unifies typographic symbols,
 * reflows hard-wrapped lines within paragraphs while preserving chapter headings
 * and e-paper hard page breaks (\f).
 */

function cleanPlainText(rawText, options = {}) {
  if (rawText === null || rawText === undefined) {
    return '';
  }

  const paragraphStyle = options.paragraphStyle || 'book'; // 'book' (default) or 'block'
  const indentSpaces = typeof options.indentSpaces === 'number' ? options.indentSpaces : 2;

  let text = String(rawText);
  if (!text) return '';

  // 1. Normalisasi baris baru (\r\n dan \r -> \n)
  text = text.replace(/\r\n/g, '\n').replace(/\r/g, '\n');

  // 2. Normalisasi karakter tipografi
  // Curly single quotes: ‘ ’ ‚ ‛ -> '
  text = text.replace(/[\u2018\u2019\u201A\u201B]/g, "'");
  // Curly double quotes: “ ” „ ‟ « » -> "
  text = text.replace(/[\u201C\u201D\u201E\u201F\u00AB\u00BB]/g, '"');
  // Em-dash and en-dash: — (u2014), – (u2013), and double dash -- -> -
  text = text.replace(/[\u2013\u2014]/g, '-');
  text = text.replace(/--+/g, '-');
  // Ellipsis: … (u2026) -> ...
  text = text.replace(/\u2026/g, '...');
  // Non-breaking space (u00A0) and invisible formatting chars -> standard space or stripped
  text = text.replace(/\u00A0/g, ' ');
  text = text.replace(/[\u200B-\u200D\uFEFF]/g, '');

  // Helper for joining blocks according to paragraph style
  function joinBlocks(blocks) {
    if (blocks.length === 0) return '';
    if (paragraphStyle === 'block') {
      return blocks.map(b => b.text).join('\n\n');
    }

    // Book style: 2-space indent for regular paragraphs, single \n between paragraphs,
    // \n\n around headers and scene dividers.
    const indent = ' '.repeat(indentSpaces);
    let out = '';

    for (let i = 0; i < blocks.length; i++) {
      const cur = blocks[i];
      let content = cur.text;

      if (cur.isSpecial) {
        if (i > 0) {
          out += '\n\n';
        }
        out += content;
      } else {
        content = indent + content;
        if (i === 0) {
          out += content;
        } else {
          const prev = blocks[i - 1];
          if (prev.isSpecial) {
            out += '\n\n' + content;
          } else {
            out += '\n' + content;
          }
        }
      }
    }

    return out;
  }

  // Helper for reflowing a single segment (between \f boundaries)
  function reflowSegment(segment) {
    const lines = segment.split('\n');
    const blocks = [];
    let currentBlock = [];

    function flushBlock() {
      if (currentBlock.length > 0) {
        let merged = '';
        for (let i = 0; i < currentBlock.length; i++) {
          const line = currentBlock[i];
          if (i === 0) {
            merged = line;
          } else {
            // If previous line ends with hyphen, join directly (e.g. word-break or reduplication)
            if (merged.endsWith('-') && !merged.endsWith(' -')) {
              merged += line;
            } else {
              merged += ' ' + line;
            }
          }
        }
        blocks.push({ text: merged, isSpecial: false });
        currentBlock = [];
      }
    }

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i].replace(/[ \t]+$/, ''); // Trim trailing horizontal spaces
      const trimmed = line.trim();

      if (trimmed === '') {
        flushBlock();
        continue;
      }

      // Preserve chapter headings (=== ... ===) and markdown headings (# ...)
      const isHeader = /^\s*===.*===\s*$/.test(trimmed) || /^\s*#{1,6}\s+/.test(trimmed) || /^\s*---\s+.*\s+---\s*$/.test(trimmed);

      // Preserve scene dividers / horizontal rules (e.g. * * *, ---, ___)
      const isDivider = /^([*_\-–—]\s*){3,}$/.test(trimmed);

      if (isHeader || isDivider) {
        flushBlock();
        blocks.push({ text: trimmed, isSpecial: true });
        continue;
      }

      currentBlock.push(trimmed);
    }

    flushBlock();
    return joinBlocks(blocks);
  }

  // 3. Handle hard page break \f (0x0C)
  if (text.includes('\f')) {
    const hasLeadingFormFeed = /^[ \t\r\n]*\f/.test(text);
    const rawSegments = text.split('\f');
    const cleanedSegments = [];

    for (let i = 0; i < rawSegments.length; i++) {
      const segTrimmed = rawSegments[i].replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, '');
      if (segTrimmed.length > 0) {
        cleanedSegments.push(reflowSegment(segTrimmed));
      }
    }

    let result = cleanedSegments.join('\n\n\f\n\n');
    if (hasLeadingFormFeed) {
      result = '\f\n\n' + result;
    }

    // Pembersihan baris kosong berlebih (maksimal 2 baris baru berurutan \n\n)
    result = result.replace(/\n{3,}/g, '\n\n');
    return result;
  }

  // If no \f, reflow entire text
  const trimmedText = text.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, '');
  let result = reflowSegment(trimmedText);

  // Pembersihan baris kosong berlebih (maksimal 2 baris baru berurutan \n\n)
  result = result.replace(/\n{3,}/g, '\n\n');
  return result;
}

if (typeof window !== 'undefined') {
  window.cleanPlainText = cleanPlainText;
  window.TextCleaner = {
    clean: async (file, options = {}) => {
      const raw = typeof file.text === 'function' ? await file.text() : String(file);
      const clean = cleanPlainText(raw, options);
      return new File([clean], file.name || 'cleaned.txt', { type: 'text/plain' });
    }
  };
}
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { cleanPlainText };
}
