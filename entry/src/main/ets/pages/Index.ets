import { promptAction } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import {
  AudioRendererPlayer,
  DecodeAudioProgress,
  PcmDecoderTool,
  PcmEqualizer,
  PcmStreamDecoder
} from '@okysu/free-pcm';

// --- 常量定义 ---
export enum EqPresetId {
  Default = 'Default',
  Ballads = 'Ballads',
  Chinese = 'Chinese',
  Classical = 'Classical',
  Dance = 'Dance',
  Jazz = 'Jazz',
  Pop = 'Pop',
  RnB = 'RnB',
  Rock = 'Rock'
}

export const EQ_PRESET_MAP: Record<EqPresetId, number[]> = {
  [EqPresetId.Default]: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  [EqPresetId.Ballads]: [3, 5, 2, -4, 1, 2, -3, 1, 4, 5],
  [EqPresetId.Chinese]: [0, 0, 2, 0, 0, 4, 4, 2, 2, 5],
  [EqPresetId.Classical]: [2, 3, 2, 1, 0, 0, -5, -5, -5, -6],
  [EqPresetId.Dance]: [4, 3, 2, -3, 0, 0, 5, 4, 2, 0],
  [EqPresetId.Jazz]: [2, 0, 2, 3, 6, 5, -1, 3, 4, 4],
  [EqPresetId.Pop]: [5, 2, 1, -1, -5, -5, -2, 1, 2, 4],
  [EqPresetId.RnB]: [1, 4, 5, 3, -2, -2, 2, 3, 5, 5],
  [EqPresetId.Rock]: [6, 4, 4, 2, 0, 1, 3, 3, 5, 4]
};

export const EQ_BAND_LABELS: string[] = ['31Hz', '62Hz', '125Hz', '250Hz', '500Hz', '1kHz', '2kHz', '4kHz', '8kHz', '16kHz'];

const EQ_PRESET_LABEL: Record<EqPresetId, string> = {
  [EqPresetId.Default]: '标准 (Default)',
  [EqPresetId.Ballads]: '民谣 (Ballads)',
  [EqPresetId.Chinese]: '中国风 (Chinese)',
  [EqPresetId.Classical]: '古典 (Classical)',
  [EqPresetId.Dance]: '舞曲 (Dance)',
  [EqPresetId.Jazz]: '爵士 (Jazz)',
  [EqPresetId.Pop]: '流行 (Pop)',
  [EqPresetId.RnB]: 'R&B',
  [EqPresetId.Rock]: '摇滚 (Rock)'
};

export interface EqPreset {
  id: EqPresetId;
  label: string;
  gainsDb: number[];
}

export const EQ_PRESET_LIST: EqPreset[] = Object.keys(EQ_PRESET_MAP).map((key) => {
  const id = key as EqPresetId;
  return {
    id: id,
    label: EQ_PRESET_LABEL[id],
    gainsDb: EQ_PRESET_MAP[id]
  } as EqPreset;
});

@Entry
@Component
struct Index {
  // --- UI 状态变量 ---
  @State message: string = '音频解码测试';
  @State status: string = '待机状态';
  @State isDecoding: boolean = false;
  @State isPlaying: boolean = false;
  @State url: string = '';
  @State progressPercent: number = 0;
  @State progressHint: string = '';

  // --- 监测与 Seek 状态 (来自片段1) ---
  @State currentPosMs: number = 0;
  @State statusText: string = '';
  @State isSeeking: boolean = false;
  @State isBuffering: boolean = false;

  // --- Seek Slider 状态 ---
  @State durationMs: number = 0;
  @State seekSliderMs: number = 0;
  @State isDraggingSeek: boolean = false;

  // --- EQ 状态 ---
  @State eqPresetId: EqPresetId = EqPresetId.Default;
  @State eqGainsDb: number[] = EQ_PRESET_MAP[EqPresetId.Default].slice();

  // --- 播放器私有变量 ---
  private player: AudioRendererPlayer = new AudioRendererPlayer();
  private decoderTool: PcmDecoderTool = new PcmDecoderTool();
  private eq: PcmEqualizer = new PcmEqualizer();
  private currentDecoder: PcmStreamDecoder | null = null;

  // --- 缓冲监测辅助变量 ---
  private lastPosMs: number = 0;
  private lastPosUpdateTs: number = 0;
  private bufferingTimerId: number = -1;

  // --- 核心方法：缓冲监控 ---
  private startBufferingWatchdog() {
    if (this.bufferingTimerId !== -1) return;

    this.lastPosMs = this.currentPosMs;
    this.lastPosUpdateTs = Date.now();

    this.bufferingTimerId = setInterval(() => {
      // 正在 seek 时不判定为 buffering
      if (this.isSeeking) {
        this.isBuffering = false;
        this.lastPosMs = this.currentPosMs;
        this.lastPosUpdateTs = Date.now();
        return;
      }

      const now = Date.now();
      // 如果位置有变动，重置计数
      if (this.currentPosMs !== this.lastPosMs) {
        this.lastPosMs = this.currentPosMs;
        this.lastPosUpdateTs = now;
        this.isBuffering = false;
        return;
      }

      // 如果正在播放中，且 800ms 位置没动，则触发 buffering
      if (this.isPlaying) {
        this.isBuffering = (now - this.lastPosUpdateTs) > 800;
      } else {
        this.isBuffering = false;
      }
    }, 200);
  }

  private stopBufferingWatchdog() {
    if (this.bufferingTimerId !== -1) {
      clearInterval(this.bufferingTimerId);
      this.bufferingTimerId = -1;
    }
    this.isBuffering = false;
  }

  // --- 核心方法：Seek 操作 ---
  private async doSeek(targetMs: number) {
    if (!this.player || !this.isPlaying) return;

    this.isSeeking = true;
    this.statusText = `seeking -> ${targetMs}ms`;
    try {
      // 执行 seek，底层会等待 post-seek PCM ready
      await this.player.seekTo(targetMs);
      this.statusText = `seek ok, pos=${targetMs}ms`;
    } catch (e) {
      this.statusText = `seek failed: ${(e as Error).message}`;
      promptAction.showToast({ message: 'Seek 失败' });
    } finally {
      this.isSeeking = false;
    }
  }

  private clampSeekMs(ms: number): number {
    if (ms < 0) return 0;
    if (this.durationMs > 0 && ms > this.durationMs) return this.durationMs;
    return ms;
  }

  private formatTimeMs(ms: number): string {
    if (!Number.isFinite(ms) || ms < 0) {
      return '00:00';
    }
    const totalSec = Math.floor(ms / 1000);
    const min = Math.floor(totalSec / 60);
    const sec = totalSec % 60;
    return `${min.toString().padStart(2, '0')}:${sec.toString().padStart(2, '0')}`;
  }

  // --- 核心方法：播放逻辑 ---
  async playUrl() {
    if (this.isDecoding || this.isPlaying) return;
    const url = this.url.trim();
    if (!url) {
      promptAction.showToast({ message: '请输入有效 URL', duration: 2000 });
      return;
    }

    this.isPlaying = true;
    this.status = '正在准备解码...';

    // 启动缓冲监测
    this.startBufferingWatchdog();

    try {
      const decoder = this.decoderTool.createStreamDecoder(url, {
        ringBytes: 256 * 1024,
        eqEnabled: true,
        eqGainsDb: this.eqGainsDb,
        sampleFormat: 3
      }, {
        onProgress: (p: DecodeAudioProgress): void => {
          this.updateProgress(p);
        },
        onError: (e: Error) => {
          this.status = `错误: ${e.message}`;
          this.stopPlayback();
        }
      });

      this.currentDecoder = decoder;
      const info = await decoder.ready;

      // Duration for slider (0 means unknown)
      this.durationMs = info.durationMs ?? 0;
      this.seekSliderMs = 0;

      this.status = `播放中: ${info.sampleRate}Hz`;

      await this.player.play(decoder, info);

      // Use internal buffer clock (ringbuffer bytes read) for UI time.
      this.player.setOnTimeUpdate((posMs: number) => {
        if (!this.isDraggingSeek) {
          this.currentPosMs = posMs;
        }
      });

      await decoder.done;
      this.status = '播放结束';
    } catch (err) {
      this.status = `播放失败: ${(err as BusinessError).message}`;
    } finally {
      this.isPlaying = false;
      this.stopBufferingWatchdog();
    }
  }

  private async stopPlayback() {
    this.stopBufferingWatchdog();
    await this.player.stop();
    this.currentDecoder = null;
    this.isPlaying = false;
    this.isDecoding = false;
    this.status = '播放已停止';
    this.currentPosMs = 0;
    this.durationMs = 0;
    this.seekSliderMs = 0;
    this.isDraggingSeek = false;
    this.progressPercent = 0;
  }

  private updateProgress(p: DecodeAudioProgress) {
    // Progress callback comes from native decode pipeline; time units may vary
    // across platform versions. For UI, prefer internal buffer clock via
    // player.setOnTimeUpdate(). Here we only show percentage when available.
    if (p.progress >= 0) {
      this.progressPercent = Math.min(100, Math.max(0, Math.round(p.progress * 100)));
      if (this.durationMs > 0) {
        this.progressHint = `${this.progressPercent}% (buffer: ${this.formatTimeMs(this.currentPosMs)} / ${this.formatTimeMs(this.durationMs)})`;
      } else {
        this.progressHint = `${this.progressPercent}% (buffer: ${this.formatTimeMs(this.currentPosMs)})`;
      }
      return;
    }

    this.progressPercent = 0;
    this.progressHint = `buffer: ${this.formatTimeMs(this.currentPosMs)}`;
  }

  private formatTime(seconds: number): string {
    const min = Math.floor(seconds / 60);
    const sec = seconds % 60;
    return `${min.toString().padStart(2, '0')}:${sec.toString().padStart(2, '0')}`;
  }

  // --- UI 构建 ---
  build() {
    Scroll() {
      Column() {
        Text(this.message).fontSize(28).fontWeight(FontWeight.Bold).margin({ top: 40, bottom: 20 })

        // 状态展示区：优先级 Seeking > Buffering > Status
        Text(this.isSeeking ? 'SEEKING...' : (this.isBuffering ? 'BUFFERING...' : this.status))
          .fontSize(16)
          .fontColor(this.isSeeking || this.isBuffering ? Color.Orange : '#666666')
          .margin({ bottom: 20 })

        TextInput({ placeholder: '请输入音频流 URL', text: this.url })
          .width('90%').height(48).onChange((v) => this.url = v).margin({ bottom: 12 })

        Progress({ value: this.progressPercent, total: 100 }).width('90%').height(8)
        Text(this.progressHint).fontSize(12).margin({ top: 5, bottom: 20 })

        Divider().strokeWidth(1).color('#EEEEEE').margin(20).width('90%')

        // --- Seek 测试区 ---
        Text('Seek 交互区').fontSize(18).fontWeight(FontWeight.Medium).margin(10)
        Text(
          this.durationMs > 0
            ? `BufferClock: ${this.formatTimeMs(this.currentPosMs)} / ${this.formatTimeMs(this.durationMs)}`
            : `BufferClock: ${this.formatTimeMs(this.currentPosMs)}`
        )
          .fontSize(12)
          .fontColor(Color.Gray)

        Slider({
          value: this.isDraggingSeek ? this.seekSliderMs : this.currentPosMs,
          min: 0,
          max: this.durationMs > 0 ? this.durationMs : 100,
          step: 200
        })
          .width('90%')
          .enabled(this.isPlaying && this.durationMs > 0)
          .showTips(true)
          .onChange(async (value: number, mode: SliderChangeMode) => {
            const v = this.clampSeekMs(Math.round(value));
            if (mode === 0 || mode === 1 || mode === 3) {
              // Begin / Moving / Click
              this.isDraggingSeek = true;
              this.seekSliderMs = v;
              this.statusText = `seek preview: ${this.formatTimeMs(v)}`;
              return;
            }

            if (mode === 2) {
              // End
              this.isDraggingSeek = false;
              this.seekSliderMs = v;
              await this.doSeek(v);
            }
          })

        Text(this.isDraggingSeek ? `目标: ${this.formatTimeMs(this.seekSliderMs)}` : '')
          .fontSize(12)
          .fontColor(Color.Gray)
          .margin({ top: 6, bottom: 10 })

        Divider().strokeWidth(1).color('#EEEEEE').margin(20).width('90%')

        // --- EQ 均衡器 ---
        Text('均衡器预设').fontSize(18).fontWeight(FontWeight.Medium).margin(10)
        Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Center }) {
          ForEach(EQ_PRESET_LIST, (preset: EqPreset) => {
            Button(preset.label)
              .fontSize(12).padding(8).margin(4)
              .backgroundColor(this.eqPresetId === preset.id ? '#1E88E5' : '#E6E8EB')
              .fontColor(this.eqPresetId === preset.id ? Color.White : Color.Black)
              .onClick(() => {
                this.eqPresetId = preset.id;
                const newGains = preset.gainsDb.slice();
                this.eq.setGainsDb(newGains);
                this.eqGainsDb = newGains;
                this.eq.applyToDecoder(this.currentDecoder);
              })
          })
        }.width('90%')

        ForEach(EQ_BAND_LABELS, (label: string, index: number) => {
          Row() {
            Text(label).width(50).fontSize(10)
            Slider({ value: this.eqGainsDb[index], min: -12, max: 12 }).layoutWeight(1)
              .onChange((v) => {
                const val = Math.round(v);
                this.eqGainsDb[index] = val;
                this.eq.setBandGain(index, val);
                this.eq.applyToDecoder(this.currentDecoder);
              })
            Text(`${this.eqGainsDb[index]}dB`).width(40).fontSize(10).textAlign(TextAlign.End)
          }.width('90%').margin({ top: 5 })
        })

        Divider().strokeWidth(1).color('#EEEEEE').margin(20).width('90%')

        // --- 主控制按钮 ---
        Button(this.isPlaying ? '正在播放' : '开始播放')
          .width('80%').height(50).margin({ bottom: 10 })
          .enabled(!this.isPlaying)
          .onClick(() => this.playUrl())

        Button('停止播放')
          .width('80%').height(50).margin({ bottom: 40 })
          .backgroundColor('#F5F5F5').fontColor('#FF3B30')
          .enabled(this.isPlaying || this.isDecoding)
          .onClick(() => this.stopPlayback())
      }
      .width('100%')
    }
    .width('100%').height('100%')
  }
}
