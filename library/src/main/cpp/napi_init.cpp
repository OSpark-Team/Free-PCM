#include "napi/native_api.h"
#include "audio_decoder.h"
#include "pcm_equalizer.h"
#include <hilog/log.h>
#include <memory>
#include <array>
#include <algorithm>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <cmath>
#include <string>

static napi_value CreateErrorObject(napi_env env, const std::string& stage, int32_t code, const std::string& message)
{
    napi_value msg;
    napi_create_string_utf8(env, message.c_str(), NAPI_AUTO_LENGTH, &msg);

    napi_value err;
    napi_create_error(env, nullptr, msg, &err);

    napi_value stageVal;
    napi_create_string_utf8(env, stage.c_str(), NAPI_AUTO_LENGTH, &stageVal);
    napi_set_named_property(env, err, "stage", stageVal);

    napi_value codeVal;
    napi_create_int32(env, code, &codeVal);
    napi_set_named_property(env, err, "code", codeVal);

    return err;
}

#undef LOG_TAG
#define LOG_TAG "NapiInit"

// 解码音频文件的 NAPI 接口（自动检测格式）
static napi_value DecodeAudio(napi_env env, napi_callback_info info) {
    size_t argc = 5;
    napi_value args[5] = {nullptr};

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) {
        napi_throw_error(env, nullptr, "At least 2 arguments required: inputPath, outputPath");
        return nullptr;
    }

    // 获取输入文件路径
    size_t inputPathLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &inputPathLen);
    char* inputPath = new char[inputPathLen + 1];
    napi_get_value_string_utf8(env, args[0], inputPath, inputPathLen + 1, &inputPathLen);

    // 获取输出文件路径
    size_t outputPathLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &outputPathLen);
    char* outputPath = new char[outputPathLen + 1];
    napi_get_value_string_utf8(env, args[1], outputPath, outputPathLen + 1, &outputPathLen);

    // 获取可选参数（如果提供）
    int32_t sampleRate = 0;
    int32_t channelCount = 0;
    int32_t bitrate = 0;

    if (argc >= 3) {
        napi_get_value_int32(env, args[2], &sampleRate);
    }
    if (argc >= 4) {
        napi_get_value_int32(env, args[3], &channelCount);
    }
    if (argc >= 5) {
        napi_get_value_int32(env, args[4], &bitrate);
    }

    OH_LOG_INFO(LOG_APP, "DecodeAudio called:");
    OH_LOG_INFO(LOG_APP, "  Input: %{public}s", inputPath);
    OH_LOG_INFO(LOG_APP, "  Output: %{public}s", outputPath);
    OH_LOG_INFO(LOG_APP, "  SampleRate: %{public}d (0=auto)", sampleRate);
    OH_LOG_INFO(LOG_APP, "  ChannelCount: %{public}d (0=auto)", channelCount);
    OH_LOG_INFO(LOG_APP, "  Bitrate: %{public}d (0=auto)", bitrate);

    // 创建解码器并执行解码
    AudioDecoder decoder;
    bool success = decoder.DecodeFile(inputPath, outputPath, sampleRate, channelCount, bitrate);

    delete[] inputPath;
    delete[] outputPath;

    // 返回结果
    napi_value result;
    napi_get_boolean(env, success, &result);
    return result;
}

struct DecodeAudioProgressPayload {
    double progress;
    int64_t ptsMs;
    int64_t durationMs;
};

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

static void CallJsProgress(napi_env env, napi_value jsCallback, void* /*context*/, void* data)
{
    std::unique_ptr<DecodeAudioProgressPayload> payload(static_cast<DecodeAudioProgressPayload*>(data));
    if (env == nullptr || jsCallback == nullptr || payload == nullptr) {
        return;
    }

    napi_value progressObj;
    napi_create_object(env, &progressObj);

    napi_value progress;
    napi_create_double(env, payload->progress, &progress);
    napi_set_named_property(env, progressObj, "progress", progress);

    napi_value ptsMs;
    napi_create_double(env, static_cast<double>(payload->ptsMs), &ptsMs);
    napi_set_named_property(env, progressObj, "ptsMs", ptsMs);

    napi_value durationMs;
    napi_create_double(env, static_cast<double>(payload->durationMs), &durationMs);
    napi_set_named_property(env, progressObj, "durationMs", durationMs);

    napi_value argv[1] = { progressObj };
    napi_value result;
    napi_call_function(env, nullptr, jsCallback, 1, argv, &result);
}

static void ExecuteDecodeAudioAsync(napi_env /*env*/, void* data)
{
    auto* ctx = static_cast<DecodeAudioAsyncContext*>(data);
    if (!ctx) {
        return;
    }

    AudioDecoder decoder;

    AudioDecoder::ProgressCallback cb;
    if (ctx->tsfn != nullptr) {
        cb = [ctx](double progress, int64_t ptsMs, int64_t durationMs) {
            auto payload = std::make_unique<DecodeAudioProgressPayload>();
            payload->progress = progress;
            payload->ptsMs = ptsMs;
            payload->durationMs = durationMs;

            napi_status st = napi_call_threadsafe_function(ctx->tsfn, payload.get(), napi_tsfn_nonblocking);
            if (st == napi_ok) {
                (void)payload.release();
            }
        };
    }

    ctx->success = decoder.DecodeFileWithProgress(
        ctx->inputPathOrUri,
        ctx->outputPath,
        ctx->sampleRate,
        ctx->channelCount,
        ctx->bitrate,
        cb);
}

static void CompleteDecodeAudioAsync(napi_env env, napi_status /*status*/, void* data)
{
    auto* ctx = static_cast<DecodeAudioAsyncContext*>(data);
    if (!ctx) {
        return;
    }

    if (ctx->tsfn != nullptr) {
        napi_release_threadsafe_function(ctx->tsfn, napi_tsfn_release);
        ctx->tsfn = nullptr;
    }

    if (ctx->success) {
        napi_value result;
        napi_get_boolean(env, true, &result);
        napi_resolve_deferred(env, ctx->deferred, result);
    } else {
        napi_value errObj = CreateErrorObject(env, "decode_to_file", -1, "Decode failed");
        napi_reject_deferred(env, ctx->deferred, errObj);
    }

    napi_delete_async_work(env, ctx->work);
    delete ctx;
}

// 解码音频文件的 NAPI 异步接口（Promise + 进度回调）
// decodeAudioAsync(inputPathOrUri, outputPath, onProgress?, sampleRate?, channelCount?, bitrate?) => Promise<boolean>
static napi_value DecodeAudioAsync(napi_env env, napi_callback_info info)
{
    size_t argc = 6;
    napi_value args[6] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) {
        napi_throw_error(env, nullptr, "At least 2 arguments required: inputPathOrUri, outputPath");
        return nullptr;
    }

    // inputPathOrUri
    size_t inputLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &inputLen);
    std::string input;
    input.resize(inputLen + 1);
    napi_get_value_string_utf8(env, args[0], &input[0], inputLen + 1, &inputLen);
    input.resize(inputLen);

    // outputPath
    size_t outputLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &outputLen);
    std::string output;
    output.resize(outputLen + 1);
    napi_get_value_string_utf8(env, args[1], &output[0], outputLen + 1, &outputLen);
    output.resize(outputLen);

    // optional onProgress
    napi_value progressCb = nullptr;
    bool hasProgressCb = false;
    if (argc >= 3) {
        napi_valuetype t;
        napi_typeof(env, args[2], &t);
        if (t == napi_function) {
            progressCb = args[2];
            hasProgressCb = true;
        }
    }

    int32_t sampleRate = 0;
    int32_t channelCount = 0;
    int32_t bitrate = 0;
    if (argc >= 4) {
        napi_get_value_int32(env, args[3], &sampleRate);
    }
    if (argc >= 5) {
        napi_get_value_int32(env, args[4], &channelCount);
    }
    if (argc >= 6) {
        napi_get_value_int32(env, args[5], &bitrate);
    }

    auto* ctx = new DecodeAudioAsyncContext();
    ctx->env = env;
    ctx->work = nullptr;
    ctx->deferred = nullptr;
    ctx->tsfn = nullptr;
    ctx->inputPathOrUri = input;
    ctx->outputPath = output;
    ctx->sampleRate = sampleRate;
    ctx->channelCount = channelCount;
    ctx->bitrate = bitrate;
    ctx->success = false;

    napi_value promise;
    napi_create_promise(env, &ctx->deferred, &promise);

    if (hasProgressCb) {
        napi_value resourceName;
        napi_create_string_utf8(env, "DecodeAudioProgress", NAPI_AUTO_LENGTH, &resourceName);
        napi_create_threadsafe_function(
            env,
            progressCb,
            nullptr,
            resourceName,
            0,
            1,
            nullptr,
            nullptr,
            nullptr,
            CallJsProgress,
            &ctx->tsfn);
    }

    napi_value workName;
    napi_create_string_utf8(env, "DecodeAudioAsync", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, ExecuteDecodeAudioAsync, CompleteDecodeAudioAsync, ctx, &ctx->work);
    napi_queue_async_work(env, ctx->work);

    return promise;
}

namespace {
class PcmRingBuffer {
public:
    explicit PcmRingBuffer(size_t capacity)
        : buf_(capacity), head_(0), tail_(0), size_(0), eos_(false), canceled_(false) {}

    void Cancel()
    {
        {
            std::lock_guard<std::mutex> lock(mu_);
            canceled_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void MarkEos()
    {
        {
            std::lock_guard<std::mutex> lock(mu_);
            eos_ = true;
        }
        notEmpty_.notify_all();
    }

    bool IsEos() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return eos_ && size_ == 0;
    }

    bool Push(const uint8_t* data, size_t len, const std::atomic<bool>* cancelFlag)
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

    size_t Read(uint8_t* dst, size_t len)
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

        notFull_.notify_all();
        return n;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(mu_);
        head_ = 0;
        tail_ = 0;
        size_ = 0;
        notFull_.notify_all();
    }

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
};

enum class DecoderEventType {
    Ready = 0,
    Progress = 1,
    Error = 2,
};

struct DecoderEventPayload {
    DecoderEventType type;

    // Ready
    int32_t sampleRate;
    int32_t channelCount;
    int32_t sampleFormat;
    int64_t durationMs;

    // Progress
    double progress;
    int64_t ptsMs;

    // Error
    std::string stage;
    int32_t code;
    std::string message;
};

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

    std::atomic<bool> cancel;
    bool success;
    bool readySettled;

    // for rejecting done
    std::string lastErrStage;
    int32_t lastErrCode;
    std::string lastErrMessage;

    std::unique_ptr<PcmRingBuffer> ring;

    // EQ (10-band) configuration shared with JS thread.
    std::atomic<bool> eqEnabled;
    std::atomic<uint32_t> eqVersion;
    std::array<std::atomic<int32_t>, PcmEqualizer::kBandCount> eqGainsDb100;

    // Worker thread state.
    uint32_t eqAppliedVersion;
    int32_t eqSampleRate;
    int32_t eqChannelCount;
    PcmEqualizer eq;

    std::vector<int16_t> eqScratch;
};

static void CallJsDecoderEvent(napi_env env, napi_value /*jsCb*/, void* context, void* data)
{
    auto* ctx = static_cast<PcmStreamDecoderContext*>(context);
    std::unique_ptr<DecoderEventPayload> payload(static_cast<DecoderEventPayload*>(data));
    if (!ctx || !payload || env == nullptr) {
        return;
    }

    switch (payload->type) {
        case DecoderEventType::Ready: {
            if (ctx->readyDeferred == nullptr) {
                ctx->readySettled = true;
                break;
            }

            napi_value info;
            napi_create_object(env, &info);

            napi_value sr;
            napi_create_int32(env, payload->sampleRate, &sr);
            napi_set_named_property(env, info, "sampleRate", sr);

            napi_value cc;
            napi_create_int32(env, payload->channelCount, &cc);
            napi_set_named_property(env, info, "channelCount", cc);

            // sampleFormat: use string for ArkTS friendliness
            napi_value sf;
            if (payload->sampleFormat == 1) {
                napi_create_string_utf8(env, "s16le", NAPI_AUTO_LENGTH, &sf);
            } else {
                napi_create_string_utf8(env, "unknown", NAPI_AUTO_LENGTH, &sf);
            }
            napi_set_named_property(env, info, "sampleFormat", sf);

            napi_value dur;
            napi_create_double(env, static_cast<double>(payload->durationMs), &dur);
            napi_set_named_property(env, info, "durationMs", dur);

            napi_resolve_deferred(env, ctx->readyDeferred, info);
            ctx->readyDeferred = nullptr;
            ctx->readySettled = true;
            break;
        }
        case DecoderEventType::Progress: {
            if (ctx->onProgressRef == nullptr) {
                break;
            }
            napi_value cb;
            napi_get_reference_value(env, ctx->onProgressRef, &cb);
            if (cb == nullptr) {
                break;
            }

            napi_value arg;
            napi_create_object(env, &arg);

            napi_value p;
            napi_create_double(env, payload->progress, &p);
            napi_set_named_property(env, arg, "progress", p);

            napi_value pts;
            napi_create_double(env, static_cast<double>(payload->ptsMs), &pts);
            napi_set_named_property(env, arg, "ptsMs", pts);

            napi_value dur;
            napi_create_double(env, static_cast<double>(payload->durationMs), &dur);
            napi_set_named_property(env, arg, "durationMs", dur);

            napi_value argv[1] = {arg};
            napi_value result;
            napi_call_function(env, nullptr, cb, 1, argv, &result);
            break;
        }
        case DecoderEventType::Error: {
            // Store for done rejection.
            ctx->lastErrStage = payload->stage;
            ctx->lastErrCode = payload->code;
            ctx->lastErrMessage = payload->message;

            napi_value errObj = CreateErrorObject(env, payload->stage, payload->code, payload->message);

            if (ctx->readyDeferred != nullptr) {
                napi_reject_deferred(env, ctx->readyDeferred, errObj);
                ctx->readyDeferred = nullptr;
                ctx->readySettled = true;
            }

            if (ctx->onErrorRef != nullptr) {
                napi_value cb;
                napi_get_reference_value(env, ctx->onErrorRef, &cb);
                if (cb != nullptr) {
                    napi_value argv[1] = {errObj};
                    napi_value result;
                    napi_call_function(env, nullptr, cb, 1, argv, &result);
                }
            }

            break;
        }
        default:
            break;
    }
}

static napi_value PcmDecoderFill(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "fill(buffer) requires 1 argument");
        return nullptr;
    }

    bool isArrayBuffer = false;
    napi_is_arraybuffer(env, args[0], &isArrayBuffer);
    if (!isArrayBuffer) {
        napi_throw_error(env, nullptr, "fill(buffer) expects an ArrayBuffer");
        return nullptr;
    }

    void* buf = nullptr;
    size_t len = 0;
    napi_get_arraybuffer_info(env, args[0], &buf, &len);
    if (!buf || len == 0) {
        napi_value zero;
        napi_create_int32(env, 0, &zero);
        return zero;
    }

    size_t n = 0;
    if (ctx->ring) {
        n = ctx->ring->Read(reinterpret_cast<uint8_t*>(buf), len);
    }
    if (n < len) {
        memset(reinterpret_cast<uint8_t*>(buf) + n, 0, len - n);
    }

    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(n), &out);
    return out;
}

static napi_value PcmDecoderClose(napi_env env, napi_callback_info info)
{
    size_t argc = 0;
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data);
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (ctx) {
        ctx->cancel.store(true);
        if (ctx->ring) {
            ctx->ring->Cancel();
        }
    }
    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

static napi_value PcmDecoderSetEqEnabled(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "setEqEnabled(enabled) requires 1 argument");
        return nullptr;
    }

    bool enabled = false;
    napi_get_value_bool(env, args[0], &enabled);
    ctx->eqEnabled.store(enabled);
    
    // Force worker to re-apply config.
    ctx->eqVersion.fetch_add(1);

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

static napi_value PcmDecoderSetEqGains(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "setEqGains(gainsDb) requires 1 argument");
        return nullptr;
    }

    bool isArray = false;
    napi_is_array(env, args[0], &isArray);
    if (!isArray) {
        napi_throw_error(env, nullptr, "setEqGains expects an array of 10 numbers");
        return nullptr;
    }

    uint32_t len = 0;
    napi_get_array_length(env, args[0], &len);
    if (len != static_cast<uint32_t>(PcmEqualizer::kBandCount)) {
        napi_throw_error(env, nullptr, "setEqGains expects exactly 10 bands");
        return nullptr;
    }

    for (uint32_t i = 0; i < len; i++) {
        napi_value v;
        napi_get_element(env, args[0], i, &v);
        double gainDb = 0.0;
        if (napi_get_value_double(env, v, &gainDb) != napi_ok) {
            int32_t gi = 0;
            if (napi_get_value_int32(env, v, &gi) != napi_ok) {
                napi_throw_error(env, nullptr, "setEqGains values must be numbers");
                return nullptr;
            }
            gainDb = static_cast<double>(gi);
        }

        // Clamp to a safe range.
        if (gainDb > 24.0) {
            gainDb = 24.0;
        } else if (gainDb < -24.0) {
            gainDb = -24.0;
        }

        const int32_t gain100 = static_cast<int32_t>(std::lround(gainDb * 100.0));
        ctx->eqGainsDb100[i].store(gain100);
    }
    
    ctx->eqVersion.fetch_add(1);

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

static void ExecutePcmStreamDecode(napi_env /*env*/, void* data)
{
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx) {
        return;
    }

    ctx->success = false;

    AudioDecoder decoder;

    AudioDecoder::InfoCallback infoCb = [ctx](int32_t sr, int32_t cc, int32_t sf, int64_t durMs) {
        ctx->eqSampleRate = sr;
        ctx->eqChannelCount = cc;
        ctx->eqAppliedVersion = 0;
        ctx->eq.Init(sr, cc);
        ctx->eq.SetEnabled(ctx->eqEnabled.load());

        auto payload = std::make_unique<DecoderEventPayload>();
        payload->type = DecoderEventType::Ready;
        payload->sampleRate = sr;
        payload->channelCount = cc;
        payload->sampleFormat = sf;
        payload->durationMs = durMs;
        payload->progress = 0.0;
        payload->ptsMs = 0;
        payload->stage = "";
        payload->code = 0;
        payload->message = "";

        napi_status st = napi_call_threadsafe_function(ctx->eventTsfn, payload.get(), napi_tsfn_nonblocking);
        if (st == napi_ok) {
            (void)payload.release();
        }
    };

    AudioDecoder::ProgressCallback progressCb = [ctx](double progress, int64_t ptsMs, int64_t durationMs) {
        if (ctx->eventTsfn == nullptr) {
            return;
        }
        auto payload = std::make_unique<DecoderEventPayload>();
        payload->type = DecoderEventType::Progress;
        payload->sampleRate = 0;
        payload->channelCount = 0;
        payload->sampleFormat = 0;
        payload->durationMs = durationMs;
        payload->progress = progress;
        payload->ptsMs = ptsMs;
        payload->stage = "";
        payload->code = 0;
        payload->message = "";

        napi_status st = napi_call_threadsafe_function(ctx->eventTsfn, payload.get(), napi_tsfn_nonblocking);
        if (st == napi_ok) {
            (void)payload.release();
        }
    };

    AudioDecoder::PcmDataCallback pcmCb = [ctx](const uint8_t* pcm, size_t size, int64_t /*ptsMs*/) {
        if (ctx->cancel.load()) {
            return false;
        }
        if (!ctx->ring) {
            return false;
        }

        const bool eqEnabled = ctx->eqEnabled.load();
        if (!eqEnabled || !ctx->eq.IsReady() || size < 2) {
            return ctx->ring->Push(pcm, size, &ctx->cancel);
        }

        const uint32_t v = ctx->eqVersion.load();
        if (v != ctx->eqAppliedVersion) {
            std::array<float, PcmEqualizer::kBandCount> gains;
            for (size_t i = 0; i < PcmEqualizer::kBandCount; i++) {
                gains[i] = static_cast<float>(ctx->eqGainsDb100[i].load()) / 100.0f;
            }
            ctx->eq.SetGainsDb(gains);
            ctx->eqAppliedVersion = v;
        }
        ctx->eq.SetEnabled(true);

        const size_t sampleCount = size / 2;
        const int32_t ch = ctx->eqChannelCount;
        if (ch != 1 && ch != 2) {
            return ctx->ring->Push(pcm, size, &ctx->cancel);
        }
        const size_t frameCount = sampleCount / static_cast<size_t>(ch);
        const size_t bytesToProcess = frameCount * static_cast<size_t>(ch) * 2;
        if (bytesToProcess == 0) {
            return ctx->ring->Push(pcm, size, &ctx->cancel);
        }

        ctx->eqScratch.resize(sampleCount);
        memcpy(ctx->eqScratch.data(), pcm, bytesToProcess);
        if (bytesToProcess < size) {
            // keep the tail bytes (should be rare)
            memcpy(reinterpret_cast<uint8_t*>(ctx->eqScratch.data()) + bytesToProcess, pcm + bytesToProcess, size - bytesToProcess);
        }

        ctx->eq.Process(ctx->eqScratch.data(), frameCount);

        return ctx->ring->Push(reinterpret_cast<const uint8_t*>(ctx->eqScratch.data()), size, &ctx->cancel);
    };

    AudioDecoder::ErrorCallback errorCb = [ctx](const std::string& stage, int32_t code, const std::string& message) {
        auto payload = std::make_unique<DecoderEventPayload>();
        payload->type = DecoderEventType::Error;
        payload->sampleRate = 0;
        payload->channelCount = 0;
        payload->sampleFormat = 0;
        payload->durationMs = 0;
        payload->progress = 0.0;
        payload->ptsMs = 0;
        payload->stage = stage;
        payload->code = code;
        payload->message = message;

        napi_status st = napi_call_threadsafe_function(ctx->eventTsfn, payload.get(), napi_tsfn_nonblocking);
        if (st == napi_ok) {
            (void)payload.release();
        }
    };

    bool ok = decoder.DecodeToPcmStream(
        ctx->inputPathOrUri,
        ctx->sampleRate,
        ctx->channelCount,
        ctx->bitrate,
        infoCb,
        progressCb,
        pcmCb,
        errorCb,
        &ctx->cancel);

    ctx->success = ok;
    if (ctx->ring) {
        ctx->ring->MarkEos();
    }
}

static void CompletePcmStreamDecode(napi_env env, napi_status /*status*/, void* data)
{
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx) {
        return;
    }

    // If ready wasn't settled (e.g. very early failure), reject it.
    if (!ctx->readySettled && ctx->readyDeferred != nullptr) {
        napi_value errObj = CreateErrorObject(env, "ready", -1, "Decoder failed before ready");
        napi_reject_deferred(env, ctx->readyDeferred, errObj);
        ctx->readyDeferred = nullptr;
        ctx->readySettled = true;
    }

    if (ctx->doneDeferred != nullptr) {
        if (ctx->cancel.load()) {
            napi_value undef;
            napi_get_undefined(env, &undef);
            napi_resolve_deferred(env, ctx->doneDeferred, undef);
        } else if (ctx->success) {
            napi_value undef;
            napi_get_undefined(env, &undef);
            napi_resolve_deferred(env, ctx->doneDeferred, undef);
        } else {
            std::string stage = ctx->lastErrStage.empty() ? "decode" : ctx->lastErrStage;
            int32_t code = ctx->lastErrCode;
            std::string message = ctx->lastErrMessage.empty() ? "Decode failed" : ctx->lastErrMessage;
            napi_value errObj = CreateErrorObject(env, stage, code, message);
            napi_reject_deferred(env, ctx->doneDeferred, errObj);
        }
        ctx->doneDeferred = nullptr;
    }

    if (ctx->eventTsfn != nullptr) {
        napi_release_threadsafe_function(ctx->eventTsfn, napi_tsfn_release);
        ctx->eventTsfn = nullptr;
    }

    if (ctx->selfRef != nullptr) {
        napi_delete_reference(env, ctx->selfRef);
        ctx->selfRef = nullptr;
    }

    napi_delete_async_work(env, ctx->work);
    ctx->work = nullptr;
}

static void FinalizePcmStreamDecoder(napi_env env, void* finalize_data, void* /*finalize_hint*/)
{
    auto* ctx = static_cast<PcmStreamDecoderContext*>(finalize_data);
    if (!ctx) {
        return;
    }

    ctx->cancel.store(true);
    if (ctx->ring) {
        ctx->ring->Cancel();
    }

    if (ctx->onProgressRef != nullptr) {
        napi_delete_reference(env, ctx->onProgressRef);
        ctx->onProgressRef = nullptr;
    }
    if (ctx->onErrorRef != nullptr) {
        napi_delete_reference(env, ctx->onErrorRef);
        ctx->onErrorRef = nullptr;
    }

    delete ctx;
}

// createPcmStreamDecoder(inputPathOrUri, options?, callbacks?)
// returns { ready: Promise<StreamInfo>, done: Promise<void>, fill(buf: ArrayBuffer): number, close(): void }
static napi_value CreatePcmStreamDecoder(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        napi_throw_error(env, nullptr, "createPcmStreamDecoder requires inputPathOrUri");
        return nullptr;
    }

    // inputPathOrUri
    size_t inputLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &inputLen);
    std::string input;
    input.resize(inputLen + 1);
    napi_get_value_string_utf8(env, args[0], &input[0], inputLen + 1, &inputLen);
    input.resize(inputLen);

    int32_t sampleRate = 0;
    int32_t channelCount = 0;
    int32_t bitrate = 0;
    size_t ringBytes = 512 * 1024; // default

    bool optEqEnabled = false;
    bool hasEqGains = false;
    std::array<int32_t, PcmEqualizer::kBandCount> optEqGainsDb100 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // options
    if (argc >= 2 && args[1] != nullptr) {
        napi_valuetype t;
        napi_typeof(env, args[1], &t);
        if (t == napi_object) {
            napi_value v;
            if (napi_get_named_property(env, args[1], "sampleRate", &v) == napi_ok) {
                napi_get_value_int32(env, v, &sampleRate);
            }
            if (napi_get_named_property(env, args[1], "channelCount", &v) == napi_ok) {
                napi_get_value_int32(env, v, &channelCount);
            }
            if (napi_get_named_property(env, args[1], "bitrate", &v) == napi_ok) {
                napi_get_value_int32(env, v, &bitrate);
            }
            if (napi_get_named_property(env, args[1], "ringBytes", &v) == napi_ok) {
                int32_t rb = 0;
                if (napi_get_value_int32(env, v, &rb) == napi_ok && rb > 0) {
                    ringBytes = static_cast<size_t>(rb);
                }
            }

            if (napi_get_named_property(env, args[1], "eqEnabled", &v) == napi_ok) {
                bool b = false;
                if (napi_get_value_bool(env, v, &b) == napi_ok) {
                    optEqEnabled = b;
                }
            }

            if (napi_get_named_property(env, args[1], "eqGainsDb", &v) == napi_ok) {
                bool isArray = false;
                napi_is_array(env, v, &isArray);
                if (isArray) {
                    uint32_t alen = 0;
                    napi_get_array_length(env, v, &alen);
                    if (alen == static_cast<uint32_t>(PcmEqualizer::kBandCount)) {
                        hasEqGains = true;
                        for (uint32_t i = 0; i < alen; i++) {
                            napi_value ev;
                            napi_get_element(env, v, i, &ev);
                            double gainDb = 0.0;
                            if (napi_get_value_double(env, ev, &gainDb) != napi_ok) {
                                int32_t gi = 0;
                                if (napi_get_value_int32(env, ev, &gi) == napi_ok) {
                                    gainDb = static_cast<double>(gi);
                                }
                            }
                            if (gainDb > 24.0) {
                                gainDb = 24.0;
                            } else if (gainDb < -24.0) {
                                gainDb = -24.0;
                            }
                            optEqGainsDb100[i] = static_cast<int32_t>(std::lround(gainDb * 100.0));
                        }
                    }
                }
            }
        }
    }

    // callbacks
    napi_value onProgress = nullptr;
    napi_value onError = nullptr;
    if (argc >= 3 && args[2] != nullptr) {
        napi_valuetype t;
        napi_typeof(env, args[2], &t);
        if (t == napi_object) {
            napi_get_named_property(env, args[2], "onProgress", &onProgress);
            napi_get_named_property(env, args[2], "onError", &onError);
        }
    }

    auto* ctx = new PcmStreamDecoderContext();
    ctx->env = env;
    ctx->work = nullptr;
    ctx->eventTsfn = nullptr;
    ctx->readyDeferred = nullptr;
    ctx->doneDeferred = nullptr;
    ctx->selfRef = nullptr;
    ctx->onProgressRef = nullptr;
    ctx->onErrorRef = nullptr;
    ctx->inputPathOrUri = input;
    ctx->sampleRate = sampleRate;
    ctx->channelCount = channelCount;
    ctx->bitrate = bitrate;
    ctx->cancel.store(false);
    ctx->success = false;
    ctx->readySettled = false;
    ctx->lastErrStage = "";
    ctx->lastErrCode = -1;
    ctx->lastErrMessage = "";
    ctx->ring = std::make_unique<PcmRingBuffer>(ringBytes);

    ctx->eqEnabled.store(optEqEnabled);
    ctx->eqVersion.store(1);
    for (size_t i = 0; i < PcmEqualizer::kBandCount; i++) {
        ctx->eqGainsDb100[i].store(hasEqGains ? optEqGainsDb100[i] : 0);
    }
    ctx->eqAppliedVersion = 0;
    ctx->eqSampleRate = 0;
    ctx->eqChannelCount = 0;

    napi_value decoderObj;
    napi_create_object(env, &decoderObj);

    // Keep object alive while decode running.
    napi_create_reference(env, decoderObj, 1, &ctx->selfRef);

    // ready promise
    napi_value readyPromise;
    napi_create_promise(env, &ctx->readyDeferred, &readyPromise);
    napi_set_named_property(env, decoderObj, "ready", readyPromise);

    // done promise
    napi_value donePromise;
    napi_create_promise(env, &ctx->doneDeferred, &donePromise);
    napi_set_named_property(env, decoderObj, "done", donePromise);

    // callbacks refs
    if (onProgress != nullptr) {
        napi_valuetype t;
        napi_typeof(env, onProgress, &t);
        if (t == napi_function) {
            napi_create_reference(env, onProgress, 1, &ctx->onProgressRef);
        }
    }
    if (onError != nullptr) {
        napi_valuetype t;
        napi_typeof(env, onError, &t);
        if (t == napi_function) {
            napi_create_reference(env, onError, 1, &ctx->onErrorRef);
        }
    }

    // Create a noop JS function required by TSFN.
    napi_value noop;
    napi_create_function(env, "noop", NAPI_AUTO_LENGTH,
                         [](napi_env env, napi_callback_info /*info*/) -> napi_value {
                             napi_value undef;
                             napi_get_undefined(env, &undef);
                             return undef;
                         },
                         nullptr, &noop);

    napi_value tsfnName;
    napi_create_string_utf8(env, "PcmStreamDecoderEvent", NAPI_AUTO_LENGTH, &tsfnName);
    napi_create_threadsafe_function(
        env,
        noop,
        nullptr,
        tsfnName,
        0,
        1,
        nullptr,
        nullptr,
        ctx,
        CallJsDecoderEvent,
        &ctx->eventTsfn);

    // methods
    napi_value fillFn;
    napi_create_function(env, "fill", NAPI_AUTO_LENGTH, PcmDecoderFill, ctx, &fillFn);
    napi_set_named_property(env, decoderObj, "fill", fillFn);

    napi_value closeFn;
    napi_create_function(env, "close", NAPI_AUTO_LENGTH, PcmDecoderClose, ctx, &closeFn);
    napi_set_named_property(env, decoderObj, "close", closeFn);

    napi_value setEqEnabledFn;
    napi_create_function(env, "setEqEnabled", NAPI_AUTO_LENGTH, PcmDecoderSetEqEnabled, ctx, &setEqEnabledFn);
    napi_set_named_property(env, decoderObj, "setEqEnabled", setEqEnabledFn);

    napi_value setEqGainsFn;
    napi_create_function(env, "setEqGains", NAPI_AUTO_LENGTH, PcmDecoderSetEqGains, ctx, &setEqGainsFn);
    napi_set_named_property(env, decoderObj, "setEqGains", setEqGainsFn);

    // wrap finalizer
    napi_wrap(env, decoderObj, ctx, FinalizePcmStreamDecoder, nullptr, nullptr);

    napi_value workName;
    napi_create_string_utf8(env, "PcmStreamDecode", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, ExecutePcmStreamDecode, CompletePcmStreamDecode, ctx, &ctx->work);
    napi_queue_async_work(env, ctx->work);

    return decoderObj;
}

} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        { "decodeAudio", nullptr, DecodeAudio, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "decodeAudioAsync", nullptr, DecodeAudioAsync, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "createPcmStreamDecoder", nullptr, CreatePcmStreamDecoder, nullptr, nullptr, nullptr, napi_default, nullptr }
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "library",
    .nm_priv = ((void*)0),
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterLibraryModule(void)
{
    napi_module_register(&demoModule);
}
