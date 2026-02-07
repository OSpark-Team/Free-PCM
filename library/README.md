# @okysu/free-pcm

Free PCM 音频解码库 - 支持多种音频格式解码为 PCM，提供流式解码和 10 段均衡器功能

## 功能特性

- **多种音频格式支持**：MP3、FLAC、WAV、AAC、OGG、Opus 等
- **流式解码**：配合 AudioRenderer 实现拉取式播放，而无需等待整个文件解码完成
- **10 段均衡器**：可实时调整音频频谱，支持 9 种预设风格
- **异步接口**：Promise 异步接口和进度回调，不会阻塞 UI 线程
- **HTTP/HTTPS 支持**：支持远程 URL 直接解码
- **自动参数检测**：自动从媒体流获取采样率、声道数等参数

## 安装

在项目的 `oh-package.json5` 中添加依赖：

```json5
{
  "dependencies": {
    "@okysu/free-pcm": "^1.0.0"
  }
}
```

## 快速开始

### 流式解码并播放

```typescript
import freePcm from '@okysu/free-pcm';

// 创建解码器
const decoder = freePcm.createPcmStreamDecoder(
  '/path/to/audio.mp3',
  {
    sampleRate: 44100,
    channelCount: 2,
    ringBytes: 1024 * 1024, // 1MB 环形缓冲区
    eqEnabled: true,
    eqGainsDb: EqPreset.Pop // 直接使用预设常量
  },
  {
    onProgress: (p) => {
      console.log(`解码进度: ${(p.progress * 100).toFixed(1)}%`);
    },
    onError: (e) => {
      console.error(`错误: ${e.message}`);
    }
  }
);

// 等待解码器准备
const info = await decoder.ready;
console.log('音频参数:', info);

// 创建 AudioRenderer
import { audio } from '@kit.AudioKit';
const audioStreamInfo: audio.AudioStreamInfo = {
  samplingRate: info.sampleRate === 44100 ? audio.AudioSamplingRate.SAMPLE_RATE_44100 :
                 info.sampleRate === 48000 ? audio.AudioSamplingRate.SAMPLE_RATE_48000 :
                 audio.AudioSamplingRate.SAMPLE_RATE_48000,
  channels: info.channelCount === 1 ? audio.AudioChannel.CHANNEL_1 :
             audio.AudioChannel.CHANNEL_2,
  sampleFormat: audio.AudioSampleFormat.SAMPLE_FORMAT_S16LE,
  encodingType: audio.AudioEncodingType.ENCODING_TYPE_RAW
};

const audioRendererInfo: audio.AudioRendererInfo = {
  usage: audio.StreamUsage.STREAM_USAGE_MUSIC,
  rendererFlags: 0
};

const audioRendererOptions: audio.AudioRendererOptions = {
  streamInfo: audioStreamInfo,
  rendererInfo: audioRendererInfo
};

const audioRenderer = await audio.createAudioRenderer(audioRendererOptions);

// 设置 writeData 回调，从解码器拉取 PCM 数据
audioRenderer.on('writeData', (buffer: ArrayBuffer) => {
  const bytesWritten = decoder.fill(buffer);
  // bytesWritten 是实际写入的有效字节数
});

// 启动播放
await audioRenderer.start();

// 等待解码完成
await decoder.done;

// 停止播放
await audioRenderer.stop();
await audioRenderer.release();
```

### 使用均衡器（推荐方式）

```typescript
import freePcm from '@okysu/free-pcm';
import { PcmEqualizer, EqPreset } from '@okysu/free-pcm';

// 创建均衡器实例
const equalizer = new PcmEqualizer();

// 应用预设（直接使用常量，非常简洁）
equalizer.setGainsDb(EqPreset.Pop);       // 流行音乐
equalizer.setGainsDb(EqPreset.Rock);       // 摇滚音乐
equalizer.setGainsDb(EqPreset.Default);    // 标准平直响应

// 设置自定义增益（每个值范围 -24 ~ +24 dB）
equalizer.setGainsDb([5, 4, 3, 1, 0, -1, 1, 3, 4, 5]);

// 获取当前增益
const gains = equalizer.getGainsDb();
console.log('当前增益:', gains);

// 应用到解码器
decoder.setEqEnabled(true);
decoder.setEqGains(equalizer.getGainsDb());

// 切换预设
equalizer.setGainsDb(EqPreset.Jazz);
decoder.setEqGains(equalizer.getGainsDb());
```

### 使用 AudioRendererPlayer（简化播放控制）

```typescript
import freePcm from '@okysu/free-pcm';
import { AudioRendererPlayer } from '@okysu/free-pcm';

// 创建播放器和解码器
const player = new AudioRendererPlayer();
const decoder = freePcm.createPcmStreamDecoder('/path/to/audio.mp3');

// 等待解码器准备
const info = await decoder.ready;

// 开始播放
await player.play(decoder, info);

// 设置音量（0.0 ~ 1.0）
await player.setVolume(0.5); // 50% 音量

// 获取当前播放状态
const state = player.getState();
console.log('播放状态:', state);

// 暂停播放
await player.pause();

// 恢复播放
await player.resume();

// 平滑渐变音量（淡出效果）
player.setVolumeWithRamp(0.0, 1000); // 1秒内渐变到静音

// 停止播放
await player.stop();
```

### 均衡器预设说明

本库提供了 8 种预设风格，可以直接通过 `EqPreset` 常量访问：

| 预设 | 常量访问 | 特点说明 |
|------|----------|----------|
| **Default** | `EqPreset.Default` | 标准平直响应，所有频段增益均为 0 dB |
| **Ballads** | `EqPreset.Ballads` | 增强人声和吉他频段 |
| **Chinese** | `EqPreset.Chinese` | 华语流行，优化人声和旋律 |
| **Classical** | `EqPreset.Classical` | 古典音乐，平衡各频段突出细节 |
| **Dance** | `EqPreset.Dance` | 舞曲，增强低频和高频提升节奏感 |
| **Jazz** | `EqPreset.Jazz` | 爵士，优化中频突出乐器 |
| **Pop** | `EqPreset.Pop` | 流行音乐，增强低频和高频 |
| **R&B** | `EqPreset.RnB` | R&B，增强低频和中高频 |
| **Rock** | `EqPreset.Rock` | 摇滚，增强低频和高频提升冲击力 |

频段对应频率（索引 0-9）：
- 0: 31Hz
- 1: 62Hz
- 2: 125Hz
- 3: 250Hz
- 4: 500Hz
- 5: 1kHz
- 6: 2kHz
- 7: 4kHz
- 8: 8kHz
- 9: 16kHz

### 进度回调

```typescript
const decoder = freePcm.createPcmStreamDecoder('/path/to/audio.mp3', null, {
  onProgress: (p) => {
    if (p.progress >= 0) {
      // 已知时长，显示百分比
      console.log(`进度: ${(p.progress * 100).toFixed(1)}%`);
    } else {
      // 时长未知，显示已解码时间
      console.log(`已解码: ${p.ptsMs}ms`);
    }
  },
  onError: (e) => {
    console.error(`[${e.stage}] 错误 ${e.code}: ${e.message}`);
  }
});
```

### HTTP 远程解码

```typescript
const decoder = freePcm.createPcmStreamDecoder(
  'https://example.com/audio.mp3',
  { sampleRate: 44100, channelCount: 2 }
);

const info = await decoder.ready;
console.log('远程音频时长:', info.durationMs);
```

### 组合使用：均衡器 + 播放器

```typescript
import freePcm from '@okysu/free-pcm';
import { AudioRendererPlayer, PcmEqualizer, EqPreset } from '@okysu/free-pcm';

const player = new AudioRendererPlayer();
const equalizer = new PcmEqualizer();

// 创建解码器并应用预设
const decoder = freePcm.createPcmStreamDecoder(
  '/path/to/audio.mp3',
  {
    sampleRate: 44100,
    channelCount: 2,
    eqEnabled: true,
    eqGainsDb: EqPreset.Pop // 直接使用预设！
  }
);

const info = await decoder.ready;

// 开始播放
await player.play(decoder, info);

// 等待播放完成...
await decoder.done;
await player.stop();

// 切换预设
equalizer.setGainsDb(EqPreset.Rock);
decoder.setEqGains(equalizer.getGainsDb());
```

## API 参考

### createPcmStreamDecoder

创建一个流式 PCM 解码器。

**参数：**
- `inputPathOrUri`: 音频文件路径或 HTTP/HTTPS URL
- `options?`: 解码器配置选项
  - `sampleRate?: number` - 采样率（Hz），0 表示自动获取
  - `channelCount?: number` - 声道数，0 表示自动获取
  - `bitrate?: number` - 比特率（bps），0 表示不设置
  - `ringBytes?: number` - 环形缓冲区大小（字节），默认 512KB
  - `eqEnabled?: boolean` - 是否启用均衡器，默认 false
  - `eqGainsDb?: number[]` - 初始 10 段增益（-24 ~ +24 dB）

**返回：** `PcmStreamDecoder` 对象

### PcmStreamDecoder

解码器接口，包含以下属性和方法：

**属性：**
- `ready: Promise<PcmStreamInfo>` - 准备完成后返回流信息
- `done: Promise<void>` - 解码完成或失败

**方法：**
- `fill(buffer: ArrayBuffer): number` - 填充 AudioRenderer 缓冲区
- `close(): void` - 请求停止解码
- `setEqEnabled(enabled: boolean): void` - 启用/禁用均衡器
- `setEqGains(gainsDb: number[]): void` - 设置 10 段均衡器增益

### PcmStreamInfo

流信息接口，包含：
- `sampleRate: number` - 采样率（Hz）
- `channelCount: number` - 声道数
- `sampleFormat: string` - 采样格式（'s16le' 或 'unknown'）
- `durationMs: number` - 音频时长（毫秒），0 表示未知

### DecodeAudioProgress

进度信息接口，包含：
- `progress: number` - 进度百分比（0~1 或 -1 表示未知）
- `ptsMs: number` - 已解码到的时间戳（毫秒）
- `durationMs: number` - 总时长（毫秒），0 表示未知

### PcmEqualizer

10 段均衡器工具类

**方法：**
- `getGainsDb(): number[]` - 获取当前 10 段增益值
- `setGainsDb(gainsDb: number[]): void` - 设置 10 段增益值
- `applyToDecoder(decoder: PcmStreamDecoder): void` - 应用到解码器

### EqPreset

均衡器预设常量对象，包含 8 种预设：

| 预设 | 特点 |
|------|------|
| `EqPreset.Default` | 标准平直响应 |
| `EqPreset.Ballads` | 民谣预设 |
| `EqPreset.Chinese` | 华语流行预设 |
| `EqPreset.Classical` | 古典音乐预设 |
| `EqPreset.Dance` | 舞曲预设 |
| `EqPreset.Jazz` | 爵士预设 |
| `EqPreset.Pop` | 流行音乐预设 |
| `EqPreset.RnB` | R&B 预设 |
| `EqPreset.Rock` | 摇滚预设 |

### AudioRendererPlayer

音频播放器封装类，简化 AudioRenderer 使用

**方法：**
- `play(decoder, info): Promise<void>` - 开始播放
- `pause(): Promise<void>` - 暂停播放
- `resume(): Promise<void>` - 恢复播放
- `stop(): Promise<void>` - 停止播放并释放资源
- `setVolume(volume): Promise<void>` - 设置音量（0.0~1.0）
- `setVolumeWithRamp(volume, durationMs): void` - 平滑渐变音量
- `setSpeed(speed): void` - 设置播放速度（0.25~4.0）
- `getState(): AudioState` - 获取播放状态
- `getDurationMs(): number` - 获取音频时长
- `getVolume(): number` - 获取当前音量
- `getSpeed(): number` - 获取当前播放速度

## 支持的音频格式

- MP3 (`audio/mpeg`)
- FLAC (`audio/flac`)
- WAV (`audio/wav`)
- AAC (`audio/mp4a`)
- OGG/Vorbis (`audio/vorbis`)
- Opus (`audio/opus`)

## 许可证

Apache License 2.0

## 作者

okysu

## 版本

1.0.0
