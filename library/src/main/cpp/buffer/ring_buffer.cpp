#include "ring_buffer.h"

namespace audio {

PcmRingBuffer::PcmRingBuffer(size_t capacity, int sampleRate, int channels, int bytesPerSample)
    : buf_(capacity), head_(0), tail_(0), size_(0), eos_(false), canceled_(false),
      totalBytesRead_(0), sampleRate_(sampleRate), channels_(channels), bytesPerSample_(bytesPerSample)
{
}

void PcmRingBuffer::Cancel()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        canceled_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
}

void PcmRingBuffer::MarkEos()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        eos_ = true;
    }
    notEmpty_.notify_all();
}

bool PcmRingBuffer::IsEos() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return eos_ && size_ == 0;
}

bool PcmRingBuffer::Push(const uint8_t* data, size_t len, const std::atomic<bool>* cancelFlag)
{
    if (data == nullptr || len == 0) {
        return true;
    }

    size_t offset = 0;
    while (offset < len) {
        if ((cancelFlag && cancelFlag->load()) || canceled_) {
            return false;
        }

        std::unique_lock<std::mutex> lock(mu_);
        notFull_.wait(lock, [&]() {
            if (canceled_) {
                return true;
            }
            return size_ < buf_.size();
        });

        if (canceled_) {
            return false;
        }

        const size_t cap = buf_.size();
        const size_t space = cap - size_;
        if (space == 0) {
            continue;
        }

        const size_t tailToEnd = cap - tail_;
        const size_t n = std::min({space, len - offset, tailToEnd});
        memcpy(&buf_[tail_], data + offset, n);
        tail_ = (tail_ + n) % cap;
        size_ += n;
        offset += n;

        lock.unlock();
        notEmpty_.notify_all();
    }

    return true;
}

size_t PcmRingBuffer::Read(uint8_t* dst, size_t len)
{
    if (dst == nullptr || len == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mu_);
    const size_t cap = buf_.size();
    const size_t n = std::min(len, size_);
    if (n == 0) {
        return 0;
    }

    const size_t headToEnd = cap - head_;
    const size_t first = std::min(n, headToEnd);
    memcpy(dst, &buf_[head_], first);
    const size_t second = n - first;
    if (second > 0) {
        memcpy(dst + first, &buf_[0], second);
    }
    head_ = (head_ + n) % cap;
    size_ -= n;

    // 累加已读字节数（原子操作）
    totalBytesRead_.fetch_add(n);

    notFull_.notify_all();
    return n;
}

void PcmRingBuffer::Clear()
{
    std::lock_guard<std::mutex> lock(mu_);
    head_ = 0;
    tail_ = 0;
    size_ = 0;
    notFull_.notify_all();
}

uint64_t PcmRingBuffer::GetBytesRead() const
{
    return totalBytesRead_.load();
}

uint64_t PcmRingBuffer::GetPositionMs() const
{
    if (sampleRate_ <= 0 || channels_ <= 0 || bytesPerSample_ <= 0) {
        return 0;
    }

    const uint64_t bytesRead = totalBytesRead_.load();

    // 计算总样本数：已读字节数 / (声道数 × 每样本字节数)
    const uint64_t totalSamples = bytesRead / (channels_ * bytesPerSample_);

    // 计算播放时间（毫秒）= (总样本数 × 1000) / 采样率
    return (totalSamples * 1000) / static_cast<uint64_t>(sampleRate_);
}

void PcmRingBuffer::ResetCounters()
{
    totalBytesRead_.store(0);
}

} // namespace audio
