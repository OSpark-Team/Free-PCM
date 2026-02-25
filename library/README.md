# @ospark/free-pcm

**Free PCM** 是一个高性能音频解码库，专为 OpenHarmony/HarmonyOS 设计。支持多种主流音频格式解码为 PCM，内置流式解码引擎与 10 段均衡器。

## ✨ 功能特性

* **全格式支持**：MP3, FLAC, WAV, AAC, OGG, Opus 等。
* **流式解码**：极速首开，配合 `AudioRenderer` 实现边解边播，无需等待大文件下载。
* **10 段均衡器**：内置 9 种专业级预设（流行、摇滚、爵士等），支持实时增益调整。
* **非阻塞异步**：全接口 Promise 化，核心任务在 Native 层异步执行，不卡顿 UI 线程。
* **智能参数**：支持从媒体流自动解析采样率和声道，亦支持手动覆盖。
* **高稳定性**：适配 API 12+ 的 `writeData` 回调，自适应环形缓冲区（64KB~512KB）。

---

## 📦 安装

### 命令行安装（推荐）

在您的项目模块目录下执行：

```bash
ohpm install @ospark/free-pcm

```

### 手动添加

在项目的 `oh-package.json5` 中添加依赖：

```json5
{
  "dependencies": {
    "@ospark/free-pcm": "^1.0.0"
  }
}

```

---

## 🚀 快速开始

### 1. 基础播放（使用 AudioRendererPlayer）

这是最简单的集成方式，自动处理了解码与 AudioKit 的对接。

```typescript
import { PcmDecoderTool, AudioRendererPlayer } from '@ospark/free-pcm';

// 创建解码器
const decoderTool = new PcmDecoderTool();
const decoder = decoderTool.createStreamDecoder('/data/storage/el2/base/test.mp3');

// 1. 等待元数据准备就绪
const info = await decoder.ready;

// 2. 创建播放器并开始播放
const player = new AudioRendererPlayer();
await player.play(decoder, info);

// 3. 播放控制
await player.setVolume(0.8);
await player.pause();
await player.resume();

```

### 2. 均衡器（EQ）调节

```typescript
import { PcmEqualizer, EqPreset, PcmDecoderTool } from '@ospark/free-pcm';

// 创建解码器和均衡器
const decoderTool = new PcmDecoderTool();
const decoder = decoderTool.createStreamDecoder('/path/to/audio.mp3');
const equalizer = new PcmEqualizer();

// 使用内置预设（推荐）
equalizer.setGainsDb(EqPreset.Pop);

// 应用到当前解码器（会自动启用 EQ）
equalizer.applyToDecoder(decoder);

```

---

## 📖 核心 API 概览

### 解码器配置 `createStreamDecoder`

通过 `PcmDecoderTool` 创建解码器：

```typescript
const decoderTool = new PcmDecoderTool();
const decoder = decoderTool.createStreamDecoder(inputPathOrUri, options, callbacks);
```

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `inputPathOrUri` | `string` | 本地路径或 HTTP/HTTPS URL |
| `options.sampleRate` | `number` | 采样率，0 为自动获取 |
| `options.eqEnabled` | `boolean` | 是否开启 EQ，默认 `false` |
| `options.ringBytes` | `number` | 缓冲区大小，不传则按 64KB~512KB 自适应 |

### 均衡器预设 `EqPreset`

| 预设常量 | 风格描述 |
| --- | --- |
| `EqPreset.Default` | 标准平直（全 0dB） |
| `EqPreset.Pop` / `Rock` | 流行 / 摇滚（增强两端） |
| `EqPreset.Chinese` | 华语流行（优化人声） |
| `EqPreset.Jazz` | 爵士（增强中频乐器） |

---

## 🛠 高级进阶：流式数据拉取

如果您需要深度定制播放逻辑，可以使用 API 12 的 `writeData` 模式：

```typescript
audioRenderer.on('writeData', (buffer: ArrayBuffer) => {
  // 数据不足时不消耗环形缓冲区，有效防止丢音
  if (decoder.fillForWriteData) {
    const n = decoder.fillForWriteData(buffer);
    return n > 0 ? audio.AudioDataCallbackResult.VALID : audio.AudioDataCallbackResult.INVALID;
  }
  
  decoder.fill(buffer);
  return audio.AudioDataCallbackResult.VALID;
});

```

---

## ⚠️ 注意事项

* **权限需求**：若解码远程 URL，请在 `module.json5` 中声明 `ohos.permission.INTERNET`。
* **WAV 格式**：对于 `audio/raw` 格式采用透传模式，以源文件采样格式为准，防止变调。

## 📄 许可证

本项目遵循 [Apache License 2.0](LICENSE) 开源协议。

---

**觉得好用？** 别忘了给个 Star 🌟