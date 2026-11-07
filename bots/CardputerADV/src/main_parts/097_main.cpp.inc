// The Lab, live on the bridge's web page. While the Lab is open on the home Wi-Fi, each pass of
// its loop can hand the bridge a report of every reading and output (lab_live.h says what and
// how often), over the bridge's plain-HTTP home door with a ticket. The ticket comes once per
// Wi-Fi address over HTTPS, through the background check's own worker, so the Lab never holds a
// second TLS connection. Away from home, or with an older bridge, nothing is sent. The answer can
// carry the page's buttons, which labPageSet applies as soon as it's read; what became of each goes
// back in the next report, sent at once.
#include "../lab_live.h"
#include "../lab_live_net.h"
namespace {
podcast::HomeTicket gLabLiveTicket;
uint32_t gLabLiveTicketFor = 0;     // The address the ticket was asked from. Another one asks again.
uint32_t gLabLiveRetryMs = 0;       // After an ask the network lost, when to try again.
uint32_t gLabLiveOpenedMs = 0;
bool gLabLiveAsking = false;        // The ticket's ask is out.
bool gLabLiveRetried = false;       // This visit already asked again after the bridge forgot its ticket.
bool gLabLiveSentAny = false;       // This visit has told the bridge something, so it should hear the end.
bool gLabLiveLast = false;          // The report under way is the last one, which nobody waits for.
lab_live::Cadence gLabLiveCadence;
lab_live::Dids gLabLiveDids;        // The page's last buttons, to answer in the next report.

uint32_t labLiveHomeIp() {
  const uint32_t mine = wifiLocalAddress();
  return mine && gLabLiveTicketFor == mine ? podcast::homeFor(gLabLiveTicket, mine) : 0;
}

const char* labLiveView() {
  switch (gLabUi.view) {
    case LabView::Devices: case LabView::PickPort: case LabView::Wiring: return "devices";
    case LabView::Device: return "device";
    case LabView::Dash: return "dashboard";
    case LabView::Play: return "playing";
    case LabView::Rules: case LabView::Wizard: case LabView::Recipes: case LabView::Say: case LabView::Offer: return "rules";
    case LabView::Seqs: case LabView::SeqEdit: case LabView::SeqAsk: case LabView::SeqPlay: case LabView::SeqOffer: return "seqs";
  }
  return "other";
}

void labLiveSnapshot(lab_live::Snapshot& s, uint32_t now) {
  s.view = labLiveView();
  if (gLabUi.view == LabView::Device && gLabUi.devRow >= 0 && gLabUi.devRow < lab::kDevCount) {
    const Dev dev = static_cast<Dev>(gLabUi.devRow);
    s.open = lab::kDevNames[gLabUi.devRow];
    s.page = gLabUi.page;
    if (dev == Dev::Strip) s.stripTest = static_cast<int>(gLabStripTest);
    s.sweeping = dev == Dev::Servo && gLabUi.sweeping;
  } else if (gLabUi.view == LabView::Dash) {
    s.page = gLabUi.dashPage;
  }
  s.us = displayUnits() == units::System::US;
  s.playing = gLabPlaying;
  for (uint8_t i = 0; i < lab::kUnitCount; ++i) s.found[i] = gLabFound[i];
  s.onWires = gLabOnWires;
  s.white = gLabBook.direct.white;
  s.yellow = gLabBook.direct.yellow;
  s.hub = labFound(Unit::Hub);
  for (uint8_t i = 0; i < pbhub::kPorts; ++i) s.ports[i] = gLabBook.wiring.port[i];
  for (uint8_t i = 0; i < lab::kSigCount; ++i) {
    const lab::Reading& reading = gLabSeen.get(static_cast<lab::Sig>(i));
    s.live[i] = reading.live;
    s.value[i] = reading.value;
  }
  for (uint8_t i = 0; i < lab::kOutCount; ++i) {
    s.driven[i] = gLabDrive[i].on;
    s.out[i] = gLabDrive[i].value;
  }
  s.gps = gLabGps.hearing(now);
  if (s.gps) {
    const lab_gps::Fix& fix = gLabGps.fix;
    s.fix = fix.valid;
    s.used = fix.satellites;
    s.inView = gLabGps.inView(now);
    s.hasAltitude = fix.valid && fix.hasAltitude;
    s.altitudeM = static_cast<int>(fix.altitudeDm / 10);
    s.hdopE1 = fix.hdopE1;
  }
  if (gLabPlaying) {
    for (uint8_t i = 0; i < gLabBook.count && i < 8; ++i) if (gLabStates[i].active) s.firing |= static_cast<uint8_t>(1u << i);
  }
  if (gLabSeqRun.running() && gLabSeqRun.index() < gLabSeqCount) s.seq = gLabSeqs[gLabSeqRun.index()].name;
  s.did = gLabLiveDids.list();
  s.dids = gLabLiveDids.count();
}

bool labLiveStart(uint32_t ip, const char* body, size_t length, uint32_t now) {
  char head[192];
  const size_t headLength = lab_live::head(head, sizeof(head), ip, gLabLiveTicket.port, gLabLiveTicket.path, length);
  if (!length || !headLength ||
      !lab_live_net::start(ip, gLabLiveTicket.port, head, headLength, body, length, now, lab_live::kRequestMs)) return false;
  gLabLiveSentAny = true;
  return true;
}

void labLiveTicketAnswered(int code, const String& body) {
  gLabLiveAsking = false;
  if (code <= 0) {  // The network lost it, or a key cancelled it: nothing was said, so ask again soon.
    gLabLiveRetryMs = millis() + 30000;
    return;
  }
  podcast::HomeTicket ticket;
  if (code == 200 && lab_live::parseTicket(body.c_str(), body.length(), ticket)) gLabLiveTicket = ticket;
  else gLabLiveTicket = podcast::HomeTicket{};  // An older bridge, or no home door: nothing goes live.
  gLabLiveTicketFor = wifiLocalAddress();
  gLabLiveCadence.restart();
  logf("[LAB_LIVE] ticket http=%d home=%d", code, gLabLiveTicket.none ? 0 : 1);
}

// Through the background check's worker, only when it's free and the Lab lets the network in.
bool labLiveAskTicket(uint32_t now) {
  const uint32_t mine = wifiLocalAddress();
  if (!mine || gLabLiveTicketFor == mine || gLabLiveAsking || gInboxHttpBusy) return false;
  if (gLabLiveRetryMs && static_cast<int32_t>(now - gLabLiveRetryMs) < 0) return false;
  if (now - gLabLiveOpenedMs < 2000 || !labAllowsNetwork() || !autoInboxPollAllowed()) return false;
  std::unique_ptr<InboxHttpPayload> ask(new (std::nothrow) InboxHttpPayload());
  if (!ask) return false;
  ask->token = gDeviceToken;
  ask->url = hubBaseUrl() + lab_live::kTicketPath;
  ask->done = labLiveTicketAnswered;
  if (!startInboxNetworkRequest(std::move(ask))) return false;
  gLabLiveAsking = true;
  return true;
}

void labLiveOpened() {
  lab_live_net::abort();  // A last report from the visit before has nothing left to say.
  gLabLiveDids.clear();
  gLabLiveLast = gLabLiveSentAny = gLabLiveRetried = false;
  gLabLiveOpenedMs = millis();
  gLabLiveCadence.restart();
}

// The page should hear the Lab closed rather than wait for reports that stop. It goes on its own,
// and nothing waits for it.
void labLiveClosing() {
  const uint32_t ip = labLiveHomeIp();
  const bool told = gLabLiveSentAny;
  lab_live_net::abort();
  gLabLiveSentAny = false;
  if (!ip || !told || gLabLiveCadence.stopped()) return;
  char body[32];
  gLabLiveLast = labLiveStart(ip, body, lab_live::closedBody(body, sizeof(body)), millis());
}

// From the Lab's loop, once its parts are read and driven.
void serviceLabLive(uint32_t now) {
  if (labLiveAskTicket(now) || lab_live_net::busy() || lab_live_net::finished()) return;
  const uint32_t ip = labLiveHomeIp();
  // A check over TLS needs every piece of memory it can get, so a report waits for it to end. The
  // page waiting to hear about its buttons doesn't wait for the cadence.
  const bool answering = gLabLiveDids.count() && !gLabLiveCadence.stopped();
  if (!ip || (!gLabLiveCadence.due(now) && !answering) || gInboxHttpBusy) return;
  gLabLiveCadence.sent(now);
  std::unique_ptr<char[]> body(new (std::nothrow) char[lab_live::kMaxBody + 1]);
  if (!body) return;
  lab_live::Snapshot snapshot;
  labLiveSnapshot(snapshot, now);
  if (!labLiveStart(ip, body.get(), lab_live::body(snapshot, body.get(), lab_live::kMaxBody + 1), now)) gLabLiveCadence.failed();
  else gLabLiveDids.clear();  // Said once.
}

// Every loop, from any screen: moves a report along and reads its answer.
void serviceLabLiveNet() {
  lab_live_net::service(millis());
  if (!lab_live_net::finished()) return;
  size_t length = 0;
  const lab_live::Answer answer = lab_live::readAnswer(lab_live_net::answer(length), length);
  lab_live_net::drop();
  if (gLabLiveLast) {
    gLabLiveLast = false;
    return;
  }
  // A ticket the bridge forgot when it restarted: ask again, once a visit.
  if (answer.status == 404 && !gLabLiveRetried) {
    gLabLiveRetried = true;
    gLabLiveTicketFor = 0;
    gLabLiveRetryMs = 0;
    return;
  }
  gLabLiveCadence.answered(answer);
  if (gLabLiveCadence.stopped()) logf("[LAB_LIVE] stopped after %u failures, last http=%d", unsigned(lab_live::kMaxFailures), answer.status);
  if (answer.status != 200) return;
  // The page's buttons, in order, here in the main loop.
  for (uint8_t i = 0; i < answer.sets; ++i) {
    const lab_live::Set& set = answer.set[i];
    const lab::PageSet result = set.check != lab::PageSet::Done ? set.check : labPageSet(set.out, set.value, millis());
    gLabLiveDids.add(set.id, result);
    if (set.check == lab::PageSet::Done) logf("[LAB_LIVE] page set %s=%d: %s", lab::kOuts[static_cast<uint8_t>(set.out)].key, set.value, lab::kPageSetWords[static_cast<uint8_t>(result)]);
    else logf("[LAB_LIVE] page set #%u: %s", unsigned(set.id), lab::kPageSetWords[static_cast<uint8_t>(result)]);
  }
}
}  // namespace
