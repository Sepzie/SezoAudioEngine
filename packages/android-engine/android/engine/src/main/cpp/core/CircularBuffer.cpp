#include "CircularBuffer.h"
#include <algorithm>
#include <cstring>

namespace sezo {
namespace core {

CircularBuffer::CircularBuffer(size_t capacity)
    : buffer_(std::make_unique<float[]>(capacity)), capacity_(capacity) {}

CircularBuffer::~CircularBuffer() = default;

size_t CircularBuffer::Write(const float* data, size_t count) {
  const size_t free = FreeSpace();
  const size_t to_write = std::min(count, free);

  if (to_write == 0) {
    return 0;
  }

  const size_t write_idx = write_pos_.load(std::memory_order_acquire);
  const size_t first_chunk = std::min(to_write, capacity_ - write_idx);

  std::memcpy(buffer_.get() + write_idx, data, first_chunk * sizeof(float));

  if (to_write > first_chunk) {
    const size_t second_chunk = to_write - first_chunk;
    std::memcpy(buffer_.get(), data + first_chunk, second_chunk * sizeof(float));
  }

  write_pos_.store((write_idx + to_write) % capacity_, std::memory_order_release);
  return to_write;
}

size_t CircularBuffer::Read(float* data, size_t count) {
  const size_t available = Available();
  const size_t to_read = std::min(count, available);

  if (to_read == 0) {
    return 0;
  }

  const size_t read_idx = read_pos_.load(std::memory_order_acquire);
  const size_t first_chunk = std::min(to_read, capacity_ - read_idx);

  std::memcpy(data, buffer_.get() + read_idx, first_chunk * sizeof(float));

  if (to_read > first_chunk) {
    const size_t second_chunk = to_read - first_chunk;
    std::memcpy(data + first_chunk, buffer_.get(), second_chunk * sizeof(float));
  }

  read_pos_.store((read_idx + to_read) % capacity_, std::memory_order_release);
  return to_read;
}

size_t CircularBuffer::Available() const {
  const size_t write_idx = write_pos_.load(std::memory_order_acquire);
  const size_t read_idx = read_pos_.load(std::memory_order_acquire);

  if (write_idx >= read_idx) {
    return write_idx - read_idx;
  }
  return capacity_ - (read_idx - write_idx);
}

size_t CircularBuffer::FreeSpace() const {
  return capacity_ - Available() - 1;  // Leave one slot empty to distinguish full from empty
}

void CircularBuffer::Reset() {
  write_pos_.store(0, std::memory_order_release);
  read_pos_.store(0, std::memory_order_release);
}

}  // namespace core
}  // namespace sezo
