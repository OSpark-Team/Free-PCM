# 更新日志 (Change Log)

所有重要的项目更改都将记录在此文件中。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)

## [1.0.1] - 2026-02-09

### 新增

- 完整 Seek：支持前向/回溯跳转，并新增 `seekToAsync()`（URL 场景等待 post-seek PCM ready 后 resolve）。
- API 12+ writeData 支持 `fillForWriteData()` + VALID/INVALID 拉取模式（数据不足不消耗 ring）。
- `ringBytes` 支持自适应：不传/<=0 时按音频参数（采样率/声道/位宽/时长/是否 URL）选择 64KB~512KB（64KB 阶梯）。

### 变更

- `DecodeAudioProgress.ptsMs/durationMs` 与 `PcmStreamInfo.durationMs` 语义统一为毫秒（ms）。

### 修复

- `audio/raw` passthrough（常见 WAV）使用源 PCM 的真实 sampleFormat，避免强制 s32le 导致“二倍速/变调”。
- seek/flush 状态机稳定性修复（避免 codec 回调队列丢 index 导致卡死）。

## [1.0.0] - 2025-02-08

### 新增

- 初始版本发布
- 支持多种音频格式解码（MP3、FLAC、WAV、AAC、OGG、Opus）
- 提供流式解码功能，配合 AudioRenderer 实现拉取式播放
- 内置 10 段均衡器，支持实时调整音频频谱
- 提供 9 种预设均衡器（Default、Ballads、Chinese、Classical、Dance、Jazz、Pop、R&B、Rock）
- 支持本地文件和 HTTP/HTTPS 远程 URL
- 提供 Promise 异步接口和进度回调
- 自动检测音频参数（采样率、声道数等）
- 支持音量控制、播放速度控制
- 支持平滑音量渐变（淡入淡出效果）

### 功能

- `decodeAudio()` - 同步解码音频文件为 PCM 格式
- `decodeAudioAsync()` - 异步解码音频文件，带进度回调
- `createPcmStreamDecoder()` - 创建流式 PCM 解码器
- `AudioRendererPlayer` - 音频播放器封装类
- `PcmEqualizer` - 10 段均衡器工具类

### 示例

- 基础解码示例
- 流式解码并播放示例
- 均衡器使用示例
- 远程 URL 解码示例

### 文档

- 完整的中文 JSDoc 注释
- README.md 使用指南
- 代码示例和最佳实践
