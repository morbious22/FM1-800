// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 morbious22 (FM1-800)
// FM1-800 step 3: render the Poly-800 web prototype's sound offline, as the reference the firmware must match.
//
//   node tools/poly800_ref/render.js PAGE.html OUT_DIR [--set page|dry|phaser] [--prog N,N..]
//
// PAGE.html is the prototype page (its own code is run unchanged, in headless Chromium: the same Web Audio as
// Chrome). Its AudioContext is swapped for an OfflineAudioContext, Math.random is seeded, and the page's own
// functions (on, off, ld, init) are called at fixed sample times (OfflineAudioContext.suspend), so every render
// is the same every time. Needs Node and Playwright with Chromium.
//
// Each program is played three ways (phrases below): a held chord, one held note with its release, a fast run.
// Sets: page   = the page as it opens (Age 3, phaser at its defaults: on, mix 5)
//       dry    = Age 0, phaser mix 0: the voices, filter, tone shaping and chorus only
//       phaser = a few programs with the phaser pushed (mix 10, feedback 0 / 10), to measure it
// Writes OUT_DIR/<set>/<number>_<phrase>.wav (32-bit float stereo, 44.1 kHz) and OUT_DIR/<set>/index.json.
'use strict';
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const FS = 44100, Q = 128;                       // Web Audio renders in blocks of 128: every event is on one
const at = (blocks) => blocks * Q;               // (sample times, in whole blocks)
const T0 = at(8);                                // first event (~23 ms in)
const RUN = [60, 62, 64, 65, 67, 69, 71, 72], RUN_STEP = at(52);   // ~151 ms a note

const PHRASES = {
  chord: { len: at(1552), ev: [...[48, 55, 60, 64].map((n) => [T0, 'on', n]),
                               ...[48, 55, 60, 64].map((n) => [T0 + at(854), 'off', n])] },   // held ~2.48 s, 4.5 s
  note: { len: at(2756), ev: [[T0, 'on', 60], [T0 + at(1026), 'off', 60]] },                  // held ~2.98 s, 8 s
  run: { len: at(862), ev: RUN.flatMap((n, i) => [[T0 + i * RUN_STEP, 'on', n], [T0 + (i + 1) * RUN_STEP, 'off', n]]) },
};

const SETS = {
  page: { over: {} },
  dry: { over: { ag: 0, pm: 0 } },
  phaser: { over: { ag: 0, pm: 10 }, vary: [{ name: 'fb0', over: { pf: 0 } }, { name: 'fb10', over: { pf: 10 } },
                                            { name: 'off', over: { pm: 0 } }], progs: [11, 17, 15, 33] },
};

// seeded Math.random (mulberry32) and the swapped AudioContext, before the page's own script runs
const INIT = `(() => {
  let s = 1;
  window.__seed = (v) => { s = v >>> 0; };
  Math.random = () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const Ctx = function () { return window.__ctx; };
  window.AudioContext = Ctx; window.webkitAudioContext = Ctx;
})();`;

// the page's functions, made reachable: one line added before its closing })(); (nothing else changes)
function expose(html) {
  const end = '};\n})();\n</script>';
  if (html.split(end).length !== 2) throw new Error('page layout not recognised: its script must end in "};\\n})();\\n</script>" once');
  return html.replace(end, '};\nwindow.__p={on:on,off:off,ld:ld,init:init,C:C,P:P,apply:apply,PG:PG};\n})();\n</script>');
}

async function renderOne(page, job) {
  return page.evaluate(async ({ job, FS }) => {
    const ctx = new OfflineAudioContext(2, job.len, FS);
    window.__ctx = ctx;
    window.__seed(job.seed);
    const p = window.__p;
    p.init();
    const idx = p.PG.findIndex((g) => g[0] === job.prog);
    if (idx < 0) throw new Error('no program ' + job.prog);
    p.ld(idx);
    for (const [id, v] of Object.entries(job.over)) p.C[id](v);
    p.apply();
    const realResume = ctx.resume.bind(ctx);
    ctx.resume = () => Promise.resolve();        // the page's on() calls resume(): only the harness resumes
    const byTime = new Map();
    for (const e of job.ev) { if (!byTime.has(e[0])) byTime.set(e[0], []); byTime.get(e[0]).push(e); }
    for (const [t, evs] of byTime) {
      ctx.suspend(t / FS).then(() => {
        for (const e of evs) if (e[1] === 'off') p.off(e[2]);   // releases first, then new notes
        for (const e of evs) if (e[1] === 'on') p.on(e[2]);
        realResume();
      });
    }
    const buf = await ctx.startRendering();
    const enc = (a) => { const b = new Uint8Array(a.buffer); let s = ''; for (let i = 0; i < b.length; i += 32768)
      s += String.fromCharCode.apply(null, b.subarray(i, i + 32768)); return btoa(s); };
    return [enc(buf.getChannelData(0)), enc(buf.getChannelData(1))];
  }, { job, FS });
}

function writeWav(file, l, r) {
  const n = l.length, data = Buffer.alloc(n * 8);
  for (let i = 0; i < n; i++) { data.writeFloatLE(l[i], i * 8); data.writeFloatLE(r[i], i * 8 + 4); }
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + data.length, 4); h.write('WAVE', 8); h.write('fmt ', 12);
  h.writeUInt32LE(16, 16); h.writeUInt16LE(3, 20); h.writeUInt16LE(2, 22); h.writeUInt32LE(FS, 24);
  h.writeUInt32LE(FS * 8, 28); h.writeUInt16LE(8, 32); h.writeUInt16LE(32, 34); h.write('data', 36);
  h.writeUInt32LE(data.length, 40);
  fs.writeFileSync(file, Buffer.concat([h, data]));
}

async function main() {
  const args = process.argv.slice(2);
  const opt = (k, d) => { const i = args.indexOf(k); return i < 0 ? d : args.splice(i, 2)[1]; };
  const setName = opt('--set', 'page'), progArg = opt('--prog', null);
  const [pagePath, outDir] = args;
  if (!pagePath || !outDir || !SETS[setName]) {
    console.error('usage: render.js PAGE.html OUT_DIR [--set page|dry|phaser] [--prog N,N..]');
    process.exit(2);
  }
  const set = SETS[setName];
  const src = fs.readFileSync(pagePath, 'utf8');
  if (!src.includes('<head>')) throw new Error('page has no <head>');
  const html = expose(src).replace('<head>', '<head><script>' + INIT + '</script>');   // runs before the page's script
  const dir = path.join(outDir, setName);
  fs.mkdirSync(dir, { recursive: true });
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const load = async () => { await page.setContent(html, { waitUntil: 'load' }); };
  await load();
  const all = await page.evaluate(() => window.__p.PG.map((g) => [g[0], g[1]]));
  let progs = progArg ? progArg.split(',').map(Number) : set.progs || all.map((g) => g[0]);
  const names = Object.fromEntries(all);
  const index = { set: setName, chromium: browser.version(), fs: FS, over: set.over, phrases: {}, renders: [] };
  for (const [k, ph] of Object.entries(PHRASES)) index.phrases[k] = { len: ph.len, events: ph.ev };
  const vary = set.vary || [{ name: '', over: {} }];
  for (const prog of progs) {
    for (const v of vary) {
      for (const [phName, ph] of Object.entries(PHRASES)) {
        await load();                            // a fresh page each time: the page makes one AudioContext
        const over = { ...set.over, ...v.over };
        const [l64, r64] = await renderOne(page, { prog, len: ph.len, ev: ph.ev, over, seed: prog * 7919 + 1 });
        const f32 = (s) => { const b = Buffer.from(s, 'base64'); return new Float32Array(b.buffer, b.byteOffset, b.length / 4); };
        const file = `${prog}${v.name ? '_' + v.name : ''}_${phName}.wav`;
        writeWav(path.join(dir, file), f32(l64), f32(r64));
        index.renders.push({ prog, name: names[prog], phrase: phName, vary: v.name, over, file });
      }
    }
    process.stdout.write(`${prog} `);
  }
  fs.writeFileSync(path.join(dir, 'index.json'), JSON.stringify(index, null, 1));
  console.log(`\n${index.renders.length} renders in ${dir}`);
  await browser.close();
}
main().catch((e) => { console.error(e); process.exit(1); });
