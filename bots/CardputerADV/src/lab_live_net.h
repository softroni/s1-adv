#pragma once
#include <stddef.h>
#include <stdint.h>

// The Lab's live reports to the bridge's home door: one at a time, on a non-blocking plain socket
// that the main loop turns over. Nothing here ever waits, so a report can't hold up the Lab's
// loop, which reads the sensors and drives the parts, and there's no task with a stack to keep
// while the Lab is open, where the chat's TLS needs every piece of memory it can get.
namespace lab_live_net {
// Copies the request's head and body and starts connecting to ip:port (host order). False when a
// report is already under way, or there's no memory or socket for it.
bool start(uint32_t ip, uint16_t port, const char* head, size_t headLength, const char* body, size_t bodyLength,
           uint32_t now, uint32_t timeoutMs);
void service(uint32_t now);  // Moves it along. Call it every loop; it returns at once.
bool busy();                 // Connecting, sending or reading.
bool finished();             // One ended: read it with answer(), then drop().
// The answer's bytes as they came, with a zero after them, or "" when nothing came back.
const char* answer(size_t& length);
void drop();                 // Forgets a finished report.
void abort();                // Closes any report now.
}  // namespace lab_live_net
