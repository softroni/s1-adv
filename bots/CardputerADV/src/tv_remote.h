#pragma once
// TV Remote: an Apple TV by infrared or as a Bluetooth keyboard, and a Samsung TV's volume and
// power by infrared. Everything here is plain data, so the host tests check it byte for byte.
#include <cstddef>
#include <cstdint>

namespace tv_remote {

// What a key asks for, whichever way it's sent.
enum class Action : uint8_t {
  None,
  // The Apple TV.
  Up, Down, Left, Right, Select, Back, PlayPause, Home, Sleep,
  // The TV it's plugged into.
  VolumeUp, VolumeDown, Mute, TvPower,
  // This app.
  Bluetooth, Typing, Forget,
};

// The Apple TV's arrows, and the TV's volume, go again while their key is held.
constexpr bool repeats(Action a) {
  return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right ||
         a == Action::VolumeUp || a == Action::VolumeDown;
}
// What goes to the TV itself always goes by infrared: Bluetooth only reaches the Apple TV.
constexpr bool forTv(Action a) {
  return a == Action::VolumeUp || a == Action::VolumeDown || a == Action::Mute || a == Action::TvPower;
}

// The remote screen's keys. The arrows are ; . , / with or without Fn, as everywhere on the
// device, and 1 to 4 are a USB keyboard's arrows. Esc never gets here: it leaves the app.
constexpr Action actionForKey(char key, bool enter, bool del) {
  if (enter || key == '\n' || key == '\r') return Action::Select;
  if (del || key == '\b' || key == 127) return Action::Back;
  switch (key) {
    case ';': case ':': case 1: return Action::Up;
    case '.': case '>': case 2: return Action::Down;
    case ',': case '<': case 3: return Action::Left;
    case '/': case '?': case 4: return Action::Right;
    case ' ': return Action::PlayPause;
    case 'h': case 'H': return Action::Home;
    case 'z': case 'Z': return Action::Sleep;
    case '=': case '+': return Action::VolumeUp;
    case '-': case '_': return Action::VolumeDown;
    case 'm': case 'M': return Action::Mute;
    case 'p': case 'P': return Action::TvPower;
    case 'b': return Action::Bluetooth;
    case 'B': return Action::Forget;  // Shift+B, twice, so it can't happen by accident.
    case 't': case 'T': return Action::Typing;
    default: return Action::None;
  }
}

constexpr const char* actionName(Action a) {
  switch (a) {
    case Action::Up: return "Up";
    case Action::Down: return "Down";
    case Action::Left: return "Left";
    case Action::Right: return "Right";
    case Action::Select: return "OK";
    case Action::Back: return "Back";
    case Action::PlayPause: return "Play/Pause";
    case Action::Home: return "Home";
    case Action::Sleep: return "Sleep";
    case Action::VolumeUp: return "Volume up";
    case Action::VolumeDown: return "Volume down";
    case Action::Mute: return "Mute";
    case Action::TvPower: return "TV power";
    case Action::Bluetooth: return "Bluetooth";
    case Action::Typing: return "Typing";
    case Action::Forget: return "Forget";
    default: return "";
  }
}

// --- Infrared ---------------------------------------------------------------------------------
// Apple TV codes are NEC frames as Arduino-IRremote's sendNECRaw takes them: four bytes, the
// first sent in the lowest byte. They're the Siri Remote's own, recorded from an Apple TV 4K
// (A2169) in the CC0 Flipper-IRDB (Streaming_Devices/Apple/Apple_TV_4K_Gen2.ir). The arrows,
// OK, Back and Play/Pause start with Apple's EE 87 and end with 8C; Home and Sleep start E5 87
// and end FE. In the EE 87 frames, the command's lowest bit is a parity bit: with it, the ones in
// the command and the last byte make an odd count. The tests check that, so a typo can't hide.
// The TV's codes are Samsung32: address 07 and the command, each sent with its own copy or
// inverse, the volume, mute and power that Samsung TVs have used for years.
enum class IrProtocol : uint8_t { None, Nec, Samsung };

struct IrCode {
  IrProtocol protocol;
  uint32_t nec;      // NEC: the whole frame.
  uint8_t address;   // Samsung32.
  uint8_t command;   // Samsung32.
};

constexpr uint32_t appleFrame(uint8_t command) { return 0x8C000000UL | (uint32_t(command) << 16) | 0x87EEUL; }
constexpr uint32_t appleTvButton(uint8_t command) { return 0xFE000000UL | (uint32_t(command) << 16) | 0x87E5UL; }
constexpr uint8_t kSamsungTv = 0x07;

constexpr IrCode irCode(Action a) {
  switch (a) {
    case Action::Up: return {IrProtocol::Nec, appleFrame(0x0A), 0, 0};
    case Action::Down: return {IrProtocol::Nec, appleFrame(0x0C), 0, 0};
    case Action::Left: return {IrProtocol::Nec, appleFrame(0x09), 0, 0};
    case Action::Right: return {IrProtocol::Nec, appleFrame(0x06), 0, 0};
    case Action::Select: return {IrProtocol::Nec, appleFrame(0x5C), 0, 0};
    case Action::Back: return {IrProtocol::Nec, appleFrame(0x03), 0, 0};
    case Action::PlayPause: return {IrProtocol::Nec, appleFrame(0x5F), 0, 0};
    case Action::Home: return {IrProtocol::Nec, appleTvButton(0x40), 0, 0};
    case Action::Sleep: return {IrProtocol::Nec, appleTvButton(0x2A), 0, 0};
    case Action::VolumeUp: return {IrProtocol::Samsung, 0, kSamsungTv, 0x07};
    case Action::VolumeDown: return {IrProtocol::Samsung, 0, kSamsungTv, 0x0B};
    case Action::Mute: return {IrProtocol::Samsung, 0, kSamsungTv, 0x0F};
    case Action::TvPower: return {IrProtocol::Samsung, 0, kSamsungTv, 0x02};
    default: return {IrProtocol::None, 0, 0, 0};
  }
}

// The 32 bits a Samsung32 frame carries, in the same order as an NEC one, for the tests and the log.
constexpr uint32_t samsungFrame(uint8_t address, uint8_t command) {
  return uint32_t(address) | (uint32_t(address) << 8) | (uint32_t(command) << 16) |
         (uint32_t(uint8_t(~command)) << 24);
}

// --- Bluetooth keyboard -----------------------------------------------------------------------
// HID usages, page 7 (keyboard) and page 12 (consumer).
constexpr uint8_t kKeyEnter = 0x28, kKeyEscape = 0x29, kKeyBackspace = 0x2A, kKeyTab = 0x2B, kKeySpace = 0x2C;
constexpr uint8_t kKeyRight = 0x4F, kKeyLeft = 0x50, kKeyDown = 0x51, kKeyUp = 0x52;
constexpr uint8_t kShift = 0x02;  // Left Shift in the report's modifier byte.
constexpr uint16_t kMediaPlayPause = 0x00CD, kMediaHome = 0x0223;

// Two reports: a keyboard (ID 1, with its lights' report the host may write) and a consumer
// control (ID 2) for Play/Pause, which a keyboard has no key for.
constexpr uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,  // Generic Desktop, Keyboard, collection, ID 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,  // modifiers
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,  // reserved byte
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,  // five lights
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,  // their padding
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,  // six keys
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,  // Consumer, Consumer Control, collection, ID 2
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,  // one usage
    0xC0,
};

struct HidKey {
  uint8_t usage;  // 0: no key for it.
  uint8_t modifiers;
};

// What a US keyboard presses for a character. Anything else, like a letter with an accent, isn't
// sent, because the host reads usages as positions on its own layout.
constexpr HidKey hidForChar(char c) {
  if (c >= 'a' && c <= 'z') return {uint8_t(0x04 + (c - 'a')), 0};
  if (c >= 'A' && c <= 'Z') return {uint8_t(0x04 + (c - 'A')), kShift};
  if (c >= '1' && c <= '9') return {uint8_t(0x1E + (c - '1')), 0};
  switch (c) {
    case '0': return {0x27, 0};
    case '!': return {0x1E, kShift};
    case '@': return {0x1F, kShift};
    case '#': return {0x20, kShift};
    case '$': return {0x21, kShift};
    case '%': return {0x22, kShift};
    case '^': return {0x23, kShift};
    case '&': return {0x24, kShift};
    case '*': return {0x25, kShift};
    case '(': return {0x26, kShift};
    case ')': return {0x27, kShift};
    case '\n': case '\r': return {kKeyEnter, 0};
    case '\b': return {kKeyBackspace, 0};
    case '\t': return {kKeyTab, 0};
    case ' ': return {kKeySpace, 0};
    case '-': return {0x2D, 0};
    case '_': return {0x2D, kShift};
    case '=': return {0x2E, 0};
    case '+': return {0x2E, kShift};
    case '[': return {0x2F, 0};
    case '{': return {0x2F, kShift};
    case ']': return {0x30, 0};
    case '}': return {0x30, kShift};
    case '\\': return {0x31, 0};
    case '|': return {0x31, kShift};
    case ';': return {0x33, 0};
    case ':': return {0x33, kShift};
    case '\'': return {0x34, 0};
    case '"': return {0x34, kShift};
    case '`': return {0x35, 0};
    case '~': return {0x35, kShift};
    case ',': return {0x36, 0};
    case '<': return {0x36, kShift};
    case '.': return {0x37, 0};
    case '>': return {0x37, kShift};
    case '/': return {0x38, 0};
    case '?': return {0x38, kShift};
    default: return {0, 0};
  }
}

// The remote's keys as a keyboard: what the Apple TV takes from one. Esc is its Back, and
// Play/Pause, which no key has, goes as a media key.
struct BluetoothSend {
  uint8_t key;     // A keyboard usage, or 0.
  uint16_t media;  // A consumer usage, or 0.
};
constexpr BluetoothSend bluetoothFor(Action a) {
  switch (a) {
    case Action::Up: return {kKeyUp, 0};
    case Action::Down: return {kKeyDown, 0};
    case Action::Left: return {kKeyLeft, 0};
    case Action::Right: return {kKeyRight, 0};
    case Action::Select: return {kKeyEnter, 0};
    case Action::Back: return {kKeyEscape, 0};
    case Action::PlayPause: return {0, kMediaPlayPause};
    default: return {0, 0};
  }
}

// The typing screen: Fn with an arrow key moves, so the Apple TV's own keyboard and its fields can
// be reached without leaving; everything else is typed.
constexpr uint8_t typingArrow(char key, bool fn) {
  if (key >= 1 && key <= 4) return key == 1 ? kKeyUp : key == 2 ? kKeyDown : key == 3 ? kKeyLeft : kKeyRight;
  if (!fn) return 0;
  switch (key) {
    case ';': return kKeyUp;
    case '.': return kKeyDown;
    case ',': return kKeyLeft;
    case '/': return kKeyRight;
    default: return 0;
  }
}

}  // namespace tv_remote
