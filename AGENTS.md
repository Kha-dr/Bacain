# Project Agent Rules & Workflow (`AGENTS.md`)

Aturan ini bersifat **WAJIB (`always_on`)** untuk setiap agent dan subagent yang bekerja di repositori ini.

## 1. Filosofi Kode (`ponytail` + Zero-Mock)

1. **Zero Mock / Zero Fake Data**:
   - Dilarang keras membuat implementasi mock, dummy return, progress bar palsu, atau angka telemetri tiruan (`Math.random()`, `setTimeout` palsu, dsb.) agar fitur terlihat "jadi".
   - Jika suatu engine atau fitur belum diimplementasikan (`direct-ort`, `wasm-ggml`, `webnn`), tandai `this.available = false` dan lempar `new Error('... belum tersedia.')` secara jujur dan eksplisit.
2. **Zero Silent Fallback**:
   - Dilarang keras melakukan fallback diam-diam ke engine/metode lain saat inisialisasi, warmup, atau transkripsi gagal, serta dilarang membuat blok `catch {}` kosong yang menelan error.
3. **Ponytail (Smallest Complete Change)**:
   - Tulis perubahan terkecil yang menyelesaikan akar masalah secara tuntas tanpa abstraksi berlapis, dead code, atau spekulasi fitur masa depan.

## 2. Protokol Delegasi Multi-Subagent (Anti-Context-Poisoning & Anti-Halusinasi)

Untuk menjaga context window main agent tetap ringan dan mencegah _context poisoning_ / halusinasi pada subagent:

1. **Granularitas Sempit & Non-Overlapping**:
   - Saat mendelegasikan tugas ke subagent (`invoke_subagent`), pecah pekerjaan menjadi unit-unit kecil yang **spesifik pada 1–4 berkas saja** dan **tidak saling tumpang-tindih (non-overlapping file scope)** antar subagent paralel.
   - Cantumkan daftar berkas yang boleh diubah (`RUANG LINGKUP BERKAS`) dan larang menyentuh berkas di luar daftar tersebut.
2. **Instruksi Baris & Spesifikasi Eksplisit (Zero-Ambiguity Prompting)**:
   - Jangan memberi instruksi kabur kepada subagent. Berikan lokasi fungsi/baris target dan spesifikasi logika/kontrak yang eksak.
   - Gunakan `Model: 'flash'` untuk tugas implementasi dan verifikasi yang sudah terdefinisi secara presisi.
3. **Pemisahan Ketat: Implementor ≠ Verifier (Zero-Trust Verification)**:
   - **Dilarang keras** menggunakan subagent yang sama untuk memverifikasi kode yang baru saja ditulisnya sendiri.
   - Setiap batch perubahan **WAJIB** diverifikasi oleh subagent verifikator independen (atau skrip pengujian whitebox Node.js terpisah) dengan cakupan verifikasi yang kecil, spesifik, dan menguji modul asli (bukan mock test) ditambah pemeriksaan `git diff` bahwa tidak ada jalan pintas/mock.
4. **Audit Final Wajib oleh Main Agent**:
   - Setelah subagent implementor dan subagent verifikator selesai, **Main Agent WAJIB melakukan verifikasi akhir sendiri** (membaca kode yang diubah, memeriksa `git status` / `git diff`, dan menjalankan ulang skrip tes verifikasi secara langsung) sebelum melaporkan tugas selesai kepada user.

## 3. Gaya Komunikasi & Kode (Lugas, Teknis, Anti-AI-Slop)

1. **Tanpa Jargon Berlebihan / AI Slop**:
   - Gunakan bahasa sederhana, langsung ke inti teknis, dan terukur.
   - Hindari istilah pemasaran/buzzword yang tidak perlu dan mengotori konteks (_context poisoning_). Jika menjelaskan hal teknis, sebutkan langsung nama berkas, fungsi, ukuran data, atau angka waktunya.
2. **Dilarang Menggunakan Kata/Penamaan Menggelembung (Zero Inflated Words)**:
   - Dilarang memberi nama variabel, objek, konstanta, atau modul dengan gaya bombastis atau pemasaran (seperti `BACA_CONFIG`, `ENTERPRISE_SUITE`, dsb.). Gunakan nama standar, rendah hati, dan fungsional seperti `CONFIG`, `state`, `options`.
   - Dilarang menulis komentar, docstring, atau log yang bernada iklan atau promosi (seperti "World-class", "Single source of truth", "Next-gen", "Ultra-fast"). Tulis langsung apa yang dilakukan kode secara faktual tanpa kata sifat berlebihan.
3. **Tanpa Tanda Baca Berlebihan (No Period/Exclamation Slop)**:
   - Dilarang menggunakan titik beruntun berlebihan (`...`, `???`), tanda seru berulang (`!!!`), atau gaya penulisan dramatis. Jaga komentar kode dan teks antarmuka tetap bersih, tenang, dan profesional.
