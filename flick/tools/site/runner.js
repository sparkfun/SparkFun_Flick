// runner.js - flash Baseline and run Flick's examples from the browser (Web Serial, Chrome/Edge).
//
// Flash buttons (button.flash, data-target/-chip/-flash-mb) write the parts in
// manifests/<target>.json with esptool-js; see flashBoard(). On RP2040/RP2350
// boards (button.flash.rp2) they write firmware/<target>/baseline-<target>.uf2
// through the boot ROM; see flashRp2().
//
// Running an example:
// Speaks the same line protocol as `flick upload` (baseline/src/loader_proto.h):
//   @BSK INFO                       -> @BSK OK baseline=.. abi=.. chip=.. target=.. ...
//   @BSK BEGIN <size> <sha256>      erase the inactive slot
//   @BSK DATA <offset> <base64>     768 bytes at a time
//   @BSK END                        verify, make the slot active, reboot
// Every other line from the board is console output.
//
// make_site.py puts this next to index.html, with examples/examples.json and
// examples/<group>/<name>/<target>.flick (tools/ci/build_examples.py --out),
// and sets window.FLICK = { abi: "1.7", boards: { target: "Board name" },
// generic: [{ target, name, chip, flash_mb, octal }], sparkfun: [{ target, name, chip, flash_mb }] }
// (the images the two Auto-detect entries pick from).

const PREFIX = "@BSK ";
const CHUNK = 768;
const UPLOAD_BAUD = 921600;    // a UART console runs at this for an upload (@BSK BAUD, Baseline 0.3+)
const ESPRESSIF = 0x303a, USB_SERIAL_JTAG = 0x1001;
const USB_SERIAL_CHIPS = [0x1a86, 0x10c4, 0x0403];     // CH340, CP210x, FTDI (the classic ESP32 boards)
const PJRC = 0x16c0;                                   // Teensy (its USB serial while Baseline runs)
const RP2_VIDS = [0x2e8a, 0x1b4f];                     // Raspberry Pi, SparkFun: RP2040/RP2350 boards running Baseline
const GROUPS = {
  "01_basics": "Basics", "02_hardware": "Hardware", "03_wifi": "WiFi", "04_bluetooth": "Bluetooth",
  "05_storage": "Storage", "06_power_and_usb": "Power and USB", "07_updates": "Updates",
};

const $ = (id) => document.getElementById(id);
// Images and lists are always checked with the server (a 304 when unchanged):
// a cached Baseline image older than the page would be flashed again and again,
// since its build never matches the page's.
const FRESH = { cache: "no-cache" };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ------------------------------------------------------------------ the serial link

class Link {
  constructor(port, onLine) {
    this.port = port;
    this.onLine = onLine;
    this.waiters = [];         // resolve() of each reply still to come, oldest first
    this.buf = "";
    this.closed = false;
  }

  async open(baud) {
    await this.port.open({ baudRate: baud, bufferSize: 65536 });
    // Release RTS before DTR, so the reset circuit never sees RTS alone (that resets
    // the chip). A TinyUSB port (S3 in USB device mode) and an RP2040/RP2350's USB
    // serial (arduino-pico) need DTR to send anything.
    const info = this.port.getInfo();
    const tinyusb = info.usbVendorId === ESPRESSIF && info.usbProductId !== USB_SERIAL_JTAG;
    await this.port.setSignals({ requestToSend: false });
    await this.port.setSignals({ dataTerminalReady: tinyusb || RP2_VIDS.includes(info.usbVendorId) });
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this.done = this.readLoop();
  }

  async readLoop() {
    const dec = new TextDecoder();
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.buf += dec.decode(value, { stream: true });
        let i;
        while ((i = this.buf.indexOf("\n")) >= 0) {
          let line = this.buf.slice(0, i).replace(/\r$/, "");
          this.buf = this.buf.slice(i + 1);
          // A sketch that prints without a newline ("....") puts Baseline's reply
          // at the end of its own line: show its text, then handle the reply.
          const at = line.indexOf(PREFIX);
          if (at > 0) {
            this.onLine(line.slice(0, at));
            line = line.slice(at);
          }
          if (line.startsWith(PREFIX)) {
            const body = line.slice(PREFIX.length);
            if (this.waiters.length && (body.startsWith("OK") || body.startsWith("ERR"))) {
              this.waiters.shift()({ ok: body.startsWith("OK"), text: body.replace(/^(OK|ERR)\s*/, "") });
            }
          } else {
            this.onLine(line);
          }
        }
      }
    } catch (e) {
      // the board went away (a reboot on native USB re-enumerates the port)
    } finally {
      this.closed = true;
      try { this.reader.releaseLock(); } catch (e) {}
    }
  }

  async send(line) {
    await this.writer.write(new TextEncoder().encode(PREFIX + line + "\n"));
  }

  // A promise for the next reply not yet claimed: { ok, text }, or null after the
  // timeout. Register it before sending the line it answers.
  expect(timeout = 5000) {
    const reply = new Promise((resolve) => { this.waiters.push(resolve); });
    return Promise.race([reply, sleep(timeout).then(() => null)]);
  }

  // The reply to one command.
  async request(line, timeout = 5000) {
    const reply = this.expect(timeout);
    await this.send(line);
    const r = await reply;
    if (!r) this.waiters = [];       // a lost reply: start over
    return r;
  }

  async close() {
    try { await this.reader.cancel(); } catch (e) {}
    await this.done;
    try { this.writer.releaseLock(); } catch (e) {}
    try { await this.port.close(); } catch (e) {}
  }
}

// ------------------------------------------------------------------ the page

// picked: the board chosen in the menu or found by Auto-detect, so the examples
// show which ones run on it before Connect.
const state = { link: null, port: null, baud: 115200, board: null, picked: "", examples: [], busy: false, selected: null };

function log(text, cls) {
  const con = $("console");
  const line = document.createElement("div");
  if (cls) line.className = cls;
  line.textContent = text;
  con.appendChild(line);
  while (con.childNodes.length > 2000) con.removeChild(con.firstChild);
  con.scrollTop = con.scrollHeight;
}

function setStatus(text, cls) {
  const s = $("board-status");
  s.textContent = text;
  s.className = "status " + (cls || "");
}

function infoFields(text) {
  const out = {};
  for (const m of text.matchAll(/(\w+)=("[^"]*"|\S+)/g)) out[m[1]] = m[2].replace(/^"|"$/g, "");
  return out;
}

async function openLink(port) {
  const info = port.getInfo();
  // USB-serial chips: Baseline's console is 115200 (921600 on Baselines 0.1 to 0.2).
  // Native USB ignores the rate.
  const bauds = USB_SERIAL_CHIPS.includes(info.usbVendorId) ? [115200, 921600] : [115200];
  for (const baud of bauds) {
    const link = new Link(port, (line) => log(line));
    await link.open(baud);
    // The newline ends whatever a probe at a wrong rate left half-received.
    await link.writer.write(new TextEncoder().encode("\n"));
    const r = await link.request("INFO", 2000);
    if (r && r.ok) {
      state.baud = baud;
      return { link, info: infoFields(r.text) };
    }
    await link.close();
  }
  return null;
}

async function connect() {
  if (state.link) return disconnect();
  if (!needs("serial")) {
    setStatus("This browser can't talk to USB serial ports. Use Chrome or Edge on a computer.", "bad");
    return;
  }
  let port;
  try {
    port = await navigator.serial.requestPort({
      filters: [ESPRESSIF, ...USB_SERIAL_CHIPS, PJRC, ...RP2_VIDS].map((v) => ({ usbVendorId: v })),
    });
  } catch (e) {
    chooserError(e, setStatus);
    return;
  }
  await attach(port);
}

// Talk to Baseline on a port the user picked. quiet: no error message when
// the board doesn't answer (the retries after a flash). True when connected.
async function attach(port, quiet) {
  setStatus("Connecting...");
  let r;
  try {
    r = await openLink(port);
  } catch (e) {
    if (!quiet) setStatus(`Can't open the port: ${e.message}. Is another program (or the Flash dialog) using it?`, "bad");
    return false;
  }
  if (!r) {
    if (!quiet) setStatus("The board doesn't answer. Flash Baseline first (step 1), then connect again.", "bad");
    try { await port.close(); } catch (e) {}
    return false;
  }
  state.port = port;
  state.link = r.link;
  state.board = r.info;
  const name = (window.FLICK.boards || {})[r.info.target] || r.info.target;
  let text = `Connected: ${name}, Baseline ${r.info.baseline}`;
  const [maj, min] = (r.info.abi || "0.0").split(".").map(Number);
  const [wantMaj, wantMin] = window.FLICK.abi.split(".").map(Number);
  let cls = "good";
  if (maj !== wantMaj || min < wantMin) {
    text += `. These examples need Baseline ABI ${window.FLICK.abi} (the board has ${r.info.abi}): flash the newer Baseline above.`;
    cls = "bad";
  }
  setStatus(text, cls);
  $("connect").textContent = "Disconnect";
  render();
  watch();
  return true;
}

// After a flash the board restarts into Baseline: connect to it without asking
// again (the browser remembers the port), so its examples show at once. A
// native USB board comes back as a new port object with the same IDs.
async function connectAfterFlash(port) {
  const ids = port.getInfo();
  for (let i = 0; i < 20 && !state.link; i++) {
    await sleep(500);
    const ports = await navigator.serial.getPorts();
    const again = ports.find((p) => p === port) || ports.find((p) => {
      const n = p.getInfo();
      return n.usbVendorId === ids.usbVendorId && n.usbProductId === ids.usbProductId;
    });
    if (again && await attach(again, true)) return true;
  }
  setStatus("Flashed. Press Connect to try the examples.");
  return false;
}

async function disconnect() {
  const link = state.link;
  state.link = null;
  state.board = null;
  if (link) await link.close();
  $("connect").textContent = "Connect";
  setStatus("Not connected.");
  render();
}

// After a reboot, a native-USB board comes back as a new port: find it and reopen.
async function watch() {
  const link = state.link;
  await link.done;
  if (state.link !== link) return;                 // disconnected on purpose
  const info = state.port.getInfo();
  setStatus("The board restarted; reconnecting...");
  try { link.writer.releaseLock(); } catch (e) {}
  try { await state.port.close(); } catch (e) {}
  for (let i = 0; i < 40 && state.link === link; i++) {
    await sleep(500);
    const ports = (await navigator.serial.getPorts()).filter((p) => {
      const pi = p.getInfo();
      return pi.usbVendorId === info.usbVendorId && pi.usbProductId === info.usbProductId;
    });
    for (const p of ports) {
      try {
        const nl = new Link(p, (line) => log(line));
        await nl.open(state.baud);
        if (state.link !== link) { await nl.close(); return; }
        state.port = p;
        state.link = nl;
        setStatus(`Connected: ${(window.FLICK.boards || {})[state.board.target] || state.board.target}`, "good");
        watch();
        return;
      } catch (e) {
        try { await p.close(); } catch (e2) {}
      }
    }
  }
  if (state.link === link) {
    state.link = null;
    $("connect").textContent = "Connect";
    setStatus("Lost the board. Press Connect again.", "bad");
    render();
  }
}

// Reopen the port at another rate. watch() of the old link stops, a new one starts.
async function reopen(baud) {
  const old = state.link;
  state.link = null;
  await old.close();
  const nl = new Link(state.port, (line) => log(line));
  await nl.open(baud);
  state.link = nl;
  watch();
  return nl;
}

function b64(bytes) {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s);
}

async function run(ex) {
  if (!state.link || state.busy) return;
  state.busy = true;
  render();
  const target = state.board.target;
  let fast = false;
  try {
    const res = await fetch(`examples/${ex.path}/${target}.flick`, FRESH);
    if (!res.ok) throw new Error(`no image for this board (${res.status})`);
    const img = new Uint8Array(await res.arrayBuffer());
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", img)))
      .map((b) => b.toString(16).padStart(2, "0")).join("");
    log(`--- uploading ${ex.name} (${img.length} bytes)`, "note");
    let link = state.link;
    // A UART console goes to UPLOAD_BAUD for the upload; Baseline's reply says how
    // many lines may be in flight. Older Baselines answer ERR: one at a time.
    let window = 1;
    if (USB_SERIAL_CHIPS.includes(state.port.getInfo().usbVendorId)) {
      const r = await link.request(`BAUD ${UPLOAD_BAUD}`, 2000);
      if (r && r.ok) {
        window = Math.max(1, parseInt(infoFields(r.text).window || "1", 10));
        if (state.baud !== UPLOAD_BAUD) {
          link = await reopen(UPLOAD_BAUD);
          fast = true;
        }
      }
    }
    let r = await link.request(`BEGIN ${img.length} ${hash}`, 15000);
    if (!r || !r.ok) throw new Error(r ? r.text : "no reply to BEGIN");
    const replies = [];
    const check = (r, what) => { if (!r || !r.ok) throw new Error(r ? r.text : `no reply ${what}`); };
    for (let off = 0; off < img.length; off += CHUNK) {
      replies.push(link.expect(5000));
      await link.send(`DATA ${off} ${b64(img.subarray(off, off + CHUNK))}`);
      while (replies.length >= window) check(await replies.shift(), `at byte ${off}`);
    }
    while (replies.length) check(await replies.shift(), "to DATA");
    r = await link.request("END", 10000);
    if (!r || !r.ok) throw new Error(r ? r.text : "no reply to END");
    log(`--- ${ex.name} installed; the board restarts into it`, "note");
  } catch (e) {
    log(`--- upload failed: ${e.message}`, "bad");
  } finally {
    // The board restarts (or, after a failure, drops back by itself) to its own rate.
    if (fast && state.link) {
      try { await reopen(state.baud); } catch (e) {}
    }
    state.busy = false;
    render();
  }
}

async function show(ex) {
  state.selected = ex;
  $("source-name").textContent = `${ex.path}/${ex.name}.ino`;
  $("source").textContent = "loading...";
  render();
  try {
    $("source").textContent = await (await fetch(`examples/${ex.path}/${ex.name}.ino`, FRESH)).text();
  } catch (e) {
    $("source").textContent = "(couldn't load the source)";
  }
}

function render() {
  const list = $("examples");
  if (!state.examples.length) {
    list.textContent = "The examples aren't on this page yet.";
    return;
  }
  list.textContent = "";
  const connected = state.board && state.board.target;
  const target = connected || state.picked;
  // Connected: only what runs on this board from here. The page's images are
  // built without anyone's WiFi network, so WiFi examples come from the kit.
  const shown = connected
    ? state.examples.filter((ex) => ex.targets.includes(target) && !ex.wifi)
    : state.examples;
  if (connected) {
    const fit = state.examples.filter((ex) => ex.targets.includes(target));
    const wifi = fit.filter((ex) => ex.wifi).length;
    const other = state.examples.length - fit.length;
    const note = document.createElement("p");
    note.className = "note-small";
    note.textContent = `${shown.length} examples run on this board.` +
      (wifi ? ` ${wifi} more need your WiFi network: build them from the kit.` : "") +
      (other ? ` ${other} are for other boards.` : "");
    list.appendChild(note);
  }
  let group = null, ul = null;
  for (const ex of shown) {
    if (ex.group !== group) {
      group = ex.group;
      const h = document.createElement("h3");
      h.textContent = GROUPS[group] || group;
      list.appendChild(h);
      ul = document.createElement("ul");
      list.appendChild(ul);
    }
    const li = document.createElement("li");
    if (state.selected === ex) li.className = "selected";
    const btn = document.createElement("button");
    btn.textContent = "Run";
    const fits = !target || ex.targets.includes(target);
    btn.disabled = state.busy || !fits || ex.wifi;
    btn.onclick = () => runPressed(ex);
    const name = document.createElement("a");
    name.href = "#";
    name.textContent = ex.name;
    name.onclick = (e) => { e.preventDefault(); show(ex); };
    const title = document.createElement("span");
    title.className = "about";
    title.textContent = " " + ex.title + (ex.wifi ? " (needs your WiFi: build it from the kit)" : "")
      + (fits ? "" : " (not for this board)");
    li.append(btn, " ", name, title);
    ul.appendChild(li);
  }
}

// Run without a connection: connect first (the port chooser opens), then run
// the example if it is for the board that answered.
async function runPressed(ex) {
  show(ex);
  if (!needs("serial")) return;
  if (!state.link) {
    await connect();
    if (!state.link) return;                     // no port picked, or no Baseline: connect() said why
    if (!ex.targets.includes(state.board.target)) {
      setStatus(`${ex.name} is not for this board: pick one from the list.`, "bad");
      return;
    }
  }
  run(ex);
}

async function sendInput(e) {
  e.preventDefault();
  const input = $("send");
  if (!state.link || !input.value) return;
  await state.link.writer.write(new TextEncoder().encode(input.value + "\n"));
  input.value = "";
}

// ------------------------------------------------------------------ flashing Baseline
//
// esptool-js (the library ESP Web Tools is built on), used directly so a
// USB-serial board flashes at 921600 instead of ESP Web Tools' fixed 115200
// (the RedBoard: about 25 s instead of 2 minutes). Choosing the port is the
// only confirmation. No erase: the image blanks NVS and otadata itself, the
// manifest adds a blank coredump partition, and LittleFS keeps the user's files.

const ESPTOOL = "https://cdn.jsdelivr.net/npm/esptool-js@0.7.1/+esm";   // 0.7: P4 flash power-on, C5 SPI registers, stale input drained
const FLASH_BAUD = 921600;

// The parts a manifest lists (image at 0, blank coredump), downloaded.
async function baselineParts(target) {
  const manifestUrl = new URL(`manifests/${target}.json`, location.href);
  const res = await fetch(manifestUrl, FRESH);
  if (!res.ok) throw new Error(`can't download the manifest for ${target} (${res.status})`);
  const manifest = await res.json();
  return Promise.all(manifest.builds[0].parts.map(async (p) => {
    const r = await fetch(new URL(p.path, manifestUrl), FRESH);
    if (!r.ok) throw new Error(`can't download ${p.path} (${r.status})`);
    return { data: new Uint8Array(await r.arrayBuffer()), address: p.offset };
  }));
}

// "Auto-detect": the generic image for what the chip reports (make_site.py
// lists them in window.FLICK.generic). Flash: the largest image that fits.
// ESP32-S3 PSRAM: eFuse PSRAM_CAP 1 or 3 is in-package octal PSRAM (8 or 16 MB),
// 2 quad (2 MB), 0 none or external (quad wiring, which also runs without PSRAM).
async function pickGeneric(loader, family, flashMb) {
  let octal = false, psram = "";
  if (family === "ESP32-S3" && loader.chip.getPsramCap) {
    const cap = await loader.chip.getPsramCap(loader);
    octal = cap === 1 || cap === 3;
    psram = cap === 1 ? ", 8 MB octal PSRAM" : cap === 3 ? ", 16 MB octal PSRAM" : cap === 2 ? ", 2 MB quad PSRAM" : "";
  }
  // An unknown size (0) gets the smallest image: it runs on any flash size.
  const fits = (window.FLICK.generic || [])
    .filter((g) => g.chip === family && (!flashMb || g.flash_mb <= flashMb) && g.octal === octal)
    .sort((a, b) => (flashMb ? b.flash_mb - a.flash_mb : a.flash_mb - b.flash_mb));
  const size = flashMb ? `${flashMb} MB flash` : "flash size not readable";
  return { pick: fits[0], found: `${family}, ${size}${psram}` };
}

// The flash size in MB from the flash chip's ID, or 0 when the ID can't be
// read. esptool-js's detectFlashSize() answers "4MB" then, which was wrong on
// a Thing Plus C5 (8 MB) and made Auto-detect refuse it.
async function flashSizeMb(loader) {
  for (let i = 0; i < 3; i++) {
    const id = await loader.readFlashId();
    const size = loader.DETECTED_FLASH_SIZES[(id >> 16) & 0xff];
    if (size && size.endsWith("MB")) return parseInt(size, 10);
    log(`--- flash ID 0x${(id >>> 0).toString(16)}: size not known`, "bad");
    await sleep(50);
  }
  return 0;
}

// "SparkFun ESP32 board - Auto-detect": the SparkFun boards with this chip and
// flash size (window.FLICK.sparkfun). Several boards share one (the classic
// ESP32 boards with 16 MB and a CH340), so the user picks among those.
// flashMb 0 (not read): every SparkFun board with this chip.
function sparkfunMatches(family, flashMb) {
  return (window.FLICK.sparkfun || []).filter((b) => b.chip === family && (!flashMb || b.flash_mb === flashMb));
}

// A board that already runs Baseline answers INFO: its target (which board it
// is) and build (which exact image). Asked before the chip is reset into its
// bootloader. null when nothing answers.
async function baselineInfo(port) {
  let r = null;
  try {
    r = await openLink(port);
  } catch (e) {
    return null;                 // the port won't open: the flasher says why
  }
  if (!r) return null;
  await r.link.close();
  return r.info;
}

// The "Found: ..." line above a Flash button's status: what the chip
// reported, kept while the status line shows the progress.
function foundLine(status, text) {
  let el = status.previousElementSibling;
  if (!el || !el.classList.contains("flash-found")) {
    el = document.createElement("div");
    el.className = "flash-found";
    status.parentElement.insertBefore(el, status);
  }
  el.textContent = text;
  el.hidden = !text;
}

// An Auto-detect panel, once it knows the board: that board's title (product
// link) and facts line. showBoard() puts the Auto-detect ones back.
function showAs(panel, target) {
  const board = [...document.querySelectorAll("div.board")].find((d) => d.dataset.target === target);
  if (!panel || !board || panel === board) return;
  for (const cls of ["board-title", "facts"]) {
    const mine = panel.querySelector("." + cls), theirs = board.querySelector("." + cls);
    if (!mine || !theirs) continue;
    if (mine.dataset.auto === undefined) mine.dataset.auto = mine.innerHTML;
    mine.innerHTML = theirs.innerHTML;
  }
}

// Buttons in the status line, one per board; resolves to the one pressed, or
// null for Cancel.
function askWhich(status, text, choices) {
  return new Promise((resolve) => {
    status.textContent = text + " ";
    status.className = "flash-status";
    for (const c of [...choices, null]) {
      const b = document.createElement("button");
      b.textContent = c ? c.name : "Cancel";
      b.className = "choice";
      b.onclick = () => resolve(c);
      status.append(b, " ");
    }
  });
}

async function flashBoard(btn) {
  let target = btn.dataset.target, family = btn.dataset.chip, need = Number(btn.dataset.flashMb);
  const auto = target.startsWith("auto-") ? target.slice(5) : "";    // "sparkfun" or "generic"
  const force = btn.dataset.force === "1";        // "Reinstall anyway": flash even if up to date
  delete btn.dataset.force;
  const status = btn.parentElement.querySelector(".flash-status");
  const say = (text, cls) => { status.textContent = text; status.className = "flash-status " + (cls || ""); };
  if (!needs("serial")) { say("This browser can't flash a board: use Chrome or Edge on a computer.", "bad"); return; }
  if (state.busy) { say("Busy: wait for the upload or flash in progress to finish.", "bad"); return; }
  if (state.link) await disconnect();              // step 2 may hold the port
  let port;
  try {
    port = await navigator.serial.requestPort({
      filters: [ESPRESSIF, ...USB_SERIAL_CHIPS].map((v) => ({ usbVendorId: v })),
    });
  } catch (e) {
    chooserError(e, say);
    return;
  }
  state.busy = true;
  render();
  for (const b of document.querySelectorAll("button.flash")) b.disabled = true;
  try {
    const notes = [];
    let files = null;
    say("Asking the board whether it already runs Baseline...");
    const running = await baselineInfo(port);
    if (running && auto) {
      const list = auto === "sparkfun" ? window.FLICK.sparkfun : window.FLICK.generic;
      const board = (list || []).find((b) => b.target === running.target);
      if (board) {
        target = board.target; family = board.chip; need = board.flash_mb;
        notes.push(`The board's Baseline says it is a ${board.name}.`);
        showAs(btn.closest("div.board"), board.target);
      }
    }
    // Already this exact Baseline (INFO build= matches the page's image): no
    // flash, just connect. Baselines before build= always get the update.
    const latest = (window.FLICK.builds || {})[target];
    if (!force && running && latest && running.target === target && running.build === latest) {
      const name = (window.FLICK.boards || {})[target] || target;
      foundLine(status, `Found: ${name}, already running the latest Baseline (${running.baseline}).`);
      say("Nothing to update. Connecting...", "good");
      const ok = await connectAfterFlash(port);
      say(ok ? "Nothing to update. Connected: its examples are below." : "Nothing to update. Press Connect below to try the examples.", "good");
      const again = document.createElement("button");
      again.textContent = "Reinstall anyway";
      again.className = "choice";
      again.onclick = () => { btn.dataset.force = "1"; flashBoard(btn); };
      status.append(" ", again);
      return;
    }
    say("Loading the flasher...");
    if (!target.startsWith("auto-")) files = await baselineParts(target);
    let ESPLoader, Transport;
    try {
      ({ ESPLoader, Transport } = await import(ESPTOOL));
    } catch (e) {
      throw new Error(`can't load esptool-js from the CDN (${e.message}). Check the internet connection.`);
    }
    const uart = USB_SERIAL_CHIPS.includes(port.getInfo().usbVendorId);
    // Native USB ignores the rate; a USB-serial chip tries 921600, then 115200.
    const bauds = uart ? [FLASH_BAUD, 115200] : [115200];
    for (let i = 0; i < bauds.length; i++) {
      const transport = new Transport(port, false);
      const terminal = {
        clean() {}, write() {},
        writeLine(text) { if (!/^Writing at/.test(text)) log(text, "note"); },
      };
      const loader = new ESPLoader({ transport, baudrate: bauds[i], terminal });
      let fatal = false;
      try {
        say("Connecting to the chip...");
        try {
          await loader.main();
        } catch (e) {
          // main() syncs at 115200 and then switches rate: a failure is retried
          // at 115200 before it is reported.
          console.error(e);
          const why = String((e && e.message) || e);
          if (/open|in use|access|InvalidState|NetworkError/i.test(why) || (e && e.name === "InvalidStateError")) {
            fatal = true;
            throw new Error(`can't open the port (${why}). Close any other tab or program using it ` +
                            "(a serial monitor, the Arduino IDE, flick monitor), then Flash again.");
          }
          throw new Error(`the chip doesn't answer (${why}). Hold BOOT, press RESET, release BOOT, then Flash again.`);
        }
        const found = loader.chip.CHIP_NAME;
        const have = await flashSizeMb(loader);      // 0: not readable
        let module = found;
        try { module = await loader.chip.getChipDescription(loader); } catch (e) {}
        foundLine(status, `Found: ${module}, ${have ? `${have} MB flash` : "flash size not readable"}`);
        if (auto && !files) {
          let pick, what;
          if (auto === "sparkfun") {
            what = `${found}, ${have ? `${have} MB flash` : "flash size not readable"}`;
            const matches = sparkfunMatches(found, have);
            if (!matches.length) {
              fatal = true;
              throw new Error(`found ${what}, and no SparkFun board has that. ` +
                              "If it isn't a SparkFun board, pick Generic ESP32 board - Auto-detect.");
            }
            pick = matches.length === 1 ? matches[0]
              : await askWhich(status, `Found ${what}. Which board is it?`, matches);
            if (!pick) { fatal = true; throw new Error("no board picked."); }
          } else {
            ({ pick, found: what } = await pickGeneric(loader, found, have));
            if (!pick) {
              fatal = true;
              throw new Error(`found ${what}, and there is no generic Baseline for it. ` +
                              "Pick the board from the menu if it is listed.");
            }
          }
          target = pick.target; family = pick.chip; need = pick.flash_mb;
          foundLine(status, `Found: ${module}, ${what.replace(/^[^,]*, /, "")}. Installing Baseline for ${pick.name}.`);
          showAs(btn.closest("div.board"), pick.target);
          say(`Downloading Baseline for ${pick.name}...`);
          notes.push(`Flashed ${pick.name}.`);
          files = await baselineParts(target);
        }
        if (found !== family) {
          fatal = true;
          throw new Error(`this board has an ${found}, but this Baseline is for an ${family}. ` +
                          "Pick your board in the menu, or Auto-detect.");
        }
        if (have && have < need) {
          fatal = true;
          throw new Error(`this board has ${have} MB of flash, this Baseline needs ${need} MB. ` +
                          "Pick your board in the menu, or Auto-detect.");
        }
        if (found === "ESP32" && loader.chip.getChipRevision && (await loader.chip.getChipRevision(loader)) < 3) {
          fatal = true;
          throw new Error("this ESP32 is an early chip revision (before 3.0); Baseline needs revision 3.0 or later.");
        }
        // The S3, C3, C5, C6 and P4 run Baseline's console on the chip's own USB:
        // flashing through a USB-serial chip works, but the console isn't there.
        if (found !== "ESP32" && USB_SERIAL_CHIPS.includes(port.getInfo().usbVendorId)) {
          notes.push("This port is a USB-serial chip, and Baseline talks over the chip's own USB: " +
                     "to Connect, plug into the board's other USB port (often labeled USB).");
        }
        const total = files.reduce((n, f) => n + f.data.length, 0);
        const t0 = performance.now();
        await loader.writeFlash({
          fileArray: files, flashSize: "keep", flashMode: "keep", flashFreq: "keep",
          eraseAll: false, compress: true,
          reportProgress: (fi, written, size) => {
            const done = files.slice(0, fi).reduce((n, f) => n + f.data.length, 0)
              + (written / size) * files[fi].data.length;
            say(`Writing ${Math.floor((done / total) * 100)}%`);
          },
        });
        say("Restarting the board...");
        await transport.setRTS(true);
        await sleep(100);
        await loader.after("hard_reset");
        await transport.disconnect();
        const secs = ((performance.now() - t0) / 1000).toFixed(0);
        let done = [...notes, `Done in ${secs} s. Connecting...`].join(" ");
        say(done, "good");
        if (auto) {
          // Show the board that was flashed, with this message, and its examples.
          const foundText = status.previousElementSibling && status.previousElementSibling.textContent;
          showBoard(target);
          const panel = [...document.querySelectorAll("div.board")].find((d) => d.dataset.target === target);
          const s2 = panel && panel.querySelector(".flash-status");
          if (s2) { s2.textContent = done; s2.className = "flash-status good"; foundLine(s2, foundText || ""); }
        }
        const shown = auto ? [...document.querySelectorAll("div.board")].find((d) => d.dataset.target === target) : null;
        const line = (shown && shown.querySelector(".flash-status")) || status;
        const ok = await connectAfterFlash(port);
        done = done.replace("Connecting...", ok ? "Connected: its examples are below." : "Press Connect below to try the examples.");
        line.textContent = done;
        return;
      } catch (e) {
        try { await transport.disconnect(); } catch (e2) {}
        if (fatal || i === bauds.length - 1) throw e;
        log(`--- flashing at ${bauds[i]} failed (${e.message}); trying ${bauds[i + 1]}`, "bad");
        await sleep(500);
      }
    }
  } catch (e) {
    say(`Failed: ${e.message}`, "bad");
  } finally {
    state.busy = false;
    for (const b of document.querySelectorAll("button.flash")) b.disabled = false;
    render();
  }
}

// ---- RP2040 / RP2350 -----------------------------------------------------
//
// The boot ROM (BOOTSEL mode) shows a USB drive and, next to it, PICOBOOT: a
// vendor interface picotool uses. The page writes the .uf2's blocks to flash
// through it with WebUSB: erase, write and read back each run of 4 KB sectors,
// then reboot. A board that runs Baseline (arduino-pico) gets there without
// the button: its USB serial reboots into the boot ROM when the port is opened
// at 1200 baud and closed. Where WebUSB can't open the device (the RP2040's
// boot ROM on Windows has no driver for it), the page saves the .uf2 onto the
// boot drive instead.

const RP2_BOOT = { 0x0003: { chip: "RP2040", family: 0xe48bff56 }, 0x000f: { chip: "RP2350", family: 0xe48bff59 } };
const UF2_ABSOLUTE = 0xe48bff57;       // RP2350 erratum E10 workaround block: not part of the image
const PICOBOOT_MAGIC = 0x431fd10b;

function uf2Blocks(buf) {
  const v = new DataView(buf), blocks = [];
  for (let i = 0; i + 512 <= buf.byteLength; i += 512) {
    if (v.getUint32(i, true) !== 0x0a324655 || v.getUint32(i + 4, true) !== 0x9e5d5157) {
      throw new Error("the .uf2 is damaged (bad block magic)");
    }
    const flags = v.getUint32(i + 8, true), size = v.getUint32(i + 16, true);
    if (flags & 1) continue;                               // "not main flash"
    blocks.push({
      addr: v.getUint32(i + 12, true),
      family: flags & 0x2000 ? v.getUint32(i + 28, true) : 0,
      data: new Uint8Array(buf, i + 32, size),
    });
  }
  return blocks;
}

class Picoboot {
  constructor(dev) { this.dev = dev; this.token = 1; }

  async open() {
    const dev = this.dev;
    await dev.open();
    if (!dev.configuration) await dev.selectConfiguration(1);
    for (const itf of dev.configuration.interfaces) {
      const alt = itf.alternates[0];
      if (alt.interfaceClass === 0xff && alt.endpoints.length === 2) {
        this.itf = itf.interfaceNumber;
        this.epOut = alt.endpoints.find((e) => e.direction === "out").endpointNumber;
        this.epIn = alt.endpoints.find((e) => e.direction === "in").endpointNumber;
      }
    }
    if (this.itf === undefined) throw new Error("the boot ROM has no PICOBOOT interface");
    await dev.claimInterface(this.itf);
    await dev.controlTransferOut({ requestType: "vendor", recipient: "interface", request: 0x41, value: 0, index: this.itf });
  }

  // One command: a 32-byte header, an optional data phase, then the zero-length ack.
  async cmd(id, args = [], data = null, inLength = 0) {
    const head = new DataView(new ArrayBuffer(32));
    head.setUint32(0, PICOBOOT_MAGIC, true);
    head.setUint32(4, this.token++, true);
    head.setUint8(8, id);
    head.setUint8(9, args.length * 4);
    head.setUint32(12, data ? data.length : inLength, true);
    args.forEach((a, i) => head.setUint32(16 + 4 * i, a >>> 0, true));
    const check = (r, what) => {
      if (r.status !== "ok") throw new Error(`PICOBOOT ${what} (command 0x${id.toString(16)}): ${r.status}`);
      return r;
    };
    check(await this.dev.transferOut(this.epOut, head.buffer), "header");
    let result = null;
    if (data) check(await this.dev.transferOut(this.epOut, data), "data");
    if (id & 0x80) {
      result = new Uint8Array(check(await this.dev.transferIn(this.epIn, inLength), "read").data.buffer);
      check(await this.dev.transferOut(this.epOut, new Uint8Array(0)), "ack");
    } else {
      check(await this.dev.transferIn(this.epIn, 1), "ack");
    }
    return result;
  }

  exclusive() { return this.cmd(0x01, [2]); }          // exclusive, and eject the drive
  exitXip() { return this.cmd(0x06); }
  erase(addr, size) { return this.cmd(0x03, [addr, size]); }
  write(addr, bytes) { return this.cmd(0x05, [addr, bytes.length], bytes); }
  read(addr, size) { return this.cmd(0x84, [addr, size], null, size); }
  async reboot(chip) {
    try {
      if (chip === "RP2350") await this.cmd(0x0a, [0, 200, 0, 0]);    // REBOOT2: normal boot, 200 ms
      else await this.cmd(0x02, [0, 0, 200]);                        // REBOOT: pc 0 = normal boot
    } catch (e) {}   // the ack can be lost to the reboot itself
  }
}

// The image as runs of whole 4 KB sectors: [{ addr, bytes }].
function uf2Runs(blocks) {
  const sectors = new Map();
  for (const b of blocks) {
    for (let off = 0; off < b.data.length;) {
      const a = b.addr + off, base = a & ~0xfff;
      if (!sectors.has(base)) sectors.set(base, new Uint8Array(4096).fill(0xff));
      const n = Math.min(b.data.length - off, base + 4096 - a);
      sectors.get(base).set(b.data.subarray(off, off + n), a - base);
      off += n;
    }
  }
  const runs = [];
  for (const base of [...sectors.keys()].sort((x, y) => x - y)) {
    const last = runs[runs.length - 1];
    if (last && last.addr + last.sectors.length * 4096 === base) last.sectors.push(sectors.get(base));
    else runs.push({ addr: base, sectors: [sectors.get(base)] });
  }
  return runs;
}

async function picobootFlash(dev, blocks, wantChip, say) {
  const boot = RP2_BOOT[dev.productId];
  if (!boot) throw new Error("that isn't an RP2040 or RP2350 boot ROM");
  if (boot.chip !== wantChip) {
    throw new Error(`this board is an ${boot.chip}, and this Baseline is for an ${wantChip}. Pick your board in the menu.`);
  }
  const image = blocks.filter((b) => b.family !== UF2_ABSOLUTE);
  if (image.some((b) => b.family && b.family !== boot.family)) throw new Error(`the .uf2 isn't for an ${boot.chip}`);
  const pb = new Picoboot(dev);
  await pb.open();
  try {
    await pb.exclusive();
    if (boot.chip === "RP2040") await pb.exitXip();
    const runs = uf2Runs(image);
    const total = runs.reduce((n, r) => n + r.sectors.length, 0);
    let done = 0;
    const t0 = performance.now();
    for (const run of runs) {
      await pb.erase(run.addr, run.sectors.length * 4096);
      for (let i = 0; i < run.sectors.length; i++) {
        const addr = run.addr + i * 4096;
        await pb.write(addr, run.sectors[i]);
        const back = await pb.read(addr, 4096);
        if (!back || back.length !== 4096 || back.some((x, k) => x !== run.sectors[i][k])) {
          throw new Error(`flash at 0x${addr.toString(16)} doesn't read back what was written`);
        }
        say(`Writing ${Math.floor((++done / total) * 100)}%`);
      }
    }
    say("Restarting the board...");
    await pb.reboot(boot.chip);
    return ((performance.now() - t0) / 1000).toFixed(0);
  } finally {
    try { await dev.close(); } catch (e) {}
  }
}

// A board that runs Baseline: open its port at 1200 baud and drop DTR.
async function rebootToBootRom(port) {
  await port.open({ baudRate: 1200 });
  try {
    await port.setSignals({ dataTerminalReady: true });
    await sleep(50);
    await port.setSignals({ dataTerminalReady: false });
  } finally {
    try { await port.close(); } catch (e) {}
  }
}

// A boot ROM this page may already use (allowed before), plugged in now.
async function knownBootRom() {
  if (!("usb" in navigator)) return null;
  const devs = await navigator.usb.getDevices();
  return devs.find((d) => d.vendorId === 0x2e8a && RP2_BOOT[d.productId]) || null;
}

// A button in the status line, and Cancel: true when pressed. A USB chooser
// needs a click of its own.
async function askButton(status, text, label) {
  return !!(await askWhich(status, text, [{ name: label }]));
}

// No WebUSB (or the device can't be opened): save the .uf2 onto the boot drive.
async function saveUf2(status, name, buf, chip) {
  const drive = chip === "RP2350" ? "RP2350" : "RPI-RP2";
  if (!await askButton(status, `This browser can't write to the board directly. Save the .uf2 onto the ${drive} ` +
                       "drive that the board shows in BOOTSEL mode; the board installs it and restarts.", "Save the .uf2...")) {
    return false;
  }
  const blob = new Blob([buf], { type: "application/octet-stream" });
  if (window.showSaveFilePicker) {
    const handle = await window.showSaveFilePicker({ suggestedName: name });
    const w = await handle.createWritable();
    await w.write(blob);
    await w.close();
  } else {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  }
  return true;
}

async function flashRp2(btn) {
  const target = btn.dataset.target, chip = btn.dataset.chip;
  const status = btn.parentElement.querySelector(".flash-status");
  const say = (text, cls) => { status.textContent = text; status.className = "flash-status " + (cls || ""); };
  if (!needs("serial")) return;
  if (state.busy) { say("Busy: wait for the upload or flash in progress to finish.", "bad"); return; }
  if (state.link) await disconnect();
  state.busy = true;
  render();
  for (const b of document.querySelectorAll("button.flash")) b.disabled = true;
  let port = null;
  try {
    const name = `baseline-${target}.uf2`;
    const resp = await fetch(`firmware/${target}/${name}`);
    if (!resp.ok) throw new Error(`can't download ${name} (${resp.status})`);
    const buf = await resp.arrayBuffer();
    const blocks = uf2Blocks(buf);

    let dev = await knownBootRom();
    if (!dev) {
      // Running Baseline: pick its port and it restarts into the boot ROM.
      say("Pick the board's port. Not listed? Hold BOOTSEL while you plug the board in, then press Cancel.");
      try {
        port = await navigator.serial.requestPort({ filters: RP2_VIDS.map((v) => ({ usbVendorId: v })) });
      } catch (e) {
        if (e && e.name !== "NotFoundError") throw e;
      }
      if (port) {
        say("Restarting the board into its boot ROM...");
        try {
          await rebootToBootRom(port);
        } catch (e) {
          throw new Error(`can't open the port (${e.message}). Close any other tab or program using it, then Flash again.`);
        }
        // A boot ROM this page was allowed before comes back by itself; else the user picks it.
        const allowed = "usb" in navigator && (await navigator.usb.getDevices()).some((d) => RP2_BOOT[d.productId]);
        for (let i = 0; i < (allowed ? 10 : 3) && !dev; i++) {
          await sleep(500);
          dev = await knownBootRom();
        }
      }
    }
    if (!dev && "usb" in navigator) {
      if (!await askButton(status, port ? "The board is in its boot ROM now." :
                           "Hold BOOTSEL while you plug the board in (or hold BOOTSEL and press RESET).", "Choose the board")) {
        say("Cancelled.");
        return;
      }
      try {
        dev = await navigator.usb.requestDevice({ filters: Object.keys(RP2_BOOT).map((p) => ({ vendorId: 0x2e8a, productId: Number(p) })) });
      } catch (e) {
        if (e && e.name === "NotFoundError") { say("No board picked. Is it in BOOTSEL mode (a drive named RP2350 or RPI-RP2)?"); return; }
        throw e;
      }
    }
    let secs = null;
    if (dev) {
      say("Writing Baseline...");
      try {
        secs = await picobootFlash(dev, blocks, chip, say);
      } catch (e) {
        if (!/SecurityError|Access denied|Unable to claim|open/i.test(`${e.name} ${e.message}`)) throw e;
        log(`--- WebUSB can't use the boot ROM (${e.message}): saving the .uf2 instead`, "note");
        dev = null;
      }
    }
    if (!dev) {
      if (!await saveUf2(status, name, buf, chip)) { say("Cancelled."); return; }
      say("Saved. The board installs it and restarts in a few seconds.", "good");
    } else {
      say(`Done in ${secs} s. Connecting...`, "good");
    }
    showBoard(target);
    const line = [...document.querySelectorAll("div.board")].find((d) => d.dataset.target === target)
      .querySelector(".flash-status");
    const msg = dev ? `Done in ${secs} s.` : "Saved: the board installs it and restarts.";
    let ok = false;
    if (port) ok = await connectAfterFlash(port);
    line.textContent = `${msg} ${ok ? "Connected: its examples are below." : "Press Connect below to try the examples."}`;
    line.className = "flash-status good";
  } catch (e) {
    if (e && e.name === "AbortError") say("Cancelled.");      // the save dialog was closed
    else say(`Failed: ${e.message}`, "bad");
  } finally {
    state.busy = false;
    for (const b of document.querySelectorAll("button.flash")) b.disabled = false;
    render();
  }
}

// ---- browser check -------------------------------------------------------

// Flashing and running examples are tested only in desktop Chrome and Edge.
// Other browsers either lack Web Serial (Firefox, Safari, DuckDuckGo) or ship
// it turned off or behind a flag (Brave, some Chromium builds), so anything
// that doesn't identify as Chrome or Edge gets a warning.
function supportedBrowser() {
  if (!("serial" in navigator)) return false;
  if (navigator.duckduckgo || navigator.brave) return false;
  const ua = navigator.userAgentData;
  if (!ua || ua.mobile) return false;
  const brands = (ua.brands || []).map((b) => b.brand);
  return brands.includes("Google Chrome") || brands.includes("Microsoft Edge");
}

// The warning dialog. On page load it shows once per session (a viewer may
// continue anyway); a button that can't work in this browser shows it on
// every press, with what that button needed.
function browserWarning(why) {
  const dlg = $("browser-warn");
  if (!dlg) { alert(why || "Please use Google Chrome or Microsoft Edge on a computer."); return; }
  $("browser-why").textContent = why ||
    ("serial" in navigator
      ? "Flashing Baseline and running examples talk to the board over USB (Web Serial), which is supported only in Google Chrome and Microsoft Edge on a computer."
      : "This browser can't talk to USB serial ports (Web Serial), so the Flash, Connect and Run buttons won't work here.");
  $("browser-copy").textContent = "Copy page link";
  $("browser-copy").onclick = async () => {
    try { await navigator.clipboard.writeText(location.href); $("browser-copy").textContent = "Link copied"; }
    catch (e) { $("browser-copy").textContent = location.href; }
  };
  $("browser-close").onclick = () => {
    try { sessionStorage.setItem("flick-browser-ok", "1"); } catch (e) {}
    if (dlg.close) dlg.close(); else dlg.removeAttribute("open");
  };
  if (dlg.open) return;
  if (dlg.showModal) dlg.showModal(); else dlg.setAttribute("open", "");
}

function browserCheck() {
  if (supportedBrowser()) return;
  try { if (sessionStorage.getItem("flick-browser-ok")) return; } catch (e) {}
  browserWarning();
}

// True if this browser has the API a button needs; otherwise shows why.
function needs(api) {
  if (api === "serial" && "serial" in navigator) return true;
  if (api === "hid" && "hid" in navigator) return true;
  browserWarning(api === "hid"
    ? "Flashing a Teensy talks to its bootloader over USB (WebHID), which this browser doesn't support. Use Google Chrome or Microsoft Edge on a computer, or flash the .hex with Teensy Loader."
    : "This browser can't talk to USB serial ports (Web Serial), so this button can't work here. Use Google Chrome or Microsoft Edge on a computer.");
  return false;
}

// requestPort/requestDevice: closing the chooser is a choice, not an error
// (NotFoundError); anything else (blocked by policy, no permission) is shown.
function chooserError(e, say) {
  if (e && e.name === "NotFoundError") { say("No port picked."); return; }
  say(`The browser refused the USB request: ${(e && e.message) || e}`, "bad");
}

// ---- board menu ------------------------------------------------------------

// One panel per board (div.board) under the menu; the menu shows one at a
// time and remembers the choice in this browser.
// Show one board's panel (and select it in the menu); its examples are marked.
function showBoard(target) {
  const pick = $("board-pick");
  if (!pick) return;
  if ([...pick.options].some((o) => o.value === target)) pick.value = target;
  for (const d of document.querySelectorAll("div.board")) d.hidden = d.dataset.target !== pick.value;
  for (const st of document.querySelectorAll(".flash-status")) { st.textContent = ""; st.className = "flash-status"; }
  for (const f of document.querySelectorAll(".flash-found")) f.hidden = true;
  for (const el of document.querySelectorAll("[data-auto]")) { el.innerHTML = el.dataset.auto; delete el.dataset.auto; }
  $("board-none").hidden = !!pick.value;
  state.picked = pick.value.startsWith("auto-") ? "" : pick.value;
  try { localStorage.setItem("flick-board", pick.value); } catch (e) {}
  render();
}

function boardPicker() {
  const pick = $("board-pick");
  if (!pick) return;
  pick.onchange = () => showBoard(pick.value);
  let saved = "";
  try { saved = localStorage.getItem("flick-board") || ""; } catch (e) {}
  if (saved && [...pick.options].some((o) => o.value === saved)) pick.value = saved;
  showBoard(pick.value);
}

async function init() {
  browserCheck();
  boardPicker();
  $("connect").onclick = connect;
  $("send-form").onsubmit = sendInput;
  $("clear").onclick = () => { $("console").textContent = ""; };
  for (const b of document.querySelectorAll("button.flash")) b.onclick = () => (b.classList.contains("rp2") ? flashRp2(b) : flashBoard(b));
  for (const b of document.querySelectorAll("button.teensy-flash")) b.onclick = () => flashTeensy(b);
  for (const b of document.querySelectorAll("button.teensy-reboot")) b.onclick = () => rebootTeensy(b);
  try {
    state.examples = (await (await fetch("examples/examples.json", FRESH)).json()).examples;
  } catch (e) {
    state.examples = [];
    render();
    return;
  }
  render();
  const first = state.examples.find((x) => x.name.endsWith("hello_flick")) || state.examples[0];
  if (first) show(first);
  if (!("serial" in navigator)) {
    setStatus("Running examples needs Chrome or Edge on a computer (Web Serial).", "bad");
  }
}

init();
