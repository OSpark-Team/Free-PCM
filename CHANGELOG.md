# 更新日志 (Change Log)

本仓库包含两个模块：
- `library/`：Free PCM 核心库
- `entry/`：测试/演示应用

所有重要更改会记录在此文件中（格式参考 Keep a Changelog）。

## [1.0.1] - 2026-02-09

### 新增

- 支持完整 Seek（前向/回溯跳转），并提供内部播放位置（BufferClock）。
- URL 场景 Seek 等待 post-seek PCM 就绪再恢复播放（避免“seek 后不确定在缓冲还是卡住”）。
- `ringBytes` 变为可选；不传时 native 侧按音频参数自适应选择 64KB~512KB（64KB 阶梯）。

### 变更

- `DecodeAudioProgress.ptsMs/durationMs` 与 `PcmStreamInfo.durationMs` 统一按毫秒（ms）语义输出。
- API 12+ 的 `writeData` 回调优先走“不足返回 INVALID，不消耗 ring”的拉取策略。

### 修复

- WAV `audio/raw` passthrough 链路使用源 PCM 的真实 sampleFormat，避免强制 s32le 导致变速/变调。
- 修复与 seek/flush 相关的状态机问题，提升 URL seek 的可用性与稳定性。
