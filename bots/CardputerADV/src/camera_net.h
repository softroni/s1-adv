#pragma once
#include <stddef.h>
#include <stdint.h>

// The Cameras app's network worker: one request to the bridge at a time, on a task of its own
// that runs only while the app is open. At home it talks plain HTTP to the bridge's computer,
// with the ticket in the path, and keeps that connection open between requests, so a live
// view's frames don't each wait for a new one. Away from home it uses HTTPS with the token.
// Answers are read whole into the heap, as big as their Content-Length says, up to a limit.
namespace camera_net {
enum class Error : uint8_t {
  None, NoMemory, TaskFailed, NoBridge, Connect, Timeout, Http, NotFound, Conflict, Busy, Malformed, TooBig
};
// Where the bridge is. Copies host and token; rootCa must outlive the worker (it's a constant).
void setBridge(const char* host, uint16_t port, const char* token, const char* rootCa);
bool open();        // Starts the worker. False when there's no memory for it.
void close();       // Ends it, cancelling any request, and frees all it holds.
bool opened();
// Asks for path. At home (ip, port: the bridge's computer, host order) or over HTTPS. maxBytes
// bounds the answer; waitMs is how long the bridge may take to start answering.
// `slot`, when one is reserved: a 200's body goes into that buffer instead of the heap.
bool start(const char* path, bool home, uint32_t ip, uint16_t port, size_t maxBytes, uint32_t waitMs, int slot = -1);
bool busy();        // A request is under way.
bool finished();    // One ended, and its answer waits for take() or drop().
Error error();      // The last request's, once it has finished.
unsigned status();  // The bridge's HTTP status, 0 if it never answered.
bool overHome();    // The last request went over the home door.
uint32_t tookMs();  // How long it took, from start() to its answer's end.
// The answer's body, which is then the caller's to free(), with a zero after it. Null when
// there was none.
uint8_t* take(size_t& length);
void drop();        // Frees the answer and forgets the request.
void cancel();      // Ends a request under way; it finishes as cancelled (Error::None, status 0).
const char* errorText(Error e);
// Fixed buffers for a run of like-sized answers (a live view's frames and sound): made once and
// reused, so the heap isn't taken and given back for every frame, and doesn't end up in pieces too
// small for the next one. take() then gives the slot's buffer, which stays the worker's (isSlot):
// the caller never frees it. A slot released while a request writes it goes as that one ends, and
// close() frees them all; after either, the caller must not use its pointers.
constexpr uint8_t kSlots = 3;
bool reserve(uint8_t slot, size_t bytes);  // False when there's no memory for it.
void release(uint8_t slot);
bool isSlot(const uint8_t* p);
// An HTTPS connection is open: during a request, or kept a few seconds after one for the next.
bool tlsOpen();
// A clip's frames' x-clip header from the last answer, or "" when it had none.
const char* clipHeader();
// After NoMemory: what ran short, then the memory free and its largest piece, like "TLS 61K/15K".
const char* memoryNote();
// Why the last connection that couldn't be made failed: how it was tried, the code (-1 when the
// network itself didn't connect; an mbedTLS one when TLS failed) and the memory free and its largest
// piece then, like "tls -10368 52K/18K" or "home -1 61K/30K". "" until one fails.
const char* failureNote();
}  // namespace camera_net
