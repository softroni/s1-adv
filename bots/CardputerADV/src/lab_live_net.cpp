#include "lab_live_net.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lab_live_net {
namespace {
enum class Phase : uint8_t { Idle, Connecting, Sending, Reading, Done };
constexpr size_t kAnswerBytes = 256;  // A status line, a few headers and "watch 1".

Phase phase = Phase::Idle;
int fd = -1;
char* buffer = nullptr;  // The request, then room for the answer after it.
size_t requestLength = 0, sentBytes = 0, readBytes = 0;
uint32_t deadline = 0;

char* answerAt() { return buffer + requestLength; }
void closeSocket() {
  if (fd >= 0) close(fd);
  fd = -1;
}
void finish() {
  closeSocket();
  if (buffer) answerAt()[readBytes] = '\0';
  phase = Phase::Done;
}
bool ready(bool forWriting) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  timeval none = {0, 0};
  return select(fd + 1, forWriting ? nullptr : &set, forWriting ? &set : nullptr, nullptr, &none) > 0;
}
}  // namespace

bool start(uint32_t ip, uint16_t port, const char* head, size_t headLength, const char* body, size_t bodyLength,
           uint32_t now, uint32_t timeoutMs) {
  if (phase != Phase::Idle || !head || !headLength || (bodyLength && !body)) return false;
  buffer = static_cast<char*>(malloc(headLength + bodyLength + kAnswerBytes + 1));
  if (!buffer) return false;
  memcpy(buffer, head, headLength);
  if (bodyLength) memcpy(buffer + headLength, body, bodyLength);
  requestLength = headLength + bodyLength;
  sentBytes = readBytes = 0;
  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    free(buffer);
    buffer = nullptr;
    return false;
  }
#if defined(SO_NOSIGPIPE)
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));  // The host's tests; lwIP has no signals.
#endif
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = htonl(ip);
  deadline = now + timeoutMs;
  phase = Phase::Connecting;
  if (connect(fd, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) phase = Phase::Sending;
  else if (errno != EINPROGRESS) finish();
  return true;
}

void service(uint32_t now) {
  if (phase == Phase::Idle || phase == Phase::Done) return;
  if (static_cast<int32_t>(now - deadline) >= 0) { finish(); return; }
  if (phase == Phase::Connecting) {
    if (!ready(true)) return;
    int error = 0;
    socklen_t size = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error) { finish(); return; }
    phase = Phase::Sending;
  }
  if (phase == Phase::Sending) {
    const ssize_t n = send(fd, buffer + sentBytes, requestLength - sentBytes, 0);
    if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) finish();
      return;
    }
    sentBytes += size_t(n);
    if (sentBytes < requestLength) return;
    phase = Phase::Reading;
  }
  if (phase == Phase::Reading) {
    const ssize_t n = recv(fd, answerAt() + readBytes, kAnswerBytes - readBytes, 0);
    if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) finish();
      return;
    }
    readBytes += size_t(n);
    // The bridge closes after answering. A full buffer already holds all that's read from it.
    if (n == 0 || readBytes >= kAnswerBytes) finish();
  }
}

bool busy() { return phase == Phase::Connecting || phase == Phase::Sending || phase == Phase::Reading; }
bool finished() { return phase == Phase::Done; }

const char* answer(size_t& length) {
  length = phase == Phase::Done && buffer ? readBytes : 0;
  return length ? answerAt() : "";
}

void drop() {
  if (phase != Phase::Done) return;
  free(buffer);
  buffer = nullptr;
  phase = Phase::Idle;
}

void abort() {
  closeSocket();
  free(buffer);
  buffer = nullptr;
  phase = Phase::Idle;
}
}  // namespace lab_live_net
