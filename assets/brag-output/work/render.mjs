import fs from "node:fs";
import path from "node:path";
import { spawn, execFileSync } from "node:child_process";
import { pathToFileURL } from "node:url";

const ROOT = path.resolve(import.meta.dirname, "..", "..");
const OUT_DIR = path.join(ROOT, "brag-output");
const WORK_DIR = path.join(OUT_DIR, "work");
const COMP_HTML = path.join(WORK_DIR, "composition.html");
const SKILL_ASSETS = "C:/Users/abdka/.gemini/config/skills/brag/assets";

const CHROME_CANDIDATES = [
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
];

function findBrowser() {
  for (const p of CHROME_CANDIDATES) {
    if (fs.existsSync(p)) return p;
  }
  throw new Error("No Chromium/Edge binary found for rendering.");
}

async function launchPage(browserPath, url) {
  const userDataDir = path.join(WORK_DIR, "chrome-profile");
  fs.mkdirSync(userDataDir, { recursive: true });

  const proc = spawn(
    browserPath,
    [
      "--headless=new",
      "--no-sandbox",
      "--disable-gpu",
      "--remote-debugging-port=0",
      `--user-data-dir=${userDataDir}`,
      "--window-size=1920,1080",
      "--force-device-scale-factor=1",
      "--hide-scrollbars",
      "--allow-file-access-from-files",
      "--disable-background-timer-throttling",
      "--disable-renderer-backgrounding",
      "--no-first-run",
      url,
    ],
    { stdio: ["ignore", "ignore", "pipe"] },
  );

  const wsBrowserUrl = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error("Timed out waiting for Chrome DevTools endpoint."));
    }, 15000);
    let buf = "";
    proc.stderr.on("data", (chunk) => {
      buf += chunk.toString("utf8");
      const m = buf.match(/DevTools listening on (ws:\/\/\S+)/);
      if (m) {
        clearTimeout(timer);
        resolve(m[1]);
      }
    });
    proc.once("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`Browser exited early with code ${code}: ${buf}`));
    });
  });

  const parsed = new URL(wsBrowserUrl);
  const targetsRes = await fetch(`http://${parsed.host}/json/list`);
  const targets = await targetsRes.json();
  const pageTarget = targets.find((t) => t.type === "page") || targets[0];
  if (!pageTarget?.webSocketDebuggerUrl) {
    throw new Error("Could not find page WebSocket target.");
  }

  const ws = new WebSocket(pageTarget.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.addEventListener("open", resolve, { once: true });
    ws.addEventListener("error", (e) => reject(new Error(`WS error: ${e.message || e}`)), { once: true });
  });

  let nextId = 1;
  const pending = new Map();
  ws.addEventListener("message", (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) reject(new Error(msg.error.message));
      else resolve(msg.result);
    }
  });

  const send = (method, params = {}) =>
    new Promise((resolve, reject) => {
      const id = nextId++;
      pending.set(id, { resolve, reject });
      ws.send(JSON.stringify({ id, method, params }));
    });

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Emulation.setDeviceMetricsOverride", {
    width: 1920,
    height: 1080,
    deviceScaleFactor: 1,
    mobile: false,
  });

  // Wait for document ready, fonts, and all images loaded cleanly
  const readiness = await send("Runtime.evaluate", {
    expression: `(async () => {
      await document.fonts.ready;
      const imgs = Array.from(document.images);
      await Promise.all(imgs.map((img) => img.complete ? Promise.resolve() : new Promise((r) => { img.onload = r; img.onerror = r; })));
      return { total: imgs.length, broken: imgs.filter((i) => i.naturalWidth === 0).map((i) => i.src) };
    })()`,
    awaitPromise: true,
    returnByValue: true,
  });
  const info = readiness.result.value;
  if (info.broken.length > 0) {
    throw new Error(`Broken images in composition: ${info.broken.join(", ")}`);
  }

  async function captureAt(tSec, outFile) {
    await send("Runtime.evaluate", {
      expression: `new Promise((resolve) => {
        window.setVideoTime(${tSec});
        requestAnimationFrame(() => requestAnimationFrame(resolve));
      })`,
      awaitPromise: true,
    });
    const shot = await send("Page.captureScreenshot", {
      format: "jpeg",
      quality: 92,
    });
    fs.writeFileSync(outFile, Buffer.from(shot.data, "base64"));
  }

  async function close() {
    ws.close();
    proc.kill();
    await new Promise((r) => setTimeout(r, 300));
    fs.rmSync(userDataDir, { recursive: true, force: true });
  }

  return { captureAt, close };
}

async function runStills() {
  const stillsDir = path.join(WORK_DIR, "stills");
  fs.mkdirSync(stillsDir, { recursive: true });
  const browser = findBrowser();
  const page = await launchPage(browser, pathToFileURL(COMP_HTML).href);

  const checks = [
    ["still_01_s1.jpg", 1.95],
    ["still_02_t12.jpg", 3.17],
    ["still_03_s2_poster.jpg", 4.8],
    ["still_04_s3_mid.jpg", 8.6],
    ["still_05_s3_done.jpg", 10.6],
    ["still_06_s4.jpg", 14.5],
    ["still_07_t45.jpg", 16.33],
    ["still_08_s5.jpg", 18.5],
  ];

  try {
    for (const [name, sec] of checks) {
      await page.captureAt(sec, path.join(stillsDir, name));
      console.log(`Captured ${name} (${sec}s)`);
    }
  } finally {
    await page.close();
  }
}

async function runFullRender() {
  const { default: ffmpegPath } = await import("ffmpeg-static");
  if (!ffmpegPath || !fs.existsSync(ffmpegPath)) {
    throw new Error("ffmpeg-static binary not found.");
  }

  const framesDir = path.join(WORK_DIR, "frames");
  fs.mkdirSync(framesDir, { recursive: true });
  const browser = findBrowser();
  const page = await launchPage(browser, pathToFileURL(COMP_HTML).href);

  const fps = 30;
  const durationSec = 20;
  const totalFrames = fps * durationSec;

  try {
    for (let i = 0; i < totalFrames; i++) {
      const t = i / fps;
      const frameName = `frame_${String(i).padStart(4, "0")}.jpg`;
      await page.captureAt(t, path.join(framesDir, frameName));
      if (i % 60 === 0 || i === totalFrames - 1) {
        console.log(`Rendered frame ${i + 1}/${totalFrames}`);
      }
    }
  } finally {
    await page.close();
  }

  const musicFile = `${SKILL_ASSETS}/music/happy-beats-business-moves-vol-9-by-ende-dot-app.mp3`;
  const sfxClick = `${SKILL_ASSETS}/sfx/interface/click_002.ogg`;
  const sfxCard = `${SKILL_ASSETS}/sfx/casino/card-place-1.ogg`;
  const sfxSwitch = `${SKILL_ASSETS}/sfx/interface/switch_004.ogg`;
  const sfxImpact = `${SKILL_ASSETS}/sfx/impact/impactSoft_medium_001.ogg`;

  const rawMp4 = path.join(WORK_DIR, "brag_raw.mp4");
  const posterJpg = path.join(OUT_DIR, "brag.jpg");
  const finalMp4 = path.join(OUT_DIR, "brag.mp4");

  const filterComplex = [
    "[1:a]atrim=0:20,asetpts=PTS-STARTPTS,afade=t=in:st=0:d=0.4,afade=t=out:st=18.2:d=1.8,volume=0.45[bgm]",
    "[2:a]adelay=350|350,volume=0.35[s1]",
    "[3:a]adelay=3450|3450,volume=0.35[s2]",
    "[4:a]adelay=7350|7350,volume=0.30[s3]",
    "[2:a]adelay=9900|9900,volume=0.32[s4]",
    "[3:a]adelay=11900|11900,volume=0.35[s5]",
    "[5:a]adelay=16450|16450,volume=0.38[s6]",
    "[bgm][s1][s2][s3][s4][s5][s6]amix=inputs=7:duration=first:normalize=0[aout]",
  ].join(";");

  console.log("Encoding video with mixed music & SFX...");
  execFileSync(
    ffmpegPath,
    [
      "-y",
      "-framerate",
      String(fps),
      "-i",
      path.join(framesDir, "frame_%04d.jpg"),
      "-i",
      musicFile,
      "-i",
      sfxClick,
      "-i",
      sfxCard,
      "-i",
      sfxSwitch,
      "-i",
      sfxImpact,
      "-filter_complex",
      filterComplex,
      "-map",
      "0:v",
      "-map",
      "[aout]",
      "-c:v",
      "libx264",
      "-crf",
      "18",
      "-preset",
      "medium",
      "-pix_fmt",
      "yuv420p",
      "-c:a",
      "aac",
      "-b:a",
      "192k",
      "-t",
      String(durationSec),
      "-movflags",
      "+faststart",
      rawMp4,
    ],
    { stdio: "inherit" },
  );

  console.log("Extracting poster frame at 4.80s...");
  execFileSync(
    ffmpegPath,
    ["-y", "-ss", "4.80", "-i", rawMp4, "-frames:v", "1", "-q:v", "2", posterJpg],
    { stdio: "inherit" },
  );

  console.log("Baking poster frame as frame 0 of brag.mp4...");
  execFileSync(
    ffmpegPath,
    [
      "-y",
      "-i",
      rawMp4,
      "-i",
      posterJpg,
      "-filter_complex",
      "[0:v][1:v]overlay=0:0:enable='eq(n,0)'[v]",
      "-map",
      "[v]",
      "-map",
      "0:a",
      "-c:v",
      "libx264",
      "-crf",
      "18",
      "-preset",
      "slow",
      "-pix_fmt",
      "yuv420p",
      "-c:a",
      "copy",
      "-movflags",
      "+faststart",
      finalMp4,
    ],
    { stdio: "inherit" },
  );

  fs.rmSync(rawMp4, { force: true });
  fs.rmSync(framesDir, { recursive: true, force: true });
  console.log(`Delivered ${finalMp4} and ${posterJpg}`);
}

const mode = process.argv[2];
if (mode === "--stills") {
  await runStills();
} else if (mode === "--render") {
  await runFullRender();
} else {
  console.error("Usage: node render.mjs --stills | --render");
  process.exit(1);
}
