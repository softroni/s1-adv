// The Cameras app's sign-in page, for a phone. The Cardputer shows a QR code of a one-time link,
// https://<the bridge's public address>/eufy/<link>, good for 15 minutes. The page saves the eufy
// account in the bridge's eufy.env, has the camera helper sign in at once, and takes eufy's code or
// captcha if it asks, so nothing has to be typed on the Mac or the Cardputer.
//
// It's on the bridge's public HTTPS address, because a phone can open that from anywhere and its
// password then never crosses the network in the clear. The link is the only key: 96 random bits,
// one at a time, gone after 15 minutes, and wrong ones count against the address that tried them as a
// wrong device token does. Neither the link nor anything typed into the page is logged.
import { randomBytes, randomUUID, timingSafeEqual } from "node:crypto";
import { readFile, rename, writeFile } from "node:fs/promises";
import path from "node:path";

export const SETUP_MS = 15 * 60 * 1000;
const RENEW_MS = 5 * 60 * 1000;      // A link with less than this left is replaced when asked for.
const MAX_POSTS = 20;                 // Sign-ins, codes and captchas through one link.
const MAX_BODY = 2048;
export const SETUP_PATH = /^\/eufy\/([A-Za-z0-9_-]{16})(?:\/(status|account|code|captcha\.png))?$/u;

export class SetupLinks {
  constructor(now = Date.now) { this.now = now; this.link = null; }
  // The link to show, reused while it has a while left, so a page already open keeps working.
  current() {
    const now = this.now();
    if (!this.link || this.link.expires - now < RENEW_MS) this.link = { token: randomBytes(12).toString("base64url"), expires: now + SETUP_MS, posts: 0 };
    return this.link;
  }
  check(token) {
    const link = this.link;
    if (!link || link.expires <= this.now() || typeof token !== "string" || token.length !== link.token.length) return null;
    return timingSafeEqual(Buffer.from(token), Buffer.from(link.token)) ? link : null;
  }
  // Once eufy has said yes, the link has done its job.
  finish() { this.link = null; }
}

// The account, as eufy.env lines. Values are checked, not escaped: no line breaks, and the password
// is quoted so its spaces and # survive (cameras-helper.mjs parseEnv takes the quotes off).
export function accountFile(account, when = new Date()) {
  const email = String(account?.email || "").trim();
  const password = String(account?.password || "");
  const country = String(account?.country || "US").trim().toUpperCase();
  if (!/^[^\s@]{1,64}@[^\s@]{1,190}\.[^\s@]{2,63}$/u.test(email)) throw Object.assign(new Error("bad_email"), { status: 400 });
  if (!password || password.length > 128 || /[\r\n\0]/u.test(password)) throw Object.assign(new Error("bad_password"), { status: 400 });
  if (!/^[A-Z]{2}$/u.test(country)) throw Object.assign(new Error("bad_country"), { status: 400 });
  return [`# eufy account for the Cardputer's Cameras app, saved from the phone page on ${when.toISOString().slice(0, 10)}.`,
    "# Only the bridge's camera helper reads it. Edit it here, or scan the Cardputer's code again.",
    `EUFY_EMAIL=${email}`, `EUFY_PASSWORD="${password}"`, `EUFY_COUNTRY=${country}`, ""].join("\n");
}
export function emailIn(text) {
  const line = String(text || "").split(/\r?\n/u).find(l => l.trim().startsWith("EUFY_EMAIL="));
  return line ? line.slice(line.indexOf("=") + 1).trim().replace(/^"(.*)"$/u, "$1").toLowerCase() : "";
}

async function readBody(req) {
  const parts = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > MAX_BODY) throw Object.assign(new Error("too_big"), { status: 413 });
    parts.push(chunk);
  }
  try { return JSON.parse(Buffer.concat(parts).toString("utf8") || "{}"); } catch { throw Object.assign(new Error("bad_json"), { status: 400 }); }
}

function headers(type, extra = {}) {
  return { "content-type": type, "cache-control": "no-store", "referrer-policy": "no-referrer", "x-content-type-options": "nosniff",
    "x-frame-options": "DENY", ...extra };
}
function sendJson(res, status, body) {
  const data = Buffer.from(JSON.stringify(body));
  res.writeHead(status, headers("application/json; charset=utf-8", { "content-length": String(data.length) }));
  res.end(data);
}
function sendPage(res, status, html, nonce) {
  const data = Buffer.from(html);
  res.writeHead(status, headers("text/html; charset=utf-8", { "content-length": String(data.length),
    "content-security-policy": `default-src 'none'; style-src 'nonce-${nonce}'; script-src 'nonce-${nonce}'; img-src 'self'; connect-src 'self'; form-action 'none'; base-uri 'none'; frame-ancestors 'none'` }));
  res.end(data);
}

// The phone's view of where the sign-in is.
async function statusFor(service) {
  const missing = service.missing();
  if (missing) return { state: "off", detail: missing };
  if (!service.port) return { state: "starting", detail: "Cameras are starting on the Mac" };
  const got = await service.call("GET", "/status");
  const status = got.status === 200 ? JSON.parse(got.body.toString("utf8")) : { state: "error", detail: "The Mac didn't answer" };
  if (status.state === "ready") {
    const list = await service.call("GET", "/list");
    if (list.status === 200) status.cameras = (JSON.parse(list.body.toString("utf8")).cameras || []).length;
    service.links.finish();
  }
  return { state: String(status.state || ""), detail: String(status.detail || ""), cameras: status.cameras };
}

// True when the path is the page's, whatever the answer. `guard` is the bridge's own
// limiter for wrong tokens: { limited(), failed() }.
export async function handleSetupPage(service, req, res, url, guard = {}) {
  const match = SETUP_PATH.exec(url.pathname);
  if (!url.pathname.startsWith("/eufy/")) return false;
  if (guard.limited?.()) { sendJson(res, 429, { error: "too_many_tries" }); return true; }
  const link = match ? service.links.check(match[1]) : null;
  const what = match?.[2] || "";
  if (!link) {
    guard.failed?.();
    if (req.method === "GET" && !what) { const nonce = randomUUID(); sendPage(res, 404, expiredPage(nonce), nonce); }
    else sendJson(res, 404, { error: "expired" });
    return true;
  }
  try {
    if (req.method === "GET" && !what) {
      const nonce = randomUUID();
      console.log("[cameras] sign-in page opened");
      sendPage(res, 200, signInPage(nonce, Math.round((link.expires - service.links.now()) / 1000)), nonce);
    } else if (req.method === "GET" && what === "status") {
      sendJson(res, 200, await statusFor(service));
    } else if (req.method === "GET" && what === "captcha.png") {
      let png;
      try { png = await readFile(path.join(service.dir, "captcha.png")); } catch { sendJson(res, 404, { error: "no_captcha" }); return true; }
      res.writeHead(200, headers("image/png", { "content-length": String(png.length) }));
      res.end(png);
    } else if (req.method === "POST" && (what === "account" || what === "code")) {
      if (++link.posts > MAX_POSTS) { sendJson(res, 429, { error: "too_many_tries" }); return true; }
      const body = await readBody(req);
      if (what === "account") await service.saveAccount(body);
      else {
        const got = body.captcha !== undefined
          ? await service.call("POST", "/captcha", { value: String(body.captcha).replace(/[^A-Za-z0-9]+/gu, "").slice(0, 8) }, 30000)
          : await service.call("POST", "/code", { value: String(body.code || "").replace(/\D+/gu, "").slice(0, 8) }, 30000);
        if (got.status !== 200) { sendJson(res, 400, { error: "wrong_code" }); return true; }
      }
      sendJson(res, 200, await statusFor(service));
    } else {
      sendJson(res, 405, { error: "not_allowed" });
    }
  } catch (error) {
    if (res.headersSent) { res.destroy(); return true; }
    const status = Number(error?.status) || 503;
    if (status >= 500) console.warn(`[cameras] sign-in page: ${String(error?.code || error?.message || error).slice(0, 80)}`);
    sendJson(res, status, { error: String(error?.code || error?.message || "failed").replace(/[^a-z_]/gu, "").slice(0, 32) || "failed" });
  }
  return true;
}

// Saved as the bridge saves its own files: whole, readable only by its owner.
export async function writeAccount(file, text) {
  const temp = `${file}.${process.pid}.tmp`;
  await writeFile(temp, text, { mode: 0o600 });
  await rename(temp, file);
}

const STYLE = `
:root{--bg:#fafaf7;--card:#fff;--ink:#1d1f24;--muted:#5b6170;--line:#dcdfe6;--accent:#2f5fd0;--on-accent:#fff;--good:#1f8a4c;--bad:#b3261e;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--bg:#111318;--card:#1a1d24;--ink:#eceef3;--muted:#a3a9b7;--line:#2d313b;--accent:#7aa2ff;--on-accent:#0b1020;--good:#5fd18f;--bad:#ff8a80;color-scheme:dark}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
main{max-width:440px;margin:0 auto;padding:28px 16px 40px}h1{font-size:24px;line-height:1.2;margin:0 0 6px}
p{margin:0 0 14px;color:var(--muted)}.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:18px;margin:18px 0}
label{display:block;font-weight:600;margin:12px 0 6px}input,select{width:100%;font:inherit;color:inherit;background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:12px}
input:focus,select:focus{outline:2px solid var(--accent);outline-offset:1px}.row{display:flex;gap:8px;align-items:center}.row input{flex:1}
button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:13px 16px;background:var(--accent);color:var(--on-accent);width:100%;margin-top:16px;cursor:pointer}
button.quiet{background:transparent;color:var(--accent);width:auto;margin:0;padding:8px}button:disabled{opacity:.55}
.status{display:flex;gap:10px;align-items:flex-start}.dot{width:10px;height:10px;border-radius:50%;margin-top:7px;flex:none;background:var(--muted)}
.dot.work{background:var(--accent);animation:p 1s ease-in-out infinite alternate}.dot.good{background:var(--good)}.dot.bad{background:var(--bad)}
@keyframes p{from{opacity:.35}to{opacity:1}}.msg{font-weight:600}.small{font-size:14px}img{max-width:100%;border-radius:8px;background:#fff}
[hidden]{display:none!important}`;

function signInPage(nonce, seconds) {
  const countries = [["US", "United States"], ["CA", "Canada"], ["GB", "United Kingdom"], ["AU", "Australia"], ["NZ", "New Zealand"],
    ["IE", "Ireland"], ["DE", "Germany"], ["FR", "France"], ["NL", "Netherlands"], ["ES", "Spain"], ["IT", "Italy"], ["SE", "Sweden"],
    ["JP", "Japan"], ["IN", "India"]];
  const options = countries.map(([code, name]) => `<option value="${code}">${name}</option>`).join("");
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex"><title>Cameras sign-in</title><style nonce="${nonce}">${STYLE}</style></head><body><main>
<h1>Cameras on your Cardputer</h1>
<p>Sign in with your eufy account. The Mac that runs your bridge keeps it, and your cameras' pictures go only to your Cardputer.</p>
<section class="card" id="form-card"><form id="account" autocomplete="on" novalidate>
<label for="email">eufy email</label><input id="email" name="email" type="email" autocomplete="username" inputmode="email" required>
<label for="password">eufy password</label><div class="row"><input id="password" name="password" type="password" autocomplete="current-password" required>
<button type="button" class="quiet" id="show">Show</button></div>
<label for="country">Country or region of the account</label><select id="country" name="country">${options}</select>
<button type="submit" id="send">Sign in</button></form></section>
<section class="card" id="status-card" hidden><div class="status"><span class="dot" id="dot"></span><div><div class="msg" id="msg"></div><div class="small" id="detail"></div></div></div>
<form id="code-form" hidden novalidate><label for="code" id="code-label">Code from eufy</label><div id="captcha-box" hidden><img id="captcha" alt="Captcha from eufy"></div>
<input id="code" inputmode="numeric" autocomplete="one-time-code" maxlength="8"><button type="submit">Send</button></form>
<button type="button" class="quiet" id="again" hidden>Change the email or password</button></section>
<p class="small">Your password goes to your own Mac over HTTPS, and is kept there in the bridge's eufy.env. It's never shown on the Cardputer. This page works until <span id="until"></span>.</p>
</main><script nonce="${nonce}">
const base = location.pathname.replace(/\\/$/, "");
const until = new Date(Date.now() + ${Number(seconds) * 1000});
document.getElementById("until").textContent = until.toLocaleTimeString([], {hour: "numeric", minute: "2-digit"});
const $ = (id) => document.getElementById(id);
let polling = null, shown = "";
$("show").onclick = () => { const p = $("password"); p.type = p.type === "password" ? "text" : "password"; $("show").textContent = p.type === "password" ? "Show" : "Hide"; };
function say(kind, msg, detail) { $("status-card").hidden = false; $("dot").className = "dot " + kind; $("msg").textContent = msg; $("detail").textContent = detail || ""; }
function show(s) {
  const state = s.state;
  $("code-form").hidden = !(state === "code" || state === "captcha");
  $("again").hidden = !(state === "error" || state === "code" || state === "captcha");
  if (state === "ready") { say("good", "Signed in" + (s.cameras !== undefined ? ": " + s.cameras + (s.cameras === 1 ? " camera" : " cameras") : ""), "The Cardputer shows them now. You can close this page."); $("form-card").hidden = true; stop(); return; }
  if (state === "code") { say("work", s.detail || "eufy sent a code", "Type it below."); $("code-label").textContent = "Code from eufy"; $("captcha-box").hidden = true; $("code").inputMode = "numeric"; if (shown !== "code") { $("code").value = ""; $("code").focus(); } }
  else if (state === "captcha") { say("work", "eufy wants a captcha", "Type the letters in the picture."); $("code-label").textContent = "Captcha"; $("captcha-box").hidden = false; $("code").inputMode = "text"; if (shown !== "captcha") { $("captcha").src = base + "/captcha.png?" + Date.now(); $("code").value = ""; } }
  else if (state === "signing-in" || state === "starting") say("work", "Signing in to eufy...", "");
  else if (state === "error") { say("bad", s.detail || "eufy said no", "Check the email and password, then sign in again."); $("form-card").hidden = false; stop(); }
  else if (state === "off") { say("bad", s.detail || "Cameras aren't set up on the Mac", ""); stop(); }
  else if (state === "setup") say("work", "Waiting for the Mac...", "");
  shown = state;
}
async function call(what, body) {
  const r = await fetch(base + "/" + what, body ? {method: "POST", headers: {"content-type": "application/json"}, body: JSON.stringify(body)} : {});
  if (r.status === 404) { stop(); say("bad", "This link has expired", "Open Cameras on the Cardputer and scan its code again."); throw new Error("expired"); }
  const data = await r.json().catch(() => ({}));
  if (!r.ok) throw Object.assign(new Error(data.error || "failed"), {data});
  return data;
}
function stop() { if (polling) clearInterval(polling); polling = null; }
function poll() { stop(); polling = setInterval(() => call("status").then(show).catch(() => {}), 2000); }
$("account").onsubmit = async (e) => {
  e.preventDefault();
  const email = $("email").value.trim(), password = $("password").value;
  if (!email.includes("@") || !password) { say("bad", "Type the email and password of your eufy account", ""); return; }
  $("send").disabled = true;
  try { $("form-card").hidden = true; say("work", "Signing in to eufy...", ""); show(await call("account", {email, password, country: $("country").value})); poll(); }
  catch (err) { if (err.message !== "expired") { $("form-card").hidden = false; say("bad", err.message === "bad_email" ? "That email doesn't look right" : "The Mac couldn't save it", ""); } }
  finally { $("send").disabled = false; }
};
$("code-form").onsubmit = async (e) => {
  e.preventDefault();
  const value = $("code").value.trim();
  if (!value) return;
  try { show(await call("code", shown === "captcha" ? {captcha: value} : {code: value})); poll(); }
  catch (err) { if (err.message !== "expired") say("bad", shown === "captcha" ? "That captcha didn't work" : "That code didn't work", "Try again."); }
};
$("again").onclick = () => { stop(); $("form-card").hidden = false; $("status-card").hidden = true; $("password").value = ""; $("password").focus(); };
call("status").then((s) => { if (s.state !== "setup") { show(s); if (s.state !== "ready" && s.state !== "error" && s.state !== "off") poll(); } }).catch(() => {});
</script></body></html>`;
}

function expiredPage(nonce) {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex"><title>Cameras sign-in</title><style nonce="${nonce}">${STYLE}</style></head><body><main>
<h1>This link has expired</h1><p>Open Cameras on the Cardputer and scan its code again. Each code works for 15 minutes.</p></main></body></html>`;
}
