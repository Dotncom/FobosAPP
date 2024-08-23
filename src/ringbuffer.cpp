#include "ringbuffer.h"

RingBuffer::RingBuffer(size_t size) : buffer(size), head(0), tail(0), maxSize(size) {}

void RingBuffer::push(const float* data, size_t length) {
    std::lock_guard<std::mutex> lock(mtx);
    for (size_t i = 0; i < length; ++i) {
        buffer[head] = data[i];
        head = (head + 1) % maxSize;
        if (head == tail) {
            tail = (tail + 1) % maxSize; // Overwrite oldest data
        }
    }
}

bool RingBuffer::pop(float* data, size_t length) {
    std::lock_guard<std::mutex> lock(mtx);
    size_t availableData = (head + maxSize - tail) % maxSize;
    if (availableData < length) {
        return false; // Not enough data
    }
    for (size_t i = 0; i < length; ++i) {
        data[i] = buffer[tail];
        tail = (tail + 1) % maxSize;
    }
    return true;
}

size_t RingBuffer::available() const {
    std::lock_guard<std::mutex> lock(mtx);
    return (head + maxSize - tail) % maxSize;
}
