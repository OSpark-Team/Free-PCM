#ifndef NAPI_DECODER_H
#define NAPI_DECODER_H

#include <napi/native_api.h>
#include "../audio_decoder.h"
#include "../napi/napi_utils.h"
#include "../types/decoder_types.h"
#include <hilog/log.h>

namespace napi_decoder {

// ============================================================================
// 同步解码接口
// ============================================================================

/**
 * @brief 解码音频文件的同步 NAPI 接口（自动检测格式）
 *
 * 参数：
 * - inputPath: 输入文件路径
 * - outputPath: 输出文件路径
 * - sampleRate: 采样率（可选，0 表示自动）
 * - channelCount: 声道数（可选，0 表示自动）
 * - bitrate: 比特率（可选，0 表示自动）
 *
 * @return boolean 成功返回 true，失败返回 false
 */
napi_value DecodeAudio(napi_env env, napi_callback_info info);

// ============================================================================
// 异步解码接口
// ============================================================================

/**
 * @brief 进度回调函数
 * @param env NAPI 环境
 * @param jsCallback JS 回调函数
 * @param context 上下文数据
 * @param data 回调数据
 */
void CallJsProgress(
    napi_env env,
    napi_value jsCallback,
    void* context,
    void* data);

/**
 * @brief 执行异步解码任务
 * @param env NAPI 环境
 * @param data 异步上下文
 */
void ExecuteDecodeAudioAsync(napi_env env, void* data);

/**
 * @brief 完成异步解码任务
 * @param env NAPI 环境
 * @param status 异步状态
 * @param data 异步上下文
 */
void CompleteDecodeAudioAsync(napi_env env, napi_status status, void* data);

/**
 * @brief 解码音频文件的异步 NAPI 接口（Promise + 进度回调）
 *
 * 参数：
 * - inputPathOrUri: 输入文件路径或 URI
 * - outputPath: 输出文件路径
 * - onProgress: 进度回调（可选）
 * - sampleRate: 采样率（可选，0 表示自动）
 * - channelCount: 声道数（可选，0 表示自动）
 * - bitrate: 比特率（可选，0 表示自动）
 *
 * @return Promise<boolean> 返回 Promise，resolve(boolean) 或 reject(error)
 */
napi_value DecodeAudioAsync(napi_env env, napi_callback_info info);

} // namespace napi_decoder

#endif // NAPI_DECODER_H