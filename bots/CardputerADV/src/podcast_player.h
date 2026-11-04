#pragma once
#include <stddef.h>
#include <stdint.h>

// The podcast player: one worker that does one job at a time.
//   List    asks the bridge for a list (a search or a page of episodes) as text.
//   Stream  plays an episode as the bridge sends it, from any second.
//   File    plays an episode saved on the SD card, from any second, with no network.
//   Save    writes an episode to the SD card as the bridge sends it.
// Lists, and audio away from home, come from the bridge over HTTPS with the device's token.
// At home, audio comes straight from the bridge's computer over plain HTTP, with a ticket the
// HTTPS side gave out: TLS's two 17 KB buffers and the decoder don't fit in memory together.
// Audio decodes with the radio's MP3 decoder on its own speaker channel, 2.
namespace podcast_player {
enum class State : uint8_t { Idle, Connecting, Buffering, Playing, Saving, Stopping, Error };
enum class Error : uint8_t {
  None, NoMemory, TaskFailed, NoBridge, Connect, Timeout, Http, Busy, NotFound, Unavailable,
  Malformed, TooBig, Decode, Speaker, Card, CardFull, Unplayable
};
// Where the bridge is. Copies host and token; rootCa must outlive every job (it's a constant).
void setBridge(const char* host, uint16_t port, const char* token, const char* rootCa);

bool startList(const char* path);
bool startStream(const char* path, uint32_t fromSeconds);
// The same over the home network: ip is the bridge's computer, host order, and path holds
// the ticket. No token goes this way.
bool startStreamHome(uint32_t ip, uint16_t port, const char* path, uint32_t fromSeconds);
bool startFile(const char* sdPath, uint32_t fromSeconds);
// expectBytes is only for showing progress: the bridge's answer has no length.
bool startSave(const char* path, const char* partPath, const char* sdPath, uint32_t expectBytes);
bool startSaveHome(uint32_t ip, uint16_t port, const char* path, const char* partPath, const char* sdPath, uint32_t expectBytes);
void stop();      // Cancels; poll busy() before starting the next job.
bool busy();
State state();
Error error();
const char* errorText();  // A short line for the screen, or nullptr.
// After NoMemory: what ran short, then the memory free and its largest piece, like "TLS 61K/15K".
const char* memoryNote();
bool overHome();          // The last audio job came over the home network.
unsigned httpStatus();    // The bridge's answer to the last job, 0 before it answered.
bool playing();           // A Stream or File job is under way.
bool saving();            // A Save job is under way.
bool listing();

// The last list's text, valid while no job runs. clearList frees it.
const char* listText(size_t& length);
void clearList();

// Where the last Stream or File job has got to, in seconds: where it began plus what's been
// played. Kept after it stops, so pausing knows where it was.
uint32_t position();
// The last Stream or File job played to the episode's end.
bool reachedEnd();
uint32_t savedBytes();    // What a Save job has written so far.
bool saveDone();          // The last Save job wrote the whole episode.
uint16_t level();         // As the radio's: the latest frame's loudness, 0 to 32767.

namespace detail {
// The compressed audio held: 0.7 seconds, as small as memory needs. Wait for the network only
// when less than kLowWater is held, so a short stall plays from what's held, not silence.
constexpr size_t kInput = 4096, kLowWater = 1024;
// One decoded frame, as the bridge sends them: 1152 mono samples. A frame that would need
// more is refused before it's decoded, so the slots can be half the decoder's largest.
constexpr size_t kSlotSamples = 1152;
// A list or a save needs no decoder, so its worker's stack is near the 8 KB the device's other
// HTTPS requests use. A player's holds the decoder's 16 KB of scratch.
constexpr uint32_t kNetStack = 10240, kPlayStack = 24576;
// The bridge can take a while to start: some podcast hosts take 20 seconds to send sound.
constexpr uint32_t kHeaderMs = 40000, kIdleMs = 25000;
constexpr size_t kListBytes = 12288;
}  // namespace detail
}  // namespace podcast_player
