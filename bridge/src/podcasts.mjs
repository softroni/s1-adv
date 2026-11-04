// Podcasts, audiobooks and old-time radio for the Cardputer. The device trusts one root
// certificate and its player decodes MP3 only, so the bridge does the work it can't: it
// searches the free iTunes Search API for podcasts and the Internet Archive for LibriVox
// audiobooks and old-time radio, reads a show's feed, and passes an episode on through
// ffmpeg as one small MP3 format, starting at whatever second the device asks for. No
// account or key is needed anywhere.
import { spawn } from "node:child_process";
import { createHash, randomBytes } from "node:crypto";
import { lookup } from "node:dns/promises";
import { existsSync } from "node:fs";
import net from "node:net";
import os from "node:os";

const ITUNES_SEARCH_URL = "https://itunes.apple.com/search";
const ITUNES_LOOKUP_URL = "https://itunes.apple.com/lookup";
const ARCHIVE_SEARCH_URL = "https://archive.org/advancedsearch.php";
const ARCHIVE_METADATA_URL = "https://archive.org/metadata/";
const ARCHIVE_DOWNLOAD_URL = "https://archive.org/download/";
// What each kind of search looks in. Books are LibriVox's own uploads to the Archive, which
// can be searched by any word, unlike LibriVox's API, which only matches a title's start.
export const KINDS = Object.freeze({ podcast: "itunes", book: "librivoxaudio", radio: "oldtimeradio" });
// What the device plays: one channel at 32 kHz and 48 kbit/s. That's MPEG-1, so every frame
// is 1152 samples, and at a constant 6000 bytes a second the device can find a second in a
// saved file by arithmetic. Speech sounds clear at this rate on the Cardputer's speaker.
export const AUDIO = Object.freeze({ rate: 32000, kbps: 48, bytesPerSecond: 6000 });
export const PAGE = 20;
const MAX_SHOWS = 20;
const MAX_QUERY_CHARS = 64;
// Bytes, as the device stores them, so nothing is cut again there.
export const LIMITS = Object.freeze({ id: 100, title: 96, by: 48 });
const MAX_FEED_BYTES = 40 * 1024 * 1024;  // Feeds with a thousand episodes run to a few MB.
const FEED_FRESH_MS = 30 * 60 * 1000;
const MAX_CACHED_SHOWS = 24;
const REQUEST_TIMEOUT_MS = 15000;
const FIRST_AUDIO_TIMEOUT_MS = 30000;
const MAX_AUDIO_GLOBAL = 2;
const USER_AGENT = "S1-ADV-Podcasts/1 (+https://github.com/softroni/s1-adv)";
// Audio for a Cardputer on the same home network goes straight there over plain HTTP, so the
// device needs no TLS while it plays: TLS and the decoder together don't fit in its memory.
// The device asks for a ticket over HTTPS first, with its token; the ticket is the only key.
export const LAN_PORT = 31890;
const TICKET_MS = 12 * 60 * 60 * 1000;
const MAX_TICKETS = 64;
const ARCHIVE_ID = /^[A-Za-z0-9][A-Za-z0-9._-]{0,98}$/u;
const SHOW_ID = /^(p\d{1,12}|a[A-Za-z0-9][A-Za-z0-9._-]{0,98})$/u;
const EPISODE_ID = /^[0-9a-f]{12}$/u;

export class PodcastError extends Error {
  constructor(code, status = 502) {
    super(code);
    this.code = code;
    this.status = status;
  }
}

// --- Text ---------------------------------------------------------------------------------
const PUNCTUATION = new Map([["‘", "'"], ["’", "'"], ["‚", "'"], ["‛", "'"], ["′", "'"],
  ["“", "\""], ["”", "\""], ["„", "\""], ["″", "\""], ["–", "-"], ["—", "-"],
  ["―", "-"], ["−", "-"], ["…", "..."], [" ", " "], ["•", "-"], ["·", "-"]]);
const NAMED_ENTITIES = { amp: "&", lt: "<", gt: ">", quot: "\"", apos: "'", nbsp: " ", ndash: "-", mdash: "-",
  hellip: "...", rsquo: "'", lsquo: "'", rdquo: "\"", ldquo: "\"", copy: "(c)", reg: "(R)", trade: "(TM)" };

export function decodeEntities(text) {
  return String(text).replace(/&(#x[0-9a-f]{1,6}|#\d{1,7}|[a-z]{2,8});/giu, (whole, name) => {
    if (name[0] === "#") {
      const code = name[1] === "x" || name[1] === "X" ? parseInt(name.slice(2), 16) : parseInt(name.slice(1), 10);
      return code > 0 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff) ? String.fromCodePoint(code) : " ";
    }
    return NAMED_ENTITIES[name.toLowerCase()] ?? whole;
  });
}

// One line of screen text: typographic punctuation becomes ASCII so the device's bold font,
// which has ASCII only, can still show it; emoji and other pictures go, because no font on
// the device has them; tabs and line breaks, which separate fields here, become spaces. Cut
// to `maxBytes` of UTF-8 at a whole letter.
export function cleanText(value, maxBytes) {
  let text = String(value ?? "").normalize("NFC");
  text = [...text].map(ch => PUNCTUATION.get(ch) ?? ch).join("");
  text = text.replace(/[\p{Extended_Pictographic}​-‏⁠︀-️﻿\u{e0000}-\u{e007f}]/gu, "")
    .replace(/[\p{Cc}\p{Zl}\p{Zp}\p{Co}\p{Cs}]/gu, " ")
    .replace(/[\p{Cf}]/gu, "")
    .replace(/\s+/gu, " ").trim();
  if (Buffer.byteLength(text) <= maxBytes) return text;
  let out = "", bytes = 0;
  for (const ch of text) {
    const size = Buffer.byteLength(ch);
    if (bytes + size > maxBytes) break;
    out += ch;
    bytes += size;
  }
  return out.trim();
}

// --- Feeds --------------------------------------------------------------------------------
function cdataOrText(inner) {
  const cdata = inner.match(/^\s*<!\[CDATA\[([\s\S]*?)\]\]>\s*$/u);
  const raw = cdata ? cdata[1] : decodeEntities(inner.replace(/<!\[CDATA\[([\s\S]*?)\]\]>/gu, "$1"));
  // Titles sometimes carry markup of their own.
  return (cdata ? decodeEntities(raw) : raw).replace(/<[^>]{0,200}>/gu, " ");
}

function tagText(xml, name) {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/gu, "\\$&");
  const match = xml.match(new RegExp(`<${escaped}(?:\\s[^>]*)?>([\\s\\S]*?)</${escaped}\\s*>`, "u"));
  return match ? cdataOrText(match[1]) : "";
}

function attributes(tag) {
  const out = {};
  for (const match of tag.matchAll(/([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/gu))
    out[match[1].toLowerCase()] = decodeEntities(match[2] ?? match[3] ?? "");
  return out;
}

// Seconds from "5131", "85:31" or "1:25:31". Zero when it can't tell.
export function parseDuration(value) {
  const text = String(value ?? "").trim();
  if (/^\d+(\.\d+)?$/u.test(text)) return Math.round(Number(text));
  const parts = text.split(":");
  if (parts.length < 2 || parts.length > 3 || !parts.every(part => /^\d{1,3}(\.\d+)?$/u.test(part))) return 0;
  return Math.round(parts.reduce((total, part) => total * 60 + Number(part), 0));
}

function isoDate(value) {
  const time = Date.parse(String(value ?? "").trim());
  return Number.isFinite(time) ? new Date(time).toISOString().slice(0, 10) : "";
}

export function episodeId(url) {
  return createHash("sha1").update(String(url)).digest("hex").slice(0, 12);
}

function audioEnclosure(item) {
  for (const tag of item.match(/<(?:enclosure|media:content)\s[^>]*>/gu) || []) {
    const { url, type = "" } = attributes(tag);
    if (!url || !/^https?:\/\//iu.test(url)) continue;
    const audio = /^audio\//iu.test(type) || (!type && /\.(mp3|m4a|aac|ogg|oga|opus|wav|flac)(\?|$)/iu.test(url));
    if (audio) return url;
  }
  return "";
}

// An RSS feed's show and its episodes, newest first. Video-only episodes are left out.
export function parseFeed(xml) {
  const text = String(xml);
  const head = text.split(/<item[\s>]/u, 1)[0].replace(/<image[\s>][\s\S]*?<\/image>/gu, "");
  const show = {
    title: cleanText(tagText(head, "title"), LIMITS.title),
    by: cleanText(tagText(head, "itunes:author") || tagText(head, "author") || tagText(head, "managingEditor"), LIMITS.by),
    episodes: []
  };
  const seen = new Set();
  let order = 0;
  for (const match of text.matchAll(/<item[\s>][\s\S]*?<\/item>/gu)) {
    const item = match[0];
    const url = audioEnclosure(item);
    if (!url) continue;
    const id = episodeId(url);
    if (seen.has(id)) continue;
    seen.add(id);
    const date = isoDate(tagText(item, "pubDate"));
    show.episodes.push({ id, url, date, order: order++,
      title: cleanText(tagText(item, "itunes:title") || tagText(item, "title"), LIMITS.title) || `Episode ${order}`,
      seconds: parseDuration(tagText(item, "itunes:duration")) });
  }
  // Feeds are usually newest first already; a few list the first episode first.
  show.episodes.sort((a, b) => (b.date > a.date ? 1 : b.date < a.date ? -1 : a.order - b.order));
  return show;
}

// An Archive item's audio, as episodes in the item's own order. Each recording is kept once,
// as the smallest MP3 the Archive made of it, since the bridge converts it anyway.
export function parseArchiveItem(identifier, body) {
  const metadata = body?.metadata || {};
  const first = value => Array.isArray(value) ? value[0] : value;
  const show = { title: cleanText(first(metadata.title) || identifier, LIMITS.title),
    by: cleanText(first(metadata.creator) || "", LIMITS.by), episodes: [] };
  const rank = format => /^64kbps mp3$/iu.test(format) ? 0 : /^vbr mp3$/iu.test(format) ? 1 :
    /^128kbps mp3$/iu.test(format) ? 2 : 3;
  const groups = new Map();
  for (const file of Array.isArray(body?.files) ? body.files : []) {
    const name = String(file?.name || "");
    if (!/mp3/iu.test(String(file?.format || "")) || !/\.mp3$/iu.test(name)) continue;
    const key = file.source === "derivative" && file.original ? String(file.original) : name;
    const known = groups.get(key);
    const pick = !known || rank(file.format) < rank(known.format) ? { ...file, key } : known;
    const other = pick === known ? file : known;
    // The original carries the track number and the derivative may not, so each fills the other.
    for (const field of ["title", "track", "length"]) if (other && !pick[field] && other[field]) pick[field] = other[field];
    groups.set(key, pick);
  }
  const trackOf = file => { const n = parseInt(String(file.track || ""), 10); return Number.isFinite(n) ? n : Infinity; };
  const files = [...groups.values()].sort((a, b) => trackOf(a) - trackOf(b) ||
    a.key.localeCompare(b.key, "en", { numeric: true, sensitivity: "base" }));
  const base = ARCHIVE_DOWNLOAD_URL + encodeURIComponent(identifier) + "/";
  for (const file of files) {
    const url = base + String(file.name).split("/").map(encodeURIComponent).join("/");
    const plain = String(file.key).replace(/^.*\//u, "").replace(/\.[a-z0-9]+$/iu, "").replace(/_/gu, " ");
    show.episodes.push({ id: episodeId(url), url, date: "", title: cleanText(file.title || plain, LIMITS.title) || plain,
      seconds: parseDuration(file.length) });
  }
  return show;
}

// --- Searching ----------------------------------------------------------------------------
export function validQuery(text) {
  return typeof text === "string" && text.trim().length > 0 && text.length <= MAX_QUERY_CHARS && !/[\p{Cc}]/u.test(text);
}

// Words for the Archive's search, with everything its query language treats as syntax gone.
function archiveWords(text) {
  return String(text).replace(/[+\-&|!(){}[\]^"~*?:\\/]/gu, " ").split(/\s+/u).filter(Boolean).slice(0, 8)
    .filter(word => !/^(and|or|not|to)$/iu.test(word));
}

export function archiveSearchUrl(collection, text) {
  const words = archiveWords(text).join(" AND ");
  const query = `collection:(${collection}) AND mediatype:(audio) AND (title:(${words}) OR creator:(${words}))`;
  const params = new URLSearchParams({ q: query, rows: String(MAX_SHOWS), output: "json" });
  for (const field of ["identifier", "title", "creator"]) params.append("fl[]", field);
  params.append("sort[]", "downloads desc");
  return `${ARCHIVE_SEARCH_URL}?${params}`;
}

// --- Lines for the device -----------------------------------------------------------------
// The device reads one record a line, its fields split by tabs, text fields last. Nothing in
// a field can hold a tab or a line break: cleanText turned them into spaces.
export function showLines(shows) {
  return shows.map(show => ["show", show.id, show.count || 0, show.title, show.by].join("\t")).join("\n") + "\n";
}

export function episodeLines(show, from) {
  const page = show.episodes.slice(from, from + PAGE);
  const lines = [["about", show.episodes.length, from, show.title, show.by].join("\t")];
  for (const episode of page) lines.push(["ep", episode.id, episode.seconds || 0, episode.date || "-", episode.title].join("\t"));
  return lines.join("\n") + "\n";
}

// --- Where audio may come from ------------------------------------------------------------
// A feed is anyone's to write, so an episode's address must not reach this Mac or its network.
export function publicAddress(address) {
  if (net.isIPv4(address)) {
    const [a, b, c] = address.split(".").map(Number);
    return !(a === 0 || a === 10 || a === 127 || a >= 224 || (a === 100 && b >= 64 && b <= 127) ||
      (a === 169 && b === 254) || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) ||
      (a === 192 && b === 0 && (c === 0 || c === 2)) || (a === 198 && (b === 18 || b === 19)) ||
      (a === 198 && b === 51 && c === 100) || (a === 203 && b === 0 && c === 113));
  }
  if (net.isIPv6(address)) {
    const lower = address.toLowerCase();
    if (lower.startsWith("::ffff:")) return publicAddress(lower.slice(7));
    return !(lower === "::" || lower === "::1" || /^f[cd]/u.test(lower) || /^fe[89ab]/u.test(lower) ||
      lower.startsWith("ff") || lower.startsWith("2001:db8") || lower.startsWith("64:ff9b"));
  }
  return false;
}

// A home network's own addresses, which is all the plain-HTTP door answers. Tailscale's and
// other carrier-grade addresses aren't among them.
export function homeAddress(address) {
  if (!net.isIPv4(address)) return false;
  const [a, b] = address.split(".").map(Number);
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
}

// This computer's addresses on home networks, each with its prefix, so the device can pick the
// one on its own network.
export function lanAddresses(interfaces = os.networkInterfaces()) {
  const out = [];
  for (const list of Object.values(interfaces || {})) for (const entry of list || []) {
    if (entry?.internal || !(entry.family === "IPv4" || entry.family === 4) || !homeAddress(entry.address)) continue;
    const prefix = Number(String(entry.cidr || "").split("/")[1]);
    if (prefix >= 8 && prefix <= 30 && !out.some(known => known.address === entry.address)) out.push({ address: entry.address, prefix });
  }
  return out;
}

export async function checkAudioUrl(url, lookupImpl = lookup) {
  let parsed;
  try { parsed = new URL(url); } catch { throw new PodcastError("bad_episode_url", 502); }
  if (!/^https?:$/u.test(parsed.protocol) || parsed.username || parsed.password) throw new PodcastError("bad_episode_url", 502);
  const host = parsed.hostname.replace(/^\[|\]$/gu, "");
  const addresses = net.isIP(host) ? [{ address: host }] : await lookupImpl(host, { all: true }).catch(() => []);
  if (!addresses.length) throw new PodcastError("episode_host_unknown", 502);
  if (!addresses.every(entry => publicAddress(entry.address))) throw new PodcastError("episode_host_private", 502);
  return parsed.href;
}

export function ffmpegArgs(url, seconds) {
  const start = Math.max(0, Math.floor(Number(seconds) || 0));
  return ["-nostdin", "-hide_banner", "-loglevel", "error",
    // Only the network: a feed must not get ffmpeg to read a file or run a playlist.
    "-protocol_whitelist", "http,https,tcp,tls",
    "-format_whitelist", "wav,mp3,ogg,flac,aac,mov,matroska,webm",
    "-user_agent", USER_AGENT, "-rw_timeout", "20000000",
    "-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_on_network_error", "1", "-reconnect_delay_max", "8",
    ...(start ? ["-ss", String(start)] : []), "-i", url,
    "-map", "0:a:0", "-vn", "-sn", "-dn", "-ac", "1", "-ar", String(AUDIO.rate),
    "-c:a", "libmp3lame", "-b:a", `${AUDIO.kbps}k`,
    // No tags and no header frame, so every frame is the same size and seconds map to bytes.
    "-write_xing", "0", "-id3v2_version", "0", "-map_metadata", "-1", "-f", "mp3", "pipe:1"];
}

export function findFfmpeg(configured) {
  if (configured) return configured;
  for (const candidate of ["/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"])
    if (existsSync(candidate)) return candidate;
  return "ffmpeg";
}

// --- The service --------------------------------------------------------------------------
export class PodcastService {
  constructor({ fetchImpl = fetch, spawnImpl = spawn, lookupImpl = lookup, now = Date.now, ffmpeg } = {}) {
    Object.assign(this, { fetchImpl, spawnImpl, lookupImpl, now });
    this.ffmpeg = findFfmpeg(ffmpeg);
    this.feedUrls = new Map();  // An iTunes show's feed, from its search result.
    this.shows = new Map();
    this.loading = new Map();
    this.playing = new Map();  // One ffmpeg per device: a new request ends the last.
    this.tickets = new Map();
    this.lanPort = 0;  // Set once the home-network door is open.
  }

  // One ticket per device and episode, good for 12 hours and any second of it.
  ticket(device, show, episode) {
    if (!SHOW_ID.test(String(show || ""))) throw new PodcastError("invalid_show", 400);
    if (!EPISODE_ID.test(String(episode || ""))) throw new PodcastError("invalid_episode", 400);
    const now = this.now();
    for (const [id, held] of this.tickets) if (held.expires <= now) this.tickets.delete(id);
    for (const [id, held] of this.tickets) {
      if (held.device === device && held.show === show && held.episode === episode) { held.expires = now + TICKET_MS; return id; }
    }
    while (this.tickets.size >= MAX_TICKETS) this.tickets.delete(this.tickets.keys().next().value);
    const id = randomBytes(16).toString("hex");
    this.tickets.set(id, { device, show, episode, expires: now + TICKET_MS });
    return id;
  }

  redeem(id) {
    const held = this.tickets.get(id);
    return held && held.expires > this.now() ? held : null;
  }

  // For the device: where to fetch this episode on the home network, or "none".
  ticketLines(device, show, episode, interfaces) {
    const addresses = this.lanPort ? lanAddresses(interfaces) : [];
    const id = this.ticket(device, show, episode);
    if (!addresses.length) return "none\n";
    return [`ticket\t${this.lanPort}\t/podcasts/${id}`, ...addresses.map(entry => `lan\t${entry.address}\t${entry.prefix}`)].join("\n") + "\n";
  }

  async get(url, { json = true, maxBytes = 4 * 1024 * 1024 } = {}) {
    const response = await this.fetchImpl(url, { redirect: "follow", headers: { "user-agent": USER_AGENT,
      accept: json ? "application/json" : "application/rss+xml, application/xml, text/xml, */*" },
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS) });
    if (!response.ok) throw new PodcastError(`source_http_${response.status}`, response.status === 404 ? 404 : 502);
    const length = Number(response.headers?.get?.("content-length") || 0);
    if (length > maxBytes) throw new PodcastError("source_too_big", 502);
    const body = Buffer.from(await response.arrayBuffer());
    if (body.length > maxBytes) throw new PodcastError("source_too_big", 502);
    const text = body.toString("utf8");
    if (!json) return text;
    try { return JSON.parse(text); } catch { throw new PodcastError("source_bad_json", 502); }
  }

  async search(kind, text) {
    if (!Object.hasOwn(KINDS, kind)) throw new PodcastError("invalid_kind", 400);
    if (!validQuery(text)) throw new PodcastError("invalid_query", 400);
    if (kind === "podcast") {
      const params = new URLSearchParams({ media: "podcast", entity: "podcast", limit: String(MAX_SHOWS), term: text.trim() });
      const body = await this.get(`${ITUNES_SEARCH_URL}?${params}`);
      const shows = [];
      for (const result of Array.isArray(body?.results) ? body.results : []) {
        const number = Number(result?.collectionId);
        if (!Number.isSafeInteger(number) || number <= 0 || typeof result.feedUrl !== "string") continue;
        const id = `p${number}`;
        this.feedUrls.set(id, result.feedUrl);
        if (this.feedUrls.size > 500) this.feedUrls.delete(this.feedUrls.keys().next().value);
        shows.push({ id, count: Number(result.trackCount) || 0, title: cleanText(result.collectionName, LIMITS.title),
          by: cleanText(result.artistName, LIMITS.by) });
      }
      return shows.filter(show => show.title);
    }
    if (!archiveWords(text).length) throw new PodcastError("invalid_query", 400);
    const body = await this.get(archiveSearchUrl(KINDS[kind], text));
    const first = value => Array.isArray(value) ? value[0] : value;
    return (Array.isArray(body?.response?.docs) ? body.response.docs : [])
      .filter(doc => ARCHIVE_ID.test(String(doc?.identifier || "")))
      .map(doc => ({ id: `a${doc.identifier}`, count: 0, title: cleanText(first(doc.title) || doc.identifier, LIMITS.title),
        by: cleanText(first(doc.creator) || "", LIMITS.by) }));
  }

  async load(id) {
    if (!SHOW_ID.test(String(id || ""))) throw new PodcastError("invalid_show", 400);
    if (id[0] === "p") {
      let feed = this.feedUrls.get(id);
      if (!feed) {
        const body = await this.get(`${ITUNES_LOOKUP_URL}?${new URLSearchParams({ id: id.slice(1), entity: "podcast" })}`);
        feed = body?.results?.find(result => typeof result?.feedUrl === "string")?.feedUrl;
        if (!feed) throw new PodcastError("unknown_show", 404);
        this.feedUrls.set(id, feed);
      }
      return parseFeed(await this.get(await checkAudioUrl(feed, this.lookupImpl), { json: false, maxBytes: MAX_FEED_BYTES }));
    }
    const identifier = id.slice(1);
    const body = await this.get(ARCHIVE_METADATA_URL + encodeURIComponent(identifier), { maxBytes: MAX_FEED_BYTES });
    if (!body?.metadata) throw new PodcastError("unknown_show", 404);
    return parseArchiveItem(identifier, body);
  }

  async show(id) {
    const saved = this.shows.get(id);
    if (saved && this.now() - saved.at < FEED_FRESH_MS) return saved.show;
    if (!this.loading.has(id)) this.loading.set(id, this.load(id).finally(() => this.loading.delete(id)));
    try {
      const show = await this.loading.get(id);
      this.shows.delete(id);
      if (this.shows.size >= MAX_CACHED_SHOWS) this.shows.delete(this.shows.keys().next().value);
      this.shows.set(id, { at: this.now(), show });
      return show;
    } catch (error) {
      if (saved) return saved.show;  // An old list beats none.
      throw error;
    }
  }

  async episodes(id, from = 0) {
    const show = await this.show(id);
    const start = Math.max(0, Math.min(Math.floor(Number(from) || 0), Math.max(0, show.episodes.length - 1)));
    return episodeLines(show, start);
  }

  async episode(showId, id) {
    if (!EPISODE_ID.test(String(id || ""))) throw new PodcastError("invalid_episode", 400);
    let show = await this.show(showId);
    let found = show.episodes.find(episode => episode.id === id);
    if (!found && this.shows.has(showId)) {
      // A new episode can push an old one out of a cached list's view, so look once more.
      this.shows.delete(showId);
      show = await this.show(showId);
      found = show.episodes.find(episode => episode.id === id);
    }
    if (!found) throw new PodcastError("unknown_episode", 404);
    return found;
  }

  // Streams the episode to res from `seconds` in, as AUDIO. Answers only once ffmpeg has
  // sound to send, so a dead link is an error the device can show, not a silent stream.
  async audio(res, { device = "device", show, episode, seconds = 0 }) {
    const found = await this.episode(show, episode);
    const url = await checkAudioUrl(found.url, this.lookupImpl);
    this.playing.get(device)?.();
    if (this.playing.size >= MAX_AUDIO_GLOBAL) throw new PodcastError("audio_busy", 503);
    const child = this.spawnImpl(this.ffmpeg, ffmpegArgs(url, seconds), { stdio: ["ignore", "pipe", "pipe"] });
    let ended = false, errors = "";
    const end = () => {
      if (ended) return;
      ended = true;
      if (this.playing.get(device) === end) this.playing.delete(device);
      child.stdout?.unpipe?.(res);
      if (child.exitCode === null) {
        child.kill("SIGTERM");
        setTimeout(() => { if (child.exitCode === null) child.kill("SIGKILL"); }, 2000).unref?.();
      }
    };
    this.playing.set(device, end);
    child.stderr?.on("data", chunk => { errors = (errors + chunk).slice(-400); });
    res.on("close", end);
    const first = await new Promise(resolve => {
      const timer = setTimeout(() => resolve(null), FIRST_AUDIO_TIMEOUT_MS);
      // Paused at once: a flowing stream with no listener would drop what came next.
      child.stdout.once("data", chunk => { child.stdout.pause(); clearTimeout(timer); resolve(chunk); });
      child.once("error", () => { clearTimeout(timer); resolve(null); });
      child.once("close", () => { clearTimeout(timer); resolve(null); });
    });
    if (!first || ended) {
      const cancelled = ended;
      end();
      if (!cancelled) console.warn(`[podcasts] audio failed: ${errors.trim().split("\n").pop()?.slice(0, 120) || "no audio"}`);
      throw new PodcastError(cancelled ? "cancelled" : "episode_unavailable", 502);
    }
    res.writeHead(200, { "content-type": "audio/mpeg", "cache-control": "no-store", "x-content-type-options": "nosniff" });
    res.write(first);
    child.stdout.pipe(res);
    child.once("close", code => {
      if (code && !ended) console.warn(`[podcasts] audio stopped: ${errors.trim().split("\n").pop()?.slice(0, 120) || code}`);
      if (this.playing.get(device) === end) this.playing.delete(device);
      ended = true;
    });
  }
}

// The home-network door: GET /podcasts/<ticket>?t=<second>, from a home address only.
export function lanHandler(service) {
  return (req, res) => {
    void (async () => {
      const remote = String(req.socket?.remoteAddress || "").replace(/^::ffff:/u, "");
      let url;
      try { url = new URL(req.url || "/", "http://lan"); } catch { url = new URL("http://lan/"); }
      const match = url.pathname.match(/^\/podcasts\/([0-9a-f]{32})$/u);
      const held = req.method === "GET" && match && (homeAddress(remote) || remote === "127.0.0.1") ? service.redeem(match[1]) : null;
      if (!held) {
        res.writeHead(404, { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" });
        res.end("not_found\n");
        return;
      }
      try {
        await service.audio(res, { device: held.device, show: held.show, episode: held.episode, seconds: url.searchParams.get("t") || 0 });
      } catch (error) {
        if (res.headersSent || res.destroyed) { res.destroy(); return; }
        if (error.code !== "cancelled") console.warn(`[podcasts] home audio failed: ${String(error.code || error.message || error).slice(0, 120)}`);
        res.writeHead(error instanceof PodcastError ? error.status : 503, { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" });
        res.end(`${error.code || "podcasts_unavailable"}\n`);
      }
    })();
  };
}
