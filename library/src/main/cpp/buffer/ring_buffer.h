#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace audio {

/**
 * @brief 线程安全的 PCM 环形缓冲区
 *
 * 用于音频流式解码的数据缓冲，支持：
 * - 线程安全的 Push/Read 操作
 * - EOS（End of Stream）标记
 * - 取消操作
 * - 动态容量配置
 */
class PcmRingBuffer {
public:
    /**
     * @brief 构造环形缓冲区
     * @param capacity 缓冲区容量（字节）
     * @param sampleRate 采样率（Hz），用于位置计算
     * @param channels 声道数，用于位置计算
     * @param bytesPerSample 每样本字节数（2=S16LE, 4=S32LE），用于位置计算
     */
    PcmRingBuffer(size_t capacity, int sampleRate, int channels, int bytesPerSample);

    /**
     * @brief 取消所有等待的操作
     */
    void Cancel();

    /**
     * @brief 标记流结束（EOS）
     */
    void MarkEos();

    /**
     * @brief 检查是否已到达 EOS
     * @return 如果已到达 EOS 且缓冲区为空，返回 true
     */
    bool IsEos() const;

    /**
     * @brief 向缓冲区推送数据
     * @param data 数据指针
     * @param len 数据长度
     * @param cancelFlag 取消标志指针
     * @return 成功返回 true，失败返回 false
     */
    bool Push(const uint8_t* data, size_t len, const std::atomic<bool>* cancelFlag);

    /**
     * @brief 从缓冲区读取数据
     * @param dst 目标缓冲区
     * @param len 要读取的长度
     * @return 实际读取的字节数
     */
    size_t Read(uint8_t* dst, size_t len);

    /**
     * @brief 清空缓冲区
     */
    void Clear();

    /**
     * @brief 获取累计读取的字节数
     * @return 累计读取字节数
     */
    uint64_t GetBytesRead() const;

    /**
     * @brief 获取当前播放位置（毫秒）
     * @return 当前播放位置（毫秒）
     */
    uint64_t GetPositionMs() const;

    /**
     * @brief 重置位置计数器
     */
    void ResetCounters();

private:
    mutable std::mutex mu_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::vector<uint8_t> buf_;
    size_t head_;
    size_t tail_;
    size_t size_;
    bool eos_;
    bool canceled_;

    // 位置追踪相关
    std::atomic<uint64_t> totalBytesRead_;  // 累计读取字节数（原子变量）
    int sampleRate_;                        // 采样率（Hz）
    int channels_;                          // 声道数
    int bytesPerSample_;                    // 每样本字节数（2=S16LE, 4=S32LE）
};

} // namespace audio

#endif // RING_BUFFER_H
