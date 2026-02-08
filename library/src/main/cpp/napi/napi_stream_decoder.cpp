#include "napi_stream_decoder.h"

#undef LOG_TAG
#define LOG_TAG "NapiStreamDecoder"

namespace napi_stream_decoder {

// ============================================================================
// 流式解码器事件回调
// ============================================================================

void CallJsDecoderEvent(napi_env env, napi_value /*jsCb*/, void* context, void* data)
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
            if (payload->sampleFormat == 3) {
                napi_create_string_utf8(env, "s32le", NAPI_AUTO_LENGTH, &sf);
            } else if (payload->sampleFormat == 1) {
                napi_create_string_utf8(env, "s16le", NAPI_AUTO_LENGTH, &sf);
            } else {
                napi_create_string_utf8(env, "unknown", NAPI_AUTO_LENGTH, &sf);
            }
            napi_set_named_property(env, info, "sampleFormat", sf);

            // sampleFormatCode: numeric format for easier handling
            napi_value sfc;
            napi_create_int32(env, payload->sampleFormat, &sfc);
            napi_set_named_property(env, info, "sampleFormatCode", sfc);

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

            napi_value errObj = napi_utils::CreateErrorObject(env, payload->stage, payload->code, payload->message);

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

// ============================================================================
// 流式解码器方法
// ============================================================================

napi_value PcmDecoderFill(napi_env env, napi_callback_info info)
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

napi_value PcmDecoderClose(napi_env env, napi_callback_info info)
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

napi_value PcmDecoderSetEqEnabled(napi_env env, napi_callback_info info)
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

napi_value PcmDecoderSetEqGains(napi_env env, napi_callback_info info)
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

// ============================================================================
// Seek 功能实现
// ============================================================================

napi_value PcmDecoderSeekTo(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "seekTo(positionMs) requires 1 argument");
        return nullptr;
    }

    int64_t positionMs = 0;
    napi_get_value_int64(env, args[0], &positionMs);

    // 验证位置范围
    if (positionMs < 0) {
        napi_throw_error(env, nullptr, "positionMs must be >= 0");
        return nullptr;
    }

    OH_LOG_INFO(LOG_APP, "PcmDecoderSeekTo called: positionMs=%{public}lld", positionMs);

    // 设置 Seek 标志和目标位置
    ctx->isSeeking_.store(true);
    ctx->targetPositionMs_.store(positionMs);
    ctx->currentSeekPositionMs_ = positionMs;

    // 清空环形缓冲区并重置计数器
    if (ctx->ring) {
        ctx->ring->Clear();
        ctx->ring->ResetCounters();
    }

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

napi_value PcmDecoderGetPosition(napi_env env, napi_callback_info info)
{
    size_t argc = 0;
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data);
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx) {
        napi_throw_error(env, nullptr, "Failed to get decoder context");
        return nullptr;
    }

    uint64_t positionMs = 0;
    if (ctx->ring) {
        positionMs = ctx->ring->GetPositionMs();
    }

    napi_value result;
    napi_create_int64(env, static_cast<int64_t>(positionMs), &result);
    return result;
}

// ============================================================================
// 流式解码器异步工作
// ============================================================================

void ExecutePcmStreamDecode(napi_env /*env*/, void* data)
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

        // 保存实际音频参数
        ctx->actualSampleRate = sr;
        ctx->actualChannelCount = cc;
        ctx->actualSampleFormat = sf;

        // 重新创建 PcmRingBuffer，使用实际的音频参数
        const int32_t bytesPerSample = (sf == 3) ? 4 : 2;  // S32LE=4, S16LE=2
        ctx->ring = std::make_unique<audio::PcmRingBuffer>(
            ctx->ringBytes,
            sr,          // sampleRate
            cc,          // channels
            bytesPerSample // bytesPerSample
        );

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
        &ctx->cancel,
        ctx->sampleFormat);

    ctx->success = ok;
    if (ctx->ring) {
        ctx->ring->MarkEos();
    }
}

void CompletePcmStreamDecode(napi_env env, napi_status /*status*/, void* data)
{
    auto* ctx = static_cast<PcmStreamDecoderContext*>(data);
    if (!ctx) {
        return;
    }

    // If ready wasn't settled (e.g. very early failure), reject it.
    if (!ctx->readySettled && ctx->readyDeferred != nullptr) {
        napi_value errObj = napi_utils::CreateErrorObject(env, "ready", -1, "Decoder failed before ready");
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
            napi_value errObj = napi_utils::CreateErrorObject(env, stage, code, message);
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

void FinalizePcmStreamDecoder(napi_env env, void* finalize_data, void* /*finalize_hint*/)
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

// ============================================================================
// 流式解码器创建
// ============================================================================

napi_value CreatePcmStreamDecoder(napi_env env, napi_callback_info info)
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
    int32_t sampleFormat = 1;  // Default to S16LE (1), S32LE = 3
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
            if (napi_get_named_property(env, args[1], "sampleFormat", &v) == napi_ok) {
                int32_t sf = 1;
                if (napi_get_value_int32(env, v, &sf) == napi_ok) {
                    // Validate: only 1 (S16LE) or 3 (S32LE) are supported
                    sampleFormat = (sf == 3) ? 3 : 1;
                }
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
    ctx->sampleFormat = sampleFormat;
    ctx->cancel.store(false);
    ctx->success = false;
    ctx->readySettled = false;
    ctx->lastErrStage = "";
    ctx->lastErrCode = -1;
    ctx->lastErrMessage = "";

    // 保存初始参数，用于在 ready 回调中重新创建 PcmRingBuffer
    ctx->ringBytes = ringBytes;
    ctx->actualSampleRate = 0;
    ctx->actualChannelCount = 0;
    ctx->actualSampleFormat = sampleFormat;

    // 使用默认参数临时创建 PcmRingBuffer，稍后在 infoCb 中重新创建
    ctx->ring = std::make_unique<audio::PcmRingBuffer>(
        ringBytes,
        sampleRate > 0 ? sampleRate : 48000,  // 默认采样率
        channelCount > 0 ? channelCount : 2,  // 默认声道数
        2  // 默认每样本字节数（S16LE）
    );

    ctx->eqEnabled.store(optEqEnabled);
    ctx->eqVersion.store(1);
    for (size_t i = 0; i < PcmEqualizer::kBandCount; i++) {
        ctx->eqGainsDb100[i].store(hasEqGains ? optEqGainsDb100[i] : 0);
    }
    ctx->eqAppliedVersion = 0;
    ctx->eqSampleRate = 0;
    ctx->eqChannelCount = 0;

    // 初始化 Seek 相关变量
    ctx->isSeeking_.store(false);
    ctx->targetPositionMs_.store(0);
    ctx->currentSeekPositionMs_ = 0;

    // 保存初始参数，用于在 ready 回调中重新创建 PcmRingBuffer
    ctx->ringBytes = ringBytes;

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

    // Seek 功能方法
    napi_value seekToFn;
    napi_create_function(env, "seekTo", NAPI_AUTO_LENGTH, PcmDecoderSeekTo, ctx, &seekToFn);
    napi_set_named_property(env, decoderObj, "seekTo", seekToFn);

    napi_value getPositionFn;
    napi_create_function(env, "getPosition", NAPI_AUTO_LENGTH, PcmDecoderGetPosition, ctx, &getPositionFn);
    napi_set_named_property(env, decoderObj, "getPosition", getPositionFn);

    // wrap finalizer
    napi_wrap(env, decoderObj, ctx, FinalizePcmStreamDecoder, nullptr, nullptr);

    napi_value workName;
    napi_create_string_utf8(env, "PcmStreamDecode", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, ExecutePcmStreamDecode, CompletePcmStreamDecode, ctx, &ctx->work);
    napi_queue_async_work(env, ctx->work);

    return decoderObj;
}

} // namespace napi_stream_decoder
