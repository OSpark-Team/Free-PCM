# Free PCM 音频解码库测试项目

这是一个基于 OpenHarmony 开发的音频解码库测试项目。

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
- 引用 `@okysu/free-pcm` 模块作为依赖

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

**导出 API：**

```typescript
import {
  PcmDecoderTool,        // 解码工具类
  AudioRendererPlayer,   // 播放器类
  PcmEqualizer,          // 均衡器类
  EqPreset,              // 预设常量
  AudioDecoderManager    // 解码管理器
} from '@okysu/free-pcm';
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
import { PcmDecoderTool, AudioRendererPlayer, PcmEqualizer, EqPreset } from '@okysu/free-pcm';

// 创建解码器
const decoderTool = PcmDecoderTool.getInstance();
const decoder = decoderTool.createStreamDecoder('/path/to/audio.mp3');
const info = await decoder.ready;

// 创建播放器
const player = new AudioRendererPlayer();
await player.play(decoder, info);

// 应用均衡器
const equalizer = new PcmEqualizer();
equalizer.setGainsDb(EqPreset.Pop);
equalizer.applyToDecoder(decoder);
```

## 技术栈

- **语言**：ArkTS (TypeScript for HarmonyOS)
- **原生代码**：C++ (NAPI)
- **音频框架**：OpenHarmony AudioKit
- **解码库**：FFmpeg

## 许可证

Apache License 2.0

## 作者

Okysu