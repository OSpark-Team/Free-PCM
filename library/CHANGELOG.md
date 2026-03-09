# 更新日志 (Change Log)

所有重要的项目更改都将记录在此文件中。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)

## [1.0.5] - 2026-03-09

### 修复

* **多声道播放兼容修复**：修复部分 `5.1` 声道音源播放异常的问题，现已支持 `1~8` 声道歌曲播放。感谢 [@Salmon515](https://github.com/Salmon515) 发现该问题。

### 新增

* **1~8 声道 EQ / DRC 支持**：均衡器（EQ）与动态范围压缩（DRC）现已支持 `1~8` 声道音频处理，可覆盖从单声道到 `7.1` 声道的常见场景。
* **F32LE 格式支持**：新增 `F32LE` 采样格式支持。感谢 [@Salmon515](https://github.com/Salmon515) 实现该能力。

### 说明

* **多声道音量增益限制**：当前音量增益功能仍仅支持 `1~2` 声道；除单声道与双声道外，其他多声道场景暂不支持音量增益。

## [1.0.4] - 2026-03-04

### 修复

* **WAV S24LE 兼容修复**：修复 `audio/raw` passthrough 模式下 `S24LE (24-bit)` 未被识别、被错误当作 `S16LE` 处理的问题；为保证均衡器（EQ）与 DRC 等后处理能力，24-bit 源将无损扩展为 `S32LE` 输出。感谢 [@Salmon515](https://github.com/Salmon515) 发现问题。
* **EQ 处理链路失真优化**：移除原有逐样本 `tanh` 软削波方案，引入 **Auto Preamp + True-Peak Limiter** 处理管线，从根本上解决高位深场景下 EQ 正增益导致的信号过载问题。当 EQ 启用且存在正增益时，预放大量将自动计算并衰减（`preampDb = -(maxPositiveGainDb + 2 dB)`），Limiter 参数为：限幅阈值 `-1.0 dBTP`、4× 过采样、5 ms 前瞻、1 ms 启动、80 ms 释放，在保留瞬态细节的同时有效消除鼓击等宽频带音频的削波失真。

---

## [1.0.3] - 2026-02-28

### 新增

* **PCM 解码数据回调**：为 AudioRendererPlayer 新增 PCM Tap 回调支持，可以用于音频分析等
* **AudioRenderer 创建回调**：允许消费者注册一个在 AudioRenderer 创建时触发的回调，不影响正常播放流程。

---

## [1.0.2] - 2026-02-12

### 新增

* **DRC (动态范围压缩) 支持**：引入动态范围控制，自动平衡音频峰值，提升高动态音源的听感稳定性。
* **双声道独立均衡器**：均衡器现在支持 `Stereo` 模式，允许对左、右声道进行差异化频段调节。
* **分声道音量控制**：支持设置每个声道的独立增益系数，适用于声道平衡调节或特殊音效场景。

### 优化

* **N-API 性能优化**：针对高解析度（Hi-Res）音频进一步优化了自适应环形缓冲区（Adaptive Ring Buffer）的分配算法，降低高频数据交换时的 CPU 开销。

---

## [1.0.1] - 2026-02-09

### 新增

* **完整 Seek 支持**：支持前向/回溯跳转，并新增 `seekToAsync()`（针对 URL 场景，等待 post-seek PCM 数据就绪后 resolve）。
* **API 12+ 写入优化**：`writeData` 支持 `fillForWriteData()` 与 VALID/INVALID 拉取模式，数据不足时不再强制消耗 ring buffer。
* **自适应 RingBytes**：当不传参数或值时，系统根据采样率、声道、位宽及音源类型，自动在 64KB~512KB 范围内阶梯式选择最优缓冲区大小。

### 变更

* **语义统一**：将 `DecodeAudioProgress.ptsMs/durationMs` 与 `PcmStreamInfo.durationMs` 的单位统一明确为毫秒（ms）。

### 修复

* **WAV 倍速修复**：解决 `audio/raw` passthrough 时强制 s32le 导致 WAV 播放变调/倍速的问题，现已正确透传原始采样格式。
* **状态机稳定性**：修复 seek/flush 过程中 Codec 回调队列索引丢失导致的播放卡死问题。

---

## [1.0.0] - 2025-02-08

### 新增

* **初始版本发布**：支持 MP3、FLAC、WAV、AAC、OGG、Opus 等主流音频格式。
* **流式解码**：提供 `createPcmStreamDecoder`，配合 AudioRenderer 实现高效的拉取式播放。
* **内置均衡器**：10 段均衡器支持，内置 Pop、Rock、Jazz 等 9 种预设模式。
* **全场景支持**：支持本地 File Descriptor 和 HTTP/HTTPS 远程链接。
* **播放控制**：提供平滑音量渐变（淡入淡出）、播放速度动态调整及自动参数检测。

### 功能接口

* `decodeAudio()` / `decodeAudioAsync()` - 基础解码接口。
* `createPcmStreamDecoder()` - 流式播放核心。
* `AudioRendererPlayer` - 高层级播放器封装。
* `PcmEqualizer` - 音频后处理工具类。
