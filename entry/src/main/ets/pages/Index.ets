import { promptAction } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import {
  AudioRendererPlayer,
  DecodeAudioProgress,
  PcmDecoderTool,
  PcmEqualizer,
  PcmStreamDecoder
} from '@okysu/free-pcm';

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
  @State message: string = '音频解码测试';
  @State status: string = '待机状态';
  @State isDecoding: boolean = false;
  @State isPlaying: boolean = false;
  @State url: string = '';
  @State progressPercent: number = 0;
  @State progressHint: string = '';
  @State eqPresetId: EqPresetId = EqPresetId.Default;
  @State eqGainsDb: number[] = EQ_PRESET_MAP[EqPresetId.Default].slice();

  private player: AudioRendererPlayer = new AudioRendererPlayer();
  private decoderTool: PcmDecoderTool = new PcmDecoderTool();
  private eq: PcmEqualizer = new PcmEqualizer();
  private currentDecoder: PcmStreamDecoder | null = null;

  private updateProgress(p: DecodeAudioProgress) {
    const ptsSec = Math.floor(p.ptsMs / 1000000);
    const durSec = Math.floor(p.durationMs / 1000000);

    if (p.progress >= 0 && durSec > 0) {
      this.progressPercent = Math.min(100, Math.max(0, Math.round(p.progress * 100)));
      this.progressHint = `${this.progressPercent}% (${this.formatTime(ptsSec)} / ${this.formatTime(durSec)})`;
    } else {
      this.progressPercent = 0;
      this.progressHint = `已处理 ${this.formatTime(ptsSec)}`;
    }
  }

  private formatTime(seconds: number): string {
    const min = Math.floor(seconds / 60);
    const sec = seconds % 60;
    return `${min.toString().padStart(2, '0')}:${sec.toString().padStart(2, '0')}`;
  }

  private async stopPlayback() {
    await this.player.stop();
    this.currentDecoder = null;
    this.isPlaying = false;
    this.isDecoding = false;
    this.status = '播放已停止';
  }

  async playUrl() {
    if (this.isDecoding || this.isPlaying) return;
    const url = this.url.trim();
    if (!url) {
      promptAction.showToast({ message: '请输入有效 URL', duration: 2000 });
      return;
    }

    this.isPlaying = true;
    this.status = '正在准备...';

    try {
      const decoder = this.decoderTool.createStreamDecoder(url, {
        ringBytes: 64 * 1024,
        eqEnabled: true,
        eqGainsDb: this.eqGainsDb
      }, {
        onProgress: (p: DecodeAudioProgress): void => this.updateProgress(p),
        onError: (e: Error) => { this.status = `错误: ${e.message}`; }
      });

      this.currentDecoder = decoder;
      const info = await decoder.ready;
      this.status = `播放中: ${info.sampleRate}Hz`;

      await this.player.play(decoder, info);
      await decoder.done;
      this.status = '播放结束';
    } catch (err) {
      this.status = `播放失败: ${(err as BusinessError).message}`;
    } finally {
      this.isPlaying = false;
    }
  }

  build() {
    Scroll() {
      Column() {
        Text(this.message).fontSize(28).fontWeight(FontWeight.Bold).margin({ top: 40, bottom: 20 })
        Text(this.status).fontSize(16).fontColor('#666666').margin({ bottom: 20 })

        TextInput({ placeholder: '音频流 URL', text: this.url })
          .width('90%').height(48).onChange((v) => this.url = v).margin({ bottom: 12 })

        Progress({ value: this.progressPercent, total: 100 }).width('90%').height(8)
        Text(this.progressHint).fontSize(12).margin({ top: 5, bottom: 20 })

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

        Button('开始播放')
          .width('80%').height(50).margin({ bottom: 10 })
          .enabled(!this.isPlaying)
          .onClick(() => this.playUrl())

        Button('停止')
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