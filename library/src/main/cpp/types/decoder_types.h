#ifndef DECODER_TYPES_H
#define DECODER_TYPES_H

#include <napi/native_api.h>
#include <cstdint>
#include <string>
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include "../pcm_equalizer.h"
#include "../buffer/ring_buffer.h"

// ============================================================================
// 解码器事件类型和负载
// ============================================================================

/**
 * @brief 解码器事件类型
 */
enum class DecoderEventType {
    Ready = 0,      ///< 解码器就绪
    Progress = 1,   ///< 进度更新
    Error = 2,      ///< 错误发生
};

/**
 * @brief 解码器事件负载
 */
struct DecoderEventPayload {
    DecoderEventType type;

    // Ready 事件数据
    int32_t sampleRate;
    int32_t channelCount;
    int32_t sampleFormat;
    int64_t durationMs;

    // Progress 事件数据
    double progress;
    int64_t ptsMs;

    // Error 事件数据
    std::string stage;
    int32_t code;
    std::string message;
};

// ============================================================================
// 文件解码异步上下文
// ============================================================================

/**
 * @brief 文件解码进度回调负载
 */
struct DecodeAudioProgressPayload {
    double progress;
    int64_t ptsMs;
    int64_t durationMs;
};

/**
 * @brief 文件解码异步上下文
 */
struct DecodeAudioAsyncContext {
    napi_env env;
    napi_async_work work;
    napi_deferred deferred;
    napi_threadsafe_function tsfn;

    std::string inputPathOrUri;
    std::string outputPath;
    int32_t sampleRate;
    int32_t channelCount;
    int32_t bitrate;

    bool success;
};

// ============================================================================
// 流式解码器上下文
// ============================================================================

/**
 * @brief 流式解码器上下文
 */
struct PcmStreamDecoderContext {
    napi_env env;
    napi_async_work work;
    napi_threadsafe_function eventTsfn;

    napi_deferred readyDeferred;
    napi_deferred doneDeferred;

    napi_ref selfRef;
    napi_ref onProgressRef;
    napi_ref onErrorRef;

    std::string inputPathOrUri;
    int32_t sampleRate;
    int32_t channelCount;
    int32_t bitrate;
    int32_t sampleFormat;  // 1=S16LE, 3=S32LE

    std::atomic<bool> cancel;
    bool success;
    bool readySettled;

    // 用于拒绝 done promise
    std::string lastErrStage;
    int32_t lastErrCode;
    std::string lastErrMessage;

    std::unique_ptr<audio::PcmRingBuffer> ring;

    // EQ（10 段均衡器）配置，与 JS 线程共享
    std::atomic<bool> eqEnabled;
    std::atomic<uint32_t> eqVersion;
    std::array<std::atomic<int32_t>, PcmEqualizer::kBandCount> eqGainsDb100;

    // 工作线程状态
    uint32_t eqAppliedVersion;
    int32_t eqSampleRate;
    int32_t eqChannelCount;
    PcmEqualizer eq;

    std::vector<int16_t> eqScratch;

    // 用于 PcmRingBuffer 重新初始化
    size_t ringBytes;
    int32_t actualSampleRate;
    int32_t actualChannelCount;
    int32_t actualSampleFormat;

    // Seek 功能相关
    std::atomic<bool> isSeeking_;     // 是否正在 Seek
    std::atomic<int64_t> targetPositionMs_;  // 目标 Seek 位置（毫秒）
    std::mutex seekMutex_;              // Seek 操作的互斥锁
    int64_t currentSeekPositionMs_;     // 当前 Seek 位置（用于解码线程判断）
};

#endif // DECODER_TYPES_H
