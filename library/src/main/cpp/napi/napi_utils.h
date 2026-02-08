#ifndef NAPI_UTILS_H
#define NAPI_UTILS_H

#include <napi/native_api.h>
#include <string>

namespace napi_utils {

/**
 * @brief 创建 NAPI 错误对象
 *
 * 创建带有 stage、code 和 message 属性的错误对象
 *
 * @param env NAPI 环境
 * @param stage 错误发生的阶段
 * @param code 错误码
 * @param message 错误消息
 * @return 创建的错误对象
 */
napi_value CreateErrorObject(
    napi_env env,
    const std::string& stage,
    int32_t code,
    const std::string& message);

} // namespace napi_utils

#endif // NAPI_UTILS_H
