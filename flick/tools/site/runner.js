// runner.js - run Flick's examples on a board from the browser (Web Serial, Chrome/Edge).
//
// Speaks the same line protocol as `flick upload` (baseline/src/loader_proto.h):
//   @BSK INFO                       -> @BSK OK baseline=.. abi=.. chip=.. target=.. ...
//   @BSK BEGIN <size> <sha256>      erase the inactive slot
//   @BSK DATA <offset> <base64>     768 bytes at a time
//   @BSK END                        verify, make the slot active, reboot
// Every other line from the board is console output.
//
// make_site.py puts this next to index.html, with examples/examples.json and
// examples/<group>/<name>/<target>.flick (tools/ci/build_examples.py --out),
// and sets window.FLICK = { abi: "1.7", boards: { target: "Board name" } }.

const PREFIX = "@BSK ";
const CHUNK = 768;
const UPLOAD_BAUD = 921600;    // a UART console runs at this for an upload (@BSK BAUD, Baseline 0.3+)
const ESPRESSIF = 0x303a, USB_SERIAL_JTAG = 0x1001;
const USB_SERIAL_CHIPS = [0x1a86, 0x10c4, 0x0403];     // CH340, CP210x, FTDI (the classic ESP32 boards)
const GROUPS = {
  "01_basics": "Basics", "02_hardware": "Hardware", "03_wifi": "WiFi", "04_bluetooth": "Bluetooth",
  "05_storage": "Storage", "06_power_and_usb": "Power and USB", "07_updates": "Updates",
};

const $ = (id) => document.getElementById(id);
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
    // the chip). A TinyUSB port (S3 in USB device mode) needs DTR to send anything.
    const info = this.port.getInfo();
    const tinyusb = info.usbVendorId === ESPRESSIF && info.usbProductId !== USB_SERIAL_JTAG;
    await this.port.setSignals({ requestToSend: false });
    await this.port.setSignals({ dataTerminalReady: tinyusb });
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

const state = { link: null, port: null, baud: 115200, board: null, examples: [], busy: false, selected: null };

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
  if (!("serial" in navigator)) {
    setStatus("This browser can't talk to USB serial ports. Use Chrome or Edge on a computer.", "bad");
    return;
  }
  let port;
  try {
    port = await navigator.serial.requestPort({
      filters: [ESPRESSIF, ...USB_SERIAL_CHIPS].map((v) => ({ usbVendorId: v })),
    });
  } catch (e) {
    return;                                        // the user closed the chooser
  }
  setStatus("Connecting...");
  let r;
  try {
    r = await openLink(port);
  } catch (e) {
    setStatus(`Can't open the port: ${e.message}. Is another program (or the Flash dialog) using it?`, "bad");
    return;
  }
  if (!r) {
    setStatus("The board doesn't answer. Flash Baseline first (step 1), then connect again.", "bad");
    try { await port.close(); } catch (e) {}
    return;
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
    const res = await fetch(`examples/${ex.path}/${target}.flick`);
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
    $("source").textContent = await (await fetch(`examples/${ex.path}/${ex.name}.ino`)).text();
  } catch (e) {
    $("source").textContent = "(couldn't load the source)";
  }
}

function render() {
  const list = $("examples");
  list.textContent = "";
  const target = state.board && state.board.target;
  let group = null, ul = null;
  for (const ex of state.examples) {
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
    btn.disabled = !state.link || state.busy || !fits || ex.wifi;
    btn.onclick = () => { show(ex); run(ex); };
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

async function sendInput(e) {
  e.preventDefault();
  const input = $("send");
  if (!state.link || !input.value) return;
  await state.link.writer.write(new TextEncoder().encode(input.value + "\n"));
  input.value = "";
}

async function init() {
  $("connect").onclick = connect;
  $("send-form").onsubmit = sendInput;
  $("clear").onclick = () => { $("console").textContent = ""; };
  // The Flash dialog (ESP Web Tools) needs the port: let go of it first.
  for (const b of document.querySelectorAll("esp-web-install-button")) {
    b.addEventListener("click", () => { if (state.link) disconnect(); }, true);
  }
  try {
    state.examples = (await (await fetch("examples/examples.json")).json()).examples;
  } catch (e) {
    $("examples").textContent = "The examples aren't on this page yet.";
    return;
  }
  render();
  const first = state.examples.find((x) => x.name.endsWith("hello_flick")) || state.examples[0];
  if (first) show(first);
  if (!("serial" in navigator)) {
    setStatus("Running examples needs Chrome or Edge on a computer (Web Serial).", "bad");
    $("connect").disabled = true;
  }
}

init();
