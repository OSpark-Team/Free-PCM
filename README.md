# Free PCM 音频解码库测试项目

这是一个基于 OpenHarmony 开发的音频解码库测试项目。

## 最近更新（开发中）

- **完整 Seek**：支持前向/回溯跳转，URL 场景会等待 post-seek PCM 就绪再恢复播放（避免“点了没反应/不确定是在卡还是在缓冲”）。
- **进度/时长单位统一**：对外回调 `DecodeAudioProgress.ptsMs/durationMs` 与 `PcmStreamInfo.durationMs` 统一为毫秒（ms）。
- **更稳的 writeData 拉取**：API 12+ 优先走“不足返回 INVALID，不消耗 ring”的模式，减少静音填充与音频丢失风险。
- **ringBytes 自适应**：`createStreamDecoder()` 的 `ringBytes` 变为可选；不传时 native 侧会按音频参数自适应选择 64KB~512KB（64KB 阶梯）。
- **WAV passthrough 修复**：`audio/raw`（常见 WAV）直通链路不做采样格式转换；会以源 PCM 的真实 sampleFormat 为准，避免强制 s32le 导致“二倍速/变调”。

## 项目结构

```
Free—PCM /
├── entry/          # 测试应用模块
├── library/        # 音频解码库模块（核心）
└── ...
```

## 模块说明

### 📱 entry/ - 测试应用

**主要作用：**

- 作为 Free PCM 音频解码库的测试和演示应用
- 验证 library 模块提供的 API 功能
- 展示音频解码、播放、均衡器等特性的实际使用方式
- 用于开发者调试和功能验证

**特点：**

- 独立的 HarmonyOS Ability 应用
- 可以在真机或模拟器上运行
- 提供 UI 界面进行交互测试
- 引用 `@ospark/free-pcm` 模块作为依赖

**当前 UI 侧重点：**

- Seek 滑块（松手触发 seek），并显示内部 BufferClock（解码器 ringbuffer 推进的时间）
- Buffering/Seeking 状态提示，便于观察 URL 缓冲与 seek 行为

### 🔧 library/ - 音频解码库

**主要作用：**

- 提供 Free PCM 音频解码的核心功能
- 支持多种音频格式（MP3、FLAC、WAV、AAC、OGG、Opus 等）
- 实现流式 PCM 解码和播放
- 内置 10 段图形均衡器
- 可作为独立库发布和使用

**核心特性：**

- **音频解码**：将各种编码格式解码为 PCM 原始数据
- **流式播放**：配合 AudioRenderer 实现拉取式播放，降低内存占用
- **实时均衡器**：10 频段可调，提供 9 种预设风格（Default、Ballads、Chinese、Classical、Dance、Jazz、Pop、RnB、Rock）
- **远程支持**：支持本地文件和网络 URL 解码
- **播放控制**：音量、播放速度、暂停/恢复、淡入淡出等
- **Seek + 当前播放位置**：解码线程支持 seek（demuxer seek + codec flush/重启），并提供内部播放位置（BufferClock）

**导出 API：**

```typescript
import {
  PcmDecoderTool,        // 解码工具类
  AudioRendererPlayer,   // 播放器类
  PcmEqualizer,          // 均衡器类
  EqPreset,              // 预设常量
  AudioDecoderManager    // 解码管理器
} from '@ospark/free-pcm';
```

## 快速开始

### 1. 构建项目

```bash
# 构建整个项目
hvigorw assembleHap

# 或仅构建 library 模块
hvigorw library:assembleHar
```

### 2. 运行测试应用

将 entry 模块部署到设备或模拟器运行，测试库的各项功能。

### 3. 使用库

```typescript
import { PcmDecoderTool, AudioRendererPlayer, PcmEqualizer, EqPreset } from '@ospark/free-pcm';

// 创建解码器
const decoderTool = PcmDecoderTool.getInstance();
const decoder = decoderTool.createStreamDecoder('/path/to/audio.mp3');
const info = await decoder.ready;

// 创建播放器
const player = new AudioRendererPlayer();
await player.play(decoder, info);

// Seek（推荐用 await，URL 时会等待数据就绪）
await player.seekTo(10_000);

// 内部播放位置（基于 ringbuffer 字节计数换算）
const posMs = player.getCurrentPosition();

// 应用均衡器
const equalizer = new PcmEqualizer();
equalizer.setGainsDb(EqPreset.Pop);
equalizer.applyToDecoder(decoder);
```

## 技术栈

- **语言**：ArkTS (TypeScript for HarmonyOS)
- **原生代码**：C++ (NAPI)
- **音频框架**：OpenHarmony AudioKit

## 许可证

Apache License 2.0

## 作者

Okysu
