# Library 模块 - Free PCM 音频解码库

> 最后更新时间：2026-02-08 18:00:00

---

## 变更记录 (Changelog)

### 2026-02-08 18:00:00 - C++ 代码重构
- **重大重构**：将 `napi_init.cpp` (1208 行) 拆分为多个模块
- 新增目录结构：`buffer/`, `napi/`, `types/`, `utils/`
- `napi_init.cpp` 精简至 31 行（仅保留模块初始化）
- 提高代码可维护性和可测试性

### 2026-02-08 17:34:47
- 初始化模块文档
- 记录核心 API 与架构信息

---

[根目录](../CLAUDE.md) > **library**

---

## 模块职责

`library` 是 Free PCM 的核心模块，提供：
- **音频解码**：将多种格式（MP3、FLAC、WAV、AAC、OGG、Opus）解码为 PCM 原始数据
- **流式处理**：支持边解码边播放，降低内存占用
- **实时均衡器**：10 频段可调，9 种预设风格
- **播放控制**：封装 AudioRenderer 提供简化的播放 API

---

## 入口与启动

### 模块入口
- **主导出文件**：`Index.ets`
- **包名**：`@ospark/free-pcm`
- **版本**：1.0.0

### 导出的核心类
```typescript
export {
  AudioDecoderManager,    // 解码管理器
  AudioRendererPlayer,   // 播放器
  PcmDecoderTool,        // 解码工具
  PcmEqualizer,          // 均衡器
  EqPreset               // 预设常量
} from '@ospark/free-pcm';
```

### 使用方式
```typescript
// 1. 创建解码器
const decoder = PcmDecoderTool.getInstance().createStreamDecoder('/path/to/audio.mp3');

// 2. 等待解码器就绪
const info = await decoder.ready;

// 3. 创建播放器
const player = new AudioRendererPlayer();
await player.play(decoder, info);
```

---

## 对外接口

### 1. PcmDecoderTool

解码器工具类，提供工厂方法创建流式解码器。

```typescript
class PcmDecoderTool {
  createStreamDecoder(
    inputPathOrUri: string,
    options?: PcmStreamDecoderOptions,
    callbacks?: PcmStreamDecoderCallbacks
  ): PcmStreamDecoder;
}
```

**参数说明：**
- `inputPathOrUri`：本地文件路径或网络 URL
- `options`：解码配置（采样率、声道数、均衡器等）
- `callbacks`：进度和错误回调

### 2. AudioDecoderManager

解码管理器，基于单例模式，封装原生解码能力。

```typescript
class AudioDecoderManager {
  // 获取单例
  static getInstance(): AudioDecoderManager;

  // 初始化应用上下文
  initContext(context: common.Context): void;

  // 创建流式解码器
  createPcmStreamDecoder(
    inputPathOrUri: string,
    options?: PcmStreamDecoderOptions,
    callbacks?: PcmStreamDecoderCallbacks
  ): PcmStreamDecoder;

  // 同步解码文件（用于 Rawfile）
  decodeFile(inputPath: string, outputPath: string): boolean;

  // 异步解码 URL
  decodeFromUrl(
    url: string,
    outputFileName: string,
    onProgress?: (p: DecodeAudioProgress) => void
  ): Promise<boolean>;
}
```

### 3. AudioRendererPlayer

播放器类，封装 AudioRenderer 实现拉取式播放。

```typescript
class AudioRendererPlayer {
  // 播放音频
  play(decoder: PcmStreamDecoder, info: PcmStreamInfo): Promise<void>;

  // 播放控制
  pause(): Promise<void>;
  resume(): Promise<void>;
  stop(): Promise<void>;

  // 音量控制
  getVolume(): number | null;
  setVolume(volume: number): Promise<void>;
  setVolumeWithRamp(volume: number, durationMs: number): void;

  // 播放速度
  getSpeed(): number | null;
  setSpeed(speed: number): void;

  // 缓冲区控制
  drain(): Promise<void>;
  flush(): Promise<void>;

  // 状态查询
  getState(): audio.AudioState | null;
  getDurationMs(): number;
  getBufferSizeSync(): number;
}
```

### 4. PcmEqualizer

10 段图形均衡器工具类。

```typescript
class PcmEqualizer {
  // 获取/设置增益
  getGainsDb(): number[];
  setGainsDb(gainsDb: number[]): void;
  setBandGain(index: number, gainDb: number): number[];

  // 应用到解码器
  applyToDecoder(decoder: PcmStreamDecoder | null): void;
}

// 预设常量
export const EqPreset = {
  Default:  [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  Ballads:  [3, 5, 2, -4, 1, 2, -3, 1, 4, 5],
  Chinese:  [0, 0, 2, 0, 0, 4, 4, 2, 2, 5],
  Classical: [2, 3, 2, 1, 0, 0, -5, -5, -5, -6],
  Dance:    [4, 3, 2, -3, 0, 0, 5, 4, 2, 0],
  Jazz:     [2, 0, 2, 3, 6, 5, -1, 3, 4, 4],
  Pop:      [5, 2, 1, -1, -5, -5, -2, 1, 2, 4],
  RnB:      [1, 4, 5, 3, -2, -2, 2, 3, 5, 5],
  Rock:     [6, 4, 4, 2, 0, 1, 3, 3, 5, 4],
};
```

---

## 关键依赖与配置

### 原生依赖（C++）

**链接库（CMakeLists.txt）：**
- `libace_napi.z.so` - NAPI 接口
- `libhilog_ndk.z.so` - 日志系统
- `libnative_media_codecbase.so` - 媒体编解码器基础
- `libnative_media_core.so` - 媒体核心
- `libnative_media_acodec.so` - 音频编解码器
- `libnative_media_avdemuxer.so` - 媒体解复用器
- `libnative_media_avsource.so` - 媒体源

### ArkTS 依赖

**OpenHarmony Kit：**
- `@kit.AbilityKit` - Ability 和上下文
- `@kit.AudioKit` - AudioRenderer 等
- `@kit.CoreFileKit` - 文件 I/O
- `@kit.BasicServicesKit` - 错误处理
- `@kit.PerformanceAnalysisKit` - 日志

### 构建配置

**CMakeLists.txt（重构后）：**
```cmake
cmake_minimum_required(VERSION 3.5.0)
project(free_pcm)

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include
                    ${NATIVERENDER_ROOT_PATH}/buffer
                    ${NATIVERENDER_ROOT_PATH}/napi
                    ${NATIVERENDER_ROOT_PATH}/types)

add_library(library SHARED
    # NAPI interface
    napi_init.cpp
    napi/napi_utils.cpp
    napi/napi_decoder.cpp
    napi/napi_stream_decoder.cpp

    # Audio decoder
    audio_decoder.cpp
    pcm_equalizer.cpp

    # Buffer module
    buffer/ring_buffer.cpp)
```

**支持架构：**
- `arm64-v8a` - 64 位 ARM
- `x86_64` - 64 位 x86

---

## 数据模型

### PcmStreamInfo - PCM 流信息

```typescript
interface PcmStreamInfo {
  sampleRate: number;        // 采样率 (Hz)
  channelCount: number;      // 声道数
  sampleFormat: string;      // 采样格式 ('s16le', 's32le')
  sampleFormatCode: number;  // 格式编码 (1=S16LE, 3=S32LE)
  durationMs: number;        // 总时长 (ms)
}
```

### PcmStreamDecoderOptions - 解码配置

```typescript
interface PcmStreamDecoderOptions {
  sampleRate?: number;       // 目标采样率
  channelCount?: number;     // 目标声道数
  bitrate?: number;          // 目标比特率
  sampleFormat?: number;     // 采样格式 (1=S16LE, 3=S32LE)
  ringBytes?: number;        // 环形缓冲区大小 (Byte)
  eqEnabled?: boolean;       // 是否启用均衡器
  eqGainsDb?: number[];      // 均衡器增益配置
}
```

### PcmStreamDecoderCallbacks - 回调配置

```typescript
interface PcmStreamDecoderCallbacks {
  onProgress?: (p: DecodeAudioProgress) => void;  // 进度回调
  onError?: (e: Error) => void;                    // 错误回调
}
```

### DecodeAudioProgress - 解码进度

```typescript
interface DecodeAudioProgress {
  progress: number;      // 进度百分比 (0~1)
  ptsMs: number;         // 当前时间戳 (ms)
  durationMs: number;    // 总时长 (ms)
}
```

---

## 测试与质量

### 测试结构

```
library/src/
├── test/                # 单元测试
│   ├── LocalUnit.test.ets
│   └── List.test.ets
└── ohosTest/            # 设备测试
    └── ets/test/
        ├── Ability.test.ets
        └── List.test.ets
```

### 测试框架
- **Hypium** (@ohos/hypium) - OpenHarmony 测试框架
- **Hamock** (@ohos/hamock) - Mock 框架

### 质量工具
- 未配置 ESLint/Ruff 等代码检查工具
- 建议添加代码格式化和静态检查工具

---

## 常见问题 (FAQ)

### Q1: 如何处理解码失败？

**A:** 检查以下方面：
1. 文件路径或 URL 是否正确
2. 文件格式是否受支持
3. 是否有足够的存储空间
4. 查看日志输出的错误信息

```typescript
const decoder = decoderTool.createStreamDecoder(url, {}, {
  onError: (e) => {
    console.error('解码失败:', e.message);
    // e 包含 stage, code, message 字段
  }
});
```

### Q2: 均衡器增益值范围是多少？

**A:** 每个频段的增益范围是 **-24 ~ +24 dB**。超出范围的值会被自动限制。

### Q3: 如何选择合适的采样率？

**A:**
- 标准音频：44100Hz 或 48000Hz
- 高质量音频：96000Hz 或 192000Hz
- 节省空间：22050Hz 或 32000Hz

如果不指定，解码器会自动使用源文件的采样率。

### Q4: 环形缓冲区大小如何设置？

**A:** 默认为 512KB。可根据网络条件调整：
- 稳定网络：256KB（减少内存）
- 不稳定网络：1MB（提高抗抖动能力）

```typescript
const decoder = decoderTool.createStreamDecoder(url, {
  ringBytes: 1024 * 1024  // 1MB
});
```

### Q5: 支持哪些音频格式？

**A:**
- ✅ MP3
- ✅ FLAC
- ✅ WAV（支持直通解码）
- ✅ AAC / M4A
- ✅ OGG / Vorbis
- ✅ Opus

---

## 相关文件清单

### ArkTS 源文件
- `Index.ets` - 模块主入口
- `src/main/ets/utils/AudioDecoderManager.ets` - 解码管理器
- `src/main/ets/utils/AudioRendererPlayer.ets` - 播放器
- `src/main/ets/utils/PcmDecoderTool.ets` - 解码工具
- `src/main/ets/utils/PcmEqualizer.ets` - 均衡器

### C++ 源文件

**NAPI 接口层：**
- `src/main/cpp/napi_init.cpp` - NAPI 模块初始化（精简至 31 行）
- `src/main/cpp/napi/napi_utils.cpp` - NAPI 工具函数（~50 行）
- `src/main/cpp/napi/napi_decoder.cpp` - 文件解码接口（~250 行）
- `src/main/cpp/napi/napi_stream_decoder.cpp` - 流式解码器（~700 行）

**核心解码模块：**
- `src/main/cpp/audio_decoder.cpp` - 音频解码器（1218 行）
- `src/main/cpp/pcm_equalizer.cpp` - 均衡器实现（248 行）

**数据结构：**
- `src/main/cpp/buffer/ring_buffer.cpp` - 环形缓冲区（~150 行）
- `src/main/cpp/types/decoder_types.h` - 类型定义

### 配置文件
- `oh-package.json5` - 模块依赖配置
- `build-profile.json5` - 构建配置
- `src/main/module.json5` - 模块元信息
- `src/main/cpp/CMakeLists.txt` - CMake 构建脚本

### 类型定义
- `src/main/cpp/types/liblibrary/Index.d.ts` - TypeScript 类型定义
- `src/main/cpp/types/liblibrary/oh-package.json5` - 原生模块依赖

---

## 覆盖率缺口

### 已完成
- [x] 完整读取 `audio_decoder.cpp`（1218 行）
- [x] 完整读取 `pcm_equalizer.cpp`（248 行）
- [x] 读取 C++ 头文件（`audio_decoder.h`、`pcm_equalizer.h`）
- [x] 分析并重构 `napi_init.cpp`（从 1208 行精简至 31 行）
- [x] 提取环形缓冲区到独立模块
- [x] 拆分 NAPI 接口层

### 待完成
- [ ] 读取测试文件以了解测试覆盖范围
- [ ] 分析 decoder/ 目录中的文件（如存在）
- [ ] 分析 utils/ 目录中的文件（如存在）