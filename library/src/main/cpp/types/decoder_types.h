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
#include "../drc_processor.h"
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
    Seek = 3,       ///< Seek 结果（用于 Promise resolve/reject）
    DrcMeter = 4,   ///< DRC meter (level/gain/GR)
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

    // Seek 事件数据
    uint64_t seekSeq = 0;
    int64_t seekTargetMs = 0;
    bool seekSuccess = false;

    // DRC meter
    double drcLevelDb = 0.0;
    double drcGainDb = 0.0;
    double drcGrDb = 0.0;
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

    // seekToAsync promise
    napi_deferred seekDeferred;
    uint64_t seekDeferredSeq;

    napi_ref selfRef;
    napi_ref onProgressRef;
    napi_ref onErrorRef;
    napi_ref onDrcMeterRef;

    std::string inputPathOrUri;
    int32_t sampleRate;
    int32_t channelCount;
    int32_t bitrate;
    int32_t sampleFormat;  // 1=S16LE, 3=S32LE

    std::atomic<bool> cancel;
    bool success;
    bool readySettled;

    // Decoder pause control: when true, decode thread waits instead of reading network.
    // This prevents network timeout during long pauses.
    std::atomic<bool> decoderPaused;

    // Decoder alive status: set to true when decode thread starts, false when it exits.
    // Used to detect if decoder has failed during long pause.
    std::atomic<bool> decoderAlive;

    // 用于拒绝 done promise
    std::string lastErrStage;
    int32_t lastErrCode;
    std::string lastErrMessage;

    std::unique_ptr<audio::PcmRingBuffer> ring;

    // EQ（10 段均衡器）配置，与 JS 线程共享
    std::atomic<bool> eqEnabled;
    std::atomic<uint32_t> eqVersion;

    // EQ gains per channel (0=left/mono, 1=right). Unit: dB * 100.
    std::array<std::array<std::atomic<int32_t>, PcmEqualizer::kBandCount>, 2> eqGainsDb100Stereo;

    // Per-channel volume compensation coefficients.
    // Unit: coefficient * 1000. 1000 = 1.0, 500 = 0.5, 1500 = 1.5.
    std::array<std::atomic<int32_t>, 2> channelVol1000;

    // 工作线程状态
    uint32_t eqAppliedVersion;
    int32_t eqSampleRate;
    int32_t eqChannelCount;
    PcmEqualizer eq;

    // DRC (dynamic range compression)
    std::atomic<bool> drcEnabled;
    std::atomic<uint32_t> drcVersion;
    std::atomic<int32_t> drcThresholdDb100;   // dB * 100
    std::atomic<int32_t> drcRatio1000;        // ratio * 1000
    std::atomic<int32_t> drcAttackMs100;      // ms * 100
    std::atomic<int32_t> drcReleaseMs100;     // ms * 100
    std::atomic<int32_t> drcMakeupDb100;      // dB * 100

    uint32_t drcAppliedVersion;
    DrcProcessor drc;

    uint64_t drcMeterLastEmitMs;

    std::vector<int16_t> eqScratch16;
    std::vector<int32_t> eqScratch32;

    // Float DSP scratch (normalized)
    std::vector<float> dspScratchF;

    // Global S32LE max absolute value for stable normalization.
    // This persists across callbacks to prevent volume rollercoasters
    // when the source data scale is ambiguous (16/24/32-bit).
    int64_t s32GlobalMaxAbs;

    // 用于 PcmRingBuffer 重新初始化
    size_t ringBytes;
    int32_t actualSampleRate;
    int32_t actualChannelCount;
    int32_t actualSampleFormat;

    // Seek 功能相关
    // Note: We use a monotonically increasing sequence so frequent seeks are
    // coalesced and the decode thread can apply only the latest one.
    std::atomic<uint64_t> seekSeq_;          // last requested seek sequence
    std::atomic<uint64_t> seekHandledSeq_;   // last handled seek sequence
    std::atomic<int64_t> targetPositionMs_;  // target seek position (ms)
    std::mutex seekMutex_;                  // protects target+seq updates

    // For seekToAsync: resolved when first post-seek PCM is produced.
    std::atomic<bool> seekAwaitOutput;
    std::atomic<uint64_t> seekAwaitSeq;
};

#endif // DECODER_TYPES_H
