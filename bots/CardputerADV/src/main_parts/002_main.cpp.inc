#ifndef DEFAULT_DEVICE_TOKEN
#define DEFAULT_DEVICE_TOKEN ""
#endif

enum class LineKind : uint8_t {
  System,
  User,
  Assistant,
  Error,
};

enum class FaceMode : uint8_t {
  Idle,
  Listening,
  Thinking,
  Speaking,
  Error,
};

enum class KeyboardLayout : uint8_t {
  En,
  Ru,
};

enum class UiMode : uint8_t {
  Home,
  Face,
  ChatFull,
  Clock,
  Battery,
  Voice,
  Focus,
  Library,
  AudioFolders,
  Notes,
  Assets,
  Ota,
  Settings,
  FocusSettings,
  ScreenSettings,
  WifiSettings,
  Calculator,
  Alarm,
  Stopwatch,
  Calendar,
  SmartLamp,
  Sonar,
  RfidLab,
  IrRemote,
  KeyRush,
  Tetris,
  CMatrix,
  Logs,
  BrainFlow,
  Nes,
  Files,
  Chess,
  Checkers,
  Mesh,
  Minesweeper,
  Snake,
  AirMusic,
  Flappy,
  Balloon,
  BallCatch,
  NfcNotes,
  Radio,
  VlcRemote,
  ChessMind,
  PocketSynth,
  LabDevices,
  LabRules,
  LabSequences,
  Podcasts,
  TvRemote,
  Cameras,
  Planes,
  BridgePage,
};

enum class FocusState : uint8_t {
  Stopped,
  Focus,
  ShortBreak,
  LongBreak,
  Reflect,
  Paused,
};
