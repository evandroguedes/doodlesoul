// doodlesoul web — wake the soul in the browser, flash it to the device.
let M = null;                 // the wasm engine
let seed = null;              // nobody, until a chip speaks
let animTimer = null;

const $ = id => document.getElementById(id);
const log = t => { const el = $('log'); el.textContent += '\n' + t; el.scrollTop = el.scrollHeight; };

// ---------- identity ----------
function macToBytes(mac) {
  const parts = mac.trim().toLowerCase().replace(/[^0-9a-f]/g, ':').split(':').filter(x => x);
  if (parts.length === 1 && parts[0].length === 12)
    return [...parts[0].matchAll(/../g)].map(m => parseInt(m[0], 16));
  if (parts.length !== 6) return null;
  const b = parts.map(p => parseInt(p, 16));
  return b.some(isNaN) ? null : b;
}

function soulFromMac(mac) {
  const bytes = macToBytes(mac);
  if (!bytes) return null;
  const p = M._malloc(6);
  M.HEAPU8.set(bytes, p);
  const s = M._soul_from_mac(p) >>> 0;
  M._free(p);
  return s;
}

// ---------- the living portrait ----------
const anim = { yaw: 0, pitch: 0, roll: 0, gaze: 0,
               yawT: 0, pitchT: 0, rollT: 0, gazeT: 0,
               mood: 0, moodSeed: 1, nextGlance: 0, nextMood: 0,
               nextBlink: 0, blinkUntil: 0, frame: 0 };

function emptyState() {
  const cv = $('cv'), ctx = cv.getContext('2d');
  ctx.fillStyle = '#ede9df';
  ctx.fillRect(0, 0, cv.width, cv.height);
  ctx.fillStyle = 'rgba(60,56,50,0.10)';
  for (let i = 0; i < 350; i++)
    ctx.fillRect(Math.random() * cv.width, Math.random() * cv.height, 1.5, 1.5);
  ctx.fillStyle = '#8a857a';
  ctx.font = '15px Georgia, serif';
  ctx.textAlign = 'center';
  ctx.fillText('nobody here yet', cv.width / 2, cv.height / 2 - 10);
  ctx.font = '13px Georgia, serif';
  ctx.fillText('connect a stick, or type a MAC', cv.width / 2, cv.height / 2 + 14);
}

function setSoul(s) {
  seed = BigInt(s >>> 0);
  const sd = Number(seed);
  anim.mood = 0; anim.moodSeed = sd;
  $('name').textContent = M.UTF8ToString(M._soul_name(sd));
  const card = M.UTF8ToString(M._soul_card(sd));
  $('tier').textContent = card.split('\n')[0] + '  ·  id ' + sd.toString(16).padStart(8, '0');
  $('card').textContent = card.split('\n').slice(1).join('\n');
  if (!animTimer) animTimer = setInterval(tick, 80);
}

function tick() {
  if (seed === null) return;
  const now = performance.now();
  if (now > anim.nextGlance) {
    anim.yawT = (Math.random() * 2 - 1) * 0.6;
    anim.pitchT = Math.random() * 0.34 - 0.12;
    anim.rollT = (Math.random() * 2 - 1) * 0.08;
    anim.gazeT = (Math.random() * 2 - 1) * 0.45;
    anim.nextGlance = now + 2500 + Math.random() * 4500;
  }
  if (now > anim.nextMood) {
    anim.mood = Math.floor(Math.random() * 7);
    anim.moodSeed = (Math.random() * 0xFFFFFFFF) >>> 0;
    anim.nextMood = now + 5000 + Math.random() * 6000;
  }
  if (now > anim.nextBlink) {
    anim.blinkUntil = now + 140;
    anim.nextBlink = now + 2200 + Math.random() * 3800;
  }
  anim.yaw += (anim.yawT + 0.04 * Math.sin(now * 0.0021) - anim.yaw) * 0.22;
  anim.pitch += (anim.pitchT - anim.pitch) * 0.22;
  anim.roll += (anim.rollT - anim.roll) * 0.18;
  anim.gaze += (anim.gazeT - anim.gaze) * 0.15;

  const sd = Number(seed);
  M._render(sd, anim.yaw, anim.pitch, anim.roll, anim.gaze,
            anim.mood, anim.moodSeed, (sd * 31 + (anim.frame % 3)) >>> 0,
            now < anim.blinkUntil ? 1 : 0, 1);
  const w = M._frame_w(), h = M._frame_h();
  const px = new Uint8ClampedArray(M.HEAPU8.buffer, M._frame_buf(), w * h * 4);
  $('cv').getContext('2d').putImageData(new ImageData(px, w, h), 0, 0);
  anim.frame++;
}

// ---------- serial: read the chip, flash the firmware ----------
// self-hosted esptool-js bundle (CDN builds mangle the flasher stub)
async function withLoader(fn, needStub) {
  const { ESPLoader, Transport } = await import('./vendor/esptool.js');
  const port = await navigator.serial.requestPort();
  const transport = new Transport(port, true);
  const term = { clean() {}, writeLine: l => log(l), write: d => {} };
  // stay at the ROM's 115200: the stick's USB-serial chip drops the stream
  // on esptool's mid-session baud switch over Web Serial. slower, reliable.
  const loader = new ESPLoader({ transport, baudrate: 115200, terminal: term });
  try {
    if (needStub) {
      await loader.main();                      // full init incl. flasher stub
    } else {
      await loader.detectChip();                // reading eFuse needs no stub
    }
    const chipName = loader.chip.CHIP_NAME;
    log('chip: ' + chipName);
    const mac = await loader.chip.readMac(loader);
    log('mac: ' + mac);
    await fn(loader, mac, chipName);
    // clean reset back into the app: clear DTR first, or the IO0 strap can
    // bounce the chip straight back into the bootloader (frozen screen)
    await transport.setDTR(false);
    await transport.setRTS(true);
    await new Promise(r => setTimeout(r, 150));
    await transport.setRTS(false);
    await new Promise(r => setTimeout(r, 100));
    log('device restarting. if the face stays frozen, tap its power button.');
  } finally {
    await transport.disconnect().catch(() => {});
  }
}

$('btnConnect').onclick = async () => {
  if (!('serial' in navigator)) return log('web serial not available — use chrome or edge');
  try {
    await withLoader(async (loader, mac) => {
      const s = soulFromMac(mac);
      setSoul(s);
      log('this chip\'s soul: ' + M.UTF8ToString(M._soul_name(s)) +
          ' (' + s.toString(16).padStart(8, '0') + ')');
      log('if doodlesoul is installed, the face on the screen should be this one.');
    }, false);
  } catch (e) { log('error: ' + e.message); }
};

$('btnFlash').onclick = async () => {
  if (!('serial' in navigator)) return log('web serial not available — use chrome or edge');
  try {
    const manifest = await (await fetch('fw/manifest.json')).json();
    const files = [];
    for (const part of manifest.parts) {
      const buf = await (await fetch('fw/' + part.path)).arrayBuffer();
      // esptool-js 0.6+ takes Uint8Array. NOT a binary string: feeding it
      // one silently coerces every non-digit byte to zero. we learned this
      // the hard way, twice, on real hardware.
      files.push({ data: new Uint8Array(buf), address: part.offset });
      log('loaded ' + part.path + ' (' + buf.byteLength + ' bytes @ 0x' + part.offset.toString(16) + ')');
    }
    await withLoader(async (loader, mac, chipName) => {
      // wrong-chip firmware boots black and stays black until reflashed.
      // this build is compiled for the plain ESP32 only.
      if (chipName !== manifest.chip) {
        log('STOP: this firmware is built for ' + manifest.chip + ', but your chip is ' +
            chipName + '. writing it would leave the device dark until you flash a proper ' +
            chipName + ' image. nothing was written.');
        const s2 = soulFromMac(mac);
        setSoul(s2);
        log('(your chip still has a soul though: ' + M.UTF8ToString(M._soul_name(s2)) + ')');
        return;
      }
      const s = soulFromMac(mac);
      setSoul(s);
      log('flashing at 115200, uncompressed — about a minute, hang in there…');
      log('the soul that will wake up is ' + M.UTF8ToString(M._soul_name(s)));
      // compress:false — the deflate path trips over browser-serial quirks
      // (stub error 0xC9); plain blocks are checksummed and predictable
      // explicit params matching the PlatformIO build — esptool-js's 'keep'
      // path can mangle the bootloader header (flash ok, boots black)
      const doWrite = () => loader.writeFlash({
        fileArray: files, flashSize: '4MB', flashMode: 'dio', flashFreq: '40m',
        eraseAll: false, compress: false,
        reportProgress: (i, written, total) => {
          const pct = Math.floor(written / total * 100);
          if (pct % 25 === 0 && written > 0) log('part ' + (i + 1) + '/' + files.length + ': ' + pct + '%');
        },
      });
      try {
        await doWrite();
      } catch (e) {
        log('hiccup (' + e.message + '), retrying once from the top…');
        await doWrite();
      }
      // trust nothing: ask the stub for an MD5 of what actually landed in
      // flash and compare with the manifest. the reply is tiny, so it works
      // even over links that mangle bulk readback.
      let allGood = true;
      for (const part of manifest.parts) {
        const got = await loader.flashMd5sum(part.offset, part.size);
        const ok = got === part.md5;
        if (!ok) allGood = false;
        log(part.path + ': ' + (ok ? 'verified ✓' : 'MD5 MISMATCH (' + got + ')'));
      }
      if (!allGood) {
        log('flash verification FAILED — the device will likely not boot.');
        log('please flash once with PlatformIO (see the repo README).');
      } else {
        log('all four parts verified in flash. say hello.');
      }
    }, true);
  } catch (e) { log('error: ' + e.message); }
};

$('btnRandom').onclick = () => {
  // a random MAC: some chip somewhere might have this soul
  const b = Array.from(crypto.getRandomValues(new Uint8Array(6)));
  const mac = b.map(x => x.toString(16).padStart(2, '0')).join(':');
  $('macInput').value = mac;   // visible, copyable, editable
  const s = soulFromMac(mac);
  setSoul(s);
  log('a stranger, from ' + mac + '. if a chip with that address exists, this is who lives in it');
};

$('btnMac').onclick = () => {
  const s = soulFromMac($('macInput').value);
  if (s === null) return log('could not parse that MAC');
  setSoul(s);
  log('soul of ' + $('macInput').value.trim() + ': ' + M.UTF8ToString(M._soul_name(s)));
};

// ---------- boot ----------
createDoodle().then(mod => {
  M = mod;
  emptyState();
  $('name').textContent = '';
  log('engine loaded. waiting for a chip.');
});
