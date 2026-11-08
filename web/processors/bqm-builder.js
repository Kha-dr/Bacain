/**
 * bqm-builder.js
 * BacaIn PWA - Mushaf (.bqm) Binary Package Compiler & Canvas Previewer
 *
 * Implements:
 * 1. renderAyahPreviewToCanvas(canvas, options):
 *    1-bit preview renderer matching view_mushaf.c and mushaf_reader.c e-paper layouts.
 * 2. buildBqmPackage(options, onProgress):
 *    Assembles a valid BQM1 container with packed 1-bit word sprites and pre-wrapped text layers.
 */

// ---------------------------------------------------------------------------
// Canonical Surahs Dataset (114 Surahs: number, name, ayahs, startJuz)
// ---------------------------------------------------------------------------
export const CANONICAL_SURAHS = [
  { number: 1, name: 'Al-Fatihah', ayahs: 7, startJuz: 1 },
  { number: 2, name: 'Al-Baqarah', ayahs: 286, startJuz: 1 },
  { number: 3, name: "Ali 'Imran", ayahs: 200, startJuz: 3 },
  { number: 4, name: 'An-Nisa', ayahs: 176, startJuz: 4 },
  { number: 5, name: "Al-Ma'idah", ayahs: 120, startJuz: 6 },
  { number: 6, name: "Al-An'am", ayahs: 165, startJuz: 7 },
  { number: 7, name: "Al-A'raf", ayahs: 206, startJuz: 8 },
  { number: 8, name: 'Al-Anfal', ayahs: 75, startJuz: 9 },
  { number: 9, name: 'At-Tawbah', ayahs: 129, startJuz: 10 },
  { number: 10, name: 'Yunus', ayahs: 109, startJuz: 11 },
  { number: 11, name: 'Hud', ayahs: 123, startJuz: 11 },
  { number: 12, name: 'Yusuf', ayahs: 111, startJuz: 12 },
  { number: 13, name: "Ar-Ra'd", ayahs: 43, startJuz: 13 },
  { number: 14, name: 'Ibrahim', ayahs: 52, startJuz: 13 },
  { number: 15, name: 'Al-Hijr', ayahs: 99, startJuz: 14 },
  { number: 16, name: 'An-Nahl', ayahs: 128, startJuz: 14 },
  { number: 17, name: 'Al-Isra', ayahs: 111, startJuz: 15 },
  { number: 18, name: 'Al-Kahf', ayahs: 110, startJuz: 15 },
  { number: 19, name: 'Maryam', ayahs: 98, startJuz: 16 },
  { number: 20, name: 'Taha', ayahs: 135, startJuz: 16 },
  { number: 21, name: 'Al-Anbiya', ayahs: 112, startJuz: 17 },
  { number: 22, name: 'Al-Hajj', ayahs: 78, startJuz: 17 },
  { number: 23, name: "Al-Mu'minun", ayahs: 118, startJuz: 18 },
  { number: 24, name: 'An-Nur', ayahs: 64, startJuz: 18 },
  { number: 25, name: 'Al-Furqan', ayahs: 77, startJuz: 18 },
  { number: 26, name: "Ash-Shu'ara", ayahs: 227, startJuz: 19 },
  { number: 27, name: 'An-Naml', ayahs: 93, startJuz: 19 },
  { number: 28, name: 'Al-Qasas', ayahs: 88, startJuz: 20 },
  { number: 29, name: "Al-'Ankabut", ayahs: 69, startJuz: 20 },
  { number: 30, name: 'Ar-Rum', ayahs: 60, startJuz: 21 },
  { number: 31, name: 'Luqman', ayahs: 34, startJuz: 21 },
  { number: 32, name: 'As-Sajdah', ayahs: 30, startJuz: 21 },
  { number: 33, name: 'Al-Ahzab', ayahs: 73, startJuz: 21 },
  { number: 34, name: 'Saba', ayahs: 54, startJuz: 22 },
  { number: 35, name: 'Fatir', ayahs: 45, startJuz: 22 },
  { number: 36, name: 'Ya-Sin', ayahs: 83, startJuz: 22 },
  { number: 37, name: 'As-Saffat', ayahs: 182, startJuz: 23 },
  { number: 38, name: 'Sad', ayahs: 88, startJuz: 23 },
  { number: 39, name: 'Az-Zumar', ayahs: 75, startJuz: 23 },
  { number: 40, name: 'Ghafir', ayahs: 85, startJuz: 24 },
  { number: 41, name: 'Fussilat', ayahs: 54, startJuz: 24 },
  { number: 42, name: 'Ash-Shura', ayahs: 53, startJuz: 25 },
  { number: 43, name: 'Az-Zukhruf', ayahs: 89, startJuz: 25 },
  { number: 44, name: 'Ad-Dukhan', ayahs: 59, startJuz: 25 },
  { number: 45, name: 'Al-Jathiyah', ayahs: 37, startJuz: 25 },
  { number: 46, name: 'Al-Ahqaf', ayahs: 35, startJuz: 26 },
  { number: 47, name: 'Muhammad', ayahs: 38, startJuz: 26 },
  { number: 48, name: 'Al-Fath', ayahs: 29, startJuz: 26 },
  { number: 49, name: 'Al-Hujurat', ayahs: 18, startJuz: 26 },
  { number: 50, name: 'Qaf', ayahs: 45, startJuz: 26 },
  { number: 51, name: 'Adh-Dhariyat', ayahs: 60, startJuz: 26 },
  { number: 52, name: 'At-Tur', ayahs: 49, startJuz: 27 },
  { number: 53, name: 'An-Najm', ayahs: 62, startJuz: 27 },
  { number: 54, name: 'Al-Qamar', ayahs: 55, startJuz: 27 },
  { number: 55, name: 'Ar-Rahman', ayahs: 78, startJuz: 27 },
  { number: 56, name: "Al-Waqi'ah", ayahs: 96, startJuz: 27 },
  { number: 57, name: 'Al-Hadid', ayahs: 29, startJuz: 27 },
  { number: 58, name: 'Al-Mujadila', ayahs: 22, startJuz: 28 },
  { number: 59, name: 'Al-Hashr', ayahs: 24, startJuz: 28 },
  { number: 60, name: 'Al-Mumtahanah', ayahs: 13, startJuz: 28 },
  { number: 61, name: 'As-Saff', ayahs: 14, startJuz: 28 },
  { number: 62, name: "Al-Jumu'ah", ayahs: 11, startJuz: 28 },
  { number: 63, name: 'Al-Munafiqun', ayahs: 11, startJuz: 28 },
  { number: 64, name: 'At-Taghabun', ayahs: 18, startJuz: 28 },
  { number: 65, name: 'At-Talaq', ayahs: 12, startJuz: 28 },
  { number: 66, name: 'At-Tahrim', ayahs: 12, startJuz: 28 },
  { number: 67, name: 'Al-Mulk', ayahs: 30, startJuz: 29 },
  { number: 68, name: 'Al-Qalam', ayahs: 52, startJuz: 29 },
  { number: 69, name: 'Al-Haqqah', ayahs: 52, startJuz: 29 },
  { number: 70, name: "Al-Ma'arij", ayahs: 44, startJuz: 29 },
  { number: 71, name: 'Nuh', ayahs: 28, startJuz: 29 },
  { number: 72, name: 'Al-Jinn', ayahs: 28, startJuz: 29 },
  { number: 73, name: 'Al-Muzzammil', ayahs: 20, startJuz: 29 },
  { number: 74, name: 'Al-Muddaththir', ayahs: 56, startJuz: 29 },
  { number: 75, name: 'Al-Qiyamah', ayahs: 40, startJuz: 29 },
  { number: 76, name: 'Al-Insan', ayahs: 31, startJuz: 29 },
  { number: 77, name: 'Al-Mursalat', ayahs: 50, startJuz: 29 },
  { number: 78, name: 'An-Naba', ayahs: 40, startJuz: 30 },
  { number: 79, name: "An-Nazi'at", ayahs: 46, startJuz: 30 },
  { number: 80, name: "'Abasa", ayahs: 42, startJuz: 30 },
  { number: 81, name: 'At-Takwir', ayahs: 29, startJuz: 30 },
  { number: 82, name: 'Al-Infitar', ayahs: 19, startJuz: 30 },
  { number: 83, name: 'Al-Mutaffifin', ayahs: 36, startJuz: 30 },
  { number: 84, name: 'Al-Inshiqaq', ayahs: 25, startJuz: 30 },
  { number: 85, name: 'Al-Buruj', ayahs: 22, startJuz: 30 },
  { number: 86, name: 'At-Tariq', ayahs: 17, startJuz: 30 },
  { number: 87, name: "Al-A'la", ayahs: 19, startJuz: 30 },
  { number: 88, name: 'Al-Ghashiyah', ayahs: 26, startJuz: 30 },
  { number: 89, name: 'Al-Fajr', ayahs: 30, startJuz: 30 },
  { number: 90, name: 'Al-Balad', ayahs: 20, startJuz: 30 },
  { number: 91, name: 'Ash-Shams', ayahs: 15, startJuz: 30 },
  { number: 92, name: 'Al-Layl', ayahs: 21, startJuz: 30 },
  { number: 93, name: 'Ad-Duha', ayahs: 11, startJuz: 30 },
  { number: 94, name: 'Ash-Sharh', ayahs: 8, startJuz: 30 },
  { number: 95, name: 'At-Tin', ayahs: 8, startJuz: 30 },
  { number: 96, name: "Al-'Alaq", ayahs: 19, startJuz: 30 },
  { number: 97, name: 'Al-Qadr', ayahs: 5, startJuz: 30 },
  { number: 98, name: 'Al-Bayyinah', ayahs: 8, startJuz: 30 },
  { number: 99, name: 'Az-Zalzalah', ayahs: 8, startJuz: 30 },
  { number: 100, name: "Al-'Adiyat", ayahs: 11, startJuz: 30 },
  { number: 101, name: "Al-Qari'ah", ayahs: 11, startJuz: 30 },
  { number: 102, name: 'At-Takathur', ayahs: 8, startJuz: 30 },
  { number: 103, name: "Al-'Asr", ayahs: 3, startJuz: 30 },
  { number: 104, name: 'Al-Humazah', ayahs: 9, startJuz: 30 },
  { number: 105, name: 'Al-Fil', ayahs: 5, startJuz: 30 },
  { number: 106, name: 'Quraysh', ayahs: 4, startJuz: 30 },
  { number: 107, name: "Al-Ma'un", ayahs: 7, startJuz: 30 },
  { number: 108, name: 'Al-Kawthar', ayahs: 3, startJuz: 30 },
  { number: 109, name: 'Al-Kafirun', ayahs: 6, startJuz: 30 },
  { number: 110, name: 'An-Nasr', ayahs: 3, startJuz: 30 },
  { number: 111, name: 'Al-Masad', ayahs: 5, startJuz: 30 },
  { number: 112, name: 'Al-Ikhlas', ayahs: 4, startJuz: 30 },
  { number: 113, name: 'Al-Falaq', ayahs: 5, startJuz: 30 },
  { number: 114, name: 'An-Nas', ayahs: 6, startJuz: 30 }
];

export const JUZ_BOUNDARIES = [
  { juz: 1, surah: 1, ayah: 1 },
  { juz: 2, surah: 2, ayah: 142 },
  { juz: 3, surah: 2, ayah: 253 },
  { juz: 4, surah: 3, ayah: 93 },
  { juz: 5, surah: 4, ayah: 24 },
  { juz: 6, surah: 4, ayah: 148 },
  { juz: 7, surah: 5, ayah: 82 },
  { juz: 8, surah: 6, ayah: 111 },
  { juz: 9, surah: 7, ayah: 88 },
  { juz: 10, surah: 8, ayah: 41 },
  { juz: 11, surah: 9, ayah: 93 },
  { juz: 12, surah: 11, ayah: 6 },
  { juz: 13, surah: 12, ayah: 53 },
  { juz: 14, surah: 15, ayah: 1 },
  { juz: 15, surah: 17, ayah: 1 },
  { juz: 16, surah: 18, ayah: 75 },
  { juz: 17, surah: 21, ayah: 1 },
  { juz: 18, surah: 23, ayah: 1 },
  { juz: 19, surah: 25, ayah: 21 },
  { juz: 20, surah: 27, ayah: 56 },
  { juz: 21, surah: 29, ayah: 46 },
  { juz: 22, surah: 33, ayah: 31 },
  { juz: 23, surah: 36, ayah: 28 },
  { juz: 24, surah: 39, ayah: 32 },
  { juz: 25, surah: 41, ayah: 47 },
  { juz: 26, surah: 46, ayah: 1 },
  { juz: 27, surah: 51, ayah: 31 },
  { juz: 28, surah: 58, ayah: 1 },
  { juz: 29, surah: 67, ayah: 1 },
  { juz: 30, surah: 78, ayah: 1 }
];

export function getJuzForSurahAyah(surah, ayah) {
  let foundJuz = 1;
  for (let i = 0; i < JUZ_BOUNDARIES.length; i++) {
    const b = JUZ_BOUNDARIES[i];
    if (surah > b.surah || (surah === b.surah && ayah >= b.ayah)) {
      foundJuz = b.juz;
    } else {
      break;
    }
  }
  return foundJuz;
}

export const JUZ_STARTS = JUZ_BOUNDARIES;
export const getJuzForAyah = getJuzForSurahAyah;

export const SURAHS = CANONICAL_SURAHS;

export const MEDINAN_SURAHS = new Set([
  2, 3, 4, 5, 8, 9, 22, 24, 33, 47, 48, 49, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 76, 98, 110
]);

export const LAYER_TAGS = ['[M]', '[LAT]', '[TRN]', '[TAF]'];
export const LAYER_NAMES = ['Mushaf Calligraphy', 'Latin Transliteration', 'Translation', 'Tafsir Ringkas'];

export const SUNNAH_PRESETS = [
  { surah: 18, ayah: 1, name: 'Al-Kahf', label: 'Al-Kahf (18:1)' },
  { surah: 36, ayah: 1, name: 'Ya-Sin', label: 'Ya-Sin (36:1)' },
  { surah: 55, ayah: 1, name: 'Ar-Rahman', label: 'Ar-Rahman (55:1)' },
  { surah: 56, ayah: 1, name: "Al-Waqi'ah", label: "Al-Waqi'ah (56:1)" },
  { surah: 67, ayah: 1, name: 'Al-Mulk', label: 'Al-Mulk (67:1)' }
];

export const MUSHAF_LAYERS = [
  { id: 0, tag: 'M', fullTag: '[M]', name: 'Mushaf', title: 'Mushaf Calligraphy' },
  { id: 1, tag: 'LAT', fullTag: '[LAT]', name: 'Latin', title: 'Latin Transliteration' },
  { id: 2, tag: 'TRN', fullTag: '[TRN]', name: 'Translation', title: 'Translation' },
  { id: 3, tag: 'TAF', fullTag: '[TAF]', name: 'Tafsir', title: 'Tafsir Ringkas' }
];

// ---------------------------------------------------------------------------
// Upstream CORS Font & Corpus Endpoints + CacheStorage ('bacain-mushaf-assets-v1')
// ---------------------------------------------------------------------------
export const MUSHAF_CACHE_NAME = 'bacain-mushaf-assets-v1';

export const MUSHAF_FONTS = {
  uthmani: {
    family: 'KFGQPC-Uthmanic',
    url: 'https://verses.quran.foundation/fonts/quran/hafs/uthmanic_hafs/UthmanicHafs1Ver18.woff2'
  },
  indopak: {
    family: 'IndoPak-Naskh',
    url: 'https://verses.quran.foundation/fonts/quran/hafs/nastaleeq/indopak/indopak-nastaleeq-waqf-lazim-v4.2.1.woff2'
  }
};

export const MUSHAF_DATA_ENDPOINTS = {
  uthmani: 'https://api.alquran.cloud/v1/quran/quran-uthmani',
  indopakPrimary: 'https://cdn.jsdelivr.net/gh/fawazahmed0/quran-api@1/editions/ara-quranindopak.json',
  indopakFallback: 'https://api.alquran.cloud/v1/quran/quran-indopak',
  latin: 'https://api.alquran.cloud/v1/quran/en.transliteration',
  idTrans: 'https://api.alquran.cloud/v1/quran/id.indonesian',
  enTrans: 'https://api.alquran.cloud/v1/quran/en.sahih',
  idTafsir: 'https://api.alquran.cloud/v1/quran/id.jalalayn',
  enTafsir: 'https://api.alquran.cloud/v1/quran/en.jalalayn'
};

/**
 * Fetch a resource with persistent CacheStorage ('bacain-mushaf-assets-v1').
 * Checks cache first; if missing, fetches upstream, puts clone in cache, and returns response.
 * Falls back to standard fetch if globalThis.caches is unavailable.
 * @param {string} url
 * @returns {Promise<Response>}
 */
export async function fetchWithCache(url) {
  if (typeof globalThis !== 'undefined' && globalThis.caches) {
    try {
      const cache = await globalThis.caches.open(MUSHAF_CACHE_NAME);
      const cached = await cache.match(url);
      if (cached) {
        return cached;
      }
      const res = await fetch(url);
      if (!res.ok) {
        throw new Error(`Failed to fetch ${url} (HTTP ${res.status}: ${res.statusText})`);
      }
      try {
        await cache.put(url, res.clone());
      } catch (cacheErr) {
        console.warn(`CacheStorage put warning for ${url}:`, cacheErr.message);
      }
      return res;
    } catch (err) {
      console.warn(`CacheStorage open/match warning for ${url}: ${err.message}. Falling back to fetch.`);
    }
  } else {
    console.warn(`globalThis.caches is unavailable in this environment, falling back to direct fetch for ${url}`);
  }

  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(`Failed to fetch ${url} (HTTP ${res.status}: ${res.statusText})`);
  }
  return res;
}

const loadedMushafFonts = new Set();

/**
 * Ensure the required .woff2 calligraphy font is loaded in browser document.fonts.
 * @param {number} scriptId 0 = Madinah Uthmani, 1 = IndoPak
 * @param {Function|null} onStatus
 * @returns {Promise<boolean>}
 */
export async function ensureMushafFont(scriptId = 0, onStatus = null) {
  if (typeof FontFace === 'undefined' || typeof document === 'undefined' || !document.fonts) {
    return false;
  }
  const fontConfig = scriptId === 1 ? MUSHAF_FONTS.indopak : MUSHAF_FONTS.uthmani;
  if (loadedMushafFonts.has(fontConfig.family)) {
    return true;
  }
  if (document.fonts.check(`16px ${fontConfig.family}`)) {
    loadedMushafFonts.add(fontConfig.family);
    return true;
  }

  if (typeof onStatus === 'function') {
    onStatus(`Fetching ${fontConfig.family} font (${MUSHAF_CACHE_NAME})...`);
  }

  const res = await fetchWithCache(fontConfig.url);
  const arrayBuffer = await res.arrayBuffer();
  const fontFace = new FontFace(fontConfig.family, arrayBuffer);
  const loaded = await fontFace.load();
  document.fonts.add(loaded);
  loadedMushafFonts.add(fontConfig.family);
  return true;
}

/**
 * Parse API JSON payload into an ayah map.
 * @param {object} json
 * @returns {Map<string, { text: string, juz: number, hizbQuarter: number }>}
 */
export function parseMushafCloud(json) {
  const map = new Map();
  if (json?.data?.surahs && Array.isArray(json.data.surahs)) {
    for (const s of json.data.surahs) {
      if (Array.isArray(s.ayahs)) {
        for (const a of s.ayahs) {
          map.set(`${s.number}:${a.numberInSurah}`, {
            text: (a.text || '').trim(),
            juz: a.juz,
            hizbQuarter: a.hizbQuarter
          });
        }
      }
    }
  }
  return map;
}

/**
 * Parse IndoPak JSON format (primary array or fallback API structure).
 * @param {object|Array} json
 * @returns {Map<string, string>}
 */
export function parseIndoPakData(json) {
  const map = new Map();
  if (json?.data?.surahs && Array.isArray(json.data.surahs)) {
    for (const s of json.data.surahs) {
      if (Array.isArray(s.ayahs)) {
        for (const a of s.ayahs) {
          map.set(`${s.number}:${a.numberInSurah}`, (a.text || '').trim());
        }
      }
    }
  } else if (Array.isArray(json)) {
    for (const item of json) {
      const s = item.chapter || item.surah;
      const a = item.verse || item.ayah;
      if (s && a && item.text) {
        map.set(`${s}:${a}`, (item.text || '').trim());
      }
    }
  } else if (json && typeof json === 'object') {
    const list = json.mushaf || Object.values(json).find(Array.isArray); if (Array.isArray(list)) for (const item of list) {
      const s = item.chapter || item.surah;
      const a = item.verse || item.ayah;
      if (s && a && item.text) {
        map.set(`${s}:${a}`, (item.text || '').trim());
      }
    }
  }
  return map;
}

/**
 * Fetch and assemble canonical Mushaf corpora directly via CacheStorage / CORS endpoints.
 * @param {object} options
 * @param {Function|null} onStatus
 * @returns {Promise<{ mushafBase: object, editionsData: object }>}
 */
export async function fetchMushafDatasets(options = {}, onStatus = null) {
  function status(msg) {
    if (typeof onStatus === 'function') onStatus(msg);
  }

  const scriptId = options.scriptId ?? options.scriptStyle ?? 0;
  const needUthmani = scriptId === 0 || !options.lazyScript;
  const needIndopak = scriptId === 1 || !options.lazyScript;
  const needLatin = options.layers?.latin ?? true;
  const needTrans = options.layers?.translation ?? true;
  const needTafsir = options.layers?.tafsir ?? true;
  const transLang = (options.translationEdition === 'en_saheeh' || options.translationId === 2) ? 'en' : 'id';
  const tafsirLang = (options.tafsirEdition === 'jalalayn' || options.tafsirId === 2) ? 'en' : 'id';

  status('Fetching Uthmani text corpus...');
  const uthmaniRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.uthmani);
  const uthmaniJson = await uthmaniRes.json();
  const uthmaniMap = parseMushafCloud(uthmaniJson);

  let indopakMap = null;
  if (needIndopak) {
    status('Fetching IndoPak text corpus...');
    try {
      const indopakRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.indopakPrimary);
      const indopakJson = await indopakRes.json();
      indopakMap = parseIndoPakData(indopakJson);
    } catch (err) {
      console.warn(`Primary IndoPak fetch failed: ${err.message}. Trying fallback...`);
      const fbRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.indopakFallback);
      const fbJson = await fbRes.json();
      indopakMap = parseIndoPakData(fbJson);
    }
  }

  let latinMap = null;
  if (needLatin) {
    status('Fetching Latin transliteration...');
    const latinRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.latin);
    const latinJson = await latinRes.json();
    latinMap = parseMushafCloud(latinJson);
  }

  let idTransMap = null;
  let enTransMap = null;
  if (needTrans) {
    if (transLang === 'id' || !options.lazyEditions) {
      status('Fetching Indonesian translation...');
      const idTrRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.idTrans);
      const idTrJson = await idTrRes.json();
      idTransMap = parseMushafCloud(idTrJson);
    }
    if (transLang === 'en' || !options.lazyEditions) {
      status('Fetching English translation...');
      const enTrRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.enTrans);
      const enTrJson = await enTrRes.json();
      enTransMap = parseMushafCloud(enTrJson);
    }
  }

  let idTafsirMap = null;
  let enTafsirMap = null;
  if (needTafsir) {
    if (tafsirLang === 'id' || !options.lazyEditions) {
      status('Fetching Indonesian tafsir...');
      const idTafRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.idTafsir);
      const idTafJson = await idTafRes.json();
      idTafsirMap = parseMushafCloud(idTafJson);
    }
    if (tafsirLang === 'en' || !options.lazyEditions) {
      status('Fetching English tafsir...');
      const enTafRes = await fetchWithCache(MUSHAF_DATA_ENDPOINTS.enTafsir);
      const enTafJson = await enTafRes.json();
      enTafsirMap = parseMushafCloud(enTafJson);
    }
  }

  status('Assembling Mushaf datasets...');
  const surahs = CANONICAL_SURAHS.map((s) => ({
    number: s.number,
    nameLatin: s.name,
    ayahCount: s.ayahs,
    startJuz: s.startJuz
  }));

  const ayahs = [];
  const idTranslations = [];
  const enTranslations = [];
  const idTafsirList = [];
  const enTafsirList = [];

  for (const s of CANONICAL_SURAHS) {
    for (let a = 1; a <= s.ayahs; a++) {
      const key = `${s.number}:${a}`;
      const u = uthmaniMap?.get(key);
      const ip = indopakMap?.get(key) || '';
      const lat = latinMap?.get(key);
      const idTr = idTransMap?.get(key);
      const enTr = enTransMap?.get(key);
      const idTaf = idTafsirMap?.get(key);
      const enTaf = enTafsirMap?.get(key);

      const uText = u?.text || '';
      const ipText = typeof ip === 'object' ? (ip.text || '') : (ip || '');
      const latText = lat?.text || '';
      const idTrText = idTr?.text || '';
      const enTrText = enTr?.text || '';
      const idTafText = idTaf?.text || '';
      const enTafText = enTaf?.text || '';

      const uthmaniWords = uText.trim().split(/\s+/).filter(Boolean);
      const indopakWords = ipText.trim().split(/\s+/).filter(Boolean);

      const juz = u?.juz || getJuzForSurahAyah(s.number, a);
      const hizbQuarter = u?.hizbQuarter || ((juz - 1) * 8 + 1);

      ayahs.push({
        surah: s.number,
        ayah: a,
        juz,
        hizbQuarter,
        uthmaniWords: uthmaniWords.length > 0 ? uthmaniWords : (indopakWords.length > 0 ? indopakWords : ['']),
        indopakWords: indopakWords.length > 0 ? indopakWords : (uthmaniWords.length > 0 ? uthmaniWords : ['']),
        latin: latText.trim(),
        translationId: idTrText.trim(),
        translationEn: enTrText.trim(),
        tafsirId: idTafText.trim(),
        tafsirEn: enTafText.trim()
      });

      idTranslations.push(idTrText.trim());
      enTranslations.push(enTrText.trim());
      idTafsirList.push(idTafText.trim());
      enTafsirList.push(enTafText.trim());
    }
  }

  const mushafBase = {
    surahs,
    ayahs
  };

  const editionsData = {
    translations: {
      id_kemenag: idTranslations,
      en_saheeh: enTranslations
    },
    tafsirs: {
      id_kemenag_ringkas: idTafsirList,
      jalalayn: enTafsirList
    }
  };

  return { mushafBase, editionsData };
}

// ---------------------------------------------------------------------------
// Standard IEEE 802.3 CRC32 Implementation
// ---------------------------------------------------------------------------
const CRC32_TABLE = new Uint32Array(256);
for (let i = 0; i < 256; i++) {
  let c = i;
  for (let j = 0; j < 8; j++) {
    c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
  }
  CRC32_TABLE[i] = c >>> 0;
}

export function calculateCrc32(bytes, length) {
  let crc = 0xFFFFFFFF;
  const len = length !== undefined ? length : bytes.length;
  for (let i = 0; i < len; i++) {
    crc = (crc >>> 8) ^ CRC32_TABLE[(crc ^ bytes[i]) & 0xFF];
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

// ---------------------------------------------------------------------------
// ASCII Sanitization & Text Wrapping
// ---------------------------------------------------------------------------
export function sanitizeAscii(str) {
  if (!str) return '';
  return str
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[\u201C\u201D]/g, '"')
    .replace(/[\u2013\u2014]/g, '-')
    .replace(/…/g, '...')
    .replace(/[^\x20-\x7E\n]/g, '')
    .trim();
}

export function wrapTextToLines(text, maxCols) {
  if (!text) return [];
  const clean = sanitizeAscii(text);
  const words = clean.split(/\s+/).filter(Boolean);
  const lines = [];
  let currentLine = '';

  for (const word of words) {
    if (word.length > maxCols) {
      if (currentLine) {
        lines.push(currentLine);
        currentLine = '';
      }
      for (let i = 0; i < word.length; i += maxCols) {
        lines.push(word.slice(i, i + maxCols));
      }
      continue;
    }

    const testLine = currentLine ? `${currentLine} ${word}` : word;
    if (testLine.length <= maxCols) {
      currentLine = testLine;
    } else {
      lines.push(currentLine);
      currentLine = word;
    }
  }

  if (currentLine) {
    lines.push(currentLine);
  }

  return lines;
}

// ---------------------------------------------------------------------------
// 1-Bit Word Sprite Rasterizer (Canvas 2D)
// ---------------------------------------------------------------------------
let _sharedRasterCanvas = null;

function getCanvasContext() {
  if (_sharedRasterCanvas) return _sharedRasterCanvas;
  if (typeof OffscreenCanvas !== 'undefined') {
    try {
      const oc = new OffscreenCanvas(300, 100);
      const ctx = oc.getContext('2d', { willReadFrequently: true });
      if (ctx) {
        _sharedRasterCanvas = { canvas: oc, ctx };
        return _sharedRasterCanvas;
      }
    } catch (err) {
      console.warn('OffscreenCanvas init warning:', err.message);
    }
  }
  if (typeof document !== 'undefined' && typeof document.createElement === 'function') {
    try {
      const c = document.createElement('canvas');
      c.width = 300;
      c.height = 100;
      const ctx = c.getContext('2d', { willReadFrequently: true });
      if (ctx) {
        _sharedRasterCanvas = { canvas: c, ctx };
        return _sharedRasterCanvas;
      }
    } catch (err) {
      console.warn('Document canvas init warning:', err.message);
    }
  }
  return null;
}

export function rasterizeWordTo1Bit(wordStr, ayahFontSize = 24, scriptId = 0, screenW = 200) {
  const envCtx = getCanvasContext();
  if (!envCtx || typeof envCtx.ctx.measureText !== 'function') {
    throw new Error('Canvas 2D context is required for 1-bit Mushaf glyph rasterization');
  }
  const { canvas, ctx } = envCtx;
  const fontName = scriptId === 1 ? 'IndoPak-Naskh, serif' : 'KFGQPC-Uthmanic, Amiri, serif';
  ctx.font = `${ayahFontSize}px ${fontName}`;
  ctx.textBaseline = 'top';

  const textMetrics = ctx.measureText(wordStr);
  const measuredW = Math.max(8, Math.ceil(textMetrics.width));
  const canvasW = Math.min(screenW - 16, Math.max(32, measuredW + 16));
  const canvasH = Math.max(36, ayahFontSize + 16);

  if (canvas.width < canvasW || canvas.height < canvasH) {
    canvas.width = Math.max(canvas.width, canvasW);
    canvas.height = Math.max(canvas.height, canvasH);
  }


  ctx.fillStyle = '#FFFFFF';
  ctx.fillRect(0, 0, canvasW, canvasH);
  ctx.fillStyle = '#000000';
  ctx.font = `${ayahFontSize}px ${fontName}`;
  ctx.textBaseline = 'top';
  ctx.fillText(wordStr, 4, 4);

  const imgData = ctx.getImageData(0, 0, canvasW, canvasH);
  const pixels = imgData.data;

  let minX = canvasW, minY = canvasH, maxX = -1, maxY = -1;
  for (let y = 0; y < canvasH; y++) {
    for (let x = 0; x < canvasW; x++) {
      const idx = (y * canvasW + x) * 4;
      const lum = 0.299 * pixels[idx] + 0.587 * pixels[idx + 1] + 0.114 * pixels[idx + 2];
      const alpha = pixels[idx + 3];
      if (lum < 160 && alpha > 100) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }

  if (maxX >= minX && maxY >= minY) {
    const width = maxX - minX + 1;
    const height = maxY - minY + 1;
    const strideBytes = (width + 7) >> 3;
    const yOffset = minY;
    const bitmapBytes = new Uint8Array(height * strideBytes);

    for (let r = 0; r < height; r++) {
      const srcY = minY + r;
      for (let c = 0; c < width; c++) {
        const srcX = minX + c;
        const idx = (srcY * canvasW + srcX) * 4;
        const lum = 0.299 * pixels[idx] + 0.587 * pixels[idx + 1] + 0.114 * pixels[idx + 2];
        const alpha = pixels[idx + 3];
        if (lum < 160 && alpha > 100) {
          bitmapBytes[r * strideBytes + (c >> 3)] |= (0x80 >> (c & 7));
        }
      }
    }

    return {
      width,
      height,
      yOffset,
      strideBytes,
      bitmapBytes
    };
  }

  return {
    width: 8,
    height: 8,
    yOffset: 0,
    strideBytes: 1,
    bitmapBytes: new Uint8Array(8)
  };
}

// ---------------------------------------------------------------------------
// 1. Exported Function: renderAyahPreviewToCanvas
// ---------------------------------------------------------------------------
export async function renderAyahPreviewToCanvas(canvas, options = {}) {
  if (!canvas) {
    throw new Error('Canvas element required for renderAyahPreviewToCanvas');
  }

  const surah = Math.max(1, Math.min(114, options.surah || 1));
  const ayah = Math.max(1, options.ayah || 1);
  const layer = Math.max(0, Math.min(3, options.layer || 0));
  const scriptId = options.scriptId ?? options.scriptStyle ?? 0;
  const translationId = options.translationId ?? (options.translationEdition === 'en_saheeh' ? 2 : 1);
  const tafsirId = options.tafsirId ?? (options.tafsirEdition === 'jalalayn' ? 2 : 1);
  const screenW = options.screenW || options.displayWidth || canvas.width || 200;
  const screenH = options.screenH || options.displayHeight || canvas.height || 200;
  const ayahFontSize = options.ayahFontSize || options.mushafFontSize || 24;
  const latinFontId = options.latinFontId || 0;

  canvas.width = screenW;
  canvas.height = screenH;

  // Ensure font is loaded in browser
  if (typeof ensureMushafFont === 'function') {
    try {
      await ensureMushafFont(scriptId);
    } catch (fontErr) {
      console.warn('Font load warning in renderAyahPreviewToCanvas:', fontErr.message);
    }
  }

  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  if (!ctx) {
    throw new Error('Canvas 2D rendering context unavailable');
  }

  // Clear canvas buffer to pure white
  ctx.fillStyle = '#FFFFFF';
  ctx.fillRect(0, 0, screenW, screenH);

  const surahInfo = CANONICAL_SURAHS[surah - 1] || { number: surah, name: 'Mushaf', ayahs: 7, startJuz: 1 };
  const juz = getJuzForSurahAyah(surah, ayah);

  // 1. Exact Header Geometry (y = 0..15, height 16px matching view_mushaf.c)
  ctx.fillStyle = '#000000';
  ctx.font = '12px monospace';
  ctx.textBaseline = 'top';

  const headerLeft = `${String(surah).padStart(2, '0')}:${ayah} ${surahInfo.name.slice(0, 9)}`;
  ctx.fillText(headerLeft, 4, 2);

  const layerTags = ['[M]', '[LAT]', '[TRN]', '[TAF]'];
  const midX = Math.round(screenW * 0.64);
  ctx.fillText(layerTags[layer] || '[M]', midX, 2);

  const batX = screenW - 35;
  ctx.fillText('100%', batX, 2);

  // Header separator line at y = 15
  ctx.fillRect(0, 15, screenW, 1);

  // 2. Exact Footer Geometry (y = screenH - 16..screenH - 1, height 16px)
  ctx.fillRect(0, screenH - 16, screenW, 1);
  ctx.fillText(`Juz ${juz}`, 4, screenH - 14);

  const pageStr = '1/1';
  const pageStrWidth = ctx.measureText(pageStr).width || 21;
  ctx.fillText(pageStr, screenW - pageStrWidth - 4, screenH - 14);

  // 3. Exact Body Geometry (y = 16 .. screenH - 17, height = screenH - 32)
  const key = `${surah}:${ayah}`;
  let ref = null;
  if (options.mushafBase && Array.isArray(options.mushafBase.ayahs)) {
    const found = options.mushafBase.ayahs.find((a) => a.surah === surah && a.ayah === ayah);
    if (found) {
      ref = {
        uthmaniWords: found.uthmaniWords,
        indopakWords: found.indopakWords,
        latin: found.latin,
        trId: found.translationId || options.editionsData?.translations?.id_kemenag?.[found.globalAyahIndex ?? -1],
        trEn: found.translationEn || options.editionsData?.translations?.en_saheeh?.[found.globalAyahIndex ?? -1],
        tafsirId: found.tafsirId || options.editionsData?.tafsirs?.id_kemenag_ringkas?.[found.globalAyahIndex ?? -1],
        tafsirEn: found.tafsirEn || options.editionsData?.tafsirs?.jalalayn?.[found.globalAyahIndex ?? -1]
      };
    }
  }
  if (!ref) {
    throw new Error(`Ayah ${key} not found in loaded Mushaf dataset`);
  }

  if (layer === 0) {
    // Mushaf Calligraphy (RTL Word Layout)
    const words = (scriptId === 1 ? ref.indopakWords : ref.uthmaniWords) || ref.uthmaniWords;
    const lineHeight = Math.max(ayahFontSize + 8, 28);
    const bodyHeight = screenH - 32;
    const margin = 6;
    const wordSpacing = 4;

    const wordSprites = words.map((w) => {
      const sp = rasterizeWordTo1Bit(w, ayahFontSize, scriptId, screenW);
      return { word: w, ...sp };
    });

    const lines = [];
    let currentLine = [];
    let currentLineWidth = 0;
    const maxLineWidth = screenW - (margin * 2);

    for (const sp of wordSprites) {
      const addedWidth = currentLine.length === 0 ? sp.width : (sp.width + wordSpacing);
      if (currentLineWidth + addedWidth <= maxLineWidth) {
        currentLine.push(sp);
        currentLineWidth += addedWidth;
      } else {
        if (currentLine.length > 0) lines.push(currentLine);
        currentLine = [sp];
        currentLineWidth = sp.width;
      }
    }
    if (currentLine.length > 0) lines.push(currentLine);

    const totalLinesHeight = lines.length * lineHeight;
    const startY = 16 + Math.max(2, Math.floor((bodyHeight - totalLinesHeight) / 2));

    for (let lineIdx = 0; lineIdx < lines.length; lineIdx++) {
      const lineWords = lines[lineIdx];
      const lineY = startY + (lineIdx * lineHeight);
      if (lineY + lineHeight > screenH - 16) break;

      let cursorX = screenW - margin;
      for (const sp of lineWords) {
        const placedX = cursorX - sp.width;
        const placedY = lineY + Math.max(0, Math.floor((lineHeight - sp.height) / 2));

        for (let r = 0; r < sp.height; r++) {
          const py = placedY + r;
          if (py < 16 || py >= screenH - 16) continue;
          for (let c = 0; c < sp.width; c++) {
            const px = placedX + c;
            if (px < 0 || px >= screenW) continue;
            const byteVal = sp.bitmapBytes[r * sp.strideBytes + (c >> 3)];
            if ((byteVal & (0x80 >> (c & 7))) !== 0) {
              ctx.fillRect(px, py, 1, 1);
            }
          }
        }
        cursorX = placedX - wordSpacing;
      }
    }
  } else {
    // Text Layers: Latin (1), Translation (2), Tafsir (3)
    let textToDraw = '';
    if (layer === 1) {
      textToDraw = ref.latin;
    } else if (layer === 2) {
      textToDraw = translationId === 2 ? ref.trEn : ref.trId;
    } else {
      textToDraw = tafsirId === 2 ? ref.tafsirEn : ref.tafsirId;
    }

    const charW = latinFontId === 1 ? 11 : 7;
    const charH = latinFontId === 1 ? 16 : 12;
    const maxCols = Math.floor((screenW - 8) / charW);
    const lines = wrapTextToLines(textToDraw, maxCols);

    ctx.font = `${charH}px monospace`;
    ctx.textBaseline = 'top';

    let curY = 18;
    for (const ln of lines) {
      if (curY + charH >= screenH - 16) break;
      ctx.fillText(ln, 4, curY);
      curY += charH + 2;
    }
  }

  // 4. Strict 1-Bit Luminance Thresholding
  try {
    const frame = ctx.getImageData(0, 0, screenW, screenH);
    const d = frame.data;
    for (let i = 0; i < d.length; i += 4) {
      const lum = 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
      const a = d[i + 3];
      const bit = (lum < 160 && a > 64) ? 0 : 255;
      d[i] = bit;
      d[i + 1] = bit;
      d[i + 2] = bit;
      d[i + 3] = 255;
    }
    ctx.putImageData(frame, 0, 0);
  } catch (err) {
    console.warn('1-bit thresholding warning:', err.message);
  }
}

// ---------------------------------------------------------------------------
// 2. Exported Function: buildBqmPackage
// ---------------------------------------------------------------------------
export async function buildBqmPackage(options = {}, onProgress = null) {
  const scriptId = options.scriptId ?? options.scriptStyle ?? 0;
  const translationId = options.translationId ?? (options.translationEdition === 'en_saheeh' ? 2 : (options.translationEdition === 'id_kemenag' ? 1 : 1));
  const tafsirId = options.tafsirId ?? (options.tafsirEdition === 'jalalayn' ? 2 : (options.tafsirEdition === 'id_kemenag_ringkas' ? 1 : 1));
  const ayahFontSize = options.ayahFontSize ?? options.mushafFontSize ?? 24;
  const latinFontId = options.latinFontId ?? 0;
  const screenW = options.screenW ?? options.displayWidth ?? 200;
  const screenH = options.screenH ?? options.displayHeight ?? 200;

  const showMushaf = options.layers?.mushaf ?? true;
  const showLatin = options.layers?.latin ?? (options.showLatin ?? true);
  const showTranslation = options.layers?.translation ?? (options.showTranslation ?? (translationId !== 0));
  const showTafsir = options.layers?.tafsir ?? (options.showTafsir ?? (tafsirId !== 0));

  function reportProgress(pct, stage) {
    if (typeof onProgress === 'function') {
      try {
        onProgress({
          stage,
          percent: Math.round(pct * 100),
          current: Math.round(pct * 100),
          total: 100
        });
        onProgress(pct, stage);
      } catch (err) {
        console.warn('onProgress handler warning:', err.message);
      }
    }
  }

  reportProgress(0.05, 'Loading Mushaf datasets');

  // Load or acquire datasets
  let mushafBase = options.mushafBase || null;
  let editionsData = options.editionsData || null;

  if (!mushafBase || !editionsData) {
    reportProgress(0.08, 'Fetching Mushaf datasets (bacain-mushaf-assets-v1)...');
    const datasets = await fetchMushafDatasets(options, (statusMsg) => {
      reportProgress(0.10, statusMsg);
    });
    if (!mushafBase) mushafBase = datasets.mushafBase;
    if (!editionsData) editionsData = datasets.editionsData;
  }

  // Ensure calligraphy font is loaded in browser
  if (typeof ensureMushafFont === 'function') {
    try {
      await ensureMushafFont(scriptId, (statusMsg) => reportProgress(0.12, statusMsg));
    } catch (fontErr) {
      console.warn('Font loading notice in buildBqmPackage:', fontErr.message);
    }
  }

  reportProgress(0.15, 'Preparing Surah and Ayah indices');

  const surahs = CANONICAL_SURAHS;
  const TOTAL_SURAHS = 114;
  const TOTAL_AYAHS = 6236;

  // Build map of provided ayahs
  const ayahMap = new Map();
  if (mushafBase && Array.isArray(mushafBase.ayahs)) {
    for (const a of mushafBase.ayahs) {
      ayahMap.set(`${a.surah}:${a.ayah}`, a);
    }
  }

  // Pre-calculate first_global_ayah for each Surah
  const surahMeta = [];
  let globalAyahCounter = 0;
  for (let s = 0; s < TOTAL_SURAHS; s++) {
    const sInfo = surahs[s];
    surahMeta.push({
      surah_number: sInfo.number,
      ayah_count: sInfo.ayahs,
      first_global_ayah: globalAyahCounter,
      juz_start: sInfo.startJuz,
      revelation_type: MEDINAN_SURAHS.has(sInfo.number) ? 1 : 0,
      name_latin: sInfo.name
    });
    globalAyahCounter += sInfo.ayahs;
  }

  reportProgress(0.25, 'Rasterizing word sprites and placing layouts');

  // Dictionary structures
  const wordDict = new Map();
  const wordSprites = [];
  const spriteBlobParts = [];
  let spriteBlobLen = 0;

  function getWordSprite(wordStr) {
    if (wordDict.has(wordStr)) {
      return wordDict.get(wordStr);
    }
    const raster = rasterizeWordTo1Bit(wordStr, ayahFontSize, scriptId, screenW);
    const sprite = {
      word_id: wordSprites.length,
      width: raster.width,
      height: raster.height,
      y_offset: raster.yOffset,
      stride_bytes: raster.strideBytes,
      bitmap_off: spriteBlobLen,
      bytes: raster.bitmapBytes
    };
    wordDict.set(wordStr, sprite);
    wordSprites.push(sprite);
    spriteBlobParts.push(raster.bitmapBytes);
    spriteBlobLen += raster.bitmapBytes.length;
    return sprite;
  }

  // Buffers for Mushaf Subpages and Text Blob
  const mushafAyahRecords = [];
  let mushafPagesLen = 0;

  const textLayerRecords = [];
  let textBlobLen = 0;

  const charW = latinFontId === 1 ? 11 : 7;
  const charH = latinFontId === 1 ? 16 : 12;
  const maxCols = Math.floor((screenW - 8) / charW);
  const maxLines = Math.floor((screenH - 36) / (charH + 2));

  const lineHeight = Math.max(ayahFontSize + 8, 28);
  const bodyHeight = screenH - 32;
  const margin = 6;
  const wordSpacing = 4;
  const maxLineWidth = screenW - (margin * 2);
  const maxMushafLinesPerSubpage = Math.max(1, Math.floor(bodyHeight / lineHeight));

  const ayahEntries = [];
  const startSurah = Math.max(1, Math.min(114, options.startSurah || 1));
  const endSurah = Math.max(startSurah, Math.min(114, options.endSurah || 114));

  for (let s = 1; s <= TOTAL_SURAHS; s++) {
    const sInfo = surahs[s - 1];
    const inRange = (s >= startSurah && s <= endSurah);
    for (let a = 1; a <= sInfo.ayahs; a++) {
      const key = `${s}:${a}`;
      const juz = getJuzForSurahAyah(s, a);
      const src = ayahMap.get(key);
      const hizb = src?.hizbQuarter || ((juz - 1) * 8 + 1);

      if (!inRange) {
        ayahEntries.push({
          juz,
          hizb_quarter: hizb,
          mushaf_subpages: 0,
          latin_subpages: 0,
          trans_subpages: 0,
          tafsir_subpages: 0,
          reserved: 0,
          mushaf_off: 0,
          latin_off: 0,
          trans_off: 0,
          tafsir_off: 0
        });
        continue;
      }

      if (!src) {
        throw new Error(`Ayah ${key} missing from loaded Mushaf dataset`);
      }

      // --- 1. Mushaf Subpage Generation ---
      let mushafSubpages = 0;
      let mushafOff = 0;

      if (showMushaf) {
        const words = scriptId === 1 ? (src.indopakWords || src.words) : (src.uthmaniWords || src.words);
        if (!Array.isArray(words) || words.length === 0) {
          throw new Error(`Ayah ${key} missing Arabic word tokens`);
        }

        const sprites = words.map(getWordSprite);

        // Group into lines (RTL)
        const lines = [];
        let curLine = [];
        let curLineWidth = 0;

        for (const sp of sprites) {
          const added = curLine.length === 0 ? sp.width : (sp.width + wordSpacing);
          if (curLineWidth + added <= maxLineWidth) {
            curLine.push(sp);
            curLineWidth += added;
          } else {
            if (curLine.length > 0) lines.push(curLine);
            curLine = [sp];
            curLineWidth = sp.width;
          }
        }
        if (curLine.length > 0) lines.push(curLine);

        // Paginate lines into subpages
        const subpages = [];
        for (let i = 0; i < lines.length; i += maxMushafLinesPerSubpage) {
          subpages.push(lines.slice(i, i + maxMushafLinesPerSubpage));
        }
        if (subpages.length === 0) subpages.push([]);

        mushafSubpages = subpages.length;
        mushafOff = mushafPagesLen;

        const subpageRelOffsets = new Uint16Array(mushafSubpages);
        const subpageByteChunks = [];
        let subpageHeaderSize = mushafSubpages * 2;
        let runningRelOffset = subpageHeaderSize;

        for (let spIdx = 0; spIdx < mushafSubpages; spIdx++) {
          subpageRelOffsets[spIdx] = runningRelOffset;
          const pageLines = subpages[spIdx];
          const totalPageHeight = pageLines.length * lineHeight;
          const startY = 16 + Math.max(2, Math.floor((bodyHeight - totalPageHeight) / 2));

          const placedWords = [];
          for (let lIdx = 0; lIdx < pageLines.length; lIdx++) {
            const lineWords = pageLines[lIdx];
            const lineY = startY + (lIdx * lineHeight);
            let cursorX = screenW - margin;

            for (const sp of lineWords) {
              const placedX = Math.max(0, cursorX - sp.width);
              let placedY = lineY;

              if (placedY < 16) placedY = 16;
              if (placedY + sp.height > screenH - 16) {
                placedY = screenH - 16 - sp.height;
              }

              placedWords.push({
                word_id: sp.word_id,
                x: placedX,
                y: placedY
              });

              cursorX = placedX - wordSpacing;
            }
          }

          const count = Math.min(64, placedWords.length);
          const chunk = new Uint8Array(1 + count * 6);
          chunk[0] = count;
          const view = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength);

          for (let pw = 0; pw < count; pw++) {
            const off = 1 + pw * 6;
            view.setUint16(off, placedWords[pw].word_id, true);
            view.setUint16(off + 2, placedWords[pw].x, true);
            view.setUint16(off + 4, placedWords[pw].y, true);
          }

          subpageByteChunks.push(chunk);
          runningRelOffset += chunk.length;
        }

        const headerBytes = new Uint8Array(subpageRelOffsets.buffer);
        mushafAyahRecords.push(headerBytes);
        for (const ch of subpageByteChunks) mushafAyahRecords.push(ch);
        mushafPagesLen += runningRelOffset;
      }

      // --- 2. Text Layers (Latin, Translation, Tafsir) ---
      function encodeTextLayer(enabled, textContent) {
        if (!enabled || !textContent) {
          return { subpages: 0, off: 0 };
        }
        const lines = wrapTextToLines(textContent, maxCols);
        const subpages = [];
        for (let i = 0; i < lines.length; i += maxLines) {
          subpages.push(lines.slice(i, i + maxLines));
        }
        if (subpages.length === 0) subpages.push([]);

        const subpageCount = subpages.length;
        const layerOff = textBlobLen;
        const subpageRelOffsets = new Uint16Array(subpageCount);
        const subpageByteChunks = [];
        let runningRelOffset = subpageCount * 2;

        for (let spIdx = 0; spIdx < subpageCount; spIdx++) {
          subpageRelOffsets[spIdx] = runningRelOffset;
          const pageLines = subpages[spIdx];
          let subpageByteSize = 1;
          for (const ln of pageLines) {
            subpageByteSize += 1 + ln.length;
          }

          const chunk = new Uint8Array(subpageByteSize);
          chunk[0] = pageLines.length;
          let wOff = 1;
          for (const ln of pageLines) {
            chunk[wOff++] = ln.length;
            for (let c = 0; c < ln.length; c++) {
              chunk[wOff++] = ln.charCodeAt(c) & 0x7F;
            }
          }

          subpageByteChunks.push(chunk);
          runningRelOffset += chunk.length;
        }

        const headerBytes = new Uint8Array(subpageRelOffsets.buffer);
        textLayerRecords.push(headerBytes);
        for (const ch of subpageByteChunks) textLayerRecords.push(ch);
        textBlobLen += runningRelOffset;

        return { subpages: subpageCount, off: layerOff };
      }

      const latinText = src.latin || '';
      const transText = translationId === 2
        ? (src.translationEn || src.trEn || editionsData?.translations?.en_saheeh?.[ayahEntries.length] || '')
        : (src.translationId || src.trId || editionsData?.translations?.id_kemenag?.[ayahEntries.length] || '');
      const tafsirText = tafsirId === 2
        ? (src.tafsirEn || editionsData?.tafsirs?.jalalayn?.[ayahEntries.length] || '')
        : (src.tafsirId || editionsData?.tafsirs?.id_kemenag_ringkas?.[ayahEntries.length] || '');

      const latinRec = encodeTextLayer(showLatin, latinText);
      const transRec = encodeTextLayer(showTranslation, transText);
      const tafsirRec = encodeTextLayer(showTafsir, tafsirText);

      ayahEntries.push({
        juz,
        hizb_quarter: hizb,
        mushaf_subpages: mushafSubpages,
        latin_subpages: latinRec.subpages,
        trans_subpages: transRec.subpages,
        tafsir_subpages: tafsirRec.subpages,
        reserved: 0,
        mushaf_off: mushafOff,
        latin_off: latinRec.off,
        trans_off: transRec.off,
        tafsir_off: tafsirRec.off
      });
    }
  }

  reportProgress(0.75, 'Assembling BQM container');

  // Section Offsets matching bqm_header_t in mushaf_reader.h
  const HEADER_SIZE = 64;
  const SURAH_TABLE_OFF = HEADER_SIZE;
  const SURAH_TABLE_SIZE = TOTAL_SURAHS * 24; // 2736 bytes
  const AYAH_TABLE_OFF = SURAH_TABLE_OFF + SURAH_TABLE_SIZE; // 2800 bytes
  const AYAH_TABLE_SIZE = TOTAL_AYAHS * 24; // 149664 bytes
  const WORD_DICT_OFF = AYAH_TABLE_OFF + AYAH_TABLE_SIZE; // 152464 bytes
  const WORD_DICT_SIZE = wordSprites.length * 8;

  let currentBlobOffset = WORD_DICT_OFF + WORD_DICT_SIZE;

  // Blob Layout: sprite_blob_off, mushaf_pages_off, text_blob_off
  const spriteBlobOff = currentBlobOffset;
  currentBlobOffset += spriteBlobLen;

  const mushafPagesOff = currentBlobOffset;
  currentBlobOffset += mushafPagesLen;

  const textBlobOff = currentBlobOffset;
  currentBlobOffset += textBlobLen;

  const totalFileSize = currentBlobOffset;
  const bqmBuffer = new Uint8Array(totalFileSize);
  const dv = new DataView(bqmBuffer.buffer);

  // 1. Write Header (64 bytes bqm_header_t)
  // Magic 'BQM1'
  bqmBuffer[0] = 0x42;
  bqmBuffer[1] = 0x51;
  bqmBuffer[2] = 0x4D;
  bqmBuffer[3] = 0x31;
  dv.setUint16(4, 1, true); // Version = 1

  let layerMask = 0;
  if (showMushaf) layerMask |= 0x01;
  if (showLatin) layerMask |= 0x02;
  if (showTranslation) layerMask |= 0x04;
  if (showTafsir) layerMask |= 0x08;

  dv.setUint8(6, layerMask); // layer_mask
  dv.setUint8(7, scriptId & 0x01); // mushaf_style_id (0 = Uthmani, 1 = IndoPak)
  dv.setUint16(8, screenW, true); // display_width
  dv.setUint16(10, screenH, true); // display_height
  dv.setUint16(12, TOTAL_SURAHS, true); // total_surahs = 114
  dv.setUint16(14, TOTAL_AYAHS, true); // total_ayahs = 6236
  dv.setUint32(16, wordSprites.length, true); // word_dict_count
  dv.setUint32(20, SURAH_TABLE_OFF, true); // surah_table_off = 64
  dv.setUint32(24, AYAH_TABLE_OFF, true); // ayah_table_off = 2800
  dv.setUint32(28, WORD_DICT_OFF, true); // word_dict_off
  dv.setUint32(32, spriteBlobOff, true); // sprite_blob_off
  dv.setUint32(36, mushafPagesOff, true); // mushaf_pages_off
  dv.setUint32(40, textBlobOff, true); // text_blob_off

  const editionTag = `${scriptId === 1 ? 'IndoPak' : 'Uthmani'}|${translationId === 2 ? 'EN-Sahih' : 'ID-Kemenag'}`;
  for (let i = 0; i < 20 && i < editionTag.length; i++) {
    bqmBuffer[44 + i] = editionTag.charCodeAt(i) & 0x7F;
  }

  // 2. Write Surah Table (114 * 24 bytes bqm_surah_entry_t at offset 64)
  for (let s = 0; s < TOTAL_SURAHS; s++) {
    const sOff = SURAH_TABLE_OFF + s * 24;
    const sm = surahMeta[s];
    dv.setUint8(sOff, sm.surah_number);
    dv.setUint16(sOff + 1, sm.ayah_count, true);
    dv.setUint16(sOff + 3, sm.first_global_ayah, true);
    dv.setUint8(sOff + 5, sm.juz_start);

    // name_latin[18] at byte 6
    const nameBytes = sanitizeAscii(sm.name_latin);
    for (let i = 0; i < 18; i++) {
      bqmBuffer[sOff + 6 + i] = i < nameBytes.length ? (nameBytes.charCodeAt(i) & 0x7F) : 0;
    }
  }

  // 3. Write Ayah Table (6236 * 24 bytes bqm_ayah_entry_t at offset 2800)
  for (let a = 0; a < TOTAL_AYAHS; a++) {
    const aOff = AYAH_TABLE_OFF + a * 24;
    const ae = ayahEntries[a];
    dv.setUint8(aOff, ae.juz);
    dv.setUint8(aOff + 1, ae.hizb_quarter);
    dv.setUint8(aOff + 2, ae.mushaf_subpages);
    dv.setUint8(aOff + 3, ae.latin_subpages);
    dv.setUint8(aOff + 4, ae.trans_subpages);
    dv.setUint8(aOff + 5, ae.tafsir_subpages);
    dv.setUint16(aOff + 6, ae.reserved, true);
    dv.setUint32(aOff + 8, ae.mushaf_off, true);
    dv.setUint32(aOff + 12, ae.latin_off, true);
    dv.setUint32(aOff + 16, ae.trans_off, true);
    dv.setUint32(aOff + 20, ae.tafsir_off, true);
  }

  // 4. Write Word Sprite Dictionary (unique_words * 8 bytes bqm_word_sprite_t at WORD_DICT_OFF)
  for (let w = 0; w < wordSprites.length; w++) {
    const wdOff = WORD_DICT_OFF + w * 8;
    const sp = wordSprites[w];
    dv.setUint8(wdOff, sp.width);
    dv.setUint8(wdOff + 1, sp.height);
    dv.setInt8(wdOff + 2, sp.y_offset);
    dv.setUint8(wdOff + 3, sp.stride_bytes);
    dv.setUint32(wdOff + 4, sp.bitmap_off, true);
  }

  // 5. Write Blobs in exact firmware offset sequence
  // Sprite Blob
  let writePtr = spriteBlobOff;
  for (const part of spriteBlobParts) {
    bqmBuffer.set(part, writePtr);
    writePtr += part.length;
  }

  // Mushaf Pages Blob
  writePtr = mushafPagesOff;
  for (const chunk of mushafAyahRecords) {
    bqmBuffer.set(chunk, writePtr);
    writePtr += chunk.length;
  }

  // Text Blob
  writePtr = textBlobOff;
  for (const chunk of textLayerRecords) {
    bqmBuffer.set(chunk, writePtr);
    writePtr += chunk.length;
  }

  reportProgress(1.0, 'Complete');

  return bqmBuffer;
}

// ---------------------------------------------------------------------------
// Global Window Attachment
// ---------------------------------------------------------------------------
if (typeof window !== 'undefined') {
  window.BqmBuilder = {
    renderAyahPreviewToCanvas,
    buildBqmPackage,
    fetchWithCache,
    ensureMushafFont,
    fetchMushafDatasets,
    MUSHAF_CACHE_NAME,
    MUSHAF_FONTS,
    MUSHAF_DATA_ENDPOINTS,
    SURAHS,
    CANONICAL_SURAHS,
    JUZ_BOUNDARIES,
    JUZ_STARTS,
    getJuzForSurahAyah,
    getJuzForAyah,
    MEDINAN_SURAHS,
    LAYER_TAGS,
    LAYER_NAMES,
    SUNNAH_PRESETS,
    MUSHAF_LAYERS,
    calculateCrc32
  };
}
