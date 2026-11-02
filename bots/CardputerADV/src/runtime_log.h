#pragma once
// The last lines of the device's log, for the Logs screen and the logs upload.
//
// These stay out of the heap on purpose. As Strings they were dozens of small blocks, replaced
// every time anything was logged, at whatever address was free. A recording logs a burst of lines
// while TLS holds its buffers, so they settled around that memory and kept it in pieces once TLS
// let go, and with 80 KB free the device could then not find room for a connection.
#include <stddef.h>
#include <string.h>

namespace runtime_log {
template <size_t Lines, size_t Chars>
class Ring {
 public:
  void add(const char* line) {
    const size_t slot = (first_ + count_) % Lines;
    strncpy(lines_[slot], line, Chars);
    lines_[slot][Chars] = '\0';
    if (count_ < Lines) ++count_;
    else first_ = (first_ + 1) % Lines;
  }
  void clear() {
    first_ = 0;
    count_ = 0;
  }
  size_t size() const { return count_; }
  // 0 is the oldest line still held.
  const char* operator[](size_t index) const { return lines_[(first_ + index) % Lines]; }

 private:
  char lines_[Lines][Chars + 1] = {};
  size_t first_ = 0, count_ = 0;
};
}  // namespace runtime_log
