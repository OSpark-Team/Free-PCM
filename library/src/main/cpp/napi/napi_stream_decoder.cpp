#include "napi_stream_decoder.h"

#undef LOG_TAG
#define LOG_TAG "NapiStreamDecoder"

#include <chrono>

namespace napi_stream_decoder {

// ============================================================================
// 流式解码器事件回调
// ============================================================================

void CallJsDecoderEvent(napi_env env, napi_value /*jsCb*/, void *context, void *data) {
    auto *ctx = static_cast<PcmStreamDecoderContext *>(context);
    std::unique_ptr<DecoderEventPayload> payload(static_cast<DecoderEventPayload *>(data));
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
    case DecoderEventType::Seek: {
        if (ctx->seekDeferred == nullptr) {
            break;
        }
        if (ctx->seekDeferredSeq != payload->seekSeq) {
            break;
        }

        if (payload->seekSuccess) {
            napi_value undef;
            napi_get_undefined(env, &undef);
            napi_resolve_deferred(env, ctx->seekDeferred, undef);
        } else {
            napi_value errObj = napi_utils::CreateErrorObject(env, "seek", payload->code, payload->message);
            napi_reject_deferred(env, ctx->seekDeferred, errObj);
        }
        ctx->seekDeferred = nullptr;
        break;
    }
    case DecoderEventType::DrcMeter: {
        if (ctx->onDrcMeterRef == nullptr) {
            break;
        }
        napi_value cb;
        napi_get_reference_value(env, ctx->onDrcMeterRef, &cb);
        if (cb == nullptr) {
            break;
        }

        napi_value arg;
        napi_create_object(env, &arg);

        napi_value level;
        napi_create_double(env, payload->drcLevelDb, &level);
        napi_set_named_property(env, arg, "levelDb", level);

        napi_value gain;
        napi_create_double(env, payload->drcGainDb, &gain);
        napi_set_named_property(env, arg, "gainDb", gain);

        napi_value gr;
        napi_create_double(env, payload->drcGrDb, &gr);
        napi_set_named_property(env, arg, "grDb", gr);

        napi_value argv[1] = {arg};
        napi_value result;
        napi_call_function(env, nullptr, cb, 1, argv, &result);
        break;
    }
    default:
        break;
    }
}

static uint64_t NowMs()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static void QueueSeekEvent(PcmStreamDecoderContext *ctx, uint64_t seq, bool success, int32_t code,
                           const std::string &message, int64_t targetMs) {
    if (!ctx || ctx->eventTsfn == nullptr) {
        return;
    }

    auto *payload = new DecoderEventPayload();
    payload->type = DecoderEventType::Seek;
    payload->seekSeq = seq;
    payload->seekTargetMs = targetMs;
    payload->seekSuccess = success;
    payload->code = code;
    payload->message = message;

    (void)napi_call_threadsafe_function(ctx->eventTsfn, payload, napi_tsfn_nonblocking);
}

static void QueueDrcMeterEvent(PcmStreamDecoderContext *ctx, double levelDb, double gainDb, double grDb) {
    if (!ctx || ctx->eventTsfn == nullptr) {
        return;
    }

    auto *payload = new DecoderEventPayload();
    payload->type = DecoderEventType::DrcMeter;
    payload->drcLevelDb = levelDb;
    payload->drcGainDb = gainDb;
    payload->drcGrDb = grDb;

    (void)napi_call_threadsafe_function(ctx->eventTsfn, payload, napi_tsfn_nonblocking);
}

// ============================================================================
// 流式解码器方法
// ============================================================================

napi_value PcmDecoderFill(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
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

    void *buf = nullptr;
    size_t len = 0;
    napi_get_arraybuffer_info(env, args[0], &buf, &len);
    if (!buf || len == 0) {
        napi_value zero;
        napi_create_int32(env, 0, &zero);
        return zero;
    }

    size_t n = 0;
    if (ctx->ring) {
        n = ctx->ring->Read(reinterpret_cast<uint8_t *>(buf), len);
    }
    if (n < len) {
        memset(reinterpret_cast<uint8_t *>(buf) + n, 0, len - n);
    }

    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(n), &out);
    return out;
}

// For AudioRenderer.on('writeData') (API 12+):
// - return 0 when not enough data, so caller can return INVALID without consuming the ring
// - return full buffer length when enough data, or when EOS is marked (with padding)
napi_value PcmDecoderFillForWriteData(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "fillForWriteData(buffer) requires 1 argument");
        return nullptr;
    }

    bool isArrayBuffer = false;
    napi_is_arraybuffer(env, args[0], &isArrayBuffer);
    if (!isArrayBuffer) {
        napi_throw_error(env, nullptr, "fillForWriteData(buffer) expects an ArrayBuffer");
        return nullptr;
    }

    void *buf = nullptr;
    size_t len = 0;
    napi_get_arraybuffer_info(env, args[0], &buf, &len);
    if (!buf || len == 0) {
        napi_value zero;
        napi_create_int32(env, 0, &zero);
        return zero;
    }

    if (!ctx->ring) {
        napi_value zero;
        napi_create_int32(env, 0, &zero);
        return zero;
    }

    const size_t avail = ctx->ring->Available();
    if (avail >= len) {
        (void)ctx->ring->Read(reinterpret_cast<uint8_t *>(buf), len);
        napi_value out;
        napi_create_int32(env, static_cast<int32_t>(len), &out);
        return out;
    }

    if (ctx->ring->IsEosMarked() && avail > 0) {
        const size_t n = ctx->ring->Read(reinterpret_cast<uint8_t *>(buf), avail);
        if (n < len) {
            memset(reinterpret_cast<uint8_t *>(buf) + n, 0, len - n);
        }
        napi_value out;
        napi_create_int32(env, static_cast<int32_t>(len), &out);
        return out;
    }

    napi_value zero;
    napi_create_int32(env, 0, &zero);
    return zero;
}

napi_value PcmDecoderClose(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
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

napi_value PcmDecoderSetEqEnabled(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
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

napi_value PcmDecoderSetEqGains(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
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
        ctx->eqGainsDb100Stereo[0][i].store(gain100);
        ctx->eqGainsDb100Stereo[1][i].store(gain100);
    }

    ctx->eqVersion.fetch_add(1);

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

// ============================================================================
// DRC (Dynamic Range Compression)
// ============================================================================

napi_value PcmDecoderSetDrcEnabled(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "setDrcEnabled(enabled) requires 1 argument");
        return nullptr;
    }

    bool enabled = false;
    napi_get_value_bool(env, args[0], &enabled);
    ctx->drcEnabled.store(enabled);
    ctx->drcVersion.fetch_add(1);

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

napi_value PcmDecoderSetDrcParams(napi_env env, napi_callback_info info) {
    size_t argc = 5;
    napi_value args[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx || argc < 5) {
        napi_throw_error(env, nullptr, "setDrcParams(thresholdDb, ratio, attackMs, releaseMs, makeupGainDb) requires 5 arguments");
        return nullptr;
    }

    auto getNum = [&](napi_value v, double &out) -> bool {
        if (napi_get_value_double(env, v, &out) == napi_ok) {
            return true;
        }
        int32_t i = 0;
        if (napi_get_value_int32(env, v, &i) == napi_ok) {
            out = static_cast<double>(i);
            return true;
        }
        return false;
    };

    double thresholdDb = -20.0;
    double ratio = 4.0;
    double attackMs = 10.0;
    double releaseMs = 100.0;
    double makeupDb = 0.0;

    if (!getNum(args[0], thresholdDb) || !getNum(args[1], ratio) || !getNum(args[2], attackMs) ||
        !getNum(args[3], releaseMs) || !getNum(args[4], makeupDb)) {
        napi_throw_error(env, nullptr, "setDrcParams expects numbers");
        return nullptr;
    }

    // Clamp ranges (match DrcProcessor clamps)
    if (thresholdDb < -60.0) thresholdDb = -60.0;
    if (thresholdDb > 0.0) thresholdDb = 0.0;
    if (ratio < 1.0) ratio = 1.0;
    if (ratio > 20.0) ratio = 20.0;
    if (attackMs < 0.1) attackMs = 0.1;
    if (attackMs > 200.0) attackMs = 200.0;
    if (releaseMs < 5.0) releaseMs = 5.0;
    if (releaseMs > 2000.0) releaseMs = 2000.0;
    if (makeupDb < -12.0) makeupDb = -12.0;
    if (makeupDb > 24.0) makeupDb = 24.0;

    ctx->drcThresholdDb100.store(static_cast<int32_t>(std::lround(thresholdDb * 100.0)));
    ctx->drcRatio1000.store(static_cast<int32_t>(std::lround(ratio * 1000.0)));
    ctx->drcAttackMs100.store(static_cast<int32_t>(std::lround(attackMs * 100.0)));
    ctx->drcReleaseMs100.store(static_cast<int32_t>(std::lround(releaseMs * 100.0)));
    ctx->drcMakeupDb100.store(static_cast<int32_t>(std::lround(makeupDb * 100.0)));

    ctx->drcVersion.fetch_add(1);

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

napi_value PcmDecoderSetEqGainsLR(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx || argc < 2) {
        napi_throw_error(env, nullptr, "setEqGainsLR(left, right) requires 2 arguments");
        return nullptr;
    }

    auto parseOne = [&](napi_value arr, size_t chIndex) -> bool {
        bool isArray = false;
        napi_is_array(env, arr, &isArray);
        if (!isArray) {
            return false;
        }
        uint32_t len = 0;
        napi_get_array_length(env, arr, &len);
        if (len != static_cast<uint32_t>(PcmEqualizer::kBandCount)) {
            return false;
        }
        for (uint32_t i = 0; i < len; i++) {
            napi_value v;
            napi_get_element(env, arr, i, &v);
            double gainDb = 0.0;
            if (napi_get_value_double(env, v, &gainDb) != napi_ok) {
                int32_t gi = 0;
                if (napi_get_value_int32(env, v, &gi) != napi_ok) {
                    return false;
                }
                gainDb = static_cast<double>(gi);
            }
            if (gainDb > 24.0) {
                gainDb = 24.0;
            } else if (gainDb < -24.0) {
                gainDb = -24.0;
            }
            const int32_t gain100 = static_cast<int32_t>(std::lround(gainDb * 100.0));
            ctx->eqGainsDb100Stereo[chIndex][i].store(gain100);
        }
        return true;
    };

    if (!parseOne(args[0], 0) || !parseOne(args[1], 1)) {
        napi_throw_error(env, nullptr, "setEqGainsLR expects two arrays of 10 numbers");
        return nullptr;
    }

    ctx->eqVersion.fetch_add(1);

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

napi_value PcmDecoderSetChannelVolumes(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx || argc < 2) {
        napi_throw_error(env, nullptr, "setChannelVolumes(left, right) requires 2 arguments");
        return nullptr;
    }

    auto parseCoeff = [&](napi_value v, int32_t &out1000) -> bool {
        double d = 0.0;
        if (napi_get_value_double(env, v, &d) != napi_ok) {
            int32_t i = 0;
            if (napi_get_value_int32(env, v, &i) != napi_ok) {
                return false;
            }
            d = static_cast<double>(i);
        }
        if (d < 0.0) d = 0.0;
        if (d > 2.0) d = 2.0;
        out1000 = static_cast<int32_t>(std::lround(d * 1000.0));
        return true;
    };

    int32_t l1000 = 1000;
    int32_t r1000 = 1000;
    if (!parseCoeff(args[0], l1000) || !parseCoeff(args[1], r1000)) {
        napi_throw_error(env, nullptr, "setChannelVolumes expects two numbers");
        return nullptr;
    }

    ctx->channelVol1000[0].store(l1000);
    ctx->channelVol1000[1].store(r1000);

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

// ============================================================================
// Seek 功能实现
// ============================================================================

napi_value PcmDecoderSeekTo(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "seekTo(positionMs) requires 1 argument");
        return nullptr;
    }

    int64_t positionMs = 0;
    {
        napi_status st = napi_get_value_int64(env, args[0], &positionMs);
        if (st != napi_ok) {
            double d = 0.0;
            if (napi_get_value_double(env, args[0], &d) == napi_ok) {
                positionMs = static_cast<int64_t>(std::llround(d));
            } else {
                int32_t i = 0;
                if (napi_get_value_int32(env, args[0], &i) != napi_ok) {
                    napi_throw_error(env, nullptr, "positionMs must be a number");
                    return nullptr;
                }
                positionMs = static_cast<int64_t>(i);
            }
        }
    }

    // 验证位置范围
    if (positionMs < 0) {
        napi_throw_error(env, nullptr, "positionMs must be >= 0");
        return nullptr;
    }

    OH_LOG_INFO(LOG_APP, "PcmDecoderSeekTo called: positionMs=%{public}lld", (long long)positionMs);

    // Request a seek to be applied by the decode thread.
    // We clear the ring immediately to stop feeding old PCM after renderer.flush().
    {
        std::lock_guard<std::mutex> lock(ctx->seekMutex_);
        ctx->targetPositionMs_.store(positionMs);
        // Increment after writing the target to keep reads consistent.
        (void)ctx->seekSeq_.fetch_add(1);
    }

    if (ctx->ring) {
        ctx->ring->Clear();
    }

    napi_value undef;
    napi_get_undefined(env, &undef);
    return undef;
}

napi_value PcmDecoderSeekToAsync(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, args, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx || argc < 1) {
        napi_throw_error(env, nullptr, "seekToAsync(positionMs) requires 1 argument");
        return nullptr;
    }

    int64_t positionMs = 0;
    {
        // ArkTS number may arrive as double on some runtimes.
        napi_status st = napi_get_value_int64(env, args[0], &positionMs);
        if (st != napi_ok) {
            double d = 0.0;
            if (napi_get_value_double(env, args[0], &d) == napi_ok) {
                positionMs = static_cast<int64_t>(std::llround(d));
            } else {
                int32_t i = 0;
                if (napi_get_value_int32(env, args[0], &i) != napi_ok) {
                    napi_throw_error(env, nullptr, "positionMs must be a number");
                    return nullptr;
                }
                positionMs = static_cast<int64_t>(i);
            }
        }
    }
    if (positionMs < 0) {
        napi_throw_error(env, nullptr, "positionMs must be >= 0");
        return nullptr;
    }

    napi_deferred deferred;
    napi_value promise;
    napi_create_promise(env, &deferred, &promise);

    // Reject previous pending seek promise if any.
    if (ctx->seekDeferred != nullptr) {
        napi_value errObj = napi_utils::CreateErrorObject(env, "seek", -2, "Seek was superseded by a new seek request");
        napi_reject_deferred(env, ctx->seekDeferred, errObj);
        ctx->seekDeferred = nullptr;
    }

    uint64_t seq = 0;
    {
        std::lock_guard<std::mutex> lock(ctx->seekMutex_);
        ctx->targetPositionMs_.store(positionMs);
        seq = ctx->seekSeq_.fetch_add(1) + 1;
    }

    ctx->seekDeferred = deferred;
    ctx->seekDeferredSeq = seq;
    ctx->seekAwaitSeq.store(seq);
    ctx->seekAwaitOutput.store(true);

    if (ctx->ring) {
        ctx->ring->Clear();
    }

    return promise;
}

napi_value PcmDecoderGetPosition(napi_env env, napi_callback_info info) {
    size_t argc = 0;
    void *data = nullptr;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data);
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
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

void ExecutePcmStreamDecode(napi_env /*env*/, void *data) {
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
    if (!ctx) {
        return;
    }

    ctx->success = false;

    // ensure meter starts fresh
    ctx->drcMeterLastEmitMs = 0;

    // Reset S32 global max for stable normalization across callbacks.
    ctx->s32GlobalMaxAbs = 0;

    AudioDecoder decoder;

    AudioDecoder::InfoCallback infoCb = [ctx](int32_t sr, int32_t cc, int32_t sf, int64_t durMs) {
        ctx->eqSampleRate = sr;
        ctx->eqChannelCount = cc;
        ctx->eqAppliedVersion = 0;
        ctx->eq.Init(sr, cc);
        ctx->eq.SetEnabled(ctx->eqEnabled.load());

        ctx->drcAppliedVersion = 0;
        ctx->drc.Init(sr, cc);
        ctx->drc.SetEnabled(ctx->drcEnabled.load());

        // 保存实际音频参数
        ctx->actualSampleRate = sr;
        ctx->actualChannelCount = cc;
        ctx->actualSampleFormat = sf;

        size_t rb = ctx->ringBytes;
        if (rb == 0) {
            // --- 动态常数定义 ---
            constexpr size_t kStep = 64 * 1024;      // 64KB 步进对齐
            constexpr size_t kMinLimit = 128 * 1024; // 最小提升到 128KB，确保 Hi-Res 基础缓冲
            size_t kMaxLimit = 1024 * 1024;          // 默认最大 1MB

            const bool isHttp =
                (ctx->inputPathOrUri.rfind("http://", 0) == 0) || (ctx->inputPathOrUri.rfind("https://", 0) == 0);

            // 精确计算每秒字节数 (BPS)
            const int32_t bytesPerSample = (sf >= 3 || sf == 2) ? 4 : 2;
            const uint64_t bytesPerSecond = (sr > 0 && cc > 0) ? static_cast<uint64_t>(sr) * static_cast<uint64_t>(cc) *
                                                                     static_cast<uint64_t>(bytesPerSample)
                                                               : 0;

            // 针对高采样率 (如 192k) 自动提升上限
            // 如果是 192k/24bit，BPS 超过 1MB/s，我们将上限放宽到 2MB 或 4MB
            if (bytesPerSecond > 1000000) {
                kMaxLimit = 2 * 1024 * 1024;
            }

            // 动态计算目标时长 (targetSec)
            double targetSec = 0.0;
            if (durMs > 0) {
                // 本地长音频或普通曲目
                if (durMs < 30 * 1000) {
                    targetSec = 0.30; // 短音频保持灵敏度
                } else if (durMs < 10 * 60 * 1000) {
                    targetSec = 0.60; // 常规曲目平衡点
                } else {
                    targetSec = 0.80; // 长音频侧重稳定
                }
            } else {
                // 网络直播流或未知长度
                targetSec = isHttp ? 1.20 : 0.60;
            }

            // 环境补偿
            if (isHttp) {
                targetSec += 0.30; // 网络环境下增加 300ms 冗余
            }

            // 计算期望字节数
            uint64_t desired = 0;
            if (bytesPerSecond > 0 && targetSec > 0.0) {
                desired = static_cast<uint64_t>(static_cast<double>(bytesPerSecond) * targetSec);
            }

            // 边界处理与阶梯化对齐
            rb = static_cast<size_t>(desired);

            // 确保不低于最小值，不超过当前规格下的最大值
            if (rb < kMinLimit)
                rb = kMinLimit;
            if (rb > kMaxLimit)
                rb = kMaxLimit;

            // 向上取整到 kStep 的倍数 (对 CPU/内存总线更友好)
            rb = ((rb + kStep - 1) / kStep) * kStep;

            // 再次约束边界
            if (rb < kMinLimit)
                rb = kMinLimit;
            if (rb > kMaxLimit)
                rb = kMaxLimit;

            ctx->ringBytes = rb;
        }

        // 重新创建 PcmRingBuffer，使用实际的音频参数
        const int32_t bytesPerSample = (sf == 3) ? 4 : 2; // S32LE=4, S16LE=2
        ctx->ring = std::make_unique<audio::PcmRingBuffer>(rb,
                                                           sr,            // sampleRate
                                                           cc,            // channels
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

    AudioDecoder::PcmDataCallback pcmCb = [ctx](const uint8_t *pcm, size_t size, int64_t /*ptsMs*/) {
        if (ctx->cancel.load()) {
            return false;
        }
        if (!ctx->ring) {
            return false;
        }

        // While a seek is pending (requested but not handled), drop PCM output.
        // This avoids pushing "old" audio (especially for backward seeks).
        if (ctx->seekSeq_.load() != ctx->seekHandledSeq_.load()) {
            return true;
        }

        // If seekToAsync is waiting for first post-seek PCM, resolve it now.
        if (ctx->seekAwaitOutput.load()) {
            const uint64_t awaitSeq = ctx->seekAwaitSeq.load();
            if (awaitSeq != 0 && awaitSeq == ctx->seekHandledSeq_.load()) {
                if (ctx->seekAwaitOutput.exchange(false)) {
                    QueueSeekEvent(ctx, awaitSeq, true, 0, "", ctx->targetPositionMs_.load());
                }
            }
        }

        const bool eqEnabled = ctx->eqEnabled.load();
        const bool needEq = eqEnabled && ctx->eq.IsReady();

        const bool drcEnabled = ctx->drcEnabled.load();
        const bool needDrc = drcEnabled && ctx->drc.IsReady();

        // Per-channel volume compensation.
        const int32_t volL1000 = ctx->channelVol1000[0].load();
        const int32_t volR1000 = ctx->channelVol1000[1].load();

        const int32_t ch = ctx->actualChannelCount;
        const int32_t sf = ctx->actualSampleFormat;
        const int32_t bytesPerSample = (sf == 3) ? 4 : 2;
        if (bytesPerSample != 2 && bytesPerSample != 4) {
            return ctx->ring->Push(pcm, size, &ctx->cancel);
        }

        const bool needChanVol = (ch == 1 && volL1000 != 1000) ||
                                 (ch == 2 && (volL1000 != 1000 || volR1000 != 1000));

        if (!needEq && !needChanVol && !needDrc) {
            return ctx->ring->Push(pcm, size, &ctx->cancel);
        }

        if (ch != 1 && ch != 2) {
            // Only stereo/mono are supported for per-ear compensation.
            return ctx->ring->Push(pcm, size, &ctx->cancel);
        }

        const size_t sampleCount = size / static_cast<size_t>(bytesPerSample);
        const size_t frameCount = sampleCount / static_cast<size_t>(ch);
        const size_t bytesToProcess = frameCount * static_cast<size_t>(ch) * static_cast<size_t>(bytesPerSample);
        if (bytesToProcess == 0) {
            return ctx->ring->Push(pcm, size, &ctx->cancel);
        }

        if (needEq) {
            const uint32_t v = ctx->eqVersion.load();
            if (v != ctx->eqAppliedVersion) {
                std::array<float, PcmEqualizer::kBandCount> gl;
                std::array<float, PcmEqualizer::kBandCount> gr;
                for (size_t i = 0; i < PcmEqualizer::kBandCount; i++) {
                    gl[i] = static_cast<float>(ctx->eqGainsDb100Stereo[0][i].load()) / 100.0f;
                    gr[i] = static_cast<float>(ctx->eqGainsDb100Stereo[1][i].load()) / 100.0f;
                }
                ctx->eq.SetGainsDbStereo(gl, gr);
                ctx->eqAppliedVersion = v;
            }
            ctx->eq.SetEnabled(true);
        } else {
            ctx->eq.SetEnabled(false);
        }

        // Apply DRC params (lazy) if enabled.
        if (needDrc) {
            const uint32_t dv = ctx->drcVersion.load();
            if (dv != ctx->drcAppliedVersion) {
                const float thr = static_cast<float>(ctx->drcThresholdDb100.load()) / 100.0f;
                const float ratio = static_cast<float>(ctx->drcRatio1000.load()) / 1000.0f;
                const float atk = static_cast<float>(ctx->drcAttackMs100.load()) / 100.0f;
                const float rel = static_cast<float>(ctx->drcReleaseMs100.load()) / 100.0f;
                const float makeup = static_cast<float>(ctx->drcMakeupDb100.load()) / 100.0f;
                ctx->drc.SetParams(thr, ratio, atk, rel, makeup);
                ctx->drcAppliedVersion = dv;
            }
            ctx->drc.SetEnabled(true);
        } else {
            ctx->drc.SetEnabled(false);
        }

        // Float DSP pipeline to avoid hard clipping artifacts.
        ctx->dspScratchF.resize(sampleCount);

        float norm = 1.0f;
        float denorm = 1.0f;
        if (bytesPerSample == 2) {
            norm = 1.0f / 32768.0f;
            denorm = 32768.0f;

            const int16_t* in = reinterpret_cast<const int16_t*>(pcm);
            for (size_t i = 0; i < sampleCount; i++) {
                ctx->dspScratchF[i] = static_cast<float>(in[i]) * norm;
            }
        } else {
            // S32LE: use global persistent maxAbs for stable normalization.
            // This prevents "volume rollercoaster" when source data scale is ambiguous.
            const int32_t* in = reinterpret_cast<const int32_t*>(pcm);

            // Update global maxAbs monotonically (only increases, never decreases).
            for (size_t i = 0; i < sampleCount; i++) {
                int64_t v = static_cast<int64_t>(in[i]);
                if (v < 0) v = -v;
                if (v > ctx->s32GlobalMaxAbs) {
                    ctx->s32GlobalMaxAbs = v;
                }
            }

            // Choose normalization based on global maxAbs (stable across entire track).
            // S32LE can come in different effective scales:
            // - 16-bit scale: abs <= ~32768
            // - 24-bit scale: abs <= ~8388608 (2^23)
            // - Q31 scale:    abs up to ~2^31
            if (ctx->s32GlobalMaxAbs <= (1LL << 20)) {
                norm = 1.0f / 32768.0f;
            } else if (ctx->s32GlobalMaxAbs <= (1LL << 27)) {
                norm = 1.0f / 8388608.0f;
            } else {
                norm = 1.0f / 2147483648.0f;
            }
            denorm = 1.0f / norm;
            for (size_t i = 0; i < sampleCount; i++) {
                ctx->dspScratchF[i] = static_cast<float>(in[i]) * norm;
            }
        }

        if (needEq) {
            ctx->eq.ProcessFloat(ctx->dspScratchF.data(), frameCount);
        }

        if (needChanVol) {
            const float l = static_cast<float>(volL1000) / 1000.0f;
            const float r = static_cast<float>(volR1000) / 1000.0f;
            if (ch == 1) {
                for (size_t i = 0; i < frameCount; i++) {
                    ctx->dspScratchF[i] *= l;
                }
            } else {
                for (size_t i = 0; i < frameCount; i++) {
                    ctx->dspScratchF[i * 2] *= l;
                    ctx->dspScratchF[i * 2 + 1] *= r;
                }
            }
        }

        if (needDrc) {
            ctx->drc.ProcessFloat(ctx->dspScratchF.data(), frameCount);

            const uint64_t now = NowMs();
            if ((now - ctx->drcMeterLastEmitMs) >= 100) {
                ctx->drcMeterLastEmitMs = now;
                QueueDrcMeterEvent(ctx,
                                  static_cast<double>(ctx->drc.GetLastLevelDb()),
                                  static_cast<double>(ctx->drc.GetLastGainDb()),
                                  static_cast<double>(ctx->drc.GetLastGrDb()));
            }
        }

        // Per-sample soft clipper using tanh to avoid pumping artifacts.
        // This provides smooth saturation instead of block-based gain reduction.
        for (size_t i = 0; i < sampleCount; i++) {
            ctx->dspScratchF[i] = std::tanh(ctx->dspScratchF[i]);
        }

        if (bytesPerSample == 2) {
            ctx->eqScratch16.resize(sampleCount);
            for (size_t i = 0; i < sampleCount; i++) {
                float v = ctx->dspScratchF[i] * denorm;
                if (v > 32767.0f) v = 32767.0f;
                if (v < -32768.0f) v = -32768.0f;
                ctx->eqScratch16[i] = static_cast<int16_t>(std::lround(v));
            }
            return ctx->ring->Push(reinterpret_cast<const uint8_t *>(ctx->eqScratch16.data()), size, &ctx->cancel);
        }

        ctx->eqScratch32.resize(sampleCount);
        for (size_t i = 0; i < sampleCount; i++) {
            double v = static_cast<double>(ctx->dspScratchF[i]) * static_cast<double>(denorm);
            if (v > 2147483647.0) v = 2147483647.0;
            if (v < -2147483648.0) v = -2147483648.0;
            ctx->eqScratch32[i] = static_cast<int32_t>(std::llround(v));
        }
        return ctx->ring->Push(reinterpret_cast<const uint8_t *>(ctx->eqScratch32.data()), size, &ctx->cancel);
    };

    AudioDecoder::ErrorCallback errorCb = [ctx](const std::string &stage, int32_t code, const std::string &message) {
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

    AudioDecoder::SeekPollCallback seekPollCb = [ctx](int64_t &targetMs, uint64_t &seq) {
        const uint64_t req = ctx->seekSeq_.load();
        const uint64_t handled = ctx->seekHandledSeq_.load();
        if (req == handled) {
            return false;
        }
        // targetPositionMs_ is written before seekSeq_ increment in seekTo().
        targetMs = ctx->targetPositionMs_.load();
        seq = req;
        return true;
    };

    AudioDecoder::SeekAppliedCallback seekAppliedCb = [ctx](uint64_t seq, bool success, int64_t targetMs) {
        // Always advance handled seq so PCM output can resume.
        ctx->seekHandledSeq_.store(seq);

        if (!success) {
            if (ctx->seekAwaitOutput.exchange(false)) {
                QueueSeekEvent(ctx, seq, false, -1, "Seek failed", targetMs);
            }
            return;
        }
        if (!ctx->ring) {
            return;
        }

        // Reset ring buffer to align position with target time.
        ctx->ring->Clear();
        ctx->ring->SetPositionMs(targetMs < 0 ? 0 : static_cast<uint64_t>(targetMs));

        // For seekToAsync: ensure await seq matches this seek request.
        ctx->seekAwaitSeq.store(seq);
    };

    bool ok = decoder.DecodeToPcmStream(ctx->inputPathOrUri, ctx->sampleRate, ctx->channelCount, ctx->bitrate, infoCb,
                                        progressCb, pcmCb, errorCb, &ctx->cancel, ctx->sampleFormat, seekPollCb,
                                        seekAppliedCb);

    ctx->success = ok;
    if (ctx->ring) {
        ctx->ring->MarkEos();
    }
}

void CompletePcmStreamDecode(napi_env env, napi_status /*status*/, void *data) {
    auto *ctx = static_cast<PcmStreamDecoderContext *>(data);
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

    if (ctx->seekDeferred != nullptr) {
        napi_value errObj = napi_utils::CreateErrorObject(env, "seek", -1, "Decoder finished before seek became ready");
        napi_reject_deferred(env, ctx->seekDeferred, errObj);
        ctx->seekDeferred = nullptr;
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

void FinalizePcmStreamDecoder(napi_env env, void *finalize_data, void * /*finalize_hint*/) {
    auto *ctx = static_cast<PcmStreamDecoderContext *>(finalize_data);
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
    if (ctx->onDrcMeterRef != nullptr) {
        napi_delete_reference(env, ctx->onDrcMeterRef);
        ctx->onDrcMeterRef = nullptr;
    }

    delete ctx;
}

// ============================================================================
// 流式解码器创建
// ============================================================================

napi_value CreatePcmStreamDecoder(napi_env env, napi_callback_info info) {
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
    int32_t sampleFormat = 1; // Default to S16LE (1), S32LE = 3
    // ringBytes:
    // - 0 means auto (adaptive by audio format + duration + source type)
    // - otherwise fixed ring buffer size
    size_t ringBytes = 0;

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
                } else {
                    ringBytes = 0;
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
    napi_value onDrcMeter = nullptr;
    if (argc >= 3 && args[2] != nullptr) {
        napi_valuetype t;
        napi_typeof(env, args[2], &t);
        if (t == napi_object) {
            napi_get_named_property(env, args[2], "onProgress", &onProgress);
            napi_get_named_property(env, args[2], "onError", &onError);
            napi_get_named_property(env, args[2], "onDrcMeter", &onDrcMeter);
        }
    }

    auto *ctx = new PcmStreamDecoderContext();
    ctx->env = env;
    ctx->work = nullptr;
    ctx->eventTsfn = nullptr;
    ctx->readyDeferred = nullptr;
    ctx->doneDeferred = nullptr;
    ctx->selfRef = nullptr;
    ctx->onProgressRef = nullptr;
    ctx->onErrorRef = nullptr;
    ctx->onDrcMeterRef = nullptr;
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
    const size_t initialRingBytes = (ringBytes > 0) ? ringBytes : (64 * 1024);
    ctx->ring = std::make_unique<audio::PcmRingBuffer>(initialRingBytes,
                                                       sampleRate > 0 ? sampleRate : 48000, // 默认采样率
                                                       channelCount > 0 ? channelCount : 2, // 默认声道数
                                                       2 // 默认每样本字节数（S16LE）
    );

    ctx->eqEnabled.store(optEqEnabled);
    ctx->eqVersion.store(1);
    for (size_t i = 0; i < PcmEqualizer::kBandCount; i++) {
        const int32_t g100 = hasEqGains ? optEqGainsDb100[i] : 0;
        ctx->eqGainsDb100Stereo[0][i].store(g100);
        ctx->eqGainsDb100Stereo[1][i].store(g100);
    }
    ctx->channelVol1000[0].store(1000);
    ctx->channelVol1000[1].store(1000);
    ctx->eqAppliedVersion = 0;
    ctx->eqSampleRate = 0;
    ctx->eqChannelCount = 0;

    // DRC defaults (disabled)
    ctx->drcEnabled.store(false);
    ctx->drcVersion.store(1);
    ctx->drcThresholdDb100.store(static_cast<int32_t>(-20 * 100));
    ctx->drcRatio1000.store(static_cast<int32_t>(4 * 1000));
    ctx->drcAttackMs100.store(static_cast<int32_t>(10 * 100));
    ctx->drcReleaseMs100.store(static_cast<int32_t>(100 * 100));
    ctx->drcMakeupDb100.store(0);
    ctx->drcAppliedVersion = 0;
    ctx->drcMeterLastEmitMs = 0;

    // Initialize seek state.
    ctx->seekSeq_.store(0);
    ctx->seekHandledSeq_.store(0);
    ctx->targetPositionMs_.store(0);
    ctx->seekDeferred = nullptr;
    ctx->seekDeferredSeq = 0;
    ctx->seekAwaitOutput.store(false);
    ctx->seekAwaitSeq.store(0);

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

    if (onDrcMeter != nullptr) {
        napi_valuetype t;
        napi_typeof(env, onDrcMeter, &t);
        if (t == napi_function) {
            napi_create_reference(env, onDrcMeter, 1, &ctx->onDrcMeterRef);
        }
    }

    // Create a noop JS function required by TSFN.
    napi_value noop;
    napi_create_function(
        env, "noop", NAPI_AUTO_LENGTH,
        [](napi_env env, napi_callback_info /*info*/) -> napi_value {
            napi_value undef;
            napi_get_undefined(env, &undef);
            return undef;
        },
        nullptr, &noop);

    napi_value tsfnName;
    napi_create_string_utf8(env, "PcmStreamDecoderEvent", NAPI_AUTO_LENGTH, &tsfnName);
    napi_create_threadsafe_function(env, noop, nullptr, tsfnName, 0, 1, nullptr, nullptr, ctx, CallJsDecoderEvent,
                                    &ctx->eventTsfn);

    // methods
    napi_value fillFn;
    napi_create_function(env, "fill", NAPI_AUTO_LENGTH, PcmDecoderFill, ctx, &fillFn);
    napi_set_named_property(env, decoderObj, "fill", fillFn);

    napi_value fillForWriteDataFn;
    napi_create_function(env, "fillForWriteData", NAPI_AUTO_LENGTH, PcmDecoderFillForWriteData, ctx,
                         &fillForWriteDataFn);
    napi_set_named_property(env, decoderObj, "fillForWriteData", fillForWriteDataFn);

    napi_value closeFn;
    napi_create_function(env, "close", NAPI_AUTO_LENGTH, PcmDecoderClose, ctx, &closeFn);
    napi_set_named_property(env, decoderObj, "close", closeFn);

    napi_value setEqEnabledFn;
    napi_create_function(env, "setEqEnabled", NAPI_AUTO_LENGTH, PcmDecoderSetEqEnabled, ctx, &setEqEnabledFn);
    napi_set_named_property(env, decoderObj, "setEqEnabled", setEqEnabledFn);

    napi_value setEqGainsFn;
    napi_create_function(env, "setEqGains", NAPI_AUTO_LENGTH, PcmDecoderSetEqGains, ctx, &setEqGainsFn);
    napi_set_named_property(env, decoderObj, "setEqGains", setEqGainsFn);

    napi_value setEqGainsLRFn;
    napi_create_function(env, "setEqGainsLR", NAPI_AUTO_LENGTH, PcmDecoderSetEqGainsLR, ctx, &setEqGainsLRFn);
    napi_set_named_property(env, decoderObj, "setEqGainsLR", setEqGainsLRFn);

    napi_value setChannelVolumesFn;
    napi_create_function(env, "setChannelVolumes", NAPI_AUTO_LENGTH, PcmDecoderSetChannelVolumes, ctx,
                         &setChannelVolumesFn);
    napi_set_named_property(env, decoderObj, "setChannelVolumes", setChannelVolumesFn);

    napi_value setDrcEnabledFn;
    napi_create_function(env, "setDrcEnabled", NAPI_AUTO_LENGTH, PcmDecoderSetDrcEnabled, ctx, &setDrcEnabledFn);
    napi_set_named_property(env, decoderObj, "setDrcEnabled", setDrcEnabledFn);

    napi_value setDrcParamsFn;
    napi_create_function(env, "setDrcParams", NAPI_AUTO_LENGTH, PcmDecoderSetDrcParams, ctx, &setDrcParamsFn);
    napi_set_named_property(env, decoderObj, "setDrcParams", setDrcParamsFn);

    // Seek 功能方法
    napi_value seekToFn;
    napi_create_function(env, "seekTo", NAPI_AUTO_LENGTH, PcmDecoderSeekTo, ctx, &seekToFn);
    napi_set_named_property(env, decoderObj, "seekTo", seekToFn);

    napi_value seekToAsyncFn;
    napi_create_function(env, "seekToAsync", NAPI_AUTO_LENGTH, PcmDecoderSeekToAsync, ctx, &seekToAsyncFn);
    napi_set_named_property(env, decoderObj, "seekToAsync", seekToAsyncFn);

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
