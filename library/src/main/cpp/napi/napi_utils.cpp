#include "napi_utils.h"

namespace napi_utils {

napi_value CreateErrorObject(
    napi_env env,
    const std::string& stage,
    int32_t code,
    const std::string& message)
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

} // namespace napi_utils
