#pragma once
#include <stddef.h>
#include <stdint.h>

// Listens for drones' Remote ID over Wi-Fi: the radio in promiscuous mode, joined to no network,
// moving between channels. It stays mostly on channel 6, where Wi-Fi Aware's Remote ID is sent,
// and visits the others in turn for drones that send it in beacons, longer where one was heard.
// The Wi-Fi driver's callback only copies frames that could be Remote ID into a small ring; the
// loop reads them with take(). The caller takes Wi-Fi off its network first and gives it back after.
namespace drone_listen {
constexpr size_t kFrameMax = 512;
struct Frame {
  uint16_t length = 0;
  int8_t rssi = 0;
  uint8_t channel = 0;
  uint8_t data[kFrameMax];
};
bool start();                    // False when the ring didn't fit or the driver refused.
void stop();
bool on();
void service(uint32_t nowMs);    // Moves between channels.
void heardOn(uint8_t channel, uint32_t nowMs);  // A drone was heard here: stay longer for a while.
bool take(Frame& out);
uint8_t channel();
uint32_t frames();               // Frames that could be Remote ID, since start.
uint32_t dropped();              // Ones the ring had no room for.
}  // namespace drone_listen
