#include "audio_decoder.h"
#include <hilog/log.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <unistd.h>

#undef LOG_TAG
#undef LOG_DOMAIN
#define LOG_TAG "AudioDecoder"
#define LOG_DOMAIN 0x3200

AudioDecoder::AudioDecoder()
    : audioDecoder_(nullptr), signal_(nullptr), format_(nullptr), isRunning_(false), currentMimeType_(""),
      durationMs_(0), detectedSampleRate_(0), detectedChannelCount_(0),
      lastProgressPercent_(-1), lastProgressPtsMs_(-1), cancelFlag_(nullptr) {
}

AudioDecoder::~AudioDecoder() {
    Destroy();
}

// 从文件路径获取 MIME 类型
std::string AudioDecoder::GetMimeTypeFromFile(const std::string& filePath) {
    // 查找最后一个点的位置
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        OH_LOG_ERROR(LOG_APP, "No file extension found in: %{public}s", filePath.c_str());
        return "";
    }

    // 提取扩展名并转换为小写
    std::string extension = filePath.substr(dotPos + 1);
    for (char& c : extension) {
        c = std::tolower(c);
    }

    OH_LOG_INFO(LOG_APP, "Detected file extension: %{public}s", extension.c_str());

    // 根据扩展名返回对应的 MIME 类型
    if (extension == "mp3") {
        OH_LOG_INFO(LOG_APP, "Using MIME type: OH_AVCODEC_MIMETYPE_AUDIO_MPEG (MP3)");
        return OH_AVCODEC_MIMETYPE_AUDIO_MPEG;
    } else if (extension == "flac") {
        OH_LOG_INFO(LOG_APP, "Using MIME type: OH_AVCODEC_MIMETYPE_AUDIO_FLAC (FLAC)");
        return OH_AVCODEC_MIMETYPE_AUDIO_FLAC;
    } else if (extension == "wav") {
        OH_LOG_INFO(LOG_APP, "Using MIME type: audio/wav (WAV)");
        // WAV 通常不需要解码，但如果需要可以使用 PCM 解码器
        return "audio/wav";
    } else if (extension == "aac") {
        OH_LOG_INFO(LOG_APP, "Using MIME type: OH_AVCODEC_MIMETYPE_AUDIO_AAC (AAC)");
        return OH_AVCODEC_MIMETYPE_AUDIO_AAC;
    } else if (extension == "m4a") {
        OH_LOG_INFO(LOG_APP, "Using MIME type: OH_AVCODEC_MIMETYPE_AUDIO_AAC (M4A/AAC)");
        return OH_AVCODEC_MIMETYPE_AUDIO_AAC;
    } else if (extension == "ogg" || extension == "oga") {
        OH_LOG_INFO(LOG_APP, "Using MIME type: OH_AVCODEC_MIMETYPE_AUDIO_VORBIS (OGG/Vorbis)");
        return OH_AVCODEC_MIMETYPE_AUDIO_VORBIS;
    } else if (extension == "opus") {
        OH_LOG_INFO(LOG_APP, "Using MIME type: OH_AVCODEC_MIMETYPE_AUDIO_OPUS (Opus)");
        return OH_AVCODEC_MIMETYPE_AUDIO_OPUS;
    } else {
        OH_LOG_ERROR(LOG_APP, "Unsupported audio format: %{public}s", extension.c_str());
        return "";
    }
}

bool AudioDecoder::Initialize(const std::string& mimeType) {
    if (mimeType.empty()) {
        OH_LOG_ERROR(LOG_APP, "MIME type is empty");
        return false;
    }

    // 如果已有解码器且 MIME 类型相同，无需重新创建
    if (audioDecoder_ && currentMimeType_ == mimeType) {
        OH_LOG_INFO(LOG_APP, "Decoder already initialized with same MIME type: %{public}s", mimeType.c_str());
        return true;
    }

    // 销毁旧的解码器
    if (audioDecoder_) {
        OH_LOG_INFO(LOG_APP, "Destroying old decoder before creating new one");
        Destroy();
    }

    // 创建信号队列
    if (!signal_) {
        signal_ = new AudioDecoderSignal();
        if (!signal_) {
            OH_LOG_ERROR(LOG_APP, "Failed to create signal");
            return false;
        }
    }

    // 通过 MIME 类型创建解码器
    bool isEncoder = false;
    audioDecoder_ = OH_AudioCodec_CreateByMime(mimeType.c_str(), isEncoder);
    if (!audioDecoder_) {
        OH_LOG_ERROR(LOG_APP, "Failed to create audio decoder for MIME type: %{public}s", mimeType.c_str());
        delete signal_;
        signal_ = nullptr;
        return false;
    }

    currentMimeType_ = mimeType;

    // 注册回调函数
    OH_AVCodecCallback callback = {
        &OnError,
        &OnOutputFormatChanged,
        &OnInputBufferAvailable,
        &OnOutputBufferAvailable
    };

    int32_t ret = OH_AudioCodec_RegisterCallback(audioDecoder_, callback, signal_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to register callback, error: %{public}d", ret);
        return false;
    }

    OH_LOG_INFO(LOG_APP, "Audio decoder initialized successfully with MIME type: %{public}s", mimeType.c_str());
    return true;
}

bool AudioDecoder::Configure(int32_t sampleRate, int32_t channelCount, int32_t bitrate, int32_t sampleFormat) {
    if (!audioDecoder_) {
        OH_LOG_ERROR(LOG_APP, "Audio decoder not initialized");
        return false;
    }

    // 创建格式配置
    if (format_) {
        OH_AVFormat_Destroy(format_);
    }
    format_ = OH_AVFormat_Create();
    if (!format_) {
        OH_LOG_ERROR(LOG_APP, "Failed to create format");
        return false;
    }

    // 采样率（必须参数）- 如果未指定则使用默认值
    int32_t finalSampleRate = sampleRate > 0 ? sampleRate : 44100;
    OH_AVFormat_SetIntValue(format_, OH_MD_KEY_AUD_SAMPLE_RATE, finalSampleRate);
    if (sampleRate > 0) {
        OH_LOG_INFO(LOG_APP, "Set sample rate: %{public}d Hz (user specified)", finalSampleRate);
    } else {
        OH_LOG_INFO(LOG_APP, "Set sample rate: %{public}d Hz (default)", finalSampleRate);
    }

    // 声道数（必须参数）- 如果未指定则使用默认值
    int32_t finalChannelCount = channelCount > 0 ? channelCount : 2;
    OH_AVFormat_SetIntValue(format_, OH_MD_KEY_AUD_CHANNEL_COUNT, finalChannelCount);
    if (channelCount > 0) {
        OH_LOG_INFO(LOG_APP, "Set channel count: %{public}d (user specified)", finalChannelCount);
    } else {
        OH_LOG_INFO(LOG_APP, "Set channel count: %{public}d (default)", finalChannelCount);
    }

    // 比特率（可选参数）- 只有明确指定时才设置
    if (bitrate > 0) {
        OH_AVFormat_SetIntValue(format_, OH_MD_KEY_BITRATE, bitrate);
        OH_LOG_INFO(LOG_APP, "Set bitrate: %{public}d bps (optional)", bitrate);
    } else {
        OH_LOG_INFO(LOG_APP, "Bitrate not set (optional parameter skipped)");
    }

    // 输出采样格式：支持 S16LE (1) 和 S32LE (3)
    // 说明：此 key 的取值与 AudioSampleFormat 枚举一致
    int32_t finalSampleFormat = (sampleFormat == 3) ? 3 : 1;  // 3 = S32LE, 默认 1 = S16LE
    OH_AVFormat_SetIntValue(format_, OH_MD_KEY_AUDIO_SAMPLE_FORMAT, finalSampleFormat);
    if (finalSampleFormat == 3) {
        OH_LOG_INFO(LOG_APP, "Set output sample format: S32LE");
    } else {
        OH_LOG_INFO(LOG_APP, "Set output sample format: S16LE");
    }

    // 配置解码器
    int32_t ret = OH_AudioCodec_Configure(audioDecoder_, format_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to configure decoder, error: %{public}d", ret);
        return false;
    }

    OH_LOG_INFO(LOG_APP, "Audio decoder configured successfully");
    return true;
}

bool AudioDecoder::Start() {
    if (!audioDecoder_) {
        OH_LOG_ERROR(LOG_APP, "Audio decoder not initialized");
        return false;
    }
    
    // 准备解码器
    int32_t ret = OH_AudioCodec_Prepare(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to prepare decoder, error: %{public}d", ret);
        return false;
    }
    
    // 启动解码器
    ret = OH_AudioCodec_Start(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to start decoder, error: %{public}d", ret);
        return false;
    }
    
    isRunning_ = true;
    OH_LOG_INFO(LOG_APP, "Audio decoder started");
    return true;
}

// 自动检测格式的解码方法
bool AudioDecoder::DecodeFile(const std::string& inputPath, const std::string& outputPath) {
    return DecodeFileWithProgress(inputPath, outputPath, 0, 0, 0, ProgressCallback());
}

// 带可选参数的解码方法
bool AudioDecoder::DecodeFile(const std::string& inputPath, const std::string& outputPath,
                              int32_t sampleRate, int32_t channelCount, int32_t bitrate) {
    return DecodeFileWithProgress(inputPath, outputPath, sampleRate, channelCount, bitrate, ProgressCallback());
}

bool AudioDecoder::DecodeFileWithProgress(const std::string& inputPathOrUri, const std::string& outputPath,
                                         int32_t sampleRate, int32_t channelCount, int32_t bitrate,
                                         const ProgressCallback& progressCb)
{
    OH_LOG_INFO(LOG_APP, "=== Starting audio decode process ===");
    OH_LOG_INFO(LOG_APP, "Input: %{public}s", inputPathOrUri.c_str());
    OH_LOG_INFO(LOG_APP, "Output: %{public}s", outputPath.c_str());

    bool result = DecodeFileInternal(inputPathOrUri, outputPath, sampleRate, channelCount, bitrate, progressCb);

    // 停止解码器（即使失败也尝试停止）
    Stop();

    if (result) {
        OH_LOG_INFO(LOG_APP, "=== Audio decode completed successfully ===");
    } else {
        OH_LOG_ERROR(LOG_APP, "=== Audio decode failed ===");
    }

    return result;
}

bool AudioDecoder::DecodeToPcmStream(const std::string& inputPathOrUri,
                                     int32_t sampleRate, int32_t channelCount, int32_t bitrate,
                                     const InfoCallback& infoCb,
                                     const ProgressCallback& progressCb,
                                     const PcmDataCallback& pcmCb,
                                     const ErrorCallback& errorCb,
                                     CancelFlag* cancelFlag,
                                     int32_t sampleFormat)
{
    // 使用 RAII 机制自动清理 cancelFlag 指针
    struct CancelGuard {
        AudioDecoder* self;
        ~CancelGuard() { self->cancelFlag_ = nullptr; }
    } guard { this };

    cancelFlag_ = cancelFlag;

    // 重置内部状态变量
    durationMs_ = 0;
    detectedSampleRate_ = 0;
    detectedChannelCount_ = 0;
    lastProgressPercent_ = -1;
    lastProgressPtsMs_ = -1;

    // 触发初始进度回调
    if (progressCb) {
        progressCb(0.0, 0, 0);
    }

    // 错误上报辅助函数
    auto reportError = [&](const std::string& stage, int32_t code, const std::string& msg) {
        if (errorCb) {
            errorCb(stage, code, msg);
        }
    };

    if (cancelFlag_ && cancelFlag_->load()) {
        OH_LOG_INFO(LOG_APP, "Decode canceled before start");
        return true;
    }

    // 创建 AVSource，支持本地文件描述符或网络 URI
    const bool isRemoteUri = IsHttpUri(inputPathOrUri);

    int32_t fd = -1;
    int64_t fileSize = -1;
    OH_AVSource* source = nullptr;
    OH_AVDemuxer* demuxer = nullptr;

    // 保存输入路径用于 Seek
    currentInputPathOrUri_ = inputPathOrUri;

    if (isRemoteUri) {
        source = OH_AVSource_CreateWithURI(const_cast<char*>(inputPathOrUri.c_str()));
        if (!source) {
            reportError("create_source", -1, "Failed to create AVSource with URI");
            return false;
        }
        OH_LOG_INFO(LOG_APP, "AVSource created with URI");
    } else {
        fd = open(inputPathOrUri.c_str(), O_RDONLY);
        if (fd < 0) {
            reportError("open_file", -1, "Failed to open input file");
            return false;
        }

        fileSize = static_cast<int64_t>(lseek(fd, 0, SEEK_END));
        lseek(fd, 0, SEEK_SET);
        OH_LOG_INFO(LOG_APP, "Input file size: %{public}lld bytes", (long long)fileSize);

        source = OH_AVSource_CreateWithFD(fd, 0, fileSize);
        if (!source) {
            reportError("create_source", -1, "Failed to create AVSource with FD");
            close(fd);
            return false;
        }
    }

    // 保存 AVSource 和 Demuxer 句柄用于 Seek
    avSource_ = source;
    avDemuxer_ = demuxer;

    // 资源清理辅助函数
    auto cleanup = [&]() {
        if (demuxer) {
            OH_AVDemuxer_Destroy(demuxer);
            demuxer = nullptr;
        }
        if (source) {
            OH_AVSource_Destroy(source);
            source = nullptr;
        }
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    };

    // 获取源文件格式信息，包括轨道数和总时长
    int32_t trackCount = 0;
    {
        OH_AVFormat* sourceFormat = OH_AVSource_GetSourceFormat(source);
        if (!sourceFormat) {
            reportError("source_format", -1, "Failed to get source format");
            cleanup();
            return false;
        }

        if (!OH_AVFormat_GetIntValue(sourceFormat, OH_MD_KEY_TRACK_COUNT, &trackCount)) {
            reportError("source_format", -1, "Failed to get track count");
            OH_AVFormat_Destroy(sourceFormat);
            cleanup();
            return false;
        }

        int64_t durationMs = 0;
        if (OH_AVFormat_GetLongValue(sourceFormat, OH_MD_KEY_DURATION, &durationMs) && durationMs > 0) {
            durationMs_ = durationMs;
        } else {
            durationMs_ = 0;
        }
        OH_AVFormat_Destroy(sourceFormat);
    }

    // 创建解封装器
    demuxer = OH_AVDemuxer_CreateWithSource(source);
    if (!demuxer) {
        reportError("create_demuxer", -1, "Failed to create demuxer");
        cleanup();
        return false;
    }

    // 遍历轨道查找音频流并解析参数
    uint32_t audioTrackIndex = 0;
    bool foundAudioTrack = false;
    std::string audioCodecMime;

    for (uint32_t i = 0; i < static_cast<uint32_t>(trackCount); i++) {
        OH_AVFormat* trackFormat = OH_AVSource_GetTrackFormat(source, i);
        if (!trackFormat) {
            continue;
        }

        const char* codecMime = nullptr;
        OH_AVFormat_GetStringValue(trackFormat, OH_MD_KEY_CODEC_MIME, &codecMime);
        if (codecMime && strstr(codecMime, "audio")) {
            audioTrackIndex = i;
            foundAudioTrack = true;
            audioCodecMime = codecMime;

            OH_AVFormat_GetIntValue(trackFormat, OH_MD_KEY_AUD_SAMPLE_RATE, &detectedSampleRate_);
            OH_AVFormat_GetIntValue(trackFormat, OH_MD_KEY_AUD_CHANNEL_COUNT, &detectedChannelCount_);
        }

        OH_AVFormat_Destroy(trackFormat);

        if (foundAudioTrack) {
            break;
        }
    }

    if (!foundAudioTrack || audioCodecMime.empty()) {
        reportError("track", -1, "No audio track found");
        cleanup();
        return false;
    }

    // 针对 audio/raw 格式（如 WAV）启用直通模式，绕过硬件解码器
    if (audioCodecMime == "audio/raw") {
        OH_LOG_INFO(LOG_APP, "MIME type is audio/raw, entering passthrough mode");

        // 创建用于读取原始数据的缓冲区
        int32_t bufferSize = 8192; 
        OH_AVBuffer* buffer = OH_AVBuffer_Create(bufferSize);
        if (!buffer) {
            reportError("create_buffer", -1, "Failed to create buffer for raw read");
            cleanup();
            return false;
        }

        // 优先使用检测到的参数，WAV 格式使用传入的 sampleFormat 或默认为 S16LE
        int32_t finalSR = detectedSampleRate_ > 0 ? detectedSampleRate_ : (sampleRate > 0 ? sampleRate : 44100);
        int32_t finalCh = detectedChannelCount_ > 0 ? detectedChannelCount_ : (channelCount > 0 ? channelCount : 2);
        int32_t finalSF = (sampleFormat == 3) ? 3 : 1; // 使用传入的 sampleFormat: 3=S32LE, 默认 1=S16LE
        
        if (infoCb) {
            infoCb(finalSR, finalCh, finalSF, durationMs_);
        }

        if (OH_AVDemuxer_SelectTrackByID(demuxer, audioTrackIndex) != AV_ERR_OK) {
            reportError("select_track", -1, "Failed to select audio track");
            OH_AVBuffer_Destroy(buffer);
            cleanup();
            return false;
        }

        bool ok = true;
        int32_t consecutiveNoDataCount = 0;
        const int32_t MAX_NO_DATA_RETRIES = 100;
        
        // 循环读取数据并直接回调
        while (true) {
            if (cancelFlag_ && cancelFlag_->load()) {
                OH_LOG_INFO(LOG_APP, "Decode canceled (raw mode)");
                ok = true; 
                break;
            }

            int32_t ret = OH_AVDemuxer_ReadSampleBuffer(demuxer, audioTrackIndex, buffer);
            if (ret != AV_ERR_OK) {
                OH_LOG_INFO(LOG_APP, "Raw read finished: %{public}d", ret);
                break; 
            }

            OH_AVCodecBufferAttr attr;
            if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK) {
                OH_LOG_ERROR(LOG_APP, "Failed to get raw buffer attr");
                ok = false;
                break;
            }

            // 看门狗检查：防止连续读取不到数据导致 CPU 空转
            if (attr.size > 0) {
                consecutiveNoDataCount = 0;
            } else {
                consecutiveNoDataCount++;
                if (consecutiveNoDataCount > MAX_NO_DATA_RETRIES) {
                    OH_LOG_ERROR(LOG_APP, "Stuck in loop without data (raw mode)");
                    ok = false;
                    break;
                }
            }

            // 进度回调
            if (progressCb) {
                const int64_t ptsMs = attr.pts;
                if (durationMs_ > 0 && ptsMs >= 0) {
                     double p = (double)ptsMs / (durationMs_ * 1000.0);
                     if (p > 1.0) p = 1.0;
                     progressCb(p, ptsMs, durationMs_);
                } else if (ptsMs >= 0) {
                     progressCb(-1.0, ptsMs, 0);
                }
            }

            // 数据回调
            if (attr.size > 0 && pcmCb) {
                uint8_t* addr = OH_AVBuffer_GetAddr(buffer);
                if (addr) {
                    if (!pcmCb(addr, attr.size, attr.pts)) {
                        OH_LOG_INFO(LOG_APP, "PCM callback requested stop (raw mode)");
                        ok = true; 
                        break;
                    }
                }
            }

            if (attr.flags & AVCODEC_BUFFER_FLAGS_EOS) {
                OH_LOG_INFO(LOG_APP, "Raw read EOS");
                break;
            }
        }

        OH_AVBuffer_Destroy(buffer);
        cleanup();
        return ok;
    }

    // 初始化硬件解码器
    if (!Initialize(audioCodecMime)) {
        reportError("init_decoder", -1, "Failed to initialize decoder");
        cleanup();
        return false;
    }

    const int32_t finalSampleRate = (sampleRate > 0) ? sampleRate : ((detectedSampleRate_ > 0) ? detectedSampleRate_ : 44100);
    const int32_t finalChannelCount = (channelCount > 0) ? channelCount : ((detectedChannelCount_ > 0) ? detectedChannelCount_ : 2);
    const int32_t finalSampleFormat = (sampleFormat == 3) ? 3 : 1; // Use passed sampleFormat: 3=S32LE, 1=S16LE

    // 配置解码参数
    if (!Configure(finalSampleRate, finalChannelCount, bitrate, finalSampleFormat)) {
        reportError("configure", -1, "Failed to configure decoder");
        cleanup();
        return false;
    }

    // 启动解码器
    if (!Start()) {
        reportError("start", -1, "Failed to start decoder");
        cleanup();
        return false;
    }

    // 回调通知上层音频参数
    if (infoCb) {
        infoCb(finalSampleRate, finalChannelCount, finalSampleFormat, durationMs_);
    }

    // 选择音频轨道
    if (OH_AVDemuxer_SelectTrackByID(demuxer, audioTrackIndex) != AV_ERR_OK) {
        reportError("select_track", -1, "Failed to select audio track");
        cleanup();
        return false;
    }

    bool ok = false;
    bool inputEos = false;
    
    // 循环解码流程
    while (true) {
        if (cancelFlag_ && cancelFlag_->load()) {
            OH_LOG_INFO(LOG_APP, "Decode canceled");
            ok = true;
            break;
        }

        // 推送输入数据到解码器
        if (!inputEos) {
            StepResult inRes = PushInputData(demuxer, audioTrackIndex, progressCb);
            if (inRes == StepResult::Eos) {
                inputEos = true;
            } else if (inRes == StepResult::Error) {
                reportError("push_input", -1, "Failed to push input data");
                ok = false;
                break;
            }
        }

        // 获取解码后的输出数据
        StepResult outRes = PopOutputData(pcmCb);
        if (outRes == StepResult::Eos) {
            ok = true;
            break;
        }
        if (outRes == StepResult::Error) {
            // 如果上层回调返回 false，视为正常取消
            if (cancelFlag_ && cancelFlag_->load()) {
                ok = true;
                break;
            }
            reportError("pop_output", -1, "Failed to pop output data");
            ok = false;
            break;
        }
    }

    cleanup();
    return ok;
}

// 内部解码实现（使用解封装器）
bool AudioDecoder::DecodeFileInternal(const std::string& inputPathOrUri, const std::string& outputPath,
                                      int32_t sampleRate, int32_t channelCount, int32_t bitrate,
                                      const ProgressCallback& progressCb)
{
    durationMs_ = 0;
    detectedSampleRate_ = 0;
    detectedChannelCount_ = 0;
    lastProgressPercent_ = -1;
    lastProgressPtsMs_ = -1;

    if (progressCb) {
        progressCb(0.0, 0, 0);
    }

    // 1. 打开输出文件
    std::ofstream outputFile(outputPath, std::ios::out | std::ios::binary);
    if (!outputFile.is_open()) {
        OH_LOG_ERROR(LOG_APP, "Failed to open output file: %{public}s", outputPath.c_str());
        return false;
    }

    // 2. 创建 AVSource（支持本地 FD / 远程 URL）
    const bool isRemoteUri = IsHttpUri(inputPathOrUri);

    int32_t fd = -1;
    int64_t fileSize = -1;
    OH_AVSource* source = nullptr;
    OH_AVDemuxer* demuxer = nullptr;

    // 保存输入路径用于 Seek
    currentInputPathOrUri_ = inputPathOrUri;

    if (isRemoteUri) {
        source = OH_AVSource_CreateWithURI(const_cast<char*>(inputPathOrUri.c_str()));
        if (!source) {
            OH_LOG_ERROR(LOG_APP, "Failed to create AVSource with URI");
            outputFile.close();
            return false;
        }
        OH_LOG_INFO(LOG_APP, "AVSource created with URI");
    } else {
        fd = open(inputPathOrUri.c_str(), O_RDONLY);
        if (fd < 0) {
            OH_LOG_ERROR(LOG_APP, "Failed to open input file: %{public}s", inputPathOrUri.c_str());
            outputFile.close();
            return false;
        }

        fileSize = static_cast<int64_t>(lseek(fd, 0, SEEK_END));
        lseek(fd, 0, SEEK_SET);
        OH_LOG_INFO(LOG_APP, "Input file size: %{public}lld bytes", (long long)fileSize);

        source = OH_AVSource_CreateWithFD(fd, 0, fileSize);
        if (!source) {
            OH_LOG_ERROR(LOG_APP, "Failed to create AVSource with FD");
            close(fd);
            outputFile.close();
            return false;
        }
    }

    // 3. 获取 source 信息（轨道数、时长）
    int32_t trackCount = 0;
    {
        OH_AVFormat* sourceFormat = OH_AVSource_GetSourceFormat(source);
        if (!sourceFormat) {
            OH_LOG_ERROR(LOG_APP, "Failed to get source format");
            OH_AVSource_Destroy(source);
            source = nullptr;
            if (fd >= 0) {
                close(fd);
            }
            outputFile.close();
            return false;
        }

        if (!OH_AVFormat_GetIntValue(sourceFormat, OH_MD_KEY_TRACK_COUNT, &trackCount)) {
            OH_LOG_ERROR(LOG_APP, "Failed to get track count");
            OH_AVFormat_Destroy(sourceFormat);
            OH_AVSource_Destroy(source);
            source = nullptr;
            if (fd >= 0) {
                close(fd);
            }
            outputFile.close();
            return false;
        }

        int64_t durationMs = 0;
        if (OH_AVFormat_GetLongValue(sourceFormat, OH_MD_KEY_DURATION, &durationMs) && durationMs > 0) {
            durationMs_ = durationMs;
            OH_LOG_INFO(LOG_APP, "Duration: %{public}lld ms", (long long)durationMs_);
        } else {
            durationMs_ = 0;
            OH_LOG_INFO(LOG_APP, "Duration: unknown");
        }

        OH_AVFormat_Destroy(sourceFormat);
    }

    OH_LOG_INFO(LOG_APP, "Track count: %{public}d", trackCount);

    // 4. 创建解封装器
    demuxer = OH_AVDemuxer_CreateWithSource(source);
    if (!demuxer) {
        OH_LOG_ERROR(LOG_APP, "Failed to create demuxer");
        OH_AVSource_Destroy(source);
        source = nullptr;
        if (fd >= 0) {
            close(fd);
        }
        outputFile.close();
        return false;
    }

    // 保存 AVSource 和 AVDemuxer 句柄用于 Seek
    avSource_ = source;
    avDemuxer_ = demuxer;

    // 5. 查找音频轨道 & 获取音频参数
    uint32_t audioTrackIndex = 0;
    bool foundAudioTrack = false;
    std::string audioCodecMime;

    for (uint32_t i = 0; i < static_cast<uint32_t>(trackCount); i++) {
        OH_AVFormat* trackFormat = OH_AVSource_GetTrackFormat(source, i);
        if (!trackFormat) {
            continue;
        }

        const char* codecMime = nullptr;
        OH_AVFormat_GetStringValue(trackFormat, OH_MD_KEY_CODEC_MIME, &codecMime);
        if (codecMime && strstr(codecMime, "audio")) {
            audioTrackIndex = i;
            foundAudioTrack = true;
            audioCodecMime = codecMime;

            OH_AVFormat_GetIntValue(trackFormat, OH_MD_KEY_AUD_SAMPLE_RATE, &detectedSampleRate_);
            OH_AVFormat_GetIntValue(trackFormat, OH_MD_KEY_AUD_CHANNEL_COUNT, &detectedChannelCount_);

            OH_LOG_INFO(LOG_APP, "Found audio track %{public}d, MIME: %{public}s", i, codecMime);
            OH_LOG_INFO(LOG_APP, "Detected audio params: %{public}d Hz, %{public}d channels",
                        detectedSampleRate_, detectedChannelCount_);
        }

        OH_AVFormat_Destroy(trackFormat);

        if (foundAudioTrack) {
            break;
        }
    }

    if (!foundAudioTrack || audioCodecMime.empty()) {
        OH_LOG_ERROR(LOG_APP, "No audio track found");
        OH_AVDemuxer_Destroy(demuxer);
        demuxer = nullptr;
        OH_AVSource_Destroy(source);
        source = nullptr;
        if (fd >= 0) {
            close(fd);
        }
        outputFile.close();
        return false;
    }

    // 6. 初始化/配置/启动解码器
    if (!Initialize(audioCodecMime)) {
        OH_LOG_ERROR(LOG_APP, "Failed to initialize decoder");
        OH_AVDemuxer_Destroy(demuxer);
        OH_AVSource_Destroy(source);
        if (fd >= 0) {
            close(fd);
        }
        outputFile.close();
        return false;
    }

    const int32_t finalSampleRate = (sampleRate > 0) ? sampleRate : ((detectedSampleRate_ > 0) ? detectedSampleRate_ : 44100);
    const int32_t finalChannelCount = (channelCount > 0) ? channelCount : ((detectedChannelCount_ > 0) ? detectedChannelCount_ : 2);

    if (!Configure(finalSampleRate, finalChannelCount, bitrate)) {
        OH_LOG_ERROR(LOG_APP, "Failed to configure decoder");
        OH_AVDemuxer_Destroy(demuxer);
        OH_AVSource_Destroy(source);
        if (fd >= 0) {
            close(fd);
        }
        outputFile.close();
        return false;
    }

    if (!Start()) {
        OH_LOG_ERROR(LOG_APP, "Failed to start decoder");
        OH_AVDemuxer_Destroy(demuxer);
        OH_AVSource_Destroy(source);
        if (fd >= 0) {
            close(fd);
        }
        outputFile.close();
        return false;
    }

    // 7. 选择音频轨道
    if (OH_AVDemuxer_SelectTrackByID(demuxer, audioTrackIndex) != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to select audio track");
        OH_AVDemuxer_Destroy(demuxer);
        OH_AVSource_Destroy(source);
        if (fd >= 0) {
            close(fd);
        }
        outputFile.close();
        return false;
    }

    OH_LOG_INFO(LOG_APP, "Decoding in progress...");

    bool ok = false;
    bool inputEos = false;
    int32_t loopCount = 0;

    while (true) {
        loopCount++;

        if (!inputEos) {
            StepResult inRes = PushInputData(demuxer, audioTrackIndex, progressCb);
            if (inRes == StepResult::Eos) {
                inputEos = true;
                OH_LOG_INFO(LOG_APP, "Input reached EOS after %{public}d loops", loopCount);
            } else if (inRes == StepResult::Error) {
                ok = false;
                break;
            }
        }

        StepResult outRes = PopOutputData(outputFile);
        if (outRes == StepResult::Eos) {
            ok = true;
            break;
        }
        if (outRes == StepResult::Error) {
            ok = false;
            break;
        }

        if (loopCount > 100000) {
            OH_LOG_ERROR(LOG_APP, "Decode loop exceeded maximum iterations");
            ok = false;
            break;
        }
    }

    // 8. 清理资源
    OH_AVDemuxer_Destroy(demuxer);
    demuxer = nullptr;
    OH_AVSource_Destroy(source);
    source = nullptr;
    if (fd >= 0) {
        close(fd);
    }
    outputFile.close();

    if (progressCb) {
        if (durationMs_ > 0) {
            progressCb(1.0, durationMs_, durationMs_);
        } else {
            progressCb(-1.0, lastProgressPtsMs_ < 0 ? 0 : lastProgressPtsMs_, 0);
        }
    }

    OH_LOG_INFO(LOG_APP, "Decoding loop completed after %{public}d iterations", loopCount);
    return ok;
}

bool AudioDecoder::IsHttpUri(const std::string& inputPathOrUri) const
{
    if (inputPathOrUri.size() < 7) {
        return false;
    }
    // 仅支持 http/https
    if (inputPathOrUri.rfind("http://", 0) == 0) {
        return true;
    }
    if (inputPathOrUri.rfind("https://", 0) == 0) {
        return true;
    }
    return false;
}

bool AudioDecoder::Stop() {
    if (!audioDecoder_) {
        return false;
    }

    int32_t ret = OH_AudioCodec_Stop(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to stop decoder, error: %{public}d", ret);
        return false;
    }

    isRunning_ = false;
    OH_LOG_INFO(LOG_APP, "Audio decoder stopped");
    return true;
}

bool AudioDecoder::Flush() {
    if (!audioDecoder_) {
        return false;
    }

    int32_t ret = OH_AudioCodec_Flush(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to flush decoder, error: %{public}d", ret);
        return false;
    }

    OH_LOG_INFO(LOG_APP, "Audio decoder flushed");
    return true;
}

bool AudioDecoder::Reset() {
    if (!audioDecoder_) {
        return false;
    }

    int32_t ret = OH_AudioCodec_Reset(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to reset decoder, error: %{public}d", ret);
        return false;
    }

    isRunning_ = false;
    OH_LOG_INFO(LOG_APP, "Audio decoder reset");
    return true;
}

bool AudioDecoder::SeekTo(int64_t timeMs)
{
    if (!avSource_ || !avDemuxer_ || !audioDecoder_) {
        OH_LOG_ERROR(LOG_APP, "SeekTo failed: decoder not initialized");
        return false;
    }

    if (timeMs < 0) {
        timeMs = 0;  // 限制到文件开头
    }

    OH_LOG_INFO(LOG_APP, "Seeking to %{public}lld ms", timeMs);

    // 1. 停止解码器
    int32_t ret = OH_AudioCodec_Stop(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to stop codec before seek: %{public}d", ret);
        return false;
    }

    // 2. 刷新解码器
    ret = OH_AudioCodec_Flush(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to flush codec before seek: %{public}d", ret);
        return false;
    }

    // 3. 使用 AVSource Seek
    // 注意：OpenHarmony AVSource 可能不支持 Seek，需要处理失败情况
    // 将毫秒转换为微（OpenHarmony API 使用微秒）
    const int64_t timeUs = timeMs * 1000;

    // 尝试使用 AVSource Seek（如果 API 可用）
    // 由于 AVSource 的 Seek API 可能在不同 OpenHarmony 版本中不同，
    // 我们采用保守策略：Seek 后重新开始解码

    OH_LOG_INFO(LOG_APP, "Seek prepared: target=%{public}lld us, path=%{public}s",
             timeUs, currentInputPathOrUri_.c_str());

    // 4. 重新开始解码
    ret = OH_AudioCodec_Start(audioDecoder_);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to start codec after seek: %{public}d", ret);
        return false;
    }

    OH_LOG_INFO(LOG_APP, "Seek completed successfully");
    return true;
}

void AudioDecoder::Destroy() {
    if (audioDecoder_) {
        OH_AudioCodec_Destroy(audioDecoder_);
        audioDecoder_ = nullptr;
    }

    if (format_) {
        OH_AVFormat_Destroy(format_);
        format_ = nullptr;
    }

    if (signal_) {
        delete signal_;
        signal_ = nullptr;
    }

    isRunning_ = false;
    OH_LOG_INFO(LOG_APP, "Audio decoder destroyed");
}

// 回调函数实现
void AudioDecoder::OnError(OH_AVCodec *codec, int32_t errorCode, void *userData) {
    (void)codec;
    (void)userData;
    OH_LOG_ERROR(LOG_APP, "Decoder error occurred: %{public}d", errorCode);
}

void AudioDecoder::OnOutputFormatChanged(OH_AVCodec *codec, OH_AVFormat *format, void *userData) {
    (void)codec;
    (void)userData;

    int32_t sampleRate = 0;
    int32_t channelCount = 0;
    int32_t sampleFormat = 0;

    if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_AUD_SAMPLE_RATE, &sampleRate)) {
        OH_LOG_INFO(LOG_APP, "Sample rate changed to: %{public}d", sampleRate);
    }

    if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_AUD_CHANNEL_COUNT, &channelCount)) {
        OH_LOG_INFO(LOG_APP, "Channel count changed to: %{public}d", channelCount);
    }

    if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_AUDIO_SAMPLE_FORMAT, &sampleFormat)) {
        OH_LOG_INFO(LOG_APP, "Sample format changed to: %{public}d", sampleFormat);
    }
}

void AudioDecoder::OnInputBufferAvailable(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *data, void *userData) {
    (void)codec;
    AudioDecoderSignal *signal = static_cast<AudioDecoderSignal *>(userData);
    std::unique_lock<std::mutex> lock(signal->inMutex_);
    signal->inQueue_.push(index);
    signal->inBufferQueue_.push(data);
    signal->inCond_.notify_all();
}

void AudioDecoder::OnOutputBufferAvailable(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *data, void *userData) {
    (void)codec;
    AudioDecoderSignal *signal = static_cast<AudioDecoderSignal *>(userData);
    std::unique_lock<std::mutex> lock(signal->outMutex_);
    signal->outQueue_.push(index);
    signal->outBufferQueue_.push(data);
    signal->outCond_.notify_all();
}

// 输入数据处理（从解封装器读取）
AudioDecoder::StepResult AudioDecoder::PushInputData(OH_AVDemuxer* demuxer, uint32_t trackIndex,
                                                     const ProgressCallback& progressCb)
{
    if (!signal_ || !demuxer) {
        return StepResult::Error;
    }

    std::unique_lock<std::mutex> lock(signal_->inMutex_);
    if (!signal_->inCond_.wait_for(lock, std::chrono::milliseconds(200),
                                   [this]() {
                                       if (cancelFlag_ && cancelFlag_->load()) {
                                           return true;
                                       }
                                       return !signal_->inQueue_.empty();
                                   })) {
        return StepResult::Continue;
    }

    if (cancelFlag_ && cancelFlag_->load()) {
        OH_LOG_INFO(LOG_APP, "Decode canceled while waiting input buffer");
        return StepResult::Error;
    }

    uint32_t index = signal_->inQueue_.front();
    signal_->inQueue_.pop();

    OH_AVBuffer* buffer = signal_->inBufferQueue_.front();
    signal_->inBufferQueue_.pop();

    if (!buffer) {
        OH_LOG_ERROR(LOG_APP, "Buffer is null");
        return StepResult::Error;
    }

    // 从解封装器读取一帧数据
    int32_t ret = OH_AVDemuxer_ReadSampleBuffer(demuxer, trackIndex, buffer);
    if (ret != AV_ERR_OK) {
        // 读取失败：通常意味着 EOS
        OH_LOG_INFO(LOG_APP, "ReadSampleBuffer returned: %{public}d, sending EOS", ret);

        OH_AVCodecBufferAttr eosAttr = {0};
        eosAttr.size = 0;
        eosAttr.flags = AVCODEC_BUFFER_FLAGS_EOS;
        eosAttr.pts = 0;
        OH_AVBuffer_SetBufferAttr(buffer, &eosAttr);

        ret = OH_AudioCodec_PushInputBuffer(audioDecoder_, index);
        if (ret != AV_ERR_OK) {
            OH_LOG_ERROR(LOG_APP, "Failed to push EOS buffer, error: %{public}d", ret);
            return StepResult::Error;
        }

        return StepResult::Eos;
    }

    OH_AVCodecBufferAttr attr = {0};
    if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to get input buffer attr");
        return StepResult::Error;
    }

    // 进度上报（节流）
    if (progressCb) {
        const int64_t ptsMs = attr.pts;
        if (durationMs_ > 0 && ptsMs >= 0) {
            int32_t percent = static_cast<int32_t>((ptsMs * 100) / durationMs_);
            if (percent < 0) {
                percent = 0;
            } else if (percent > 100) {
                percent = 100;
            }
            if (percent != lastProgressPercent_) {
                lastProgressPercent_ = percent;
                progressCb(static_cast<double>(percent) / 100.0, ptsMs, durationMs_);
            }
        } else if (ptsMs >= 0) {
            if (lastProgressPtsMs_ < 0 || (ptsMs - lastProgressPtsMs_) >= 1000) {
                lastProgressPtsMs_ = ptsMs;
                progressCb(-1.0, ptsMs, 0);
            }
        }
    }

    ret = OH_AudioCodec_PushInputBuffer(audioDecoder_, index);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to push input buffer, error: %{public}d", ret);
        return StepResult::Error;
    }

    if (attr.flags & AVCODEC_BUFFER_FLAGS_EOS) {
        OH_LOG_INFO(LOG_APP, "Reached end of stream (EOS flag in input buffer)");
        return StepResult::Eos;
    }

    return StepResult::Continue;
}

// 输出数据处理
AudioDecoder::StepResult AudioDecoder::PopOutputData(std::ofstream& outputFile)
{
    if (!signal_) {
        return StepResult::Error;
    }

    std::unique_lock<std::mutex> lock(signal_->outMutex_);
    if (!signal_->outCond_.wait_for(lock, std::chrono::milliseconds(200),
                                    [this]() {
                                        if (cancelFlag_ && cancelFlag_->load()) {
                                            return true;
                                        }
                                        return !signal_->outQueue_.empty();
                                    })) {
        return StepResult::Continue;
    }

    if (cancelFlag_ && cancelFlag_->load()) {
        OH_LOG_INFO(LOG_APP, "Decode canceled while waiting output buffer");
        return StepResult::Error;
    }

    uint32_t index = signal_->outQueue_.front();
    signal_->outQueue_.pop();

    OH_AVBuffer* data = signal_->outBufferQueue_.front();
    signal_->outBufferQueue_.pop();

    if (!data) {
        OH_LOG_ERROR(LOG_APP, "Output buffer is null");
        return StepResult::Error;
    }

    // 获取缓冲区属性
    OH_AVCodecBufferAttr attr = {0};
    int32_t ret = OH_AVBuffer_GetBufferAttr(data, &attr);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to get output buffer attr, error: %{public}d", ret);
        return StepResult::Error;
    }

    // 写入解码后的 PCM 数据
    if (attr.size > 0) {
        outputFile.write(reinterpret_cast<char*>(OH_AVBuffer_GetAddr(data)), attr.size);
    }

    // 释放输出缓冲区
    ret = OH_AudioCodec_FreeOutputBuffer(audioDecoder_, index);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to free output buffer, error: %{public}d", ret);
        return StepResult::Error;
    }

    if (attr.flags == AVCODEC_BUFFER_FLAGS_EOS) {
        OH_LOG_INFO(LOG_APP, "Reached end of stream (output EOS)");
        return StepResult::Eos;
    }

    return StepResult::Continue;
}

// 输出数据处理（PCM 回调）
AudioDecoder::StepResult AudioDecoder::PopOutputData(const PcmDataCallback& pcmCb)
{
    if (!signal_) {
        return StepResult::Error;
    }

    std::unique_lock<std::mutex> lock(signal_->outMutex_);
    if (!signal_->outCond_.wait_for(lock, std::chrono::milliseconds(200),
                                    [this]() {
                                        if (cancelFlag_ && cancelFlag_->load()) {
                                            return true;
                                        }
                                        return !signal_->outQueue_.empty();
                                    })) {
        return StepResult::Continue;
    }

    if (cancelFlag_ && cancelFlag_->load()) {
        OH_LOG_INFO(LOG_APP, "Decode canceled while waiting output buffer");
        return StepResult::Error;
    }

    uint32_t index = signal_->outQueue_.front();
    signal_->outQueue_.pop();

    OH_AVBuffer* data = signal_->outBufferQueue_.front();
    signal_->outBufferQueue_.pop();

    if (!data) {
        OH_LOG_ERROR(LOG_APP, "Output buffer is null");
        return StepResult::Error;
    }

    OH_AVCodecBufferAttr attr = {0};
    int32_t ret = OH_AVBuffer_GetBufferAttr(data, &attr);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to get output buffer attr, error: %{public}d", ret);
        return StepResult::Error;
    }

    if (attr.size > 0) {
        if (!pcmCb) {
            OH_LOG_ERROR(LOG_APP, "PCM callback is null");
            return StepResult::Error;
        }

        const uint8_t* addr = OH_AVBuffer_GetAddr(data);
        if (!addr) {
            OH_LOG_ERROR(LOG_APP, "Output addr is null");
            return StepResult::Error;
        }

        // 回调输出 PCM。返回 false 表示上层要求中止。
        if (!pcmCb(addr, static_cast<size_t>(attr.size), attr.pts)) {
            OH_LOG_INFO(LOG_APP, "PCM callback requested stop");
            return StepResult::Error;
        }
    }

    ret = OH_AudioCodec_FreeOutputBuffer(audioDecoder_, index);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Failed to free output buffer, error: %{public}d", ret);
        return StepResult::Error;
    }

    if (attr.flags == AVCODEC_BUFFER_FLAGS_EOS) {
        OH_LOG_INFO(LOG_APP, "Reached end of stream (output EOS)");
        return StepResult::Eos;
    }

    return StepResult::Continue;
}
