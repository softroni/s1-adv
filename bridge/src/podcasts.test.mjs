import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { PassThrough } from "node:stream";
import test from "node:test";
import { AUDIO, LIMITS, PAGE, PodcastError, PodcastService, archiveSearchUrl, checkAudioUrl, cleanText, decodeEntities,
  episodeId, episodeLines, ffmpegArgs, homeAddress, lanAddresses, lanHandler, parseArchiveItem, parseDuration, parseFeed,
  publicAddress, showLines, validQuery } from "./podcasts.mjs";

const FEED = `<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:itunes="http://www.itunes.com/dtds/podcast-1.0.dtd">
<channel>
  <image><title>Logo title, not the show</title><url>https://example.com/a.jpg</url></image>
  <title><![CDATA[History & Tea]]></title>
  <itunes:author>Ann &amp; Bob</itunes:author>
  <item>
    <title>Older one</title>
    <pubDate>Mon, 26 Oct 2026 09:00:00 GMT</pubDate>
    <itunes:duration>45:30</itunes:duration>
    <enclosure url="https://cdn.example.com/older.mp3?a=1&amp;b=2" length="0" type="audio/mpeg"/>
  </item>
  <item>
    <title>12. Newest one: “Quotes” — and an emoji 🎧</title>
    <itunes:title>Newest one: “Quotes” — and an emoji 🎧</itunes:title>
    <pubDate>Mon, 02 Nov 2026 00:05:00 -0000</pubDate>
    <itunes:duration>5131</itunes:duration>
    <enclosure url="https://cdn.example.com/new.mp3" type="audio/mpeg" length="1"/>
  </item>
  <item>
    <title>A video</title>
    <enclosure url="https://cdn.example.com/v.mp4" type="video/mp4"/>
  </item>
  <item>
    <title>Same file twice</title>
    <enclosure url="https://cdn.example.com/new.mp3" type="audio/mpeg"/>
  </item>
  <item>
    <title><![CDATA[No date <b>but</b> an m4a]]></title>
    <itunes:duration>1:02:03</itunes:duration>
    <enclosure url='https://cdn.example.com/x.m4a'/>
  </item>
</channel></rss>`;

const ARCHIVE = { metadata: { title: "The Adventures of Sherlock Holmes", creator: ["Sir Arthur Conan Doyle", "Reader"] }, files: [
  { name: "holmes_02.mp3", format: "VBR MP3", title: "02 - The Red-Headed League", track: "2/3", length: "3546.64", source: "original" },
  { name: "holmes_01_64kb.mp3", format: "64Kbps MP3", title: "01 - A Scandal in Bohemia", length: "65:06", source: "derivative", original: "holmes_01.mp3" },
  { name: "holmes_01.mp3", format: "VBR MP3", title: "01 - A Scandal in Bohemia", track: "1/3", length: "3906.74", source: "original" },
  { name: "holmes_02_64kb.mp3", format: "64Kbps MP3", length: "59:06", source: "derivative", original: "holmes_02.mp3" },
  { name: "disc 2/holmes_10.mp3", format: "VBR MP3", track: "10", source: "original" },
  { name: "holmes_01.ogg", format: "Ogg Vorbis", source: "derivative", original: "holmes_01.mp3" },
  { name: "cover.jpg", format: "JPEG", source: "original" }
] };

test("text for the screen keeps to one line, ASCII where it can, and its byte limit", () => {
  assert.equal(cleanText("  “Quotes” — dashes…\tand\ntabs  ", 96), "\"Quotes\" - dashes... and tabs");
  assert.equal(cleanText("Listen 🎧 now 👩‍👩‍👧 ok", 96), "Listen now ok");
  assert.equal(cleanText("Café Привет", 96), "Café Привет");
  assert.equal(Buffer.byteLength(cleanText("Привет мир".repeat(20), 48)) <= 48, true);
  assert.equal(cleanText("Привет", 5), "Пр");  // Never half a letter.
  assert.equal(decodeEntities("a &amp; b &#8217; &#x41; &bogus; &nbsp;"), "a & b ’ A &bogus;  ");
});

test("durations come in seconds, minutes or hours", () => {
  assert.equal(parseDuration("5131"), 5131);
  assert.equal(parseDuration("45:30"), 2730);
  assert.equal(parseDuration("1:02:03"), 3723);
  assert.equal(parseDuration("3906.74"), 3907);
  for (const bad of ["", "abc", "1:2:3:4", null, "-5"]) assert.equal(parseDuration(bad), 0, String(bad));
});

test("a feed gives its show and audio episodes, newest first, each once", () => {
  const show = parseFeed(FEED);
  assert.equal(show.title, "History & Tea");
  assert.equal(show.by, "Ann & Bob");
  assert.deepEqual(show.episodes.map(e => e.title), ["Newest one: \"Quotes\" - and an emoji", "Older one", "No date but an m4a"]);
  assert.deepEqual(show.episodes.map(e => e.seconds), [5131, 2730, 3723]);
  assert.deepEqual(show.episodes.map(e => e.date), ["2026-11-02", "2026-10-26", ""]);
  assert.equal(show.episodes[1].url, "https://cdn.example.com/older.mp3?a=1&b=2");
  assert.equal(show.episodes[0].id, episodeId("https://cdn.example.com/new.mp3"));
  assert.match(show.episodes[0].id, /^[0-9a-f]{12}$/u);
});

test("an Archive item keeps each recording once, as its smallest MP3, in track order", () => {
  const show = parseArchiveItem("holmes", ARCHIVE);
  assert.equal(show.title, "The Adventures of Sherlock Holmes");
  assert.equal(show.by, "Sir Arthur Conan Doyle");
  assert.deepEqual(show.episodes.map(e => e.title), ["01 - A Scandal in Bohemia", "02 - The Red-Headed League", "holmes 10"]);
  assert.deepEqual(show.episodes.map(e => e.url), ["https://archive.org/download/holmes/holmes_01_64kb.mp3",
    "https://archive.org/download/holmes/holmes_02_64kb.mp3", "https://archive.org/download/holmes/disc%202/holmes_10.mp3"]);
  assert.deepEqual(show.episodes.map(e => e.seconds), [3906, 3546, 0]);
});

test("the device's lines put text last and never hold a tab or a break", () => {
  const show = parseFeed(FEED);
  const lines = episodeLines({ ...show, by: "x" }, 0).split("\n");
  assert.equal(lines[0], "about\t3\t0\tHistory & Tea\tx");
  assert.equal(lines[1], `ep\t${show.episodes[0].id}\t5131\t2026-11-02\tNewest one: "Quotes" - and an emoji`);
  assert.equal(lines[3].split("\t")[3], "-");
  assert.equal(lines.at(-1), "");
  const many = { title: "t", by: "", episodes: Array.from({ length: 45 }, (_, i) => ({ id: "0".repeat(12), seconds: i, date: "", title: `E${i}` })) };
  const page = episodeLines(many, 40).trim().split("\n");
  assert.equal(page.length, 6);
  assert.equal(page[0], "about\t45\t40\tt\t");
  assert.equal(episodeLines(many, 0).trim().split("\n").length, PAGE + 1);
  assert.equal(showLines([{ id: "p1", count: 7, title: "A", by: "B" }]), "show\tp1\t7\tA\tB\n");
});

test("searches are words of reasonable length, and the Archive's syntax is taken out", () => {
  for (const good of ["history", "rest is history", "Привет"]) assert.ok(validQuery(good), good);
  for (const bad of ["", "   ", "x".repeat(65), "a\nb", null, 5]) assert.ok(!validQuery(bad), String(bad));
  const url = new URL(archiveSearchUrl("oldtimeradio", "the (shadow) OR \"x:y\""));
  assert.equal(url.searchParams.get("q"), "collection:(oldtimeradio) AND mediatype:(audio) AND (title:(the AND shadow AND x AND y) OR creator:(the AND shadow AND x AND y))");
  assert.deepEqual(url.searchParams.getAll("fl[]"), ["identifier", "title", "creator"]);
});

test("episodes may only come from the public internet", async () => {
  for (const ok of ["8.8.8.8", "151.101.1.1", "2606:4700::1111"]) assert.ok(publicAddress(ok), ok);
  for (const bad of ["127.0.0.1", "10.1.2.3", "192.168.0.30", "172.20.0.1", "169.254.1.1", "100.100.1.1", "0.0.0.0",
    "::1", "fd7a:115c::1", "fe80::1", "::ffff:192.168.1.1", "nonsense"]) assert.ok(!publicAddress(bad), bad);
  const lookup = async host => host === "inside.example" ? [{ address: "192.168.0.30" }] : host === "mixed.example"
    ? [{ address: "8.8.8.8" }, { address: "10.0.0.1" }] : [{ address: "93.184.216.34" }];
  assert.equal(await checkAudioUrl("https://cdn.example.com/a.mp3", lookup), "https://cdn.example.com/a.mp3");
  for (const bad of ["file:///etc/passwd", "concat:a|b", "https://user:pw@cdn.example.com/a.mp3", "https://inside.example/a.mp3",
    "https://mixed.example/a.mp3", "http://127.0.0.1:31889/internal", "not a url"])
    await assert.rejects(checkAudioUrl(bad, lookup), PodcastError, bad);
});

test("ffmpeg reads only from the network and writes one constant MP3 format", () => {
  const args = ffmpegArgs("https://cdn.example.com/a.mp3", 90.7);
  const value = flag => args[args.indexOf(flag) + 1];
  assert.equal(value("-protocol_whitelist"), "http,https,tcp,tls");
  assert.equal(value("-ss"), "90");
  assert.ok(args.indexOf("-ss") < args.indexOf("-i"), "a seek before the input is fast");
  assert.equal(value("-ac"), "1");
  assert.equal(value("-ar"), String(AUDIO.rate));
  assert.equal(value("-b:a"), `${AUDIO.kbps}k`);
  assert.equal(value("-write_xing"), "0");
  assert.equal(args.at(-1), "pipe:1");
  assert.ok(!ffmpegArgs("https://cdn.example.com/a.mp3", 0).includes("-ss"));
  assert.equal(AUDIO.bytesPerSecond, AUDIO.kbps * 1000 / 8);
});

function fakeFetch(routes) {
  const calls = [];
  return { calls, impl: async (url) => {
    calls.push(String(url));
    for (const [part, body] of routes) if (String(url).includes(part)) {
      const text = typeof body === "string" ? body : JSON.stringify(body);
      return { ok: true, status: 200, headers: new Map(), arrayBuffer: async () => Buffer.from(text) };
    }
    return { ok: false, status: 404, headers: new Map(), arrayBuffer: async () => Buffer.alloc(0) };
  } };
}
const publicLookup = async () => [{ address: "93.184.216.34" }];

test("a podcast is found on iTunes and its feed read once in a while", async () => {
  let time = 0;
  const fetch = fakeFetch([["itunes.apple.com/search", { results: [
    { collectionId: 42, collectionName: "History & Tea 🎧", artistName: "Ann", feedUrl: "https://feeds.example.com/tea", trackCount: 3 },
    { collectionId: 43, collectionName: "No feed" }] }], ["feeds.example.com/tea", FEED]]);
  const service = new PodcastService({ fetchImpl: fetch.impl, lookupImpl: publicLookup, now: () => time });
  const shows = await service.search("podcast", "history");
  assert.deepEqual(shows, [{ id: "p42", count: 3, title: "History & Tea", by: "Ann" }]);
  const page = await service.episodes("p42", 0);
  assert.match(page, /^about\t3\t0\tHistory & Tea\tAnn & Bob\n/u);
  await service.episodes("p42", 0);
  assert.equal(fetch.calls.filter(url => url.includes("feeds.example.com")).length, 1);
  time += 31 * 60 * 1000;
  await service.episodes("p42", 0);
  assert.equal(fetch.calls.filter(url => url.includes("feeds.example.com")).length, 2);
  const term = new URL(fetch.calls[0]).searchParams;
  assert.equal(term.get("media"), "podcast");
  assert.equal(term.get("term"), "history");
});

test("a podcast seen before a restart is looked up by its number", async () => {
  const fetch = fakeFetch([["itunes.apple.com/lookup", { results: [{ feedUrl: "https://feeds.example.com/tea" }] }], ["feeds.example.com/tea", FEED]]);
  const service = new PodcastService({ fetchImpl: fetch.impl, lookupImpl: publicLookup });
  assert.match(await service.episodes("p42", 0), /History & Tea/u);
  assert.equal(new URL(fetch.calls[0]).searchParams.get("id"), "42");
});

test("books and old-time radio come from the Archive's collections", async () => {
  const fetch = fakeFetch([["advancedsearch", { response: { docs: [{ identifier: "holmes", title: "Holmes", creator: ["Doyle"] },
    { identifier: "../bad", title: "x" }] } }], ["archive.org/metadata/holmes", ARCHIVE]]);
  const service = new PodcastService({ fetchImpl: fetch.impl, lookupImpl: publicLookup });
  assert.deepEqual(await service.search("book", "sherlock"), [{ id: "aholmes", count: 0, title: "Holmes", by: "Doyle" }]);
  assert.match(new URL(fetch.calls[0]).searchParams.get("q"), /^collection:\(librivoxaudio\)/u);
  await service.search("radio", "shadow");
  assert.match(new URL(fetch.calls[1]).searchParams.get("q"), /^collection:\(oldtimeradio\)/u);
  assert.match(await service.episodes("aholmes", 0), /\tA Scandal in Bohemia|01 - A Scandal in Bohemia/u);
  await assert.rejects(service.search("video", "x"), { code: "invalid_kind" });
  await assert.rejects(service.search("radio", "  "), { code: "invalid_query" });
  await assert.rejects(service.episodes("a../etc", 0), { code: "invalid_show" });
  await assert.rejects(service.episodes("x1", 0), { code: "invalid_show" });
});

function fakeChild() {
  const child = new EventEmitter();
  child.stdout = new PassThrough();
  child.stderr = new PassThrough();
  child.exitCode = null;
  child.killed = [];
  child.kill = (signal) => { child.killed.push(signal); child.exitCode = 1; setImmediate(() => child.emit("close", 1)); };
  return child;
}
function fakeResponse() {
  const res = new PassThrough();
  res.headers = null;
  res.headersSent = false;
  res.writeHead = (status, headers) => { res.status = status; res.headers = headers; res.headersSent = true; };
  res.chunks = [];
  res.on("data", chunk => res.chunks.push(chunk));
  return res;
}

test("audio starts once ffmpeg has sound, and stopping it ends ffmpeg", async () => {
  const fetch = fakeFetch([["archive.org/metadata/holmes", ARCHIVE]]);
  const children = [];
  const spawnImpl = (command, args) => { const child = fakeChild(); child.args = args; children.push(child); return child; };
  const service = new PodcastService({ fetchImpl: fetch.impl, lookupImpl: publicLookup, spawnImpl, ffmpeg: "/x/ffmpeg" });
  const show = parseArchiveItem("holmes", ARCHIVE);
  const res = fakeResponse();
  const playing = service.audio(res, { device: "d1", show: "aholmes", episode: show.episodes[1].id, seconds: "75" });
  await new Promise(resolve => setImmediate(resolve));
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(children.length, 1);
  assert.equal(children[0].args[children[0].args.indexOf("-ss") + 1], "75");
  assert.equal(children[0].args[children[0].args.indexOf("-i") + 1], show.episodes[1].url);
  assert.equal(res.headersSent, false, "nothing is promised before there's sound");
  children[0].stdout.write(Buffer.from([0xff, 0xfb, 1]));
  children[0].stdout.write(Buffer.from([2, 3]));
  await playing;
  assert.equal(res.status, 200);
  assert.equal(res.headers["content-type"], "audio/mpeg");
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual([...Buffer.concat(res.chunks)], [0xff, 0xfb, 1, 2, 3], "nothing is dropped between the first chunk and the rest");
  // The same device asking again ends the first, so one listener never runs two.
  const again = service.audio(fakeResponse(), { device: "d1", show: "aholmes", episode: show.episodes[0].id, seconds: 0 });
  await new Promise(resolve => setImmediate(resolve));
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(children[0].killed, ["SIGTERM"]);
  children[1].stdout.write(Buffer.from([0xff]));
  await again;
  assert.ok(!children[1].args.includes("-ss"));
  res.emit("close");
});

test("an episode that never sounds is an error, not a silent stream", async () => {
  const fetch = fakeFetch([["archive.org/metadata/holmes", ARCHIVE]]);
  let child;
  const service = new PodcastService({ fetchImpl: fetch.impl, lookupImpl: publicLookup, spawnImpl: () => (child = fakeChild()) });
  const show = parseArchiveItem("holmes", ARCHIVE);
  const res = fakeResponse();
  const playing = service.audio(res, { device: "d1", show: "aholmes", episode: show.episodes[0].id });
  await new Promise(resolve => setImmediate(resolve));
  await new Promise(resolve => setImmediate(resolve));
  child.stderr.write("Connection timed out\n");
  child.exitCode = 1;
  child.emit("close", 1);
  await assert.rejects(playing, { code: "episode_unavailable", status: 502 });
  assert.equal(res.headersSent, false);
  // A file ffmpeg can't read is a different answer: asking again won't help.
  const again = service.audio(fakeResponse(), { device: "d1", show: "aholmes", episode: show.episodes[0].id });
  await new Promise(resolve => setImmediate(resolve));
  await new Promise(resolve => setImmediate(resolve));
  child.stderr.write("Error opening input files: Invalid data found when processing input\n");
  await new Promise(resolve => setImmediate(resolve));
  child.exitCode = 1;
  child.emit("close", 1);
  await assert.rejects(again, { code: "episode_unplayable", status: 415 });
  await assert.rejects(service.audio(res, { device: "d1", show: "aholmes", episode: "nothex" }), { code: "invalid_episode" });
  await assert.rejects(service.audio(res, { device: "d1", show: "aholmes", episode: "0123456789ab" }), { code: "unknown_episode" });
});

test("limits match what the device keeps", () => {
  assert.deepEqual(LIMITS, { id: 100, title: 96, by: 48 });
});

const INTERFACES = {
  lo0: [{ address: "127.0.0.1", family: "IPv4", internal: true, cidr: "127.0.0.1/8" }],
  en0: [{ address: "192.168.0.20", family: "IPv4", internal: false, cidr: "192.168.0.20/22" },
        { address: "fe80::1", family: "IPv6", internal: false, cidr: "fe80::1/64" }],
  en1: [{ address: "192.168.0.20", family: "IPv4", internal: false, cidr: "192.168.0.20/22" }],
  utun4: [{ address: "100.101.102.103", family: "IPv4", internal: false, cidr: "100.101.102.103/32" }],
  en5: [{ address: "10.0.0.7", family: 4, internal: false, cidr: "10.0.0.7/24" }]
};

test("the home-network door is only for home addresses", () => {
  for (const home of ["192.168.0.30", "10.1.2.3", "172.16.0.1", "172.31.255.255"]) assert.ok(homeAddress(home), home);
  for (const not of ["100.101.102.103", "8.8.8.8", "172.32.0.1", "127.0.0.1", "::1", "fd7a:115c::1", "x"]) assert.ok(!homeAddress(not), not);
  assert.deepEqual(lanAddresses(INTERFACES), [{ address: "192.168.0.20", prefix: 22 }, { address: "10.0.0.7", prefix: 24 }]);
});

test("a ticket is one per device and episode, and lasts 12 hours", () => {
  let time = 1000;
  const service = new PodcastService({ now: () => time });
  const a = service.ticket("d1", "p42", "111111111111");
  assert.match(a, /^[0-9a-f]{32}$/u);
  assert.equal(service.ticket("d1", "p42", "111111111111"), a);
  assert.notEqual(service.ticket("d2", "p42", "111111111111"), a);
  assert.notEqual(service.ticket("d1", "p42", "222222222222"), a);
  assert.deepEqual({ ...service.redeem(a), expires: 0 }, { device: "d1", show: "p42", episode: "111111111111", expires: 0 });
  time += 12 * 60 * 60 * 1000 + 1;
  assert.equal(service.redeem(a), null);
  assert.throws(() => service.ticket("d1", "../x", "111111111111"), { code: "invalid_show" });
  assert.throws(() => service.ticket("d1", "p42", "nope"), { code: "invalid_episode" });
  for (let i = 0; i < 100; ++i) service.ticket("d1", "p42", i.toString(16).padStart(12, "0"));
  assert.equal(service.tickets.size, 64);
});

test("the device is told where to fetch it at home, or that it can't", () => {
  const service = new PodcastService();
  assert.equal(service.ticketLines("d1", "p42", "111111111111", INTERFACES), "none\n");  // The door isn't open.
  service.lanPort = 31890;
  const lines = service.ticketLines("d1", "p42", "111111111111", INTERFACES).trim().split("\n");
  assert.match(lines[0], /^ticket\t31890\t\/podcasts\/[0-9a-f]{32}$/u);
  assert.deepEqual(lines.slice(1), ["lan\t192.168.0.20\t22", "lan\t10.0.0.7\t24"]);
  assert.equal(service.ticketLines("d1", "p42", "111111111111", { utun4: INTERFACES.utun4 }), "none\n");
});

test("the door plays a ticket's episode from the second asked, and nothing else", async () => {
  const fetch = fakeFetch([["archive.org/metadata/holmes", ARCHIVE]]);
  const children = [];
  const service = new PodcastService({ fetchImpl: fetch.impl, lookupImpl: publicLookup,
    spawnImpl: (command, args) => { const child = fakeChild(); child.args = args; children.push(child); return child; } });
  const episode = parseArchiveItem("holmes", ARCHIVE).episodes[0];
  const id = service.ticket("d1", "aholmes", episode.id);
  const handler = lanHandler(service);
  const call = async (path, remote = "192.168.0.30", method = "GET") => {
    const res = fakeResponse();
    res.end = (body) => { res.body = String(body || ""); res.ended = true; };
    handler({ method, url: path, socket: { remoteAddress: remote } }, res);
    for (let i = 0; i < 6; ++i) await new Promise(resolve => setImmediate(resolve));
    return res;
  };
  for (const [path, remote, method] of [[`/podcasts/${"0".repeat(32)}`], [`/podcasts/${id}`, "100.101.102.103"], [`/podcasts/${id}`, "8.8.8.8"],
    [`/podcasts/${id}`, "192.168.0.30", "POST"], [`/api/cardputer/podcasts/search?q=x`], [`/podcasts/${id}/../x`]]) {
    const res = await call(path, remote, method);
    assert.equal(res.status, 404, `${path} ${remote} ${method}`);
  }
  assert.equal(children.length, 0);
  const res = await call(`/podcasts/${id}?t=95`, "::ffff:192.168.0.30");
  assert.equal(children.length, 1);
  assert.equal(children[0].args[children[0].args.indexOf("-ss") + 1], "95");
  children[0].stdout.write(Buffer.from([0xff, 0xfb]));
  for (let i = 0; i < 4; ++i) await new Promise(resolve => setImmediate(resolve));
  assert.equal(res.status, 200);
  assert.equal(res.headers["content-type"], "audio/mpeg");
  res.emit("close");
});

test("a session ticket carries searches, lists and audio over the home door, and nothing else", async () => {
  const fetch = fakeFetch([["advancedsearch", { response: { docs: [{ identifier: "holmes", title: "Holmes", creator: "Doyle" }] } }],
    ["archive.org/metadata/holmes", ARCHIVE]]);
  const children = [];
  const service = new PodcastService({ fetchImpl: fetch.impl, lookupImpl: publicLookup,
    spawnImpl: (command, args) => { const child = fakeChild(); child.args = args; children.push(child); return child; } });
  service.lanPort = 31890;
  const lines = service.ticketLines("d1", "", "", INTERFACES).trim().split("\n");
  const id = lines[0].match(/\/podcasts\/([0-9a-f]{32})$/u)[1];
  assert.equal(service.ticketLines("d1", "", "", INTERFACES).split("\n")[0], lines[0]);  // The same session again.
  const handler = lanHandler(service);
  const call = async (path, remote = "192.168.0.30") => {
    const res = fakeResponse();
    res.end = (body) => { res.body = String(body || ""); res.ended = true; };
    handler({ method: "GET", url: path, socket: { remoteAddress: remote } }, res);
    for (let i = 0; i < 8; ++i) await new Promise(resolve => setImmediate(resolve));
    return res;
  };
  const found = await call(`/podcasts/${id}/search?kind=book&q=sherlock`);
  assert.equal(found.status, 200);
  assert.equal(found.body, "show\taholmes\t0\tHolmes\tDoyle\n");
  const page = await call(`/podcasts/${id}/episodes?show=aholmes&from=0`);
  assert.match(page.body, /^about\t3\t0\tThe Adventures of Sherlock Holmes\t/u);
  assert.equal((await call(`/podcasts/${id}`)).status, 404, "a session isn't one episode's ticket");
  assert.equal((await call(`/podcasts/${id}/search?kind=book&q=x`, "100.101.102.103")).status, 404);
  const episode = parseArchiveItem("holmes", ARCHIVE).episodes[1];
  const audio = await call(`/podcasts/${id}/audio?show=aholmes&ep=${episode.id}&t=42`);
  assert.equal(children.length, 1);
  assert.equal(children[0].args[children[0].args.indexOf("-ss") + 1], "42");
  assert.equal(children[0].args[children[0].args.indexOf("-i") + 1], episode.url);
  children[0].stdout.write(Buffer.from([0xff, 0xfb]));
  for (let i = 0; i < 4; ++i) await new Promise(resolve => setImmediate(resolve));
  assert.equal(audio.status, 200);
  audio.emit("close");
  // An episode's ticket can't open the session's routes.
  const single = service.ticket("d1", "aholmes", episode.id);
  assert.equal((await call(`/podcasts/${single}/search?kind=book&q=x`)).status, 404);
  // A report goes to the log, its values cut to plain words.
  const logged = [];
  const log = console.log;
  console.log = (line) => logged.push(line);
  try {
    const said = await call(`/podcasts/${id}/report?stage=play&e=1&note=player%2017K%2F9K&free=61000&via=home&secret=x&title=%3Cscript%3E`);
    assert.equal(said.body, "ok\n");
  } finally { console.log = log; }
  assert.deepEqual(logged, ["[podcasts] device report stage=play e=1 note=player_17K/9K free=61000 via=home"]);
});
