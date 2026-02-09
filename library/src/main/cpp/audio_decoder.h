#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <multimedia/player_framework/native_avcodec_audiocodec.h>
#include <multimedia/native_audio_channel_layout.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avformat.h>
#include <multimedia/player_framework/native_avbuffer.h>
#include <multimedia/player_framework/native_avdemuxer.h>
#include <multimedia/player_framework/native_avsource.h>
#include <fcntl.h>
#include <stdint.h>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <string>

// 音频解码器缓冲区信号类
class AudioDecoderSignal {
public:
    std::mutex inMutex_;
    std::mutex outMutex_;
    std::mutex startMutex_;
    std::condition_variable inCond_;
    std::condition_variable outCond_;
    std::condition_variable startCond_;
    std::queue<uint32_t> inQueue_;
    std::queue<uint32_t> outQueue_;
    std::queue<OH_AVBuffer *> inBufferQueue_;
    std::queue<OH_AVBuffer *> outBufferQueue_;
};

// 音频解码器类
class AudioDecoder {
public:
    using ProgressCallback = std::function<void(double progress, int64_t ptsMs, int64_t durationMs)>;
    using PcmDataCallback = std::function<bool(const uint8_t* data, size_t size, int64_t ptsMs)>;
    using InfoCallback = std::function<void(int32_t sampleRate, int32_t channelCount, int32_t sampleFormat, int64_t durationMs)>;
    using ErrorCallback = std::function<void(const std::string& stage, int32_t code, const std::string& message)>;
    using CancelFlag = std::atomic<bool>;

    // Seek coordination between JS thread and decode thread.
    // - pollCb: called from decode thread to check whether a new seek request exists.
    // - appliedCb: called after the seek attempt is applied (success or failure).
    using SeekPollCallback = std::function<bool(int64_t& targetMs, uint64_t& seq)>;
    using SeekAppliedCallback = std::function<void(uint64_t seq, bool success, int64_t targetMs)>;

    AudioDecoder();
    ~AudioDecoder();

    // 解码文件（自动检测格式，使用默认参数：44100Hz, 2声道）
    bool DecodeFile(const std::string& inputPath, const std::string& outputPath);

    // 解码文件（可选参数覆盖，0或负值表示使用默认值）
    // 默认值：sampleRate=44100, channelCount=2, bitrate=不设置
    bool DecodeFile(const std::string& inputPath, const std::string& outputPath,
                   int32_t sampleRate, int32_t channelCount, int32_t bitrate);

    // 解码文件（带进度回调；progress=0~1，durationMs 可能为 0 表示未知）
    bool DecodeFileWithProgress(const std::string& inputPathOrUri, const std::string& outputPath,
                                int32_t sampleRate, int32_t channelCount, int32_t bitrate,
                                const ProgressCallback& progressCb);

    // 流式解码：输出 PCM 数据给回调（用于 AudioRenderer/writeData 拉取式播放）
    // 说明：
    // - infoCb：在解析出音频参数并启动解码器后调用一次
    // - pcmCb：持续回调输出 PCM；返回 false 将中止解码
    // - cancelFlag：可选，置 true 时尽快停止
    // - sampleFormat: 输出采样格式，1=S16LE, 3=S32LE，默认 1
    bool DecodeToPcmStream(const std::string& inputPathOrUri,
                           int32_t sampleRate, int32_t channelCount, int32_t bitrate,
                           const InfoCallback& infoCb,
                           const ProgressCallback& progressCb,
                           const PcmDataCallback& pcmCb,
                           const ErrorCallback& errorCb,
                           CancelFlag* cancelFlag,
                           int32_t sampleFormat = 1,
                           const SeekPollCallback& seekPollCb = SeekPollCallback(),
                           const SeekAppliedCallback& seekAppliedCb = SeekAppliedCallback());

    // 停止解码
    bool Stop();

    // 刷新解码器
    bool Flush();

    // 重置解码器
    bool Reset();

    /**
     * @brief 跳转到指定时间点
     * @param timeMs 目标时间（毫秒）
     * @return 成功返回 true，失败返回 false
     */
    bool SeekTo(int64_t timeMs);

    // 销毁解码器
    void Destroy();

private:
    enum class StepResult {
        Continue = 0,
        Eos = 1,
        Error = 2,
    };

    OH_AVCodec* audioDecoder_;
    AudioDecoderSignal* signal_;
    OH_AVFormat* format_;
    bool isRunning_;
    std::string currentMimeType_;

    // 用于 Seek 功能
    OH_AVSource* avSource_;
    OH_AVDemuxer* avDemuxer_;
    int32_t audioTrackIndex_;
    std::string currentInputPathOrUri_;

    // 用于进度与参数自适应（仅在一次 Decode 调用期间有效）
    int64_t durationMs_;
    int32_t detectedSampleRate_;
    int32_t detectedChannelCount_;
    int32_t detectedSampleFormat_;

    int32_t lastProgressPercent_;
    int64_t lastProgressPtsMs_;

    CancelFlag* cancelFlag_;

    // 初始化解码器（使用指定的 MIME 类型）
    bool Initialize(const std::string& mimeType);

    // 配置解码器参数
    // sampleRate: 采样率（必须），<= 0 时使用默认值 44100
    // channelCount: 声道数（必须），<= 0 时使用默认值 2
    // bitrate: 比特率（可选），<= 0 时不设置
    // sampleFormat: 采样格式（可选），1=S16LE, 3=S32LE，默认 1=S16LE
    bool Configure(int32_t sampleRate = 0, int32_t channelCount = 0, int32_t bitrate = 0, int32_t sampleFormat = 1);

    // 开始解码
    bool Start();

    // 从文件路径获取 MIME 类型
    std::string GetMimeTypeFromFile(const std::string& filePath);

    // 回调函数
    static void OnError(OH_AVCodec *codec, int32_t errorCode, void *userData);
    static void OnOutputFormatChanged(OH_AVCodec *codec, OH_AVFormat *format, void *userData);
    static void OnInputBufferAvailable(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *data, void *userData);
    static void OnOutputBufferAvailable(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *data, void *userData);

    // 输入数据处理（从解封装器读取）
    StepResult PushInputData(OH_AVDemuxer* demuxer, uint32_t trackIndex, const ProgressCallback& progressCb);

    // 输出数据处理
    StepResult PopOutputData(std::ofstream& outputFile);

    // 输出数据处理（PCM 回调）
    StepResult PopOutputData(const PcmDataCallback& pcmCb);

    // 内部解码实现（使用解封装器）
    bool DecodeFileInternal(const std::string& inputPathOrUri, const std::string& outputPath,
                            int32_t sampleRate, int32_t channelCount, int32_t bitrate,
                            const ProgressCallback& progressCb);

    bool IsHttpUri(const std::string& inputPathOrUri) const;
};

#endif // AUDIO_DECODER_H
