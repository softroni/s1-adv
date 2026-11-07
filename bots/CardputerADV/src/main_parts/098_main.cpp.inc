// System > Bridge page (UiMode::BridgePage): the bridge's web page, as a QR code a phone scans and
// its address, for when you've forgotten where it is. The address is asked from the bridge each
// time the screen opens (bridge_page.h), through the background check's worker, so it never goes
// in the firmware and follows wherever the bridge's computer shares the page. docs/BRIDGE-UI.md.
#include "../bridge_page.h"

namespace {
enum class BridgePageView : uint8_t { Asking, Link, None, Failed, NoWifi };
BridgePageView gBridgePageView = BridgePageView::Asking;
bridge_page::Link gBridgePageLink;  // The last answer, kept while the screen's closed.
bool gBridgePageHaveLink = false;   // gBridgePageLink.url is a link the bridge gave since start-up.
bool gBridgePageWant = false;       // Ask as soon as the worker is free.
bool gBridgePageAsking = false;     // The question is out.

uint16_t bridgePageAccent() { return rgb565_local(180, 150, 255); }  // The System tab's.
uint16_t bridgePageMuted() { return rgb565_local(146, 146, 170); }
uint16_t bridgePageAmber() { return rgb565_local(255, 196, 64); }

void bridgePageRedraw() {
  gUiFramePolicy.invalidate();
  gLastRenderMs = 0;
}

void bridgePageAnswered(int code, const String& body) {
  gBridgePageAsking = false;
  bridge_page::Link link = gBridgePageLink;
  const bridge_page::Answer answer = code == 200 ? bridge_page::parse(body.c_str(), body.length(), link) : bridge_page::Answer::Bad;
  if (answer == bridge_page::Answer::Link) {
    gBridgePageLink = link;
    gBridgePageHaveLink = true;
    gBridgePageView = BridgePageView::Link;
  } else if (answer == bridge_page::Answer::None) {
    gBridgePageLink = link;
    gBridgePageHaveLink = false;
    gBridgePageView = BridgePageView::None;
  } else {
    // A bridge from before the page answers 404. With an answer from earlier, that stays up.
    gBridgePageView = gBridgePageHaveLink ? BridgePageView::Link : BridgePageView::Failed;
  }
  logf("[BRIDGE_PAGE] http=%d answer=%d", code, static_cast<int>(answer));
  bridgePageRedraw();
}

void beginBridgePage() {
  gBridgePageWant = true;
  gBridgePageView = gBridgePageHaveLink ? BridgePageView::Link : BridgePageView::Asking;
}

void serviceBridgePage() {
  if (gUiMode != UiMode::BridgePage || !gBridgePageWant || gBridgePageAsking || gInboxHttpBusy) return;
  if (!wifiLocalAddress()) {
    if (!gBridgePageHaveLink && gBridgePageView != BridgePageView::NoWifi) {
      gBridgePageView = BridgePageView::NoWifi;
      bridgePageRedraw();
    }
    return;
  }
  if (!autoInboxPollAllowed()) return;
  std::unique_ptr<InboxHttpPayload> ask(new (std::nothrow) InboxHttpPayload());
  if (!ask) return;
  ask->token = gDeviceToken;
  ask->url = hubBaseUrl() + bridge_page::kPath;
  ask->done = bridgePageAnswered;
  if (!startInboxNetworkRequest(std::move(ask))) return;
  gBridgePageWant = false;
  gBridgePageAsking = true;
  if (!gBridgePageHaveLink && gBridgePageView != BridgePageView::Asking) {
    gBridgePageView = BridgePageView::Asking;
    bridgePageRedraw();
  }
}

// R asks again. Everything else, Esc included, goes on as on any screen.
bool handleBridgePageKey(char key, const Keyboard_Class::KeysState& keys) {
  if (keys.ctrl || keys.alt || tolower(static_cast<unsigned char>(key)) != 'r') return false;
  gBridgePageWant = true;
  if (!gBridgePageHaveLink) gBridgePageView = BridgePageView::Asking;
  bridgePageRedraw();
  return true;
}

// The QR code on the white it needs, the screen's height, at the left, as Cameras' sign-in has it;
// the address and what to do at the right. Without a link, the words take the screen.
void renderBridgePageUi() {
  auto& d = gCanvas;
  d.fillScreen(TFT_BLACK);
  const bool link = gBridgePageView == BridgePageView::Link && gBridgePageHaveLink;
  int16_t x = 8;
  constexpr int16_t kRight = 238;
  if (link) {
    d.fillRect(0, 0, 135, 135, TFT_WHITE);
    d.qrcode(gBridgePageLink.url, 0, 0, 135, 1, true);
    x = 142;
  }
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(bridgePageAccent());
  d.setCursor(x, 6);
  d.print(link ? "Bridge" : "Bridge page");
  d.setFont(&fonts::Font2);
  if (link) {
    // The address without https://, in pieces that fit, the way a person reads it out: five lines
    // of about 13 letters take any tailnet name.
    const char* shown = bridge_page::shown(gBridgePageLink.url);
    size_t starts[bridge_page::kMaxLines], lengths[bridge_page::kMaxLines];
    const int lines = bridge_page::wrap(shown, kRight - x, [&](const char* text, size_t n) {
      char piece[bridge_page::kUrlChars];
      memcpy(piece, text, n);
      piece[n] = 0;
      return static_cast<int>(d.textWidth(piece));
    }, starts, lengths, 5);
    d.setTextColor(bridgePageAccent());
    for (int i = 0; i < lines; ++i) {
      char piece[bridge_page::kUrlChars];
      memcpy(piece, shown + starts[i], lengths[i]);
      piece[lengths[i]] = 0;
      d.setCursor(x, 30 + i * 16);
      d.print(piece);
    }
    d.setFont(&fonts::Font0);
    d.setTextColor(bridgePageMuted());
    d.setCursor(x, 116);
    d.print("Tailscale on");
    d.setCursor(x, 126);
    d.print(gBridgePageAsking ? "Checking..." : "R checks again");
    return;
  }
  const char* first = "Asking the bridge...";
  const char* second = "";
  uint16_t ink = bridgePageMuted();
  switch (gBridgePageView) {
    case BridgePageView::None: first = gBridgePageLink.why; second = "docs/BRIDGE-UI.md says how"; ink = bridgePageAmber(); break;
    case BridgePageView::Failed: first = "The bridge didn't say"; second = "Its page needs a newer bridge"; ink = bridgePageAmber(); break;
    case BridgePageView::NoWifi: first = "No Wi-Fi"; second = "It asks once Wi-Fi is back"; ink = bridgePageAmber(); break;
    default: break;
  }
  // The bridge's reason can run to two lines.
  size_t starts[2], lengths[2];
  const int lines = bridge_page::wrap(first, 224, [&](const char* text, size_t n) {
    char piece[bridge_page::kWhyChars];
    const size_t take = n < sizeof(piece) - 1 ? n : sizeof(piece) - 1;
    memcpy(piece, text, take);
    piece[take] = 0;
    return static_cast<int>(d.textWidth(piece));
  }, starts, lengths, 2);
  d.setTextColor(ink);
  for (int i = 0; i < lines; ++i) {
    char piece[bridge_page::kWhyChars];
    const size_t take = lengths[i] < sizeof(piece) - 1 ? lengths[i] : sizeof(piece) - 1;
    memcpy(piece, first + starts[i], take);
    piece[take] = 0;
    d.drawCenterString(piece, 120, 42 + i * 18 - (lines > 1 ? 9 : 0) + 8);
  }
  d.setTextColor(bridgePageMuted());
  if (*second) d.drawCenterString(second, 120, 78);
  d.setFont(&fonts::Font0);
  d.drawCenterString("R asks again  Esc back", 120, 122);
}
}  // namespace
