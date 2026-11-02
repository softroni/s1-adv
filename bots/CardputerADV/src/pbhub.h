#pragma once
// The M5Stack I/O Hub (Unit PbHub): six Grove ports behind one I2C address. Each port has two
// pins, and the hub's own chip makes the servo pulses and LED data, so the Cardputer only sends
// short commands. This builds those commands, and the bus is handed in, so it runs on the host.
//
// Bus needs:
//   bool send(uint8_t address, const uint8_t* bytes, uint8_t length);
//   bool fetch(uint8_t address, uint8_t reg, uint8_t* out, uint8_t length);
//
// The numbers below come from the hub's firmware (M5Unit-PbHub-Internal-FW), which is more exact
// than its documentation. On a port, pin 0 is the input side, which is the white wire on a Port B
// unit, and pin 1 is the output side, the yellow wire. Only pin 0 reads analog, and LED data
// always leaves on pin 1.
#include <stdint.h>

namespace pbhub {
constexpr uint8_t kAddress = 0x61;
constexpr uint8_t kPorts = 6;
constexpr uint8_t kInputPin = 0, kOutputPin = 1;  // White, then yellow.
constexpr uint8_t kMaxAngle = 180;
constexpr uint16_t kMaxLeds = 74;

// The sixth port skips 0x90.
constexpr uint8_t channel(uint8_t port) { return port == 5 ? 0xA0 : static_cast<uint8_t>((port + 4) << 4); }

enum Command : uint8_t {
  kDigitalWrite = 0x00,  // +pin. Stops any servo pulses on that pin, which lets the servo go limp.
  kDigitalRead = 0x04,   // +pin
  kAnalogRead = 0x06,    // Pin 0 only. Two bytes, low first.
  kLedCount = 0x08,
  kOneLed = 0x09,
  kFillLeds = 0x0A,
  kServoAngle = 0x0C,    // +pin
};
constexpr uint8_t kShowMode = 0xFA, kFirmware = 0xFE;

template <class Bus>
class Hub {
 public:
  explicit Hub(Bus& bus) : mBus(bus) {}

  bool digitalWrite(uint8_t port, uint8_t pin, bool high) {
    return put(command(port, kDigitalWrite + pin), high ? 1 : 0);
  }
  bool digitalRead(uint8_t port, uint8_t pin, bool& high) {
    uint8_t value = 0;
    if (!mBus.fetch(kAddress, command(port, kDigitalRead + pin), &value, 1)) return false;
    high = value != 0;
    return true;
  }
  bool analogRead(uint8_t port, uint16_t& value) {
    uint8_t bytes[2] = {};
    if (!mBus.fetch(kAddress, command(port, kAnalogRead), bytes, 2)) return false;
    value = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    return true;
  }
  // The hub ignores an angle above 180, so a bad value would leave the servo where it is.
  bool servo(uint8_t port, uint8_t pin, uint8_t degrees) {
    return put(command(port, kServoAngle + pin), degrees > kMaxAngle ? kMaxAngle : degrees);
  }
  bool ledCount(uint8_t port, uint16_t count) {
    const uint8_t bytes[3] = {command(port, kLedCount), low8(count), high8(count)};
    return mBus.send(kAddress, bytes, 3);
  }
  bool oneLed(uint8_t port, uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t bytes[6] = {command(port, kOneLed), low8(index), high8(index), r, g, b};
    return mBus.send(kAddress, bytes, 6);
  }
  // Colors `count` LEDs from `start`. The hub only refreshes LEDs up to the end of this run, or
  // the whole strip when `count` reaches its length, so a part-lit strip takes two fills.
  bool fillLeds(uint8_t port, uint16_t start, uint16_t count, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t bytes[8] = {command(port, kFillLeds), low8(start), high8(start),
                              low8(count), high8(count), r, g, b};
    return mBus.send(kAddress, bytes, 8);
  }
  // Mode 0 is the default LED timing, and mode 1 is the hub's other one.
  bool showMode(uint8_t mode) { return put(kShowMode, mode ? 1 : 0); }
  bool firmware(uint8_t& version) { return mBus.fetch(kAddress, kFirmware, &version, 1); }

 private:
  static constexpr uint8_t low8(uint16_t value) { return static_cast<uint8_t>(value & 0xFF); }
  static constexpr uint8_t high8(uint16_t value) { return static_cast<uint8_t>(value >> 8); }
  static uint8_t command(uint8_t port, uint8_t what) { return static_cast<uint8_t>(channel(port) | what); }
  bool put(uint8_t reg, uint8_t value) {
    const uint8_t bytes[2] = {reg, value};
    return mBus.send(kAddress, bytes, 2);
  }
  Bus& mBus;
};
}  // namespace pbhub
