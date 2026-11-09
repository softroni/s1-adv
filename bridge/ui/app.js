// S1 Bridge's web page. Plain script, no build step: each screen asks the bridge for what it shows
// and draws it, again every few seconds while it's open.
"use strict";

// ---------------------------------------------------------------------------------------------
// Basics
const $ = (sel, root = document) => root.querySelector(sel);
const esc = (value) => String(value ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const ICON = {
  check: '<svg class="i s" viewBox="0 0 24 24"><path d="M5 12l5 5L20 7"/></svg>',
  warn: '<svg class="i s" viewBox="0 0 24 24"><path d="M12 9v4M12 17h.01M10.3 3.9L1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/></svg>',
  power: '<svg class="i" viewBox="0 0 24 24"><path d="M12 2v10M18.4 6.6a9 9 0 1 1-12.8 0"/></svg>',
  play: '<svg class="i s" viewBox="0 0 24 24" style="fill:currentColor;stroke:none"><path d="M7 4l13 8-13 8z"/></svg>',
  speaker: '<svg class="i s" viewBox="0 0 24 24"><path d="M11 5L6 9H2v6h4l5 4zM15.5 8.5a5 5 0 0 1 0 7"/></svg>',
  restart: '<svg class="i" viewBox="0 0 24 24"><path d="M21 12a9 9 0 1 1-3-6.7L21 8M21 3v5h-5"/></svg>',
  clock: '<svg class="i" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>',
  info: '<svg class="i" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 16v-4M12 8h.01"/></svg>',
  flask: '<svg class="i s" viewBox="0 0 24 24"><path d="M9 3h6M10 3v6L4.5 18.5A1.6 1.6 0 0 0 6 21h12a1.6 1.6 0 0 0 1.5-2.5L14 9V3"/></svg>',
  code: '<svg class="i" viewBox="0 0 24 24"><path d="M16 18l6-6-6-6M8 6l-6 6 6 6"/></svg>',
  camera: '<svg class="i" viewBox="0 0 24 24"><path d="M23 7l-7 5 7 5zM1 5h15v14H1z"/></svg>',
  home: '<svg class="i" viewBox="0 0 24 24"><path d="M3 11l9-7 9 7v9a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z"/></svg>',
  wave: '<svg class="i" viewBox="0 0 24 24"><path d="M2 12h3l3-7 4 14 3-7h7"/></svg>',
  dot: '<svg class="i" viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/></svg>'
};

async function api(path, body) {
  const options = body === undefined ? {} : { method: "POST", headers: { "content-type": "application/json", "x-s1-ui": "1" }, body: JSON.stringify(body) };
  const res = await fetch(`api/${path}`, options);
  if (!res.ok) {
    let error = `HTTP ${res.status}`;
    try { error = (await res.json()).error || error; } catch {}
    throw new Error(error);
  }
  return res.headers.get("content-type")?.includes("json") ? res.json() : res.blob();
}

function toast(text, ms = 3200) {
  const el = document.createElement("div");
  el.className = "toast";
  el.textContent = text;
  document.body.appendChild(el);
  setTimeout(() => el.remove(), ms);
}

const pad = (n) => String(n).padStart(2, "0");
function clock(at, withSeconds = false) {
  if (!at) return "";
  const d = new Date(at);
  const today = new Date().toDateString() === d.toDateString();
  const time = `${pad(d.getHours())}:${pad(d.getMinutes())}${withSeconds ? `:${pad(d.getSeconds())}` : ""}`;
  return today ? time : `${d.toLocaleDateString(undefined, { month: "short", day: "numeric" })} ${time}`;
}
function ago(ms) {
  if (ms === null || ms === undefined) return "never";
  const s = Math.round(ms / 1000);
  if (s < 60) return `${s} s ago`;
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86400) return `${Math.round(s / 3600)} h ago`;
  return `${Math.round(s / 86400)} days ago`;
}
function span(ms) {
  const s = Math.round(ms / 1000);
  return s < 60 ? `${s} s` : s < 3600 ? `${Math.floor(s / 60)}:${pad(s % 60)}` : `${Math.round(s / 3600)} h`;
}
const mb = (bytes) => `${(bytes / 1048576).toFixed(bytes < 10 * 1048576 ? 2 : 0)} MB`;
const status = (kind, text, icon = "") => `<span class="status ${kind}">${icon}${esc(text)}</span>`;

// ---------------------------------------------------------------------------------------------
// Screens and their timers
const SCREENS = { status: drawStatus, chat: drawChat, remote: drawRemote, lab: drawLab, radio: drawRadio, logs: drawLogs, firmware: drawFirmware, settings: drawSettings };
const EVERY = { status: 5000, chat: 2500, remote: 5000, lab: 1000, radio: 10000, logs: 10000 };
let current = "";
let timer = 0;
let lastStatus = null;

function show() {
  const id = (location.hash || "#status").slice(1);
  const screen = SCREENS[id] ? id : "status";
  const changed = screen !== current;
  current = screen;
  document.querySelectorAll("section.screen").forEach((el) => el.classList.toggle("on", el.id === screen));
  document.querySelectorAll("[data-go]").forEach((a) => a.classList.toggle("on", a.dataset.go === screen));
  $("#composer").hidden = screen !== "chat";
  document.title = `S1 Bridge · ${screen[0].toUpperCase()}${screen.slice(1)}`;
  if (changed) { window.scrollTo(0, 0); refresh(true); }
}
async function refresh(first = false) {
  clearTimeout(timer);
  const screen = current;
  try { await SCREENS[screen](first); }
  catch (error) { if (first) $(`#${screen}`).innerHTML = `<div class="empty err">Couldn't reach the bridge: ${esc(error.message)}</div>`; }
  if (EVERY[screen] && screen === current) timer = setTimeout(() => refresh(), EVERY[screen]);
}
addEventListener("hashchange", show);
document.addEventListener("visibilitychange", () => { if (!document.hidden) refresh(); });

// The pill at the top says whether everything works, from the last status.
function topPill(s) {
  const pill = $("#topPill");
  let kind = "good", text = "All working";
  if (!s) { kind = "off"; text = "No bridge"; }
  else if (!s.device?.online) { kind = "warn"; text = s.device?.last_seen_ms === null ? "Cardputer not seen yet" : "Cardputer offline"; }
  else if (s.claude.waiting_for_login) { kind = "warn"; text = "Claude signed out"; }
  else if (s.delivery.waiting.some((w) => w.waiting_ms > 180000)) { kind = "warn"; text = "Reply stuck"; }
  else if (s.storage.bytes / s.storage.max_bytes > 0.8) { kind = "warn"; text = "Storage nearly full"; }
  const color = { good: "var(--good)", warn: "var(--warn)", off: "var(--mute)" }[kind];
  pill.innerHTML = `<span class="dot" style="background:${color};box-shadow:0 0 0 3px ${kind === "good" ? "rgba(12,163,12,.22)" : "rgba(250,178,25,.2)"}"></span><span>${esc(text)}</span>`;
}
async function pollTop() {
  try { lastStatus = await api("status"); topPill(lastStatus); } catch { topPill(null); }
}

// ---------------------------------------------------------------------------------------------
// Status
async function drawStatus() {
  const s = await api("status");
  lastStatus = s;
  topPill(s);
  const d = s.device;
  const el = $("#status");
  const strip = d ? d.strip.map((n, i) => {
    const t = new Date(d.strip_start + i * 60000);
    return `<i class="${n >= 2 ? "y" : n ? "l" : ""}" title="${pad(t.getHours())}:${pad(t.getMinutes())} · ${n} request${n === 1 ? "" : "s"}"></i>`;
  }).join("") : "";
  const stripStart = d ? new Date(d.strip_start) : null;
  const offered = s.offered?.version;
  const installed = d?.firmware?.version;
  const fwStatus = !installed ? status("off", "Not reported yet") : offered && offered !== installed ? status("wait", `${offered} offered`) : status("good", "Latest", ICON.check);
  const starts = d?.starts?.length ? d.starts.slice(0, 6).map((st) => `
    <div class="item"><div class="ico ${st.warn ? "warn" : ""}">${st.warn ? ICON.warn.replace("i s", "i") : ICON.power}</div>
      <div class="grow"><b>${esc(st.title)}</b><div class="tiny muted">${clock(st.at)} · ${esc(st.detail)}</div></div>
      ${st.warn ? status("warn", "Check") : ""}</div>`).join("")
    : `<div class="empty">No starts since the bridge started at ${clock(s.bridge.started_at)}.</div>`;
  const c = s.claude;
  const working = c.state !== "idle";
  const storePct = s.storage.bytes / s.storage.max_bytes;
  const filesPct = s.storage.entries / s.storage.max_entries;
  const worst = Math.max(storePct, filesPct);
  const storeStatus = worst >= 0.8 ? status("crit", "Nearly full", ICON.warn) : worst >= 0.6 ? status("warn", "Filling up", ICON.warn) : status("good", "Plenty", ICON.check);
  const waiting = s.delivery.waiting;
  const sv = s.services;
  const service = (name, detail, on, icon = ICON.dot) => `<div class="item"><div class="ico ${on ? "lime" : ""}">${icon}</div><div class="grow"><b>${esc(name)}</b><div class="tiny muted">${esc(detail)}</div></div>${status(on ? "good" : "off", on ? "On" : "Off")}</div>`;
  el.innerHTML = `
  <div class="colA">
    <div class="h2"><h2>Cardputer</h2><span class="tiny muted">${esc(d?.id || "")}</span></div>
    <div class="card">
      <div class="between"><div class="col"><span class="big">${d ? (d.online ? "Online" : d.last_seen_ms === null ? "Not seen yet" : "Offline") : "No Cardputer"}</span>
        <span class="small ink2">${!d ? "No device is set up on this bridge" : d.last_seen_ms === null ? `Not since the bridge started at ${clock(s.bridge.started_at)}` : `Last check-in ${ago(d.last_seen_ms)}`}</span></div>
        ${d && !d.online && d.last_seen_ms !== null ? status("warn", "Not seen", ICON.warn) : ""}</div>
      ${d ? `<div class="label" style="margin-top:14px">Requests each minute, last hour</div>
      <div class="strip">${strip}</div>
      <div class="axis"><span>${stripStart ? `${pad(stripStart.getHours())}:${pad(stripStart.getMinutes())}` : ""}</span><span>now</span></div>` : ""}
      <div class="sep"></div>
      <div class="between"><div class="col"><span class="label">Firmware</span><span class="mid mono">${esc(installed || "—")}</span></div>${fwStatus}</div>
      <div class="tiny muted" style="margin-top:6px"><a class="link" href="#firmware">Builds and room left →</a></div>
    </div>
    <div class="h2"><h2>Starts</h2><a href="#logs">Log</a></div>
    <div class="card list">${starts}</div>
  </div>
  <div class="colB">
    <div class="h2"><h2>Claude</h2><span class="tiny muted">${esc(niceModel(c.model))}${c.effort ? ` · effort ${esc(c.effort)}` : ""}</span></div>
    <div class="card">
      <div class="between"><div class="col"><span class="big">${working ? esc(activityWord(c.state)) : "Idle"}</span>
        <span class="small ink2">${c.waiting_for_login ? "Waiting for you to sign in to Claude on the Mac" : working && c.detail ? esc(c.detail) : c.connected ? "Signed in" : "Not connected"}</span></div>
        ${c.waiting_for_login ? status("warn", "Signed out", ICON.warn) : working ? status("wait", "Working") : status("good", "Ready", ICON.check)}</div>
      <div class="sep"></div>
      <div class="grid2"><div class="col"><span class="label">Your messages today</span><span class="mid">${c.today.messages}</span></div>
        <div class="col"><span class="label">By voice</span><span class="mid">${c.today.voice}</span></div></div>
    </div>
    <div class="h2"><h2>Replies to the Cardputer</h2></div>
    <div class="card">
      <div class="col"><span class="big">${waiting.length} waiting</span><span class="small ink2">${waiting.length ? waiting.map((w) => `Reply ${w.seq}, ${span(w.waiting_ms)}`).join(" · ") : "Every reply has been picked up"}</span></div>
      ${s.delivery.recent.length ? `<div class="sep"></div><div class="label">Last ones, picked up after</div>
      <div class="chips" style="margin-top:8px">${s.delivery.recent.slice(0, 5).map((r) => `<span class="chip">${span(r.after_ms)}</span>`).join("")}</div>` : ""}
      <div class="tiny muted" style="margin-top:10px">A reply still waiting after a few minutes means the Cardputer stopped asking, not that Claude is slow.</div>
    </div>
    <div class="h2"><h2>Storage</h2></div>
    <div class="card">
      <div class="between"><span class="mid">${mb(s.storage.bytes)} <span class="small muted">of ${mb(s.storage.max_bytes)}</span></span>${storeStatus}</div>
      <div class="meter"><i style="width:${Math.min(100, storePct * 100).toFixed(1)}%"></i></div>
      <div class="between" style="margin-top:12px"><span class="small ink2">${s.storage.entries.toLocaleString()} of ${s.storage.max_entries.toLocaleString()} files</span><span class="tiny muted">${Math.round(filesPct * 100)}%</span></div>
      <div class="meter"><i style="width:${Math.min(100, filesPct * 100).toFixed(1)}%"></i></div>
      <div class="tiny muted" style="margin-top:8px">When it's full the bridge refuses the Cardputer's messages, voice notes and logs.</div>
    </div>
  </div>
  <div class="colC">
    <div class="h2"><h2>Services</h2></div>
    <div class="card list">
      ${service("Spoken replies", sv.voice.on ? (sv.voice.ready ? "Kokoro voice, on this Mac" : "Kokoro voice, starting") : "Not set up", sv.voice.on, ICON.speaker.replace("i s", "i"))}
      ${service("Cameras", sv.cameras.on ? (sv.cameras.running ? "Helper running" : "Helper starting") : sv.cameras.missing, sv.cameras.on, ICON.camera)}
      ${service("Home Wi-Fi door", sv.home_door.port ? `Port ${sv.home_door.port}: podcasts, cameras, planes, the Lab` : "Closed", Boolean(sv.home_door.port), ICON.home)}
      ${service("Lab, live", sv.lab_live.reporting ? "The Cardputer's Lab is reporting" : "Only while the Lab is open at home", sv.lab_live.reporting, ICON.flask.replace("i s", "i"))}
      ${service("Live voice", "Experimental", sv.realtime.on, ICON.wave)}
    </div>
    <div class="h2"><h2>Bridge</h2></div>
    <div class="card">
      <div class="between"><div class="col"><span class="label">Running since</span><b>${clock(s.bridge.started_at)}</b></div>
        <div class="col" style="text-align:right"><span class="label">Code</span><b class="mono">${esc(s.bridge.commit || "—")}</b></div></div>
      ${s.bridge.can_restart ? `<div class="sep"></div><button class="btn block" id="restartBtn">${ICON.restart}Restart bridge</button>
      <div class="tiny muted" style="margin-top:8px">Waits for Claude to finish a reply first.</div>` : ""}
    </div>
  </div>`;
  $("#restartBtn")?.addEventListener("click", restartBridge);
}

const niceModel = (id) => ({ "claude-opus-5-5": "Opus 5.5", "claude-sonnet-5-5": "Sonnet 5.5", "claude-haiku-5-5": "Haiku 5.5", "claude-fable-5-1": "Fable 5.1" }[id] || id || "");
const activityWord = (state) => ({ thinking: "Thinking", searching: "Searching", researching: "Researching", reading: "Reading", coding: "Coding",
  working: "Working", building: "Building", testing: "Testing", writing: "Writing" }[state] || "Working");

async function restartBridge() {
  if (!confirm("Restart the bridge? The Cardputer loses it for a few seconds.")) return;
  const before = lastStatus?.bridge?.started_at;
  const r = await api("restart", {}).catch((e) => ({ restarting: false, reason: e.message }));
  if (!r.restarting) { toast(r.reason === "claude_busy" ? "Claude is still working. Try again when it's done." : "The bridge can't restart from here."); return; }
  waitForRestart(before);
}
function waitForRestart(before) {
  const cover = document.createElement("div");
  cover.className = "restarting";
  cover.textContent = "Restarting the bridge…";
  document.body.appendChild(cover);
  const started = Date.now();
  const check = async () => {
    try {
      const s = await api("status");
      if (s.bridge.started_at !== before) { cover.remove(); toast("The bridge is back."); refresh(true); return; }
    } catch {}
    if (Date.now() - started > 60000) { cover.textContent = "The bridge hasn't come back. Check the Mac."; return; }
    setTimeout(check, 1500);
  };
  setTimeout(check, 2500);
}

// ---------------------------------------------------------------------------------------------
// Chat
let chatLimit = 60;
let chatSig = "";
const ACTION_WORDS = {
  "lab.play": (a) => `Lab · play “${a.name}”`, "lab.rule": (a) => `Lab · a rule to check: “${a.name}”`,
  "lab.sequence": (a) => `Lab · a sequence to check: “${a.name}”`, "lab.once": () => "Lab · a one-time check",
  "ui.open": (a) => `Open ${SCREENS_ON_DEVICE.find(([mode]) => mode === a.mode)?.[1] || a.mode}`, "ota.check": () => "Check for an update", "ota.apply": () => "Install the update",
  "settings.set_ota_auto": (a) => `Automatic updates ${a.value ? "on" : "off"}`,
  "settings.set_ota_boot_check": (a) => `Check at start ${a.value ? "on" : "off"}`,
  "settings.set_ota_min_battery": (a) => `Update only above ${a.value}% battery`,
  "focus.start": () => "Focus timer", "audio.play": () => "Play a sound", "voice.play": () => "Play a voice note"
};
const actionWords = (a) => (ACTION_WORDS[a.type] || (() => a.type))(a);

async function drawChat(first) {
  const c = await api(`chat?limit=${chatLimit}`);
  const sig = JSON.stringify([c.messages.map((m) => [m.seq, m.delivery?.state]), c.activity, c.busy]);
  if (sig === chatSig && !first) return;
  chatSig = sig;
  const list = $("#chatList");
  const nearBottom = window.innerHeight + window.scrollY > document.body.scrollHeight - 220;
  let day = "";
  const rows = [];
  if (c.total > c.messages.length) rows.push(`<button class="btn small loadmore" id="loadMore">Show older messages</button>`);
  for (const m of c.messages) {
    const d = new Date(m.at).toDateString();
    if (d !== day) { day = d; rows.push(`<div class="day">${d === new Date().toDateString() ? "Today" : new Date(m.at).toLocaleDateString(undefined, { weekday: "long", month: "short", day: "numeric" })}</div>`); }
    if (m.from === "page") {
      const done = m.delivery?.state === "picked";
      rows.push(`<div class="pagenote">Sent from this page: ${esc(m.actions.map(actionWords).join(", ") || m.text)} · ${done ? "picked up" : m.delivery ? "waiting for the Cardputer" : ""}</div>`);
      continue;
    }
    if (m.from === "you") {
      rows.push(`<div class="msg me">${esc(m.text)}</div><div class="meta r">${m.via === "voice" ? "Said to the Cardputer" : m.via === "web" ? "Typed here" : "Typed on the Cardputer"} · ${clock(m.at)}</div>`);
      continue;
    }
    const actions = m.actions.map((a) => `<div class="action">${ICON.flask}${esc(actionWords(a))}</div>`).join("");
    const delivery = !m.delivery ? "" : m.delivery.state === "picked"
      ? `<span class="ok">✓</span> Picked up by the Cardputer ${span(m.delivery.after_ms)} later`
      : `${ICON.clock.replace("class=\"i\"", "class=\"i s\"")} Waiting for the Cardputer`;
    rows.push(`<div class="msg cl${m.failed ? " err" : ""}">${esc(m.text) || "<i>(only an action)</i>"}${actions}
      <div class="tools"><button data-speak="${m.seq}">${ICON.speaker}Listen</button></div></div>
      <div class="meta l">${clock(m.at)}${delivery ? ` · ${delivery}` : ""}</div>`);
  }
  if (c.busy) rows.push(`<div class="activity">${ICON.code}${esc(activityWord(c.activity.state))}${c.activity.detail ? ` <span class="mono">${esc(c.activity.detail)}</span>` : ""}</div>`);
  if (!c.messages.length) rows.push(`<div class="empty">No messages yet.</div>`);
  list.innerHTML = rows.join("");
  $("#loadMore")?.addEventListener("click", () => { chatLimit = Math.min(250, chatLimit + 80); refresh(true); });
  list.querySelectorAll("[data-speak]").forEach((b) => b.addEventListener("click", () => speak(b)));
  if (first || nearBottom) window.scrollTo(0, document.body.scrollHeight);
}

let playing = null;
async function speak(button) {
  playing?.pause();
  button.disabled = true;
  try {
    const blob = await api("speak", { seq: Number(button.dataset.speak) });
    playing = new Audio(URL.createObjectURL(blob));
    await playing.play();
  } catch (e) { toast(e.message === "voice_unavailable" ? "The voice isn't ready on the Mac." : `Couldn't play it: ${e.message}`); }
  button.disabled = false;
}

async function sendChat() {
  const input = $("#chatInput");
  const text = input.value.trim();
  if (!text) return;
  $("#chatSend").disabled = true;
  try {
    await api("chat", { text });
    input.value = "";
    input.style.height = "";
    chatSig = "";
    refresh(true);
  } catch (e) { toast(`Not sent: ${e.message}`); }
  $("#chatSend").disabled = false;
}
$("#chatSend").addEventListener("click", sendChat);
$("#chatInput").addEventListener("keydown", (e) => { if (e.key === "Enter" && !e.shiftKey && !e.isComposing) { e.preventDefault(); sendChat(); } });
$("#chatInput").addEventListener("input", (e) => { e.target.style.height = ""; e.target.style.height = `${e.target.scrollHeight}px`; });

// ---------------------------------------------------------------------------------------------
// Remote: what the bridge can ask the Cardputer to do, outside the Lab
const SCREENS_ON_DEVICE = [["home", "Home"], ["pulse", "Pulse"], ["chat", "Chat"], ["voice", "Voice notes"], ["library", "Library"],
  ["folders", "Folders"], ["assets", "Assets"], ["lab", "Lab"], ["rules", "Lab rules"], ["sequences", "Lab sequences"],
  ["calculator", "Calculator"], ["battery", "Battery"], ["logs", "Logs"], ["settings", "Settings"], ["firmware", "Firmware"], ["bridge_page", "Bridge page"]];
let minBattery = 40;

async function sendAction(action, words, path = "remote") {
  try {
    await api(path, { action, words });
    toast("Sent. The Cardputer picks it up when it next checks in.");
    refresh();
  } catch (e) { toast(`Not sent: ${e.message}`); }
}

async function drawRemote(first) {
  const c = await api("chat?limit=250");
  const sent = c.messages.filter((m) => m.from === "page").slice(-8).reverse();
  const el = $("#remote");
  if (first || !el.innerHTML) {
    el.innerHTML = `
    <div class="note">${ICON.clock}<span>Each button reaches the Cardputer the next time it checks in, usually within 20 to 50 seconds. It shows in the chat there too.</span></div>
    <div class="h2"><h2>Open a screen</h2></div>
    <div class="card"><div class="chips">${SCREENS_ON_DEVICE.map(([mode, name]) => `<button class="chip" data-open="${mode}">${esc(name)}</button>`).join("")}</div></div>
    <div class="h2"><h2>Updates on the Cardputer</h2></div>
    <div class="card list">
      <div class="item"><div class="grow">Install updates by itself</div><button class="btn small" data-set="settings.set_ota_auto" data-value="true">On</button><button class="btn small" data-set="settings.set_ota_auto" data-value="false">Off</button></div>
      <div class="item"><div class="grow">Check for updates when it starts</div><button class="btn small" data-set="settings.set_ota_boot_check" data-value="true">On</button><button class="btn small" data-set="settings.set_ota_boot_check" data-value="false">Off</button></div>
      <div class="item"><div class="grow">Only install above this battery</div><span class="stepper"><button id="batDown">−</button><b id="batVal">${minBattery}%</b><button id="batUp">+</button></span><button class="btn small" id="batSet">Set</button></div>
      <div class="item" style="padding-bottom:0"><div class="btns" style="width:100%"><button class="btn" id="otaCheck">Check now</button><button class="btn primary" id="otaApply">Install update</button></div></div>
    </div>
    <div class="tiny muted" style="margin:6px 4px 0">The Cardputer doesn't tell the bridge how these are set, so the buttons only set them.</div>
    <div class="h2"><h2>Sent from here</h2></div>
    <div class="card list" id="sentList"></div>`;
    el.querySelectorAll("[data-open]").forEach((b) => b.addEventListener("click", () => sendAction({ type: "ui.open", mode: b.dataset.open }, `Opening ${b.textContent}.`)));
    el.querySelectorAll("[data-set]").forEach((b) => b.addEventListener("click", () => sendAction({ type: b.dataset.set, value: b.dataset.value === "true" }, "Changing an update setting.")));
    $("#batDown").addEventListener("click", () => { minBattery = Math.max(5, minBattery - 5); $("#batVal").textContent = `${minBattery}%`; });
    $("#batUp").addEventListener("click", () => { minBattery = Math.min(95, minBattery + 5); $("#batVal").textContent = `${minBattery}%`; });
    $("#batSet").addEventListener("click", () => sendAction({ type: "settings.set_ota_min_battery", value: minBattery }, `Updates only above ${minBattery}% battery.`));
    $("#otaCheck").addEventListener("click", () => sendAction({ type: "ota.check" }, "Checking for an update."));
    $("#otaApply").addEventListener("click", () => { if (confirm("Install the offered update on the Cardputer? It restarts when it's done.")) sendAction({ type: "ota.check" }, "Checking for an update.").then(() => sendAction({ type: "ota.apply" }, "Installing the update.")); });
  }
  $("#sentList").innerHTML = sent.length ? sent.map((m) => `<div class="item"><div class="grow"><b>${esc(m.actions.map(actionWords).join(", ") || m.text)}</b><div class="tiny muted">${clock(m.at)}</div></div>
    ${m.delivery?.state === "picked" ? status("good", "✓ Picked up") : m.delivery ? status("wait", `Waiting · ${span(Date.now() - Date.parse(m.delivery.since))}`) : status("off", "Not sent")}</div>`).join("")
    : `<div class="empty">Nothing sent from this page yet.</div>`;
}

// ---------------------------------------------------------------------------------------------
// Lab: every part, live, as the Cardputer's own test screens show it
const SIG = {
  distance: { name: "Distance", metric: "cm", us: "in", conv: (v) => v / 2.54 },
  temperature: { name: "Temperature", metric: "°C", us: "°F", conv: (v) => v * 9 / 5 + 32 },
  humidity: { name: "Humidity", metric: "%", us: "%" },
  knob: { name: "Knob", metric: "%", us: "%" },
  light: { name: "Light", metric: "%", us: "%" },
  motion: { name: "Motion", onOff: true },
  stick_x: { name: "Stick X" }, stick_y: { name: "Stick Y" },
  stick_button: { name: "Stick button", onOff: true },
  tilt: { name: "Tilt", metric: "°", us: "°" },
  satellites: { name: "Satellites" },
  spoken: { name: "Its name", onOff: true },
  speed: { name: "Speed", metric: "km/h", us: "mph", conv: (v) => v * 0.621371 },
  heading: { name: "Heading", metric: "°", us: "°" },
  spot_distance: { name: "From spot", metric: "m", us: "ft", conv: (v) => v * 3.28084 }
};
const OUT = {
  servo: { name: "Servo", unit: "°" }, strip_color: { name: "Strip color", hue: true }, strip_bright: { name: "Strip brightness", unit: "%" },
  strip_length: { name: "Strip length", unit: " LEDs" }, relay_1: { name: "Relay 1", onOff: true }, relay_2: { name: "Relay 2", onOff: true },
  led: { name: "Cardputer LED", hue: true }, speaker: { name: "Speaker", unit: " Hz" }, tone: { name: "Speaker", unit: " Hz" },
  lamp_power: { name: "Lamp power", onOff: true }, lamp_bright: { name: "Lamp brightness", unit: "%" },
  ac_tcl: { name: "TCL AC", onOff: true }, ac_daikin: { name: "Daikin AC", onOff: true }, say: { name: "Say" }, sequence: { name: "Sequence" }
};
const HUES = [[0, "Red"], [30, "Orange"], [60, "Yellow"], [120, "Green"], [180, "Cyan"], [240, "Blue"], [280, "Purple"], [330, "Pink"]];
const hueName = (h) => HUES.reduce((best, [v, n]) => (Math.abs(((h - v + 540) % 360) - 180) < Math.abs(((h - best[0] + 540) % 360) - 180) ? [v, n] : best), HUES[0])[1];
let units = "us";
function sigValue(key, v) {
  const info = SIG[key] || {};
  if (v === undefined || v === null) return "—";
  if (info.onOff) return v ? "on" : "off";
  const n = units === "us" && info.conv ? info.conv(v) : v;
  const unit = (units === "us" ? info.us : info.metric) || "";
  return `${Math.round(n)}${/^[a-z]/iu.test(unit) ? " " : ""}${unit}`;
}
function toLabUnits(key, v) {
  const info = SIG[key] || {};
  if (units !== "us" || !info.conv) return Math.round(v);
  const back = { distance: (x) => x * 2.54, temperature: (x) => (x - 32) * 5 / 9, speed: (x) => x / 0.621371, spot_distance: (x) => x / 3.28084 }[key];
  return Math.round(back ? back(v) : v);
}
function outValue(key, v) {
  const info = OUT[key] || {};
  if (info.onOff) return v ? "on" : "off";
  if (v < 0) return info.hue ? "off" : "at rest";
  if (info.hue) return hueName(v);
  return `${v}${info.unit || ""}`;
}

function spark(points, lo, hi) {
  if (!points || points.length < 2) return `<svg class="spark" viewBox="0 0 120 44"></svg>`;
  const vals = points.map((p) => p[1]);
  let min = Math.min(...vals), max = Math.max(...vals);
  if (lo !== undefined) { min = Math.min(min, lo); max = Math.max(max, hi); }
  if (max === min) { max += 1; min -= 1; }
  const xy = points.map(([t, v]) => `${(120 + t).toFixed(1)},${(40 - ((v - min) / (max - min)) * 36).toFixed(1)}`).join(" ");
  return `<svg class="spark" viewBox="0 0 120 44" preserveAspectRatio="none"><polyline fill="none" stroke="#c6f432" stroke-width="2" vector-effect="non-scaling-stroke" points="${xy}"/></svg>`;
}
const range = (points) => points?.length ? [Math.min(...points.map((p) => p[1])), Math.max(...points.map((p) => p[1]))] : null;

// Each of the Lab's parts, in the order its Devices list has them, with how it knows it's there.
const PARTS = [
  { name: "Ultrasonic", unit: "Ultrasonic", draw: (s, h) => partReading("distance", s, h, "Wave a hand in front of it.") },
  { name: "ENV III", unit: "ENV III", draw: (s, h) => {
    const t = s.sigs.temperature, rh = s.sigs.humidity;
    const comfy = t !== undefined && rh !== undefined ? (t >= 20 && t <= 26 && rh >= 30 && rh <= 60 ? "Feels comfortable" : t > 26 ? "Warm" : t < 20 ? "Cool" : rh > 60 ? "Humid" : "Dry") : "";
    return `<div class="grid2"><div><div class="val">${sigValue("temperature", t)}</div>${spark(h.temperature)}</div><div><div class="val">${sigValue("humidity", rh)}</div>${spark(h.humidity, 0, 100)}</div></div><div class="sub">${esc(comfy)}</div>`;
  } },
  { name: "Joystick 2", unit: "Joystick 2", draw: (s) => {
    const x = s.sigs.stick_x ?? 0, y = s.sigs.stick_y ?? 0;
    return `<div class="row"><div class="joy"><i class="${s.sigs.stick_button ? "pressed" : ""}" style="left:${50 + x * 0.42}%;top:${50 - y * 0.42}%"></i></div>
      <div class="col"><span class="sub">X ${x}, Y ${y}</span><span class="sub">Button ${s.sigs.stick_button ? "<b style='color:var(--ink)'>pressed</b>" : "up"}</span></div></div>`;
  } },
  { name: "Mini Angle", part: "Mini Angle", draw: (s, h) => {
    const v = s.sigs.knob;
    const a = ((v ?? 0) / 100) * 270 - 135;
    return `<div class="row"><svg viewBox="0 0 80 80" width="78" height="78"><circle cx="40" cy="40" r="32" fill="none" stroke="rgba(198,244,50,.15)" stroke-width="8" stroke-dasharray="150.8 50.3" transform="rotate(135 40 40)"/>
      <circle cx="40" cy="40" r="32" fill="none" stroke="#c6f432" stroke-width="8" stroke-dasharray="${(((v ?? 0) / 100) * 150.8).toFixed(1)} 201" transform="rotate(135 40 40)"/>
      <line x1="40" y1="40" x2="${40 + 22 * Math.sin((a * Math.PI) / 180)}" y2="${40 - 22 * Math.cos((a * Math.PI) / 180)}" stroke="#fff" stroke-width="3" stroke-linecap="round"/></svg>
      <div class="col"><div class="val">${sigValue("knob", v)}</div>${spark(h.knob, 0, 100)}</div></div>`;
  } },
  { name: "Light", part: "Light", unit: "DLight", draw: (s, h) => {
    const v = s.sigs.light;
    const sky = v === undefined ? "#1d2244" : `hsl(${210 - (v / 100) * 20}, ${30 + v * 0.5}%, ${8 + v * 0.55}%)`;
    return `<div class="row"><div class="ledball" style="background:${sky};border-radius:12px"></div><div class="col" style="flex:1"><div class="val">${sigValue("light", v)}</div>${spark(h.light, 0, 100)}</div></div>`;
  } },
  { name: "PIR motion", part: "PIR motion", draw: (s) => {
    const v = s.sigs.motion;
    return `<div class="val" style="color:${v ? "var(--warn)" : "var(--ink)"}">${v === undefined ? "—" : v ? "Motion" : "Still"}</div><div class="sub">It stays on for about 2 seconds after movement stops.</div>`;
  } },
  { name: "2 relays", part: "2 relays", draw: (s) => `<div class="relays"><span class="${s.outs.relay_1 ? "on" : ""}">Relay 1 ${s.outs.relay_1 ? "on" : "off"}</span><span class="${s.outs.relay_2 ? "on" : ""}">Relay 2 ${s.outs.relay_2 ? "on" : "off"}</span></div>` },
  { name: "Servo", part: "Servo", draw: (s) => {
    const v = s.outs.servo !== undefined && s.outs.servo >= 0 ? s.outs.servo : undefined;
    const a = ((v ?? 90) - 90) * (Math.PI / 180);
    return `<div class="row"><svg viewBox="0 0 120 66" width="120" height="66"><path d="M10 60a50 50 0 0 1 100 0" fill="none" stroke="rgba(198,244,50,.18)" stroke-width="8"/>
      <line x1="60" y1="60" x2="${60 + 44 * Math.sin(a)}" y2="${60 - 44 * Math.cos(a)}" stroke="#c6f432" stroke-width="4" stroke-linecap="round"/><circle cx="60" cy="60" r="5" fill="#fff"/></svg>
      <div class="col"><div class="val">${v === undefined ? "At rest" : `${v}°`}</div>${s.extra.sweeping === "1" ? `<span class="sub">Sweeping</span>` : ""}</div></div>`;
  } },
  { name: "LED strip", part: "LED strip", draw: (s) => {
    const hue = s.outs.strip_color, bright = s.outs.strip_bright >= 0 ? s.outs.strip_bright : 100, len = s.outs.strip_length >= 0 ? s.outs.strip_length : 30;
    const on = hue !== undefined && hue >= 0 && bright > 0;
    const leds = Array.from({ length: 30 }, (_, i) => `<i style="${on && i < len ? `background:hsl(${hue},100%,${20 + bright * 0.35}%)` : ""}"></i>`).join("");
    return `<div class="leds">${leds}</div><div class="sub">${on ? `${hueName(hue)} · ${bright}% · ${len} LEDs` : "Off"}${s.extra.strip_test ? ` · test pattern: ${esc(s.extra.strip_test)}` : ""}</div>`;
  } },
  { name: "I/O hub", unit: "I/O hub", draw: (s) => s.hub ? `<div class="hubports">${s.hub.map((p, i) => `<span><b>Port ${i}</b>${esc(p === "Nothing" ? "—" : p)}</span>`).join("")}</div>` : `<div class="sub">Plug it in, then set each port on the Cardputer.</div>` },
  { name: "NFC", unit: "NFC", draw: (s) => `<div class="sub">${s.found.includes("NFC") ? "Answering. NFC Notes on the Cardputer is its full test." : "Not found."}</div>` },
  { name: "LoRa Cap GPS", gps: true, draw: (s) => {
    const g = s.gps;
    const heading = s.sigs.heading;
    const needle = heading === undefined ? "" : `<line x1="40" y1="40" x2="${40 + 26 * Math.sin((heading * Math.PI) / 180)}" y2="${40 - 26 * Math.cos((heading * Math.PI) / 180)}" stroke="#c6f432" stroke-width="4" stroke-linecap="round"/>`;
    return `<div class="row"><svg viewBox="0 0 80 80" width="80" height="80"><circle cx="40" cy="40" r="34" fill="none" stroke="rgba(255,255,255,.15)" stroke-width="2"/><text x="40" y="14" fill="#8f8d87" font-size="9" text-anchor="middle">N</text>${needle}<circle cx="40" cy="40" r="3" fill="#fff"/></svg>
      <div class="col"><span class="mid">${g ? (g.fix ? "Fix" : "No fix yet") : "—"}</span>
      <span class="sub">${g ? `${g.used} of ${g.inView} satellites` : ""}${g?.altitude !== null && g?.altitude !== undefined ? ` · ${units === "us" ? Math.round(g.altitude * 3.28084) + " ft" : g.altitude + " m"} up` : ""}</span>
      <span class="sub">${s.sigs.speed !== undefined ? `${sigValue("speed", s.sigs.speed)}` : ""}${s.sigs.spot_distance !== undefined ? ` · ${sigValue("spot_distance", s.sigs.spot_distance)} from your spot` : ""}</span></div></div>`;
  } },
  { name: "Tilt", builtIn: true, draw: (s, h) => {
    const v = s.sigs.tilt ?? 0;
    return `<div class="row"><svg viewBox="0 0 120 30" width="120" height="30"><rect x="2" y="8" width="116" height="14" rx="7" fill="none" stroke="rgba(255,255,255,.2)"/><line x1="60" y1="6" x2="60" y2="24" stroke="rgba(255,255,255,.25)"/>
      <circle cx="${60 + Math.max(-50, Math.min(50, v * 0.55))}" cy="15" r="6" fill="#c6f432"/></svg><div class="val">${s.sigs.tilt === undefined ? "—" : `${v}°`}</div></div>${spark(h.tilt, -90, 90)}`;
  } },
  { name: "Cardputer LED", builtIn: true, draw: (s) => {
    const v = s.outs.led;
    const on = v !== undefined && v >= 0;
    return `<div class="row"><div class="ledball" style="background:${on ? `hsl(${v},100%,50%)` : "#1d2244"};box-shadow:${on ? `0 0 18px hsl(${v},100%,50%)` : "none"}"></div><div class="mid">${on ? hueName(v) : "Off"}</div></div>`;
  } },
  { name: "Speaker", builtIn: true, draw: (s) => { const v = s.outs.speaker ?? s.outs.tone; return `<div class="val">${v > 0 ? `${v}<small>Hz</small>` : "Quiet"}</div>`; } },
  { name: "smartLamp", lamp: true, draw: (s) => `<div class="val">${s.outs.lamp_power === undefined ? "—" : s.outs.lamp_power ? "On" : "Off"}</div><div class="sub">${s.outs.lamp_bright >= 0 ? `Brightness ${s.outs.lamp_bright}%` : "Looks for a smartLamp on your Wi-Fi."}</div>` },
  { name: "IR for AC", ac: true, draw: (s) => `<div class="sub">${s.outs.ac_tcl !== undefined || s.outs.ac_daikin !== undefined ? `TCL ${s.outs.ac_tcl ? "on" : "off"} · Daikin ${s.outs.ac_daikin ? "on" : "off"}` : "Rules can switch a TCL or Daikin air conditioner on and off."}</div>` }
];
function partReading(key, s, h, hint) {
  const v = s.sigs[key];
  const r = range(h[key]);
  return `<div class="val">${sigValue(key, v)}</div>${spark(h[key])}<div class="sub">${r ? `Last 2 minutes: ${sigValue(key, r[0])} to ${sigValue(key, r[1])}` : esc(hint)}</div>`;
}
function presence(part, s) {
  if (part.builtIn) return ["here", "Built in"];
  if (part.unit && s.found.includes(part.unit)) return ["here", "Answering"];
  if (part.gps && s.gps) return ["here", s.gps.fix ? "Has a fix" : "Searching"];
  if (part.part) {
    if (s.wires?.white === part.part) return ["ring", "On Grove, white"];
    if (s.wires?.yellow === part.part) return ["ring", "On Grove, yellow"];
    if (s.wires && part.part === "2 relays" && (s.wires.white === part.part || s.wires.yellow === part.part)) return ["ring", "On Grove"];
    const port = s.hub?.indexOf(part.part);
    if (port !== undefined && port >= 0) return ["ring", `Hub port ${port}`];
  }
  if (part.lamp && (s.outs.lamp_power === 1 || s.outs.lamp_bright >= 0)) return ["ring", "In use"];
  if (part.ac) return ["ring", "Signal only"];
  return ["", "Not found"];
}

let labTab = "parts";
let labData = null;
async function drawLab(first) {
  labData = await api("lab");
  const el = $("#lab");
  const live = labData.live;
  const snap = live.snapshot || { found: [], sigs: {}, outs: {}, extra: {}, hub: null, wires: null, gps: null };
  if (snap.units) units = snap.units;
  if (first || !el.querySelector(".seg")) {
    el.innerHTML = `<div class="seg"><a data-tab="parts">Parts</a><a data-tab="rules">Rules</a><a data-tab="seqs">Sequences</a></div><div id="labBody"></div>`;
    el.querySelectorAll("[data-tab]").forEach((a) => a.addEventListener("click", () => { labTab = a.dataset.tab; drawLabBody(true); }));
  }
  drawLabBody(first);
}
function liveBanner(live) {
  const s = live.snapshot;
  const views = { devices: "the Devices list", device: "a part's screen", dashboard: "the Dashboard", rules: "the rules", seqs: "the sequences", playing: "rules playing" };
  if (!live.reported) return `<div class="livebar"><span class="dot"></span><span>No readings yet. The Cardputer sends them while its Lab is open on your home Wi-Fi, with the newest firmware.</span></div>`;
  if (s.view === "closed") return `<div class="livebar"><span class="dot"></span><span>The Lab is closed on the Cardputer. Open it there to see your parts live. These are the last readings, ${ago(live.age_ms)}.</span></div>`;
  if (!live.live) return `<div class="livebar"><span class="dot"></span><span>No readings for ${ago(live.age_ms).replace(" ago", "")}. The Lab may be closed, or the Cardputer is away from home.</span></div>`;
  const firing = s.extra.firing ? s.extra.firing.split(",").filter(Boolean) : [];
  const playing = s.playing ? ` · rules playing${firing.length ? `, ${firing.length === 1 ? `rule ${firing[0]}` : `rules ${firing.join(", ")}`} firing` : ""}` : "";
  return `<div class="livebar live"><span class="dot"></span><span>Live from the Cardputer${s.open ? ` · ${esc(s.open)} is open` : views[s.view] ? ` · on ${views[s.view]}` : ""}${playing}${s.extra.seq ? ` · playing “${esc(s.extra.seq)}”` : ""}</span></div>`;
}
// --- Parts, live, with controls for what can be driven ---
// Each card is made once and its reading redrawn in place, so a slider isn't lost mid-drag.
const partCards = new Map();
// The outputs each card can set, and how. Sliders send as they move, a few times a second.
const CONTROLS = {
  "Servo": () => `${slider("servo", 0, 180, 90, "°")}<div class="btnrow">${btn("servo", 0, "0°")}${btn("servo", 90, "Center")}${btn("servo", 180, "180°")}</div>`,
  "LED strip": () => `<div class="btnrow">${swatches("strip_color")}${btn("strip_color", -1, "Off")}</div>${slider("strip_bright", 0, 100, 60, "%", "Brightness")}${slider("strip_length", 0, 30, 30, " LEDs", "Length")}`,
  "Cardputer LED": () => `<div class="btnrow">${swatches("led")}${btn("led", -1, "Off")}</div>`,
  "Speaker": () => `${slider("speaker", 200, 2000, 880, " Hz", "Pitch", true)}<div class="btnrow"><button class="btn small" data-beep>Beep</button></div>`,
  "2 relays": () => `<div class="btnrow">${toggle("relay_1", "Relay 1")}${toggle("relay_2", "Relay 2")}</div>`,
  "smartLamp": () => `<div class="btnrow">${btn("lamp_power", 1, "On")}${btn("lamp_power", 0, "Off")}</div>${slider("lamp_bright", 0, 100, 60, "%", "Brightness")}`,
  "IR for AC": () => `<div class="btnrow"><span class="sub">TCL</span>${btn("ac_tcl", 1, "On")}${btn("ac_tcl", 0, "Off")}<span class="sub">Daikin</span>${btn("ac_daikin", 1, "On")}${btn("ac_daikin", 0, "Off")}</div>`
};
const PART_OUTPUTS = { "Servo": ["servo"], "LED strip": ["strip_color", "strip_bright", "strip_length"], "Cardputer LED": ["led"], "Speaker": ["speaker"],
  "2 relays": ["relay_1", "relay_2"], "smartLamp": ["lamp_power", "lamp_bright"], "IR for AC": ["ac_tcl", "ac_daikin"] };
const btn = (output, value, label) => `<button class="btn small" data-out="${output}" data-val="${value}">${esc(label)}</button>`;
const toggle = (output, label) => `<button class="btn small relaybtn" data-toggle="${output}" data-label="${esc(label)}">${esc(label)}</button>`;
const swatches = (output) => HUES.map(([v, n]) => `<button class="sw" title="${n}" data-out="${output}" data-val="${v}" style="background:hsl(${v},100%,50%)"></button>`).join("");
const slider = (output, min, max, value, unit, label = "", hold = false) => `<label class="sl"><span>${esc(label || OUT[output]?.name || output)}</span>
  <input type="range" min="${min}" max="${max}" value="${value}" data-slide="${output}" ${hold ? "data-hold" : ""}><b data-show="${output}">${value}${unit}</b></label>`;

async function sendControl(output, value) {
  try { await api("lab/control", { output, value: Number(value) }); setTimeout(() => refresh(), 150); }
  catch (e) { toast(e.message === "lab_not_live" ? "Open the Lab on the Cardputer, at home, to use the controls." : `Not sent: ${e.message}`); }
}
function wireControls(card, part) {
  card.querySelectorAll("[data-out]").forEach((b) => b.addEventListener("click", () => sendControl(b.dataset.out, b.dataset.val)));
  card.querySelectorAll("[data-toggle]").forEach((b) => b.addEventListener("click", () => {
    const now = labData.live.snapshot?.outs?.[b.dataset.toggle];
    sendControl(b.dataset.toggle, now ? 0 : 1);
  }));
  const pitch = card.querySelector('[data-slide="speaker"]');
  card.querySelector("[data-beep]")?.addEventListener("click", () => sendControl("speaker", pitch.value));
  card.querySelectorAll("input[data-slide]").forEach((input) => {
    const show = card.querySelector(`[data-show="${input.dataset.slide}"]`);
    const unit = show.textContent.replace(/^-?\d+/u, "");
    let last = 0, timer = 0;
    const go = () => { last = Date.now(); if (!input.hasAttribute("data-hold")) sendControl(input.dataset.slide, input.value); };
    input.addEventListener("input", () => {
      show.textContent = `${input.value}${unit}`;
      input.dataset.touched = String(Date.now());
      clearTimeout(timer);
      if (Date.now() - last > 250) go(); else timer = setTimeout(go, 250);
    });
    input.addEventListener("change", () => { clearTimeout(timer); go(); });
  });
}
// A slider follows the Cardputer's value, unless it was moved in the last few seconds.
function syncControls(card, s) {
  card.querySelectorAll("input[data-slide]").forEach((input) => {
    const v = s.outs?.[input.dataset.slide];
    if (v === undefined || v < 0 || Date.now() - Number(input.dataset.touched || 0) < 3000 || input.hasAttribute("data-hold")) return;
    input.value = v;
    const show = card.querySelector(`[data-show="${input.dataset.slide}"]`);
    show.textContent = `${v}${show.textContent.replace(/^-?\d+/u, "")}`;
  });
  card.querySelectorAll("[data-toggle]").forEach((b) => {
    // Says what a tap does, since the card above already shows which way it is.
    const on = Boolean(s.outs?.[b.dataset.toggle]);
    b.textContent = `Turn ${b.dataset.label.toLowerCase()} ${on ? "off" : "on"}`;
  });
}
function controlNote(part, live) {
  const mine = (live.controls || []).filter((c) => PART_OUTPUTS[part.name]?.includes(c.output) && c.age_ms < 15000).at(-1);
  if (!mine) return "";
  const words = { waiting: "Sending…", sent: "Sent", done: "Done", replaced: "Sent", dropped: "Not sent: the Lab wasn't reporting",
    refused: `Not done: ${mine.why}`, unanswered: "No answer. The Cardputer needs firmware 0.2.330 or later" }[mine.state] || "";
  return `<span class="${mine.state === "refused" || mine.state === "unanswered" || mine.state === "dropped" ? "err" : ""}">${esc(words)}</span>`;
}

function drawParts(body) {
  const live = labData.live;
  const s = live.snapshot || { found: [], sigs: {}, outs: {}, extra: {}, hub: null, wires: null, gps: null };
  const h = live.history || {};
  if (body.dataset.tab !== "parts" || !body.querySelector(".parts")) {
    body.dataset.tab = "parts";
    body.innerHTML = `<div id="liveBar"></div><div class="parts"></div>`;
    partCards.clear();
  }
  $("#liveBar", body).innerHTML = liveBanner(live) + (live.reported && !live.can_control
    ? `<div class="tiny muted" style="margin:-4px 4px 10px">Controls work while the Lab is open on the Cardputer at home.</div>` : "");
  const grid = $(".parts", body);
  const order = [];
  for (const part of PARTS) {
    const [dot, where] = presence(part, s);
    const open = s.open === part.name;
    let card = partCards.get(part.name);
    if (!card) {
      card = document.createElement("div");
      card.innerHTML = `<div class="head"></div><div class="pv"></div>${CONTROLS[part.name] ? `<div class="pc">${CONTROLS[part.name]()}<div class="pnote"></div></div>` : ""}`;
      if (CONTROLS[part.name]) wireControls(card, part);
      partCards.set(part.name, card);
    }
    card.className = `part${open ? " open" : ""}${!dot && !open ? " absent" : ""}`;
    card.querySelector(".head").innerHTML = `<span class="pdot ${dot}"></span><b>${esc(part.name)}</b><span class="where">${open ? "Open on the Cardputer" : esc(where)}</span>`;
    card.querySelector(".pv").innerHTML = part.draw(s, h);
    const controls = card.querySelector(".pc");
    if (controls) {
      controls.classList.toggle("off", !live.can_control);
      syncControls(card, s);
      card.querySelector(".pnote").innerHTML = controlNote(part, live);
    }
    order.push({ card, rank: open ? 0 : dot === "here" ? 1 : dot === "ring" ? 2 : 3 });
  }
  // The part open on the Cardputer first, then the ones that are there, then the rest. Cards only
  // move when the order changes, so one being used stays put.
  order.sort((a, b) => a.rank - b.rank);
  const wanted = order.map((o) => o.card);
  if (wanted.some((card, i) => grid.children[i] !== card)) wanted.forEach((card) => grid.appendChild(card));
}

function drawLabBody(first) {
  document.querySelectorAll("#lab [data-tab]").forEach((a) => a.classList.toggle("on", a.dataset.tab === labTab));
  const body = $("#labBody");
  if (!body) return;
  if (labTab === "parts") { drawParts(body); return; }
  if (!first && body.dataset.tab === labTab) return; // Rules and sequences only change when asked.
  body.dataset.tab = labTab;
  const saved = labData.saved;
  const note = `<div class="note lime">${ICON.info}<span>${saved.known ? `What's saved, from the Cardputer's last chat message (${clock(saved.at)}).` : "The Cardputer hasn't told the bridge what's saved yet. Send any chat message from it."} Anything sent from here waits on the Cardputer until you check it there.</span></div>`;
  if (labTab === "rules") body.innerHTML = note + drawRules(saved) + drawBuilder();
  else body.innerHTML = note + drawSequences(saved);
  wireLab(body);
}

function condText(c) {
  const info = SIG[c.sensor] || { name: c.sensor };
  if (c.test === "changes") return [info.name, "changes"];
  if (info.onOff) return [info.name, c.test === "on" || c.test === "over" ? "is on" : "is off"];
  return [info.name, `${c.test} ${sigValue(c.sensor, c.value)}`];
}
function doText(a) {
  const info = OUT[a.output] || { name: a.output };
  if (a.output === "say") return [info.name, `“${a.text}”`];
  if (a.output === "sequence") return ["Plays", a.value];
  if (a.how === "follow" || a.how === "oppose") return [info.name, `${a.how === "follow" ? "follows" : "opposes"} ${(SIG[a.value] || { name: a.value }).name.toLowerCase()}`];
  return [info.name, outValue(a.output, a.value)];
}
function drawRules(saved) {
  if (!saved.rules.length) return `<div class="empty">${saved.known ? "No rules saved on the Cardputer." : ""}</div>`;
  return saved.rules.map((r) => `<div class="card rulecard">
    <div class="between"><div class="col"><b style="font-size:17px">${esc(r.name)}</b><span class="tiny muted">${r.when.length > 1 ? (r.any ? "Any one starts it" : "All of them start it") : ""}${r.hold_ms ? `${r.when.length > 1 ? " · " : ""}keeps going ${r.hold_ms / 1000} s` : ""}</span></div>${status(r.on ? "good" : "off", r.on ? "On" : "Off")}</div>
    <div class="flow"><div class="side-col">${r.when.map((c) => { const [n, v] = condText(c); return `<div class="node"><span class="label">${esc(n)}</span><b>${esc(v)}</b></div>`; }).join("")}</div>
      <div class="join">${r.any ? "ANY" : "ALL"}</div>
      <div class="side-col">${r.do.map((a) => { const [n, v] = doText(a); return `<div class="node"><span class="label">${esc(n)}</span><b>${esc(v)}</b></div>`; }).join("")}</div></div>
  </div>`).join("");
}
const RULE_SENSORS = Object.keys(SIG);
const RULE_OUTPUTS = ["strip_color", "strip_bright", "strip_length", "servo", "led", "speaker", "relay_1", "relay_2", "lamp_power", "lamp_bright", "ac_tcl", "ac_daikin", "say"];
function drawBuilder() {
  const sensorOpts = RULE_SENSORS.map((k) => `<option value="${k}">${esc(SIG[k].name)}</option>`).join("");
  const outOpts = RULE_OUTPUTS.map((k) => `<option value="${k}">${esc(OUT[k].name)}</option>`).join("");
  return `<div class="h2"><h2>New rule</h2><span class="tiny muted">in ${units === "us" ? "US" : "metric"} units</span></div>
  <div class="card builder" id="builder">
    <div class="label">When</div>
    ${[0, 1, 2].map((i) => `<div class="editrow" data-when="${i}" ${i ? "hidden" : ""}><select class="w-sensor"><option value="">—</option>${sensorOpts}</select>
      <select class="w-test"><option value="under">under</option><option value="over">over</option><option value="on">is on</option><option value="off">is off</option><option value="changes">changes</option></select>
      <input class="w-value" type="number" value="12"></div>`).join("")}
    <div class="editrow"><button class="link" id="addWhen">+ Add a sensor</button><span style="margin-left:auto" class="chips"><button class="chip on" data-any="0">All of them</button><button class="chip" data-any="1">Any one</button></span></div>
    <div class="sep"></div>
    <div class="label">Do</div>
    ${[0, 1, 2].map((i) => `<div class="editrow" data-do="${i}" ${i ? "hidden" : ""}><select class="d-out"><option value="">—</option>${outOpts}</select>
      <select class="d-how"><option value="set">set to</option><option value="follow">follows</option><option value="oppose">opposes</option></select>
      <input class="d-value" type="number" value="120"><select class="d-hue">${HUES.map(([v, n]) => `<option value="${v}">${n}</option>`).join("")}</select>
      <select class="d-src">${sensorOpts}</select><input class="d-text" type="text" maxlength="40" placeholder="What to say"></div>`).join("")}
    <div class="editrow"><button class="link" id="addDo">+ Add an output</button></div>
    <div class="sep"></div>
    <div class="between"><span>Keeps going for</span><span><input id="holdS" type="number" min="0" max="30" value="2" style="width:70px"> s</span></div>
    <div class="between" style="margin-top:10px"><span>Name</span><input id="ruleName" type="text" maxlength="15" placeholder="Up to 15 letters"></div>
    <button class="btn primary block" style="margin-top:14px" id="sendRule">Send to the Cardputer to check</button>
    <div class="tiny muted" style="margin-top:8px;text-align:center">Or describe it in the chat and let Claude write it.</div>
  </div>`;
}
function drawSequences(saved) {
  if (!saved.sequences.length) return `<div class="empty">${saved.known ? "No sequences saved on the Cardputer." : ""}</div>`;
  return saved.sequences.map((q) => {
    const total = q.steps.reduce((sum, st) => sum + (st.wait_ms || 0), 0);
    const steps = q.steps.map((st) => st.wait_ms ? `<span class="step wait">wait ${st.wait_ms >= 1000 ? `${st.wait_ms / 1000} s` : `${st.wait_ms} ms`}</span>`
      : `<span class="step">${OUT[st.output]?.hue && !st.off ? `<span class="swatch" style="background:hsl(${st.set},100%,50%)"></span>` : ""}${esc(OUT[st.output]?.name || st.output)} ${st.off ? "off" : esc(outValue(st.output, st.set))}</span>`).join("");
    return `<div class="card"><div class="between"><div class="col"><b style="font-size:17px">${esc(q.name)}</b><span class="tiny muted">${q.steps.length} steps · ${(total / 1000).toFixed(1)} s · ${q.loop ? "plays over and over" : "plays once"}</span></div>
      <button class="btn primary" data-play="${esc(q.name)}">${ICON.play}Play</button></div><div class="steps">${steps}</div></div>`;
  }).join("");
}
function wireLab(body) {
  body.querySelectorAll("[data-play]").forEach((b) => b.addEventListener("click", () => sendAction({ type: "lab.play", name: b.dataset.play }, `Playing ${b.dataset.play} in the Lab.`, "lab/action")));
  const builder = $("#builder", body);
  if (!builder) return;
  let any = false;
  const syncRow = (row) => {
    const out = $(".d-out", row)?.value;
    if (!out) return;
    const info = OUT[out] || {};
    const how = $(".d-how", row).value;
    $(".d-how", row).hidden = info.onOff || out === "say";
    $(".d-value", row).hidden = out === "say" || info.hue && how === "set" || how !== "set";
    $(".d-hue", row).hidden = !(info.hue && how === "set");
    $(".d-src", row).hidden = how === "set" || info.onOff || out === "say";
    $(".d-text", row).hidden = out !== "say";
    if (info.onOff && how !== "set") $(".d-how", row).value = "set";
  };
  const syncWhen = (row) => {
    const key = $(".w-sensor", row).value;
    const onOff = SIG[key]?.onOff;
    const test = $(".w-test", row);
    [...test.options].forEach((o) => { o.hidden = onOff ? ["under", "over"].includes(o.value) : ["on", "off"].includes(o.value); });
    if (test.selectedOptions[0]?.hidden) test.value = onOff ? "on" : "under";
    $(".w-value", row).hidden = onOff || test.value === "changes";
  };
  builder.querySelectorAll("[data-do]").forEach((row) => { row.addEventListener("change", () => syncRow(row)); syncRow(row); });
  builder.querySelectorAll("[data-when]").forEach((row) => { row.addEventListener("change", () => syncWhen(row)); syncWhen(row); });
  $("#addWhen", builder).addEventListener("click", () => builder.querySelector("[data-when][hidden]")?.removeAttribute("hidden"));
  $("#addDo", builder).addEventListener("click", () => builder.querySelector("[data-do][hidden]")?.removeAttribute("hidden"));
  builder.querySelectorAll("[data-any]").forEach((chip) => chip.addEventListener("click", () => {
    any = chip.dataset.any === "1";
    builder.querySelectorAll("[data-any]").forEach((c) => c.classList.toggle("on", c === chip));
  }));
  $("#sendRule", builder).addEventListener("click", () => {
    const when = [...builder.querySelectorAll("[data-when]:not([hidden])")].map((row) => {
      const sensor = $(".w-sensor", row).value;
      if (!sensor) return null;
      const test = $(".w-test", row).value;
      const line = { sensor, test };
      if (["under", "over"].includes(test)) line.value = toLabUnits(sensor, Number($(".w-value", row).value));
      return line;
    }).filter(Boolean);
    const dos = [...builder.querySelectorAll("[data-do]:not([hidden])")].map((row) => {
      const output = $(".d-out", row).value;
      if (!output) return null;
      if (output === "say") return { output, text: $(".d-text", row).value.trim().slice(0, 40) };
      const how = $(".d-how", row).value;
      const info = OUT[output] || {};
      if (how !== "set" && !info.onOff) return { output, [how]: $(".d-src", row).value };
      return { output, set: info.hue ? Number($(".d-hue", row).value) : Number($(".d-value", row).value) };
    }).filter(Boolean);
    const name = $("#ruleName", builder).value.trim().slice(0, 15);
    if (!when.length || !dos.length || !name) { toast("A rule needs a sensor, an output and a name."); return; }
    const rule = { type: "lab.rule", name, any, hold_ms: Math.round(Math.max(0, Math.min(30, Number($("#holdS", builder).value) || 0)) * 1000), when, do: dos };
    sendAction(rule, `A rule to check: ${name}.`, "lab/action");
  });
}

// ---------------------------------------------------------------------------------------------
// Logs
const logState = { tag: "", q: "", problems: false, checks: false };
async function drawLogs(first) {
  const el = $("#logs");
  if (first || !el.innerHTML) {
    el.innerHTML = `<div class="search"><svg class="i" viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4"/></svg><input id="logQ" placeholder="Search the log" style="border:0;background:transparent;flex:1;padding:0;outline:none"></div>
      <div class="chips" id="logTags" style="margin-bottom:10px"></div>
      <div class="card flat between" style="padding:11px 14px"><span class="small" id="checksLabel">Hide the 20-second update checks</span><span class="toggle on" id="checksToggle" role="switch"></span></div>
      <div class="card log" id="logList"><div class="empty">Loading…</div></div>
      <div class="h2"><h2>Logs the Cardputer sent</h2></div><div class="card uploads" id="uploads"><div class="empty">Loading…</div></div>`;
    let typing = 0;
    $("#logQ").addEventListener("input", (e) => { clearTimeout(typing); typing = setTimeout(() => { logState.q = e.target.value; drawLogLines(); }, 300); });
    $("#checksToggle").addEventListener("click", (e) => { logState.checks = !logState.checks; e.target.classList.toggle("on", !logState.checks); drawLogLines(); });
    drawUploads();
  }
  await drawLogLines();
}
const TAG_NAMES = { device: "Cardputer", "device-events": "Deliveries", turn: "Chat", "voice-raw": "Voice", "voice-tool": "Voice tools", tts: "Spoken replies",
  cameras: "Cameras", podcasts: "Podcasts", planes: "Planes", firmware: "Firmware", "claude-code": "Claude", ui: "Web page", "lab-live": "Lab", bridge: "Bridge" };
async function drawLogLines() {
  const params = new URLSearchParams({ tag: logState.tag, q: logState.q, problems: logState.problems ? "1" : "0", checks: logState.checks ? "1" : "0" });
  const r = await api(`logs?${params}`);
  const tags = Object.entries(r.tags).filter(([t]) => TAG_NAMES[t]).sort((a, b) => b[1] - a[1]);
  $("#logTags").innerHTML = `<button class="chip ${!logState.tag && !logState.problems ? "on" : ""}" data-tag="">Everything</button>` +
    tags.map(([t]) => `<button class="chip ${logState.tag === t ? "on" : ""}" data-tag="${esc(t)}">${esc(TAG_NAMES[t])}</button>`).join("") +
    `<button class="chip ${logState.problems ? "on" : ""}" data-problems="1">${ICON.warn}Problems</button>`;
  $("#logTags").querySelectorAll("[data-tag]").forEach((b) => b.addEventListener("click", () => { logState.tag = b.dataset.tag; logState.problems = false; drawLogLines(); }));
  $("#logTags [data-problems]").addEventListener("click", () => { logState.problems = !logState.problems; logState.tag = ""; drawLogLines(); });
  $("#checksLabel").innerHTML = `Hide the 20-second update checks <span class="muted">· ${r.hidden_checks.toLocaleString()} lines</span>`;
  $("#logList").innerHTML = r.lines.length ? r.lines.map((l) => `<div class="ln ${l.level !== "info" ? "warnln" : ""}"><span class="t">${l.at ? clock(l.at, true) : "—"}</span><span class="k">${esc(TAG_NAMES[l.tag] || l.tag)}</span><span class="m">${esc(l.text.replace(/^\[[a-z0-9-]+\]\s*/u, ""))}</span></div>`).join("")
    : `<div class="empty">Nothing matches.</div>`;
}
async function drawUploads() {
  const r = await api("device-logs");
  $("#uploads").innerHTML = r.uploads.length ? r.uploads.map((u, i) => `<details ${i ? "" : "open"}><summary class="small"><b>${clock(u.at)}</b> · ${u.events.length} line${u.events.length === 1 ? "" : "s"}</summary>
    <pre>${esc(u.events.map((e) => typeof e === "string" ? e : e.message || JSON.stringify(e)).join("\n"))}</pre></details>`).join("")
    : `<div class="empty">None yet. On the Cardputer, type /sendlogs in the chat to send them.</div>`;
}

// ---------------------------------------------------------------------------------------------
// Firmware
async function drawFirmware() {
  const f = await api("firmware");
  const installed = f.installed?.version, offered = f.offered?.version;
  const size = f.offered?.size || f.kept[0]?.size || 0;
  const pct = size / f.slot_bytes;
  const sizes = f.sizes.filter((x) => x.size);
  let chart = "";
  if (sizes.length >= 2) {
    const lo = Math.min(...sizes.map((x) => x.size), f.slot_bytes * 0.8), hi = f.slot_bytes;
    const x = (i) => 34 + (i / (sizes.length - 1)) * 278, y = (v) => 78 - ((v - lo) / (hi - lo)) * 64;
    chart = `<svg class="size-chart" viewBox="0 0 320 96" role="img" aria-label="Firmware size, last ${sizes.length} builds">
      <line x1="34" y1="14" x2="312" y2="14" stroke="#d03b3b" stroke-dasharray="4 4" opacity=".8"/><text x="312" y="10" fill="#8f8d87" font-size="9" text-anchor="end">limit ${mb(f.slot_bytes)}</text>
      <line x1="34" y1="78" x2="312" y2="78" stroke="#383835"/>
      <polyline fill="none" stroke="#c6f432" stroke-width="2" stroke-linejoin="round" points="${sizes.map((s, i) => `${x(i).toFixed(1)},${y(s.size).toFixed(1)}`).join(" ")}"/>
      ${sizes.map((s, i) => `<circle cx="${x(i).toFixed(1)}" cy="${y(s.size).toFixed(1)}" r="7" fill="transparent"><title>${esc(s.version)}: ${mb(s.size)}</title></circle>`).join("")}
      <circle cx="${x(sizes.length - 1).toFixed(1)}" cy="${y(sizes.at(-1).size).toFixed(1)}" r="4" fill="#c6f432" stroke="#11162e" stroke-width="2"/>
      <text x="34" y="90" fill="#8f8d87" font-size="9">${esc(sizes[0].version)}</text><text x="312" y="90" fill="#8f8d87" font-size="9" text-anchor="end">${esc(sizes.at(-1).version)}</text>
      <text x="30" y="${y(lo) + 3}" fill="#8f8d87" font-size="9" text-anchor="end">${(lo / 1048576).toFixed(1)}</text></svg>`;
  }
  $("#firmware").innerHTML = `
  <div class="grid2">
    <div class="card"><span class="label">On the Cardputer</span><div class="mid mono">${esc(installed || "—")}</div><span class="tiny muted mono">${esc(f.installed?.git || "not reported since the bridge started")}</span></div>
    <div class="card"><span class="label">Offered by the bridge</span><div class="mid mono">${esc(offered || "—")}</div><span class="tiny muted">${f.offered?.built_at ? clock(f.offered.built_at) : ""}</span></div>
  </div>
  <div class="card between">${!installed ? status("off", "Waiting for the Cardputer") : offered && offered !== installed ? status("wait", "Update offered") : status("good", "Up to date", ICON.check)}
    <button class="btn" id="fwCheck">Ask it to check now</button></div>
  <div class="h2"><h2>Room on the Cardputer</h2></div>
  <div class="card">
    <div class="between"><span class="mid">${mb(size)} <span class="small muted">of ${mb(f.slot_bytes)} per app slot</span></span>
      ${pct >= 0.95 ? status("crit", `${Math.round(pct * 100)}% full`, ICON.warn) : pct >= 0.85 ? status("warn", `${Math.round(pct * 100)}% full`, ICON.warn) : status("good", `${Math.round(pct * 100)}% full`)}</div>
    <div class="meter"><i style="width:${Math.min(100, pct * 100).toFixed(1)}%"></i></div>
    <div class="tiny muted" style="margin-top:6px">About ${Math.max(0, Math.round((f.slot_bytes - size) / 1024))} KB left before a build no longer fits.${sizes.length >= 2 ? " Sizes of the last builds:" : ""}</div>
    ${chart}
  </div>
  <div class="h2"><h2>Builds kept</h2><span class="tiny muted">newest ${f.keep}</span></div>
  <div class="card list">${f.kept.length ? f.kept.map((k) => `<div class="item"><div class="grow"><b class="mono">${esc(k.version)}</b><div class="tiny muted">${clock(k.at)} · ${mb(k.size)}</div></div>
    ${k.version === installed ? status("good", "Installed") : k.version === offered ? status("wait", "Offered") : ""}</div>`).join("") : `<div class="empty">No builds published yet.</div>`}</div>
  ${f.archive.count ? `<div class="card between"><div class="col"><b>${f.archive.count} older builds</b><span class="tiny muted">${mb(f.archive.bytes)}, in the archive folder, outside the storage budget</span></div></div>` : ""}
  <div class="tiny muted" style="margin:6px 4px">Installs need ${f.min_battery}% battery${f.confirm_required ? ", and the Cardputer asks before installing" : ""}.</div>`;
  $("#fwCheck").addEventListener("click", () => sendAction({ type: "ota.check" }, "Checking for an update."));
}

// ---------------------------------------------------------------------------------------------
// Settings
async function drawSettings() {
  const s = await api("settings");
  const opt = (list, value, name = (x) => x) => list.map((x) => `<option value="${esc(x)}" ${x === value ? "selected" : ""}>${esc(name(x))}</option>`).join("");
  const voiceName = (v) => `${v.slice(3).replace(/^./, (c) => c.toUpperCase())} (${v[0] === "a" ? "US" : "UK"}, ${v[1] === "f" ? "female" : "male"})`;
  $("#settings").innerHTML = `
  <div class="h2"><h2>Claude</h2></div>
  <div class="card">
    <div class="formrow"><span class="grow">Model</span><select id="sModel">${opt(s.claude.models.includes(s.claude.model) ? s.claude.models : [s.claude.model, ...s.claude.models], s.claude.model, niceModel)}</select></div>
    <div class="formrow"><span class="grow">Effort</span><select id="sEffort">${opt(s.claude.efforts, s.claude.effort, (e) => ({ xhigh: "Extra high", max: "Max" }[e] || e[0].toUpperCase() + e.slice(1)))}</select></div>
    <div class="formrow"><span class="grow">Can search the web</span><span class="toggle ${s.claude.web ? "on" : ""}" id="sWeb" role="switch"></span></div>
    <div class="formrow"><span class="grow">Can change and build the firmware</span><span class="toggle ${s.claude.firmware ? "on" : ""}" id="sFw" role="switch"></span></div>
  </div>
  <div class="h2"><h2>Spoken replies</h2></div>
  <div class="card">${s.voice.on ? `
    <div class="formrow"><span class="grow">Voice</span><select id="sVoice">${opt(s.voice.voices.includes(s.voice.voice) ? s.voice.voices : [s.voice.voice, ...s.voice.voices], s.voice.voice, voiceName)}</select></div>
    <div class="formrow"><span class="grow">Speed</span><input id="sSpeed" type="number" min="0.5" max="2" step="0.05" value="${s.voice.speed}" style="width:90px"></div>
    <div class="tiny muted">A new voice or speed is used after the restart. Use Listen on a reply in the chat to hear it.</div>`
    : `<div class="empty">Kokoro isn't set up on this Mac.</div>`}</div>
  <div class="h2"><h2>Storage</h2></div>
  <div class="card">
    <div class="formrow"><span class="grow">Space the bridge may use</span><select id="sStorage">${opt(s.storage.choices.includes(s.storage.max_bytes) ? s.storage.choices : [s.storage.max_bytes, ...s.storage.choices], s.storage.max_bytes, (b) => mb(b))}</select></div>
    <div class="formrow"><span class="grow">Firmware builds to keep</span><input id="sKeep" type="number" min="1" max="10" value="${s.firmware.keep_images}" style="width:70px"></div>
  </div>
  <div class="h2"><h2>Cameras and the home door</h2></div>
  <div class="card">
    <div class="formrow"><span class="grow">Cameras account<div class="tiny muted">${esc(s.cameras.missing || "A one-time link for signing in from a phone")}</div></span>${s.cameras.missing ? "" : `<button class="btn small" id="camLink">Sign-in link</button>`}</div>
    <div class="formrow"><span class="grow">Home Wi-Fi door<div class="tiny muted">Podcasts, cameras, planes and the Lab use it at home</div></span>${status(s.home_door.port ? "good" : "off", s.home_door.port ? `Port ${s.home_door.port}` : "Closed")}</div>
    <div id="camOut" class="tiny" style="word-break:break-all"></div>
  </div>
  <button class="btn primary block" id="sSave" ${s.can_save ? "" : "disabled"} style="margin-top:6px">Save${s.can_restart ? " and restart the bridge" : ""}</button>
  <div class="tiny muted" style="margin:8px 4px">${s.can_restart ? "Saving restarts the bridge once Claude has finished any reply." : "Saved changes are used the next time the bridge starts."}</div>`;
  const toggle = (id) => $(id).addEventListener("click", (e) => e.target.classList.toggle("on"));
  toggle("#sWeb"); toggle("#sFw");
  $("#camLink")?.addEventListener("click", async () => {
    const r = await api("cameras/link");
    $("#camOut").innerHTML = r.link ? `Open this on your phone within ${Math.round(r.expires_in_s / 60)} minutes: <a class="link" href="${esc(r.link)}" target="_blank" rel="noopener">${esc(r.link)}</a>` : esc(r.reason || "No link right now.");
  });
  $("#sSave").addEventListener("click", async () => {
    const body = {
      claude: { model: $("#sModel").value, effort: $("#sEffort").value, web: $("#sWeb").classList.contains("on"), firmware: $("#sFw").classList.contains("on") },
      storage: { max_bytes: Number($("#sStorage").value) },
      firmware: { keep_images: Number($("#sKeep").value) }
    };
    if ($("#sVoice")) body.voice = { voice: $("#sVoice").value, speed: Number($("#sSpeed").value) };
    const before = lastStatus?.bridge?.started_at || (await api("status")).bridge.started_at;
    try {
      const r = await api("settings", body);
      if (!r.changed.length) { toast("Nothing changed."); return; }
      if (r.restarting) waitForRestart(before);
      else toast(r.reason === "claude_busy" ? "Saved. Claude is working, so restart the bridge once it's done." : "Saved. It's used the next time the bridge starts.", 5000);
    } catch (e) { toast(`Not saved: ${e.message}`); }
  });
}

// ---------------------------------------------------------------------------------------------
// Radio: which air-band frequency the planes around the Cardputer are probably on (radio.mjs).
// Pick the frequency your radio is on, then the plane you hear. The grid keeps its order: my
// guesses, then second choices, then the rest, each by airline and number; a new plane joins the end
// of its group, outlined for 30 s, and one that leaves the range fades in place.
const radio = { view: "freq", freq: "", range: 50, mine: true, filt: "", sel: "", snap: null, data: null, last: new Map(), saved: new Set(), editSpot: false, wired: false };
try {
  radio.freq = localStorage.getItem("radio.freq") || "";
  radio.range = Number(localStorage.getItem("radio.range")) || 50;
  radio.view = localStorage.getItem("radio.view") || "freq";
} catch {}
const RANGES = [5, 10, 25, 50];
const SIDE = { likely: "good", maybe: "warn", guess: "serious" };
function radioKeep() { try { localStorage.setItem("radio.freq", radio.freq); localStorage.setItem("radio.range", radio.range); localStorage.setItem("radio.view", radio.view); } catch {} }
const radioIcao = (p) => (/^[A-Z]{3}\d/.test(p?.cs || "") ? p.cs.slice(0, 3) : "");
const radioLogo = (p, big) => (radioIcao(p) ? `<img class="${big ? "rlogo big" : "rlogo"}" src="api/radio/logo/${radioIcao(p)}" alt="">` : "");
function radioParts(p) {
  const s = p.say || p.cs || p.hex;
  const i = s.lastIndexOf(" ");
  if (i > 0) return [s.slice(0, i), s.slice(i + 1)];
  return /^[A-Z]{3}\d/.test(s) ? [s.slice(0, 3), s.slice(3)] : ["", s];
}
function radioCmp(x, y) {
  const [a1, b1] = radioParts(x), [a2, b2] = radioParts(y);
  const n1 = parseInt(b1, 10), n2 = parseInt(b2, 10);
  return a1.toLowerCase().localeCompare(a2.toLowerCase()) || (isNaN(n1) ? 1e9 : n1) - (isNaN(n2) ? 1e9 : n2) || b1.localeCompare(b2);
}
const radioClimb = (p) => (p.vr > 300 ? "climbing" : p.vr < -300 ? "descending" : "level");
function radioLabel(f) {
  for (const [, rows] of radio.data?.catalog || []) for (const [x, l] of rows) if (x === f) return l;
  return "";
}

async function drawRadio(first) {
  const el = $("#radio");
  if (!radio.wired) { radioWire(el); radio.wired = true; }
  const d = await api("radio");
  radio.data = d;
  for (const p of d.planes || []) radio.last.set(p.hex, p);
  if (first || !el.querySelector("#rBody")) {
    el.innerHTML = `<div class="seg"><a data-rview="freq">By frequency</a><a data-rview="near">Near me</a></div><div id="rWhere"></div><div id="rBody"></div><div id="rPanel"></div>`;
  }
  el.querySelectorAll("[data-rview]").forEach((a) => a.classList.toggle("on", a.dataset.rview === radio.view));
  radioWhere(d);
  if (d.off) { $("#rBody").innerHTML = `<div class="empty">Radio needs Skywatch's planes, which this bridge doesn't have.</div>`; return; }
  if (!d.spot) { $("#rBody").innerHTML = ""; $("#rPanel").innerHTML = ""; return; }
  if (radio.view === "near") radioNear();
  else if (!radio.freq) radioFreqs();
  else radioGrid();
}

// Where the planes are counted from: the Cardputer's last Skywatch position, or a typed town.
function radioWhere(d) {
  const box = $("#rWhere");
  if (document.activeElement?.id === "rTown") return;
  if (!d.spot || radio.editSpot) {
    box.innerHTML = `<div class="card"><div class="label">${d.spot ? "Use a town instead" : "Where are you?"}</div>
      <div class="tiny muted" style="margin:4px 0 8px">${d.spot ? "" : "Open Skywatch on the Cardputer and its position is used, or "}type a town and state, like Salem, MA. It's kept only in the bridge's memory, never saved.</div>
      <div class="row" style="display:flex;gap:8px"><input id="rTown" style="flex:1" autocomplete="off"><button class="btn small" data-rspot="set">Use</button>${d.spot ? `<button class="btn small" data-rspot="cancel">Cancel</button>` : ""}</div>
      <div class="tiny" id="rTownErr" style="color:var(--serious);margin-top:6px"></div></div>`;
    return;
  }
  const notes = [];
  if (!d.faa) notes.push("Getting the FAA's list of frequencies…");
  if (!d.area) notes.push("No approach frequencies set for this area, so guesses use towers and Center only.");
  box.innerHTML = `<div class="tiny muted rwhere">Around ${esc(d.label)} · updated ${d.age} s ago${d.saved ? ` · ${d.saved} saved` : ""}${d.error ? ` · <span style="color:var(--warn)">${esc(d.error)}</span>` : ""}
    · <a class="link" data-rspot="${d.from === "typed" ? "clear" : "edit"}">${d.from === "typed" ? "Use the Cardputer's" : "Use a town"}</a></div>
    ${notes.map((n) => `<div class="tiny muted rwhere">${esc(n)}</div>`).join("")}`;
}

function radioRanges() {
  return `<div class="seg rrange">${RANGES.map((r) => `<a data-rrange="${r}" class="${radio.range === r ? "on" : ""}">${r} mi</a>`).join("")}</div>`;
}

function radioFreqs() {
  const n1 = {}, n2 = {};
  for (const p of radio.data.planes) {
    if (p.mi > radio.range) continue;
    if (p.freq) n1[p.freq] = (n1[p.freq] || 0) + 1;
    if (p.alt2) n2[p.alt2] = (n2[p.alt2] || 0) + 1;
  }
  $("#rPanel").innerHTML = "";
  $("#radio").classList.remove("haspanel");
  $("#rBody").innerHTML = `<div class="tiny muted" style="margin:0 4px 8px">Pick the frequency your radio is on. The numbers are how many planes within your range I think are on it now.</div>
    ${radioRanges()}
    ${radio.data.catalog.map(([name, rows]) => `<div class="h2"><h2>${esc(name)}</h2></div><div class="card rfreqs">${rows.map(([f, l]) => `
      <a class="rfq" data-rfreq="${esc(f)}"><span class="n mono">${esc(f)}</span><span class="l">${esc(l)}</span>
      <span class="c">${n1[f] ? `<b class="good">${n1[f]}</b> likely` : ""}${n2[f] ? `<br>${n2[f]} maybe` : ""}</span></a>`).join("")}</div>`).join("")}
    <div class="h2"><h2>Something else</h2></div>
    <div class="card" style="display:flex;gap:8px"><input id="rOther" style="flex:1" placeholder="A frequency, like 121.5" inputmode="decimal"><button class="btn small" data-rother="1">Listen</button></div>`;
}

function radioGrid() {
  const F = radio.freq;
  const all = radio.data.planes;
  const cls = (p) => (p.freq === F ? 0 : p.alt2 === F ? 1 : 2);
  const cand = all.filter((p) => p.mi <= radio.range && (!radio.mine || p.freq === F || p.alt2 === F));
  const key = `${F}|${radio.mine}|${radio.range}`;
  if (!radio.snap || radio.snap.key !== key) {
    const order = cand.slice().sort((x, y) => cls(x) - cls(y) || radioCmp(x, y));
    radio.snap = { key, order: order.map((p) => p.hex), cls: Object.fromEntries(order.map((p) => [p.hex, cls(p)])), added: {} };
  }
  const snap = radio.snap;
  // New matches join the end of their group: nothing already shown moves within its group.
  for (const p of cand.filter((x) => !snap.order.includes(x.hex)).sort((x, y) => cls(x) - cls(y) || radioCmp(x, y))) {
    const c = cls(p);
    let at = 0;
    for (let i = snap.order.length - 1; i >= 0; i--) if ((snap.cls[snap.order[i]] ?? 2) <= c) { at = i + 1; break; }
    snap.order.splice(at, 0, p.hex);
    snap.cls[p.hex] = c;
    snap.added[p.hex] = Date.now();
  }
  const live = new Map(all.map((p) => [p.hex, p]));
  const rows = snap.order.map((h) => (live.has(h) ? { ...live.get(h), gone: live.get(h).mi > radio.range } : { ...radio.last.get(h), gone: true }))
    .filter((p) => p.hex && (!radio.filt || `${p.say} ${p.cs} ${p.type}`.toLowerCase().includes(radio.filt)));
  if (!$("#rHead") || $("#rHead").dataset.key !== key) {
    $("#rBody").innerHTML = `<div class="card" id="rHead" data-key="${esc(key)}">
      <div style="display:flex;align-items:center;gap:10px"><div class="rbig mono">${esc(F)}</div><div style="flex:1;font-size:14px;color:var(--ink2)">${esc(radioLabel(F))}</div>
      <button class="btn small" data-rchange="1">Change</button></div>
      <input id="rFilt" style="width:100%;margin:10px 0" placeholder="Filter, like 371 or Southwest" value="${esc(radio.filt)}" autocomplete="off">
      <div class="seg"><a data-rmine="1" class="${radio.mine ? "on" : ""}">My guesses</a><a data-rmine="0" class="${radio.mine ? "" : "on"}">Every plane</a></div>
      ${radioRanges()}
      <div class="tiny muted"><b class="good">Green ★</b> my guess for ${esc(F)} · <b class="warnc">amber</b> my second choice · faded: left your range or out of reach. New planes join the end of their group, outlined for 30 s.</div></div>
      <div id="rGrid"></div>
      <div class="h2"><h2>Heard one that isn't here?</h2></div>
      <div class="card" style="display:flex;gap:8px"><input id="rTyped" style="flex:1" placeholder="What you heard, like Southwest 371" autocomplete="off"><button class="btn small" data-rtyped="1">Save</button></div>`;
  }
  const heads = ["My guesses ★", "Second choice", "Others"];
  let prev = -1;
  $("#rGrid").innerHTML = rows.length ? `<div class="rgrid">${rows.map((p) => {
    const g = snap.cls[p.hex] ?? 2;
    const head = g !== prev ? `<div class="rgh">${heads[g]}</div>` : "";
    prev = g;
    const [a, b] = radioParts(p);
    const mine = p.freq === F, second = p.alt2 === F, done = radio.saved.has(`${F}|${p.hex}`);
    const fresh = Date.now() - (snap.added[p.hex] || 0) < 30000;
    return `${head}<button class="rtile ${mine ? "mine" : second ? "second" : ""} ${p.gone ? "gone" : ""} ${radio.sel === p.hex ? "sel" : ""} ${fresh ? "fresh" : ""}" data-rhex="${esc(p.hex)}">
      <span class="a">${radioLogo(p)}<span>${esc(a)}</span></span><span class="b mono">${esc(b)}</span>
      <span class="m">${done ? `<span class="good">✓ saved</span>` : mine ? `<span class="good">★</span>` : second ? `<span class="warnc">maybe</span>` : ""}${fresh && !done ? ` <span class="cyanc">new</span>` : ""}</span></button>`;
  }).join("")}</div>` : `<div class="empty">${radio.mine ? "No plane guessed on this one right now. Try Every plane or a wider range." : "No planes in this range."}</div>`;
  radioPanel();
}

function radioPanel() {
  const box = $("#rPanel");
  const p = radio.sel ? (radio.data.planes.find((x) => x.hex === radio.sel) || radio.last.get(radio.sel)) : null;
  $("#radio").classList.toggle("haspanel", !!p);
  if (!p) { box.innerHTML = ""; return; }
  const F = radio.freq, here = radio.data.planes.some((x) => x.hex === p.hex);
  const mine = p.freq === F, second = p.alt2 === F, done = radio.saved.has(`${F}|${p.hex}`);
  box.innerHTML = `<div class="rpanel"><div class="rname">${radioLogo(p, true)}<span>${esc(p.say || p.cs)}</span>
    ${mine ? `<span class="good rbadge">★ my guess</span>` : second ? `<span class="warnc rbadge">maybe</span>` : ""}</div>
    <div class="tiny muted" style="margin:3px 0 4px">${esc(p.cs)} ${esc(p.type)} · ${Number(p.alt).toLocaleString()} ft ${radioClimb(p)} · ${p.mi} mi ${esc(p.dir)}${here ? (p.mi > radio.range ? " · outside your range now" : "") : " · out of reach now"}</div>
    <div style="font-size:14px">My guess for it: <b>${esc(p.freq || "none")}</b>${p.alt2 ? ` or ${esc(p.alt2)}` : ""} · ${esc(p.who)}</div>
    <div class="tiny muted">${esc(p.why)}</div>
    <div class="btns" style="margin-top:10px">${done ? `<button class="btn" style="flex:2" disabled>✓ Saved as heard on ${esc(F)}</button>` :
      `<button class="btn primary" style="flex:2" data-rheard="1">I heard it on ${esc(F)}</button>`}<button class="btn" data-rclose="1">Close</button></div></div>`;
}

function radioNear() {
  $("#rPanel").innerHTML = "";
  $("#radio").classList.remove("haspanel");
  const near = radio.data.planes.filter((p) => p.mi <= 9.2);
  $("#rBody").innerHTML = near.length ? near.map((p) => `<div class="card ${p.near ? "" : "rfar"}">
    <div class="rname">${radioLogo(p, true)}<span>${esc(p.say)}</span></div>
    <div class="tiny muted" style="margin:3px 0 8px">${esc(p.cs)} ${esc(p.type)} · ${Number(p.alt).toLocaleString()} ft ${radioClimb(p)} · ${p.mi} mi ${esc(p.dir)}</div>
    ${p.freq ? `<div class="rbig mono">${esc(p.freq)}</div>` : ""}${p.alt2 ? `<div class="tiny muted">or ${esc(p.alt2)}</div>` : ""}
    <div style="font-size:14px;margin-top:4px">${status(SIDE[p.sure] || "warn", p.sure)} ${esc(p.who)}</div><div class="tiny muted">${esc(p.why)}</div>
    ${p.freq ? `<button class="btn small" style="margin-top:8px" data-rlisten="${esc(p.freq)}" data-rlhex="${esc(p.hex)}">Listen on ${esc(p.freq)}</button>` : ""}</div>`).join("")
    : `<div class="empty">No planes within 8 miles right now.</div>`;
}

function radioWire(el) {
  // A logo that isn't there just isn't shown (no inline handlers on this page).
  el.addEventListener("error", (e) => { if (e.target.tagName === "IMG") e.target.remove(); }, true);
  el.addEventListener("input", (e) => {
    if (e.target.id === "rFilt") { radio.filt = e.target.value.toLowerCase(); radioGrid(); }
  });
  el.addEventListener("click", async (e) => {
    const t = e.target.closest("[data-rview],[data-rfreq],[data-rrange],[data-rmine],[data-rhex],[data-rheard],[data-rclose],[data-rchange],[data-rtyped],[data-rother],[data-rspot],[data-rlisten]");
    if (!t) return;
    const ds = t.dataset;
    if (ds.rview) { radio.view = ds.rview; radio.sel = ""; radioKeep(); return refresh(true); }
    if (ds.rfreq) { radio.freq = ds.rfreq; radio.sel = ""; radio.filt = ""; radio.mine = true; radio.snap = null; radioKeep(); window.scrollTo(0, 0); return refresh(true); }
    if (ds.rrange) { radio.range = Number(ds.rrange); radio.snap = null; radioKeep(); return refresh(true); }
    if (ds.rmine) { radio.mine = ds.rmine === "1"; radio.snap = null; return refresh(true); }
    if (ds.rhex) { radio.sel = radio.sel === ds.rhex ? "" : ds.rhex; radioGrid(); return; }
    if (ds.rclose) { radio.sel = ""; radioGrid(); return; }
    if (ds.rchange) { radio.freq = ""; radio.sel = ""; radio.snap = null; radioKeep(); window.scrollTo(0, 0); return refresh(true); }
    if (ds.rheard) {
      const r = await api("radio/heard", { listening: radio.freq, hex: radio.sel }).catch((err) => ({ error: err.message }));
      if (r.error) return toast(r.error === "not_found" ? "That plane has gone out of reach: type what you heard instead." : `Not saved: ${r.error}`);
      radio.saved.add(`${radio.freq}|${radio.sel}`);
      return radioGrid();
    }
    if (ds.rtyped) {
      const v = $("#rTyped").value.trim();
      if (!v) return;
      const r = await api("radio/heard", { listening: radio.freq, typed: v }).catch((err) => ({ error: err.message }));
      if (r.error) return toast(`Not saved: ${r.error}`);
      $("#rTyped").value = "";
      return toast(`Saved: ${v} on ${radio.freq}`);
    }
    if (ds.rother) {
      const v = $("#rOther").value.trim();
      if (/^\d{3}\.\d{1,3}$/.test(v)) { radio.freq = v; radio.snap = null; radio.mine = false; radioKeep(); return refresh(true); }
      return toast("A frequency looks like 121.5");
    }
    if (ds.rlisten) { radio.view = "freq"; radio.freq = ds.rlisten; radio.sel = ds.rlhex; radio.snap = null; radio.mine = true; radioKeep(); window.scrollTo(0, 0); return refresh(true); }
    if (ds.rspot === "edit") { radio.editSpot = true; radioWhere(radio.data); $("#rTown")?.focus(); return; }
    if (ds.rspot === "cancel") { radio.editSpot = false; return refresh(true); }
    if (ds.rspot === "clear") { await api("radio/spot", { place: "" }); radio.snap = null; return refresh(true); }
    if (ds.rspot === "set") {
      const r = await api("radio/spot", { place: $("#rTown").value }).catch((err) => ({ error: err.message }));
      if (r.error) { $("#rTownErr").textContent = r.error; return; }
      radio.editSpot = false; radio.snap = null;
      document.activeElement?.blur();
      return refresh(true);
    }
  });
}

// ---------------------------------------------------------------------------------------------
show();
pollTop();
setInterval(() => { if (current !== "status") pollTop(); }, 15000);
