#pragma once
// TV Remote: a page for each device it works. The Apple TV by infrared (or as a Bluetooth keyboard,
// when that's built in), a Bose soundbar, an Epson projector and a Samsung TV, all by infrared.
// Everything here is plain data, so the host tests check it byte for byte.
#include <cstddef>
#include <cstdint>

namespace tv_remote {

// The pages, in the order of their tabs and the number keys that open them.
enum class Device : uint8_t { AppleTv, Bose, Epson, Samsung };
constexpr uint8_t kDeviceCount = 4;
constexpr const char* deviceName(Device d) {
  return d == Device::AppleTv ? "Apple TV" : d == Device::Bose ? "Bose" : d == Device::Epson ? "Epson" : "Samsung";
}
// As the bottom line names it: "Up to the Apple TV".
constexpr const char* deviceThe(Device d) {
  return d == Device::AppleTv ? "the Apple TV" : d == Device::Bose ? "the Bose" : d == Device::Epson ? "the projector" : "the TV";
}

// What a key asks a device for.
enum class Action : uint8_t {
  None,
  // A ring: the Apple TV's, the projector's menus, or the Samsung's.
  Up, Down, Left, Right, Select, Back,
  // The Apple TV.
  PlayPause, Home, Sleep,
  // Sound, and power.
  VolumeUp, VolumeDown, Mute, Power,
  // The projector's, and the Samsung's Source and Exit.
  Source, Hdmi1, Hdmi2, Menu, Blank, Exit,
  // The Bose.
  TvInput, BluetoothInput, Codes,
  // The Cardputer as a Bluetooth keyboard.
  Bluetooth, Typing, Forget,
};
struct Press {
  Device device;
  Action action;
  constexpr bool operator==(const Press& o) const { return device == o.device && action == o.action; }
  constexpr bool operator!=(const Press& o) const { return !(*this == o); }
};
constexpr Press kNothing{Device::AppleTv, Action::None};

// The arrows and the volume go again while their key is held.
constexpr bool repeats(Action a) {
  return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right ||
         a == Action::VolumeUp || a == Action::VolumeDown;
}

// What a key does on each page. The arrows are ; . , / with or without Fn, as everywhere on the
// device, and 1 to 4 are a USB keyboard's arrows. The number keys that change page, and Esc, which
// leaves, are the app's and never get here.
constexpr Action ringKey(char key, bool enter, bool del) {
  if (enter || key == '\n' || key == '\r') return Action::Select;
  if (del || key == '\b' || key == 127) return Action::Back;
  switch (key) {
    case ';': case ':': case 1: return Action::Up;
    case '.': case '>': case 2: return Action::Down;
    case ',': case '<': case 3: return Action::Left;
    case '/': case '?': case 4: return Action::Right;
    default: return Action::None;
  }
}
constexpr Action soundKey(char key) {
  switch (key) {
    case '=': case '+': return Action::VolumeUp;
    case '-': case '_': return Action::VolumeDown;
    case 'm': case 'M': return Action::Mute;
    case 'p': case 'P': return Action::Power;
    default: return Action::None;
  }
}
constexpr Press keyFor(Device page, char key, bool enter, bool del) {
  switch (page) {
    case Device::AppleTv: {
      // The Apple TV's sound goes through the Bose, so its volume keys are the Bose's.
      if (const Action ring = ringKey(key, enter, del); ring != Action::None) return {Device::AppleTv, ring};
      if (const Action sound = soundKey(key); sound != Action::None) return {Device::Bose, sound};
      switch (key) {
        case ' ': return {Device::AppleTv, Action::PlayPause};
        case 'h': case 'H': return {Device::AppleTv, Action::Home};
        case 'z': case 'Z': return {Device::AppleTv, Action::Sleep};
        case 'b': return {Device::AppleTv, Action::Bluetooth};
        case 'B': return {Device::AppleTv, Action::Forget};  // Shift+B, twice, so it can't happen by accident.
        case 't': case 'T': return {Device::AppleTv, Action::Typing};
        default: return {Device::AppleTv, Action::None};
      }
    }
    case Device::Bose:
      if (const Action sound = soundKey(key); sound != Action::None) return {Device::Bose, sound};
      switch (key) {
        case 't': case 'T': return {Device::Bose, Action::TvInput};
        case 'b': case 'B': return {Device::Bose, Action::BluetoothInput};
        case 'c': case 'C': return {Device::Bose, Action::Codes};
        default: return {Device::Bose, Action::None};
      }
    case Device::Epson:
      if (const Action ring = ringKey(key, enter, del); ring != Action::None) return {Device::Epson, ring};
      // Its own remote's volume keys: with its sound going out over HDMI, they turn that up and down.
      if (key == '=' || key == '+') return {Device::Epson, Action::VolumeUp};
      if (key == '-' || key == '_') return {Device::Epson, Action::VolumeDown};
      switch (key) {
        case 'p': case 'P': return {Device::Epson, Action::Power};
        case 'i': case 'I': return {Device::Epson, Action::Hdmi1};
        case 'o': case 'O': return {Device::Epson, Action::Hdmi2};
        case 'm': case 'M': return {Device::Epson, Action::Menu};
        case 'b': case 'B': return {Device::Epson, Action::Blank};
        default: return {Device::Epson, Action::None};
      }
    default:
      if (const Action ring = ringKey(key, enter, del); ring != Action::None) return {Device::Samsung, ring};
      if (const Action sound = soundKey(key); sound != Action::None) return {Device::Samsung, sound};
      switch (key) {
        case 'h': case 'H': return {Device::Samsung, Action::Home};
        case 's': case 'S': return {Device::Samsung, Action::Source};
        case 'e': case 'E': return {Device::Samsung, Action::Exit};
        default: return {Device::Samsung, Action::None};
      }
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
    case Action::Power: return "Power";
    case Action::Source: return "Source";
    case Action::Hdmi1: return "HDMI 1";
    case Action::Hdmi2: return "HDMI 2";
    case Action::Menu: return "Menu";
    case Action::Blank: return "Blank";
    case Action::Exit: return "Exit";
    case Action::TvInput: return "TV input";
    case Action::BluetoothInput: return "Bluetooth input";
    case Action::Codes: return "Codes";
    case Action::Bluetooth: return "Bluetooth";
    case Action::Typing: return "Typing";
    case Action::Forget: return "Forget";
    default: return "";
  }
}

// --- Infrared ---------------------------------------------------------------------------------
// NEC frames are four bytes as Arduino-IRremote's sendNECRaw takes them, the first sent in the
// lowest byte. Every code is from the CC0 Flipper-IRDB, recorded from each maker's own remote:
// - Apple TV: the Siri Remote's, from an Apple TV 4K (A2169), Streaming_Devices/Apple/
//   Apple_TV_4K_Gen2.ir. The arrows, OK, Back and Play/Pause start with Apple's EE 87 and end
//   with 8C; Home and Sleep start E5 87 and end FE. In the EE 87 frames the command's lowest bit
//   is a parity bit: the ones in the command and the last byte make an odd count, which the tests
//   check, so a typo can't hide.
// - Epson: one family across every Epson file there, address 83 55, from the EH-TW5650 home
//   cinema projector's remote (Projectors/Epson/Epson_EHTW5650.ir) and three others that agree.
// - Bose: two remotes with the same buttons in different formats. The Smart Soundbar 600's
//   (SoundBars/Bose/Bose_Soundbar_600.ir) uses Bose's own timing, a 1 ms header and the command
//   then its inverse, eight bits each, first bit first, sent twice 50 ms apart. The Soundbar
//   300's (Bose_Soundbar_300.ir) is NEC with address BA 4B. Which another Bose soundbar takes
//   isn't known, so C on its page switches between them.
// - Samsung: Samsung32 at address 07. The volume, mute and power Samsung TVs have used for years,
//   and the arrows, OK, Return, Smart Hub, Source and Exit as three files under TVs/Samsung have
//   them alike (Samsung_UN60JU6500.ir, Samsung_UE48JU6490U.ir, Samsung_TV_Full.ir).
enum class IrProtocol : uint8_t { None, Nec, Samsung, Bose };

struct IrCode {
  IrProtocol protocol;
  uint32_t nec;      // NEC: the whole frame.
  uint8_t address;   // Samsung32.
  uint8_t command;   // Samsung32 and Bose.
};

constexpr uint32_t necFrame(uint8_t first, uint8_t second, uint8_t command) {
  return uint32_t(first) | (uint32_t(second) << 8) | (uint32_t(command) << 16) | (uint32_t(uint8_t(~command)) << 24);
}
constexpr uint32_t appleFrame(uint8_t command) { return 0x8C000000UL | (uint32_t(command) << 16) | 0x87EEUL; }
constexpr uint32_t appleTvButton(uint8_t command) { return 0xFE000000UL | (uint32_t(command) << 16) | 0x87E5UL; }
constexpr IrCode nec(uint32_t frame) { return {IrProtocol::Nec, frame, 0, 0}; }
constexpr IrCode epson(uint8_t command) { return nec(necFrame(0x83, 0x55, command)); }
constexpr uint8_t kSamsungTv = 0x07;
constexpr IrCode samsung(uint8_t command) { return {IrProtocol::Samsung, 0, kSamsungTv, command}; }
constexpr uint8_t kBoseCodeSets = 2;  // 0: the Smart Soundbar 600's remote, 1: the Soundbar 300's.
constexpr IrCode bose(uint8_t set, uint8_t smart600, uint8_t soundbar300) {
  return set == 0 ? IrCode{IrProtocol::Bose, 0, 0, smart600} : nec(necFrame(0xBA, 0x4B, soundbar300));
}
constexpr IrCode kNoCode{IrProtocol::None, 0, 0, 0};

constexpr IrCode irCode(Press p, uint8_t boseSet = 0) {
  switch (p.device) {
    case Device::AppleTv:
      switch (p.action) {
        case Action::Up: return nec(appleFrame(0x0A));
        case Action::Down: return nec(appleFrame(0x0C));
        case Action::Left: return nec(appleFrame(0x09));
        case Action::Right: return nec(appleFrame(0x06));
        case Action::Select: return nec(appleFrame(0x5C));
        case Action::Back: return nec(appleFrame(0x03));
        case Action::PlayPause: return nec(appleFrame(0x5F));
        case Action::Home: return nec(appleTvButton(0x40));
        case Action::Sleep: return nec(appleTvButton(0x2A));
        default: return kNoCode;
      }
    case Device::Bose:
      switch (p.action) {
        case Action::Power: return bose(boseSet, 0x32, 0x4C);
        case Action::VolumeUp: return bose(boseSet, 0xC0, 0x03);
        case Action::VolumeDown: return bose(boseSet, 0x40, 0x02);
        case Action::Mute: return bose(boseSet, 0x80, 0x01);
        case Action::TvInput: return bose(boseSet, 0x70, 0x0E);
        case Action::BluetoothInput: return bose(boseSet, 0x81, 0x81);
        default: return kNoCode;
      }
    case Device::Epson:
      switch (p.action) {
        case Action::Power: return epson(0x90);
        case Action::Hdmi1: return epson(0x73);
        case Action::Hdmi2: return epson(0x77);
        case Action::Menu: return epson(0x9A);
        case Action::Blank: return epson(0x93);
        case Action::VolumeUp: return epson(0x98);
        case Action::VolumeDown: return epson(0x99);
        case Action::Up: return epson(0xB0);
        case Action::Down: return epson(0xB2);
        case Action::Left: return epson(0xB3);
        case Action::Right: return epson(0xB1);
        case Action::Select: return epson(0x85);
        case Action::Back: return epson(0x84);
        default: return kNoCode;
      }
    default:
      switch (p.action) {
        case Action::VolumeUp: return samsung(0x07);
        case Action::VolumeDown: return samsung(0x0B);
        case Action::Mute: return samsung(0x0F);
        case Action::Power: return samsung(0x02);
        case Action::Up: return samsung(0x60);
        case Action::Down: return samsung(0x61);
        case Action::Left: return samsung(0x65);
        case Action::Right: return samsung(0x62);
        case Action::Select: return samsung(0x68);
        case Action::Back: return samsung(0x58);
        case Action::Home: return samsung(0x79);  // Smart Hub.
        case Action::Source: return samsung(0x01);
        case Action::Exit: return samsung(0x2D);
        default: return kNoCode;
      }
  }
}

// The 32 bits a Samsung32 frame carries, in the same order as an NEC one, for the tests and the log.
constexpr uint32_t samsungFrame(uint8_t address, uint8_t command) {
  return uint32_t(address) | (uint32_t(address) << 8) | (uint32_t(command) << 16) |
         (uint32_t(uint8_t(~command)) << 24);
}

// A Bose frame as the Smart Soundbar 600's remote sends it, for IRremote's sendRaw at 38 kHz:
// microseconds, mark first. The header, the command's eight bits then their inverse, first bit
// first, a closing mark, and the same again after a 50 ms gap.
constexpr uint16_t kBoseHeaderMark = 970, kBoseHeaderSpace = 1510, kBoseMark = 480, kBoseZero = 515,
                   kBoseOne = 1513, kBoseGap = 50680;
constexpr size_t kBoseFrame = 2 + 16 * 2 + 1, kBoseRaw = kBoseFrame * 2 + 1;
inline size_t boseRaw(uint8_t command, uint16_t (&out)[kBoseRaw]) {
  size_t n = 0;
  const uint16_t bits = uint16_t(command | (uint16_t(uint8_t(~command)) << 8));
  for (int frame = 0; frame < 2; ++frame) {
    if (frame) out[n++] = kBoseGap;
    out[n++] = kBoseHeaderMark;
    out[n++] = kBoseHeaderSpace;
    for (int i = 0; i < 16; ++i) {
      out[n++] = kBoseMark;
      out[n++] = (bits >> i) & 1 ? kBoseOne : kBoseZero;
    }
    out[n++] = kBoseMark;
  }
  return n;
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

// The Apple TV's keys as a keyboard: what it takes from one. Esc is its Back, and Play/Pause,
// which no key has, goes as a media key. Nothing else goes by Bluetooth.
struct BluetoothSend {
  uint8_t key;     // A keyboard usage, or 0.
  uint16_t media;  // A consumer usage, or 0.
};
constexpr BluetoothSend bluetoothFor(Press p) {
  if (p.device != Device::AppleTv) return {0, 0};
  switch (p.action) {
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
