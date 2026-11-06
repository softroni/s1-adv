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
bool start(const char* path, bool home, uint32_t ip, uint16_t port, size_t maxBytes, uint32_t waitMs);
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
// A clip's frames' x-clip header from the last answer, or "" when it had none.
const char* clipHeader();
// After NoMemory: what ran short, then the memory free and its largest piece, like "TLS 61K/15K".
const char* memoryNote();
}  // namespace camera_net
