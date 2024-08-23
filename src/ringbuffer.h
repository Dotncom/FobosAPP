#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <vector>
#include <mutex>

class RingBuffer {
public:
    RingBuffer(size_t size);
    void push(const float* data, size_t length);
    bool pop(float* data, size_t length);
    size_t available() const;

private:
    std::vector<float> buffer;
    size_t head;
    size_t tail;
    size_t maxSize;
    mutable std::mutex mtx;
};

#endif // RINGBUFFER_H
