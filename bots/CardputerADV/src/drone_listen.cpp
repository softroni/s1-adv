#include "drone_listen.h"
#include "drone_id.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <string.h>

namespace drone_listen {
namespace {
constexpr uint8_t kSlots = 6;                  // About 3 KB, only while listening.
constexpr uint8_t kHome = 6;                   // Wi-Fi Aware's channel.
constexpr uint32_t kHomeMs = 500, kVisitMs = 150, kHotVisitMs = 450, kHotForMs = 60000;
constexpr uint8_t kVisits[] = {1, 11, 2, 7, 3, 9, 4, 10, 5, 8};  // The US channels but 6, spread out.

portMUX_TYPE gate = portMUX_INITIALIZER_UNLOCKED;
Frame* ring = nullptr;
volatile uint8_t head = 0, tail = 0, held = 0;
volatile uint32_t taken = 0, lost = 0;
bool listening = false;
uint8_t current = kHome, visit = 0;
bool atHome = true;
uint32_t switchAtMs = 0;
uint32_t hotUntil[14] = {};

void rx(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT || !buf) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  size_t len = pkt->rx_ctrl.sig_len;
  if (len > 4) len -= 4;  // The frame check sequence.
  const uint8_t* data = pkt->payload;
  if (!drone_id::looksLikeRemoteId(data, len)) return;
  if (len > kFrameMax) len = kFrameMax;
  portENTER_CRITICAL(&gate);
  if (ring && held < kSlots) {
    Frame& f = ring[head];
    f.length = uint16_t(len);
    f.rssi = int8_t(pkt->rx_ctrl.rssi);
    f.channel = uint8_t(pkt->rx_ctrl.channel);
    memcpy(f.data, data, len);
    head = uint8_t((head + 1) % kSlots);
    ++held;
    ++taken;
  } else {
    ++lost;
  }
  portEXIT_CRITICAL(&gate);
}

bool tune(uint8_t ch) {
  if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
  current = ch;
  return true;
}
}  // namespace

bool start() {
  if (listening) return true;
  auto* fresh = static_cast<Frame*>(heap_caps_malloc(sizeof(Frame) * kSlots, MALLOC_CAP_8BIT));
  if (!fresh) return false;
  portENTER_CRITICAL(&gate);
  ring = fresh;
  head = tail = held = 0;
  taken = lost = 0;
  portEXIT_CRITICAL(&gate);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  if (esp_wifi_set_promiscuous_filter(&filter) != ESP_OK || esp_wifi_set_promiscuous_rx_cb(&rx) != ESP_OK ||
      esp_wifi_set_promiscuous(true) != ESP_OK) {
    esp_wifi_set_promiscuous(false);
    portENTER_CRITICAL(&gate);
    ring = nullptr;
    portEXIT_CRITICAL(&gate);
    heap_caps_free(fresh);
    return false;
  }
  listening = true;
  atHome = true;
  visit = 0;
  for (auto& t : hotUntil) t = 0;
  tune(kHome);
  switchAtMs = millis() + kHomeMs;
  return true;
}

void stop() {
  if (!listening) return;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  portENTER_CRITICAL(&gate);
  Frame* old = ring;
  ring = nullptr;
  held = 0;
  portEXIT_CRITICAL(&gate);
  heap_caps_free(old);
  listening = false;
}

bool on() { return listening; }

void service(uint32_t nowMs) {
  if (!listening || int32_t(nowMs - switchAtMs) < 0) return;
  if (atHome) {
    const uint8_t ch = kVisits[visit];
    visit = uint8_t((visit + 1) % sizeof(kVisits));
    atHome = false;
    const bool hot = hotUntil[ch] && int32_t(hotUntil[ch] - nowMs) > 0;
    if (!tune(ch)) { atHome = true; switchAtMs = nowMs + kHomeMs; return; }
    switchAtMs = nowMs + (hot ? kHotVisitMs : kVisitMs);
  } else {
    atHome = true;
    tune(kHome);
    switchAtMs = nowMs + kHomeMs;
  }
}

void heardOn(uint8_t ch, uint32_t nowMs) {
  if (ch && ch < 14 && ch != kHome) hotUntil[ch] = nowMs + kHotForMs;
}

bool take(Frame& out) {
  portENTER_CRITICAL(&gate);
  if (!ring || !held) { portEXIT_CRITICAL(&gate); return false; }
  const Frame& f = ring[tail];
  out.length = f.length;
  out.rssi = f.rssi;
  out.channel = f.channel;
  memcpy(out.data, f.data, f.length);
  tail = uint8_t((tail + 1) % kSlots);
  --held;
  portEXIT_CRITICAL(&gate);
  return true;
}

uint8_t channel() { return current; }
uint32_t frames() { return taken; }
uint32_t dropped() { return lost; }
}  // namespace drone_listen
