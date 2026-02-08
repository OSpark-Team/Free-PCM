#include "napi/native_api.h"
#include "napi/napi_decoder.h"
#include "napi/napi_stream_decoder.h"

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        { "decodeAudio", nullptr, napi_decoder::DecodeAudio, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "decodeAudioAsync", nullptr, napi_decoder::DecodeAudioAsync, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "createPcmStreamDecoder", nullptr, napi_stream_decoder::CreatePcmStreamDecoder, nullptr, nullptr, nullptr, napi_default, nullptr }
    };
    napi_define_properties(env, exports, 3, desc);
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
