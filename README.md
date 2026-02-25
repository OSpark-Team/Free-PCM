# Free-PCM 音频解码库 (OpenHarmony)

`Free-PCM` 是一个专为 OpenHarmony 打造的高性能音频解码与播放解决方案。它基于 NAPI 封装了 C++ 底层解码能力，支持流式拉取播放、实时 10 段均衡器调节以及多种主流音频格式的无缝解码。

## 🚀 核心特性

* **多格式支持**：支持 MP3, FLAC, WAV (Raw Passthrough), AAC, OGG, Opus 等主流格式。
* **精准 Seek 机制**：支持前向/回溯跳转。针对 URL 场景优化，确保 Seek 后 PCM 数据就绪再恢复播放，彻底告别“点击无响应”的焦虑。
* **自适应 RingBuffer**：`createStreamDecoder` 支持 `ringBytes` 自适应（64KB~512KB），根据音频参数动态分配，平衡内存与流畅度。
* **高性能播放**：API 12+ 优先采用“非消耗型写入”模式，显著降低静音填充率和音频丢失风险。
* **实时均衡器 (EQ)**：内置 10 段图形均衡器，支持 9 种预设（Pop, Rock, Classical 等）及自定义增益调节。
* **智能采样**：WAV 格式自动进入 Passthrough 链路，保留原始采样格式，避免强制转换导致的变调或倍速问题。

---

## 📂 项目架构

```text
Free-PCM /
├── entry/           # 示例工程：功能演示、UI 交互、真机调试
└── library/         # 核心模块：ArkTS 接口定义 + C++ 原生解码实现 (NAPI)

```

### 模块职责划分

| 模块 | 核心职责 | 关键组件 |
| --- | --- | --- |
| **entry** | UI 演示、性能验证、真机调试 | Seek 滑块、BufferClock 显示、状态监听 |
| **library** | 解码逻辑、流控制、EQ 处理 | `PcmDecoderTool`, `AudioRendererPlayer`, `PcmEqualizer` |

---

## 🛠 快速集成

### 1. 构建与引用

```bash
# 构建 library 模块生成 HAR 包
hvigorw library:assembleHar

```

在项目中引用：`import { ... } from '@ospark/free-pcm';`

### 2. 基础播放示例

```typescript
import { PcmDecoderTool, AudioRendererPlayer, PcmEqualizer, EqPreset } from '@ospark/free-pcm';

async function startAudio() {
  // 1. 创建解码工具实例
  const decoderTool = new PcmDecoderTool();
  
  // 2. 初始化解码器（支持本地路径与网络 URL）
  const decoder = decoderTool.createStreamDecoder('https://example.com/music.mp3');
  const info = await decoder.ready; // 等待元数据就绪

  // 3. 关联播放器并启动
  const player = new AudioRendererPlayer();
  await player.play(decoder, info);

  // 4. 实时 EQ 调节
  const equalizer = new PcmEqualizer();
  equalizer.setGainsDb(EqPreset.Rock);
  equalizer.applyToDecoder(decoder);
}

```

---

## 📊 核心 API 概览

### 状态监听与位置获取

* **统一单位**：所有时间单位（`ptsMs`, `durationMs`）均统一为 **毫秒 (ms)**。
* **BufferClock**：通过 `player.getCurrentPosition()` 获取基于 RingBuffer 字节计数换算的精准播放位置。

### 播放控制矩阵

| 方法 | 描述 | 备注 |
| --- | --- | --- |
| `seekTo(ms)` | 跳转至指定位置 | 异步方法，URL 模式下建议 `await` |
| `setVolume(0-1)` | 调节音量 | 支持平滑淡入淡出 |
| `pause() / resume()` | 暂停与恢复 | 解码线程同步挂起/唤醒 |

---

## 🛠 技术栈

* **语言**：ArkTS, C++ (Native)
* **核心框架**：OpenHarmony AudioKit, NAPI
* **许可证**：[Apache License 2.0](https://www.google.com/search?q=LICENSE)

---

## 🤝 贡献与反馈

如果你在测试过程中发现任何 Buffer 异常或格式不兼容问题，欢迎提交 Issue。

**Author:** [Okysu](https://github.com/Okysu)

---