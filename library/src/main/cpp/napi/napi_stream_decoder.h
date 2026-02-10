#ifndef NAPI_STREAM_DECODER_H
#define NAPI_STREAM_DECODER_H

#include <napi/native_api.h>
#include "../audio_decoder.h"
#include "../napi/napi_utils.h"
#include "../types/decoder_types.h"
#include <hilog/log.h>

namespace napi_stream_decoder {

// ============================================================================
// 流式解码器事件回调
// ============================================================================

/**
 * @brief 调用 JS 解码器事件回调
 * @param env NAPI 环境
 * @param jsCallback JS 回调函数
 * @param context 解码器上下文
 * @param data 事件数据
 */
void CallJsDecoderEvent(
    napi_env env,
    napi_value jsCallback,
    void* context,
    void* data);

// ============================================================================
// 流式解码器方法
// ============================================================================

/**
 * @brief 填充 PCM 数据到缓冲区
 * @param env NAPI 环境
 * @param info 回调信息
 * @return 填充的字节数
 */
napi_value PcmDecoderFill(napi_env env, napi_callback_info info);

/**
 * @brief 专用于 AudioRenderer.on('writeData') 的填充方法（API 12+）
 * @param env NAPI 环境
 * @param info 回调信息
 * @return 0=数据不足（建议返回 INVALID），否则返回 buffer.byteLength
 */
napi_value PcmDecoderFillForWriteData(napi_env env, napi_callback_info info);

/**
 * @brief 关闭流式解码器
 * @param env NAPI 环境
 * @param info 回调信息
 * @return undefined
 */
napi_value PcmDecoderClose(napi_env env, napi_callback_info info);

/**
 * @brief 设置均衡器启用状态
 * @param env NAPI 环境
 * @param info 回调信息
 * @return undefined
 */
napi_value PcmDecoderSetEqEnabled(napi_env env, napi_callback_info info);

/**
 * @brief 设置均衡器增益
 * @param env NAPI 环境
 * @param info 回调信息
 * @return undefined
 */
napi_value PcmDecoderSetEqGains(napi_env env, napi_callback_info info);

/**
 * @brief 设置左右声道独立均衡器增益
 * @param env NAPI 环境
 * @param info 回调信息
 * @return undefined
 */
napi_value PcmDecoderSetEqGainsLR(napi_env env, napi_callback_info info);

/**
 * @brief 设置左右声道独立音量系数（听感补偿）
 * @param env NAPI 环境
 * @param info 回调信息
 * @return undefined
 */
napi_value PcmDecoderSetChannelVolumes(napi_env env, napi_callback_info info);

// ============================================================================
// Seek 功能接口
// ============================================================================

/**
 * @brief 跳转到指定播放位置
 * @param env NAPI 环境
 * @param info 回调信息
 * @return undefined
 */
napi_value PcmDecoderSeekTo(napi_env env, napi_callback_info info);

/**
 * @brief 异步跳转到指定播放位置
 * @param env NAPI 环境
 * @param info 回调信息
 * @return Promise<void>
 */
napi_value PcmDecoderSeekToAsync(napi_env env, napi_callback_info info);

/**
 * @brief 获取当前播放位置
 * @param env NAPI 环境
 * @param info 回调信息
 * @return 当前播放位置（毫秒）
 */
napi_value PcmDecoderGetPosition(napi_env env, napi_callback_info info);

// ============================================================================
// 流式解码器异步工作
// ============================================================================

/**
 * @brief 执行流式解码任务
 * @param env NAPI 环境
 * @param data 解码器上下文
 */
void ExecutePcmStreamDecode(napi_env env, void* data);

/**
 * @brief 完成流式解码任务
 * @param env NAPI 环境
 * @param status 异步状态
 * @param data 解码器上下文
 */
void CompletePcmStreamDecode(napi_env env, napi_status status, void* data);

/**
 * @brief 析构流式解码器
 * @param env NAPI 环境
 * @param finalize_data 要释放的数据
 * @param finalize_hint 析构提示
 */
void FinalizePcmStreamDecoder(napi_env env, void* finalize_data, void* finalize_hint);

// ============================================================================
// 流式解码器创建
// ============================================================================

/**
 * @brief 创建 PCM 流式解码器
 *
 * 参数：
 * - inputPathOrUri: 输入文件路径或 URI
 * - options: 选项对象（可选）
 *   - sampleRate: 采样率
 *   - channelCount: 声道数
 *   - bitrate: 比特率
 *   - sampleFormat: 采样格式（1=S16LE, 3=S32LE）
 *   - ringBytes: 环形缓冲区大小
 *   - eqEnabled: 是否启用均衡器
 *   - eqGainsDb: 均衡器增益数组
 * - callbacks: 回调对象（可选）
 *   - onProgress: 进度回调
 *   - onError: 错误回调
 *
 * 返回：解码器对象
 * - ready: Promise<StreamInfo> 解码器就绪 Promise
 * - done: Promise<void> 解码完成 Promise
 * - fill(buffer: ArrayBuffer): number 填充数据
 * - close(): void 关闭解码器
 * - setEqEnabled(enabled: boolean): void 设置均衡器启用
 * - setEqGains(gainsDb: number[]): void 设置均衡器增益
 *
 * @return 解码器对象
 */
napi_value CreatePcmStreamDecoder(napi_env env, napi_callback_info info);

} // namespace napi_stream_decoder

#endif // NAPI_STREAM_DECODER_H
