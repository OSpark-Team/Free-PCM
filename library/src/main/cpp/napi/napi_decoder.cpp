#include "napi_decoder.h"

#undef LOG_TAG
#define LOG_TAG "NapiDecoder"

namespace napi_decoder {

// ============================================================================
// 同步解码接口
// ============================================================================

napi_value DecodeAudio(napi_env env, napi_callback_info info)
{
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

// ============================================================================
// 异步解码接口
// ============================================================================

void CallJsProgress(napi_env env, napi_value jsCallback, void* /*context*/, void* data)
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

void ExecuteDecodeAudioAsync(napi_env /*env*/, void* data)
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

void CompleteDecodeAudioAsync(napi_env env, napi_status /*status*/, void* data)
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
        napi_value errObj = napi_utils::CreateErrorObject(env, "decode_to_file", -1, "Decode failed");
        napi_reject_deferred(env, ctx->deferred, errObj);
    }

    napi_delete_async_work(env, ctx->work);
    delete ctx;
}

napi_value DecodeAudioAsync(napi_env env, napi_callback_info info)
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

} // namespace napi_decoder
