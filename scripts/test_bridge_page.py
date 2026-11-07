#!/usr/bin/env python3
"""System > Bridge page: the bridge's answer read as the device reads it, the address broken where
a person would break it, the screen asking through the background check's worker only when it's
free, and every state drawn inside the screen, with the QR code clear of the words, under ASan/UBSan."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
part = lambda name: (parts / name).read_text()
screen = part('098_main.cpp.inc')
cxx = os.environ.get('CXX', 'c++')
flags = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']

# --- Wired in ------------------------------------------------------------------------------------
assert 'main_parts/098_main.cpp.inc' in (src / 'main.cpp').read_text()
assert '{"Bridge page", UiMode::BridgePage}' in part('004_main.cpp.inc').split('{"System",', 1)[1].split('{"Lab",', 1)[0]
assert 'BridgePage,' in part('002_main.cpp.inc')
assert 'case UiMode::BridgePage: return "Bridge page";' in part('007_main.cpp.inc')
assert 'if (modeChanged && mode == UiMode::BridgePage) beginBridgePage();' in part('024_main.cpp.inc')
assert 'gUiMode == UiMode::BridgePage && !gLauncherVisible && !keys.tab && handleBridgePageKey(key, keys)' in part('048_main.cpp.inc')
assert 'case UiMode::BridgePage: return kQr;' in part('052_main.cpp.inc')
loop = part('061_main.cpp.inc')
assert 'case UiMode::BridgePage:\n        renderBridgePageUi();' in loop
assert 'serviceBridgePage();' in loop.split('void loop() {', 1)[1]
assert 'constexpr uint8_t kQr[32]' in (src / 'launcher_icons.h').read_text()
for name in ['void beginBridgePage();', 'void serviceBridgePage();', 'bool handleBridgePageKey(char key, const Keyboard_Class::KeysState& keys);', 'void renderBridgePageUi();']:
    assert name in part('005_main.cpp.inc'), name
assert screen.count('namespace {') == 1 and screen.rstrip().endswith('}  // namespace'), 'one anonymous namespace, like every part'

# --- The screen, compiled against a canvas that checks where everything lands --------------------
body = screen.split('#include "../bridge_page.h"', 1)[1]
code = r'''
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "bridge_page.h"

class String {
 public:
  String(const char* s = "") : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  String operator+(const char* o) const { return String(s_ + o); }
 private:
  std::string s_;
};
struct Keyboard_Class { struct KeysState { bool ctrl = false, alt = false, tab = false; }; };
enum class UiMode : uint8_t { Home, BridgePage };
UiMode gUiMode = UiMode::BridgePage;
struct { int invalidated = 0; void invalidate() { ++invalidated; } } gUiFramePolicy;
uint32_t gLastRenderMs = 1;
void logf(const char*, ...) {}
uint16_t rgb565_local(int r, int g, int b) { return uint16_t(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)); }
constexpr uint16_t TFT_BLACK = 0, TFT_WHITE = 0xffff;

struct InboxHttpPayload { String url, token; void (*done)(int code, const String& body) = nullptr; };
bool gInboxHttpBusy = false, gPollAllowed = true;
uint32_t gWifi = 0x1400a8c0;
String gDeviceToken = "token";
std::vector<std::string> gAsked;
void (*gDone)(int, const String&) = nullptr;
uint32_t wifiLocalAddress() { return gWifi; }
bool autoInboxPollAllowed() { return gPollAllowed; }
String hubBaseUrl() { return String("https://bridge.example.com"); }
bool startInboxNetworkRequest(std::unique_ptr<InboxHttpPayload> p) {
  if (gInboxHttpBusy) return false;
  gAsked.push_back(p->url.c_str());
  gDone = p->done;
  gInboxHttpBusy = true;
  return true;
}
void answer(int code, const char* text) { gInboxHttpBusy = false; auto done = gDone; gDone = nullptr; done(code, String(text)); }

// Fonts by their height and a width per character close to the real ones, so a line that would
// spill here would spill on the device too.
struct Font { int w, h; };
namespace fonts { Font Font0{6, 8}, Font2{8, 16}, FreeSansBold9pt7b{11, 13}; }
struct Item { std::string kind, text; int x, y, w, h; };
struct Canvas {
  const Font* font = &fonts::Font0;
  int cx = 0, cy = 0;
  std::vector<Item> items;
  void fillScreen(uint16_t) { items.clear(); }
  void fillRect(int x, int y, int w, int h, uint16_t) { items.push_back({"rect", "", x, y, w, h}); }
  void qrcode(const char* text, int x, int y, int w, int, bool) { items.push_back({"qr", text, x, y, w, w}); }
  void setFont(const Font* f) { font = f; }
  void setTextColor(uint16_t) {}
  void setCursor(int x, int y) { cx = x; cy = y; }
  int textWidth(const char* t) { return int(strlen(t)) * font->w; }
  void print(const char* t) { items.push_back({"text", t, cx, cy, textWidth(t), font->h}); cx += textWidth(t); }
  void drawCenterString(const char* t, int x, int y) { int w = textWidth(t); items.push_back({"text", t, x - w / 2, y, w, font->h}); }
} gCanvas;

''' + body + r'''

void inside() {
  for (const auto& i : gCanvas.items) {
    if (i.x < 0 || i.y < 0 || i.x + i.w > 240 || i.y + i.h > 135) { printf("outside: %s %s %d,%d %dx%d\n", i.kind.c_str(), i.text.c_str(), i.x, i.y, i.w, i.h); assert(false); }
  }
}
std::string texts() { std::string all; for (const auto& i : gCanvas.items) if (i.kind == "text") all += i.text + "|"; return all; }
bool qr() { for (const auto& i : gCanvas.items) if (i.kind == "qr") return true; return false; }

int main() {
  using namespace bridge_page;
  // --- The bridge's answer ---
  Link link;
  const char* good = "url\thttps://mac.example-tail.ts.net:8443/\n";
  assert(parse(good, strlen(good), link) == Answer::Link && !strcmp(link.url, "https://mac.example-tail.ts.net:8443/"));
  const char* none = "none\tNot shared on Tailscale yet\n";
  assert(parse(none, strlen(none), link) == Answer::None && !strcmp(link.why, "Not shared on Tailscale yet"));
  Link kept = link;
  for (const char* bad : {"", "url", "url\t", "url\tftp://x.example/", "url\thttps://a b.example/", "url\thttps://x", "hello\tthere",
                          "none\t", "url\thttps://\x01.example.com/"}) {
    Link l = kept;
    assert(parse(bad, strlen(bad), l) == Answer::Bad);
    assert(!strcmp(l.url, kept.url) && !strcmp(l.why, kept.why));
  }
  std::string tooLong = "url\thttps://" + std::string(kUrlChars, 'a') + "\n";
  assert(parse(tooLong.c_str(), tooLong.size(), link) == Answer::Bad);
  const char* crlf = "url\thttps://h.example:9/\r\n";
  assert(parse(crlf, strlen(crlf), link) == Answer::Link && !strcmp(link.url, "https://h.example:9/"));
  assert(!strcmp(shown("https://h.example/"), "h.example/") && !strcmp(shown("http://h/"), "h/"));

  // --- Breaking the address ---
  auto six = [](const char*, size_t n) { return int(n) * 6; };
  size_t starts[kMaxLines], lengths[kMaxLines];
  const char* host = "mac.example-tail.ts.net:8443/";
  int n = wrap(host, 60, six, starts, lengths, kMaxLines);
  std::string joined;
  for (int i = 0; i < n; ++i) { assert(int(lengths[i]) * 6 <= 60); joined.append(host + starts[i], lengths[i]); }
  assert(joined == host);
  assert(std::string(host + starts[0], lengths[0]) == "mac.");
  assert(std::string(host + starts[1], lengths[1]) == "example-");
  // A long tailnet name still fits the screen's five lines of 96 pixels.
  const char* longer = "macbook-pro-2.tail1234ab.ts.net:20443/";
  auto eight = [](const char*, size_t k) { return int(k) * 8; };
  assert(wrap(longer, 96, eight, starts, lengths, kMaxLines) <= 5);
  assert(wrap("abc", 0, six, starts, lengths, kMaxLines) == 3);  // A letter always goes.
  assert(wrap(host, 30, six, starts, lengths, 2) == 2);

  // --- Opening asks once the worker is free, and only with Wi-Fi ---
  gBridgePageHaveLink = false;
  gInboxHttpBusy = true;
  beginBridgePage();
  serviceBridgePage();
  assert(gAsked.empty() && gBridgePageView == BridgePageView::Asking);
  gInboxHttpBusy = false;
  gPollAllowed = false;
  serviceBridgePage();
  assert(gAsked.empty());
  gPollAllowed = true;
  serviceBridgePage();
  assert(gAsked.size() == 1 && gAsked[0] == "https://bridge.example.com/api/cardputer/ui/link");
  serviceBridgePage();
  assert(gAsked.size() == 1);  // One question at a time.
  renderBridgePageUi(); inside();
  assert(!qr() && texts().find("Asking the bridge...") != std::string::npos);

  // A link: the QR code at the left, the words beside it, the whole address shown.
  answer(200, "url\thttps://macbook-pro-2.tail1234ab.ts.net:20443/\n");
  assert(gBridgePageView == BridgePageView::Link && gBridgePageHaveLink);
  renderBridgePageUi(); inside();
  {
    std::string all;
    for (const auto& i : gCanvas.items) if (i.kind == "text" && i.y >= 30 && i.y < 112) all += i.text;
    assert(all == "macbook-pro-2.tail1234ab.ts.net:20443/");
  }
  handleBridgePageKey('r', Keyboard_Class::KeysState());
  serviceBridgePage();
  answer(200, "url\thttps://mac.example-tail.ts.net:8443/\n");
  renderBridgePageUi(); inside();
  assert(qr());
  std::string shownAll;
  for (const auto& i : gCanvas.items) {
    if (i.kind == "qr") { assert(i.text == "https://mac.example-tail.ts.net:8443/" && i.x == 0 && i.w == 135); continue; }
    if (i.kind == "text") { assert(i.x >= 142); if (i.y >= 30 && i.y < 112) shownAll += i.text; }
  }
  assert(shownAll == "mac.example-tail.ts.net:8443/");

  // Reopening shows the last link straight away and asks again; a failed answer leaves it up.
  gUiMode = UiMode::Home; gUiMode = UiMode::BridgePage;
  beginBridgePage();
  assert(gBridgePageView == BridgePageView::Link);
  serviceBridgePage();
  assert(gAsked.size() == 3);
  answer(404, "not_found\n");
  assert(gBridgePageView == BridgePageView::Link);
  // R asks again; other keys and Ctrl+R go on as usual.
  Keyboard_Class::KeysState plain, ctrl; ctrl.ctrl = true;
  assert(!handleBridgePageKey('x', plain) && !handleBridgePageKey('r', ctrl));
  assert(handleBridgePageKey('R', plain));
  serviceBridgePage();
  assert(gAsked.size() == 4);

  // No page: the bridge's reason, in up to two lines, and nothing to scan.
  answer(200, "none\tIt's on Funnel: take it off\n");
  assert(gBridgePageView == BridgePageView::None && !gBridgePageHaveLink);
  renderBridgePageUi(); inside();
  assert(!qr() && texts().find("It's on Funnel: take it off") != std::string::npos);
  answer_long:
  handleBridgePageKey('r', plain);
  serviceBridgePage();
  answer(200, "none\tThe page is off on the bridge, and this reason is long\n");
  renderBridgePageUi(); inside();
  // An older bridge, with nothing from before.
  handleBridgePageKey('r', plain);
  serviceBridgePage();
  answer(404, "not_found\n");
  assert(gBridgePageView == BridgePageView::Failed);
  renderBridgePageUi(); inside();
  // The network lost the question: no link, so it says so, and R asks again.
  handleBridgePageKey('r', plain);
  serviceBridgePage();
  answer(-1, "");
  assert(gBridgePageView == BridgePageView::Failed);
  // No Wi-Fi: it waits, says so, and asks once Wi-Fi is back.
  gWifi = 0;
  handleBridgePageKey('r', plain);
  size_t before = gAsked.size();
  serviceBridgePage();
  assert(gAsked.size() == before && gBridgePageView == BridgePageView::NoWifi);
  renderBridgePageUi(); inside();
  gWifi = 0x1400a8c0;
  serviceBridgePage();
  assert(gAsked.size() == before + 1);
  // Away from the screen, nothing is asked.
  answer(200, good);
  gUiMode = UiMode::Home;
  handleBridgePageKey('r', plain);
  serviceBridgePage();
  assert(gAsked.size() == before + 1);
  puts("bridge page ok");
}
'''
code = code.replace('  answer_long:\n', '')
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'test.cpp').write_text(code)
    subprocess.run([cxx, *flags, '-I' + str(src), str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
    run = subprocess.run([str(tmp / 'test')], capture_output=True, text=True)
    assert run.returncode == 0 and 'bridge page ok' in run.stdout, run.stdout + run.stderr[-1500:]
print('PASS Bridge page: the bridge\'s answer, the address in pieces, asking only when free, and every state on screen')
