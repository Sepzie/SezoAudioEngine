#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

namespace sezo {
namespace core {

/**
 * Lock-free circular buffer for real-time audio streaming.
 * Single producer, single consumer (SPSC) design.
 */
class CircularBuffer {
 public:
  /**
   * Constructor.
   * @param capacity Buffer capacity in samples
   */
  explicit CircularBuffer(size_t capacity);
  ~CircularBuffer();

  /**
   * Write data to the buffer.
   * @param data Source data pointer
   * @param count Number of samples to write
   * @return Number of samples actually written
   */
  size_t Write(const float* data, size_t count);

  /**
   * Read data from the buffer.
   * @param data Destination data pointer
   * @param count Number of samples to read
   * @return Number of samples actually read
   */
  size_t Read(float* data, size_t count);

  /**
   * Get available samples for reading.
   * @return Number of samples available
   */
  size_t Available() const;

  /**
   * Get free space for writing.
   * @return Number of samples that can be written
   */
  size_t FreeSpace() const;

  /**
   * Reset the buffer (clear all data).
   */
  void Reset();

 private:
  std::unique_ptr<float[]> buffer_;
  size_t capacity_;
  std::atomic<size_t> write_pos_{0};
  std::atomic<size_t> read_pos_{0};
};

}  // namespace core
}  // namespace sezo
