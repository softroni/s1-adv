#pragma once
#include <stddef.h>
#include <stdint.h>

namespace internet_radio {
enum class State : uint8_t { Idle, Connecting, Buffering, Playing, Stopping, Error };
enum class Error : uint8_t {
  None, InvalidUrl, PublicHttpOnly, NoMemory, TaskFailed, Dns, Connect,
  Timeout, Http, Unsupported, Malformed, Redirect, Decode, Speaker
};
// Sole producer on virtual speaker channel 7. Integration must reserve that
// channel and must not globally stop/end the speaker until busy() is false.
// start copies URL (max 511 bytes), returns false if busy or invalid URL.
// stop is cancellation only; poll busy() before switching stations/audio modes.
bool start(const char* url);
void stop();
bool busy();
State state();
Error error();
const char* status();  // Immutable literals; no shared String access.
const char* statusText();  // UI alias of status(); immutable short status/error.
const char* errorMessage();  // nullptr when Error::None; immutable error text.
uint32_t bytes();     // Decoded transport audio bytes (wraps at UINT32_MAX).
// The song the station says is on, UTF-8, from the stream's ICY metadata. Copies at most
// room-1 bytes and returns the length; 0 until the station sends a title.
size_t title(char* out, size_t room);
// How loud the latest decoded frame is: the mean of its samples' sizes, 0 to 32767, and 0
// while nothing plays. Measured from the audio itself, for a level meter.
uint16_t level();
// The station's own figure from its icy-br header, in kbit/s, or 0 when it didn't say.
uint16_t bitrate();
// For full-screen visuals. While wanted, each decoded frame also gives 16 bands of its spectrum
// and 120 points of its waveform; not wanted, the worker skips the work.
constexpr size_t kBands = 16, kWavePoints = 120;
struct Visuals {
  uint8_t bands[kBands];     // 60 Hz to 16 kHz, log-spaced, each 0 to 255 for -60 to 0 dBFS.
  int8_t wave[kWavePoints];  // Every 4th sample from the frame's start, as the top 8 bits.
  uint32_t frames;           // Counts up with each frame analysed.
};
void wantVisuals(bool on);
bool visuals(Visuals& out);  // False until a frame playing now has been analysed.

// A station found by searching radio-browser.info, a public directory with no account.
struct Found {
  char name[48];
  char url[256];
  char tags[48];
  char country[3];
  uint16_t bitrate;
  bool russian;
};
constexpr size_t kMaxFound = 20;
// Searches by genre (the directory's tags) or by part of a name, for plain-HTTP MP3 stations
// only, most listened first. country is a two-letter code like US, or empty for anywhere.
// It's the same single worker as start(), so stop playback first.
bool startSearch(bool byGenre, const char* text, const char* country = "");
bool searching();  // The worker is busy with a search rather than a stream.
// The last search's results, for reading while no search runs. A stream doesn't touch them.
size_t foundCount();
const Found* found(size_t i);
void clearFound();  // Frees the results. Does nothing while a search runs.

// Portable production parsers, also exercised by host tests. No allocation.
namespace detail {
constexpr size_t kUrlSize = 512;
struct Url { char host[128]{}; char path[384]{}; uint16_t port = 80; };
bool parseUrl(const char*, Url&);
bool publicIpv4(uint32_t hostOrder);
struct Headers {
  unsigned status = 0, total = 0;
  uint32_t metaint = 0, length = 0;
  uint16_t bitrate = 0;  // From icy-br, which some stations send as "128,128". Never an error.
  bool chunked = false, hasLength = false, hasType = false;
  bool hasTransfer = false, hasMeta = false, hasLocation = false, done = false;
  bool json = false;  // Set before the first line: a search wants JSON, a stream MP3.
  char location[kUrlSize]{};
  Error line(const char*);  // Lines exclude CRLF; empty line finalizes.
};
// Copies n bytes of text to out as UTF-8. Control characters become spaces, the ends are
// trimmed, and text that isn't valid UTF-8 is read as Latin-1, which older stations send.
// Cuts at a whole letter. Returns the length.
size_t cleanText(const char* in, size_t n, char* out, size_t room);
class Body {
 public:
  explicit Body(const Headers&);
  // Feed exactly once per wire byte: 1=audio, 0=framing/metadata, -1=error.
  int feed(uint8_t byte);
  bool done() const { return ended_; }
  bool complete() const; // EOF is legal only outside chunk/meta framing.
  // The StreamTitle from the latest metadata block that had one, and how many have come.
  const char* title() const { return title_; }
  uint32_t titles() const { return titles_; }
 private:
  int entity(uint8_t);
  void readTitle();
  enum class Phase : uint8_t { Size, SizeLF, Data, DataCR, DataLF, EndCR, EndLF };
  Phase phase_ = Phase::Size;
  bool chunked_, bounded_, ended_ = false, bad_ = false;
  uint32_t remaining_, chunk_ = 0, interval_, audioLeft_, metadata_ = 0;
  uint8_t digits_ = 0;
  uint16_t metaUsed_ = 0;
  uint32_t titles_ = 0;
  char meta_[256]{};
  char title_[128]{};
};
// One frame's analysis: a Hann-windowed 512-point FFT of its start, mixed to mono, read as
// the bands above, and its waveform. Uses 4 KB of stack and no allocation.
void analyse(const int16_t* pcm, int samples, int channels, int hz, Visuals& out);
// The directory's search address. Empty text finds the most listened. False when the text is
// over 64 bytes, or the country isn't empty or two capital letters.
bool searchUrl(char* out, size_t room, bool byGenre, const char* text, const char* country = "");
// Reads the directory's JSON answer, an array of flat objects, a byte at a time. Keeps MP3
// stations with a plain-HTTP address not already kept, up to `room`. Other keys, nested
// values and the parts of values too long to keep are skipped without being stored.
class StationJson {
 public:
  StationJson(Found* out, size_t room) : out_(out), room_(room) {}
  bool feed(uint8_t c);  // False: not the JSON expected. Stop reading.
  bool finished() const { return phase_ == Phase::Done; }
  size_t count() const { return count_; }
 private:
  enum class Phase : uint8_t { Start, Item, AfterItem, Key, KeyText, Colon, Value, ValueText, Number, Word, Nested, AfterMember, Done, Bad };
  enum class Field : uint8_t { None, Name, Url, Tags, Country, Language, Codec, Bitrate, Hls };
  bool fail() { phase_ = Phase::Bad; return false; }
  bool text(uint8_t c, bool& closed);
  void emit(uint32_t codepoint);
  void put(char c);
  void startValue();
  void endValue();
  void endItem();
  Found* out_;
  size_t room_, count_ = 0;
  Found cur_{};
  char key_[16]{};
  char language_[32]{}, codec_[8]{};
  char* dst_ = nullptr;
  size_t dstRoom_ = 0, dstLen_ = 0;
  bool overflow_ = false, urlTooLong_ = false, expectItem_ = false, expectKey_ = false, hls_ = false;
  bool nestedString_ = false, nestedEscape_ = false;
  Field field_ = Field::None;
  uint32_t number_ = 0, unit_ = 0, high_ = 0;
  uint16_t depth_ = 0;
  uint8_t escape_ = 0;
  Phase phase_ = Phase::Start;
};
}  // namespace detail
}  // namespace internet_radio
