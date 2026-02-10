import { promptAction, window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import {
  AudioRendererPlayer,
  PcmDecoderTool,
  PcmEqualizer,
  PcmStreamDecoder
} from '@ospark/free-pcm';
import { fileIo as fs } from '@kit.CoreFileKit';
import { common } from '@kit.AbilityKit';

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

const EQ_PRESET_LABEL: Record<string, string> = {
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

export const EQ_PRESET_MAP: Record<string, number[]> = {
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

export const EQ_BAND_LABELS: string[] = ['31', '62', '125', '250', '500', '1k', '2k', '4k', '8k', '16k'];

const COLOR_BG = '#F1F3F5';
const COLOR_CARD = '#FFFFFF';
const COLOR_ACCENT = '#007DFF';
const COLOR_WARN = '#FA2A2D';
const COLOR_TEXT_PRI = '#191919';
const COLOR_TEXT_SEC = '#999999';
const COLOR_DIVIDER = '#E5E5E5';

@Entry
@Component
struct Index {
  @State message: string = '音频实验室';
  @State status: string = '设备就绪';
  @State isDecoding: boolean = false;
  @State isPlaying: boolean = false;
  @State url: string = '';
  @State currentIndex: number = 0;

  @State progressPercent: number = 0;
  @State currentPosMs: number = 0;
  @State durationMs: number = 0;
  @State seekSliderMs: number = 0;
  @State isDraggingSeek: boolean = false;
  @State isSeeking: boolean = false;
  @State isBuffering: boolean = false;

  @State isStereoEqMode: boolean = false;
  @State editRightChannel: boolean = false;
  @State eqPresetId: EqPresetId = EqPresetId.Default;
  @State currentPresetLabel: string = EQ_PRESET_LABEL[EqPresetId.Default];
  @State eqGainsUnified: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];
  @State eqGainsLeft: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];
  @State eqGainsRight: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];

  @State volLinearLeft: number = 1.0;
  @State volLinearRight: number = 1.0;

  private player: AudioRendererPlayer = new AudioRendererPlayer();
  private decoderTool: PcmDecoderTool = new PcmDecoderTool();
  private eq: PcmEqualizer = new PcmEqualizer();
  private currentDecoder: PcmStreamDecoder | null = null;
  private bufferingTimerId: number = -1;
  private volTimerId: number = -1;
  private lastPosMs: number = 0;
  private lastPosUpdateTs: number = 0;

  aboutToAppear(): void {
    window.getLastWindow(getContext(this)).then((win) => {
      win.setWindowLayoutFullScreen(true);
      win.setWindowSystemBarProperties({ statusBarContentColor: '#000000' });
    });
  }

  private applyChannelVolumes() {
    if (!this.isPlaying || !this.player.setChannelVolumes) return;
    try {
      this.player.setChannelVolumes(this.volLinearLeft, this.volLinearRight);
    } catch (e) {
      console.warn(`Set volume failed: ${e}`);
    }
  }

  private debounceVolumeApply() {
    if (this.volTimerId !== -1) {
      clearTimeout(this.volTimerId);
    }
    this.volTimerId = setTimeout(() => {
      this.applyChannelVolumes();
      this.volTimerId = -1;
    }, 100);
  }

  private toPlainArray(arr: number[]): number[] {
    return [...arr];
  }

  private applyEqSettings() {
    if (!this.isPlaying || !this.currentDecoder) return;
    try {
      if (this.isStereoEqMode) {
        if (this.player.setEqGainsLR) {
          this.player.setEqGainsLR(this.toPlainArray(this.eqGainsLeft), this.toPlainArray(this.eqGainsRight));
        }
      } else {
        this.eq.setGainsDb(this.toPlainArray(this.eqGainsUnified));
        this.eq.applyToDecoder(this.currentDecoder);
      }
    } catch (e) {
      console.error(`Apply EQ Error: ${e}`);
    }
  }

  private startBufferingWatchdog() {
    if (this.bufferingTimerId !== -1) return;
    this.lastPosMs = this.currentPosMs;
    this.lastPosUpdateTs = Date.now();
    this.bufferingTimerId = setInterval(() => {
      if (this.isSeeking) {
        this.isBuffering = false;
        this.lastPosUpdateTs = Date.now();
        return;
      }
      const now = Date.now();
      if (this.currentPosMs !== this.lastPosMs) {
        this.lastPosMs = this.currentPosMs;
        this.lastPosUpdateTs = now;
        this.isBuffering = false;
        return;
      }
      if (this.isPlaying) {
        this.isBuffering = (now - this.lastPosUpdateTs) > 800;
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

  private async doSeek(targetMs: number) {
    if (!this.player || !this.isPlaying) return;
    if (this.isSeeking) return;
    this.isSeeking = true;
    try {
      await this.player.seekTo(targetMs);
    } catch (e) {
      console.warn(`Seek failed: ${e}`);
    } finally {
      this.isSeeking = false;
    }
  }

  private formatTime(ms: number): string {
    if (!Number.isFinite(ms) || ms < 0) return '00:00';
    const totalSec = Math.floor(ms / 1000);
    const min = Math.floor(totalSec / 60);
    const sec = totalSec % 60;
    return `${min.toString().padStart(2, '0')}:${sec.toString().padStart(2, '0')}`;
  }

  async playUrl() {
    if (this.isDecoding || this.isPlaying) return;
    let targetUrl = this.url.trim();
    if (!targetUrl) {
      promptAction.showToast({ message: '请输入音频地址' });
      return;
    }

    if (targetUrl === "test") {
      try {
        const ctx = getContext(this) as common.UIAbilityContext;
        const fileData = await ctx.resourceManager.getRawFileContent('test.flac');
        const tempPath = ctx.filesDir + '/temp_input.flac';
        const file = fs.openSync(tempPath, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY);
        fs.writeSync(file.fd, fileData.buffer);
        fs.closeSync(file);
        targetUrl = tempPath;
      } catch (e) {
        promptAction.showToast({ message: '测试文件读取失败' });
        return;
      }
    }

    this.isPlaying = true;
    this.status = '正在初始化...';
    this.startBufferingWatchdog();

    try {
      const decoder = this.decoderTool.createStreamDecoder(targetUrl, {
        eqEnabled: true,
        eqGainsDb: this.toPlainArray(this.eqGainsUnified),
        sampleFormat: 3
      }, {
        onProgress: (p) => {
          if (p.progress >= 0) this.progressPercent = Math.min(100, Math.round(p.progress * 100));
        },
        onError: (e) => {
          this.status = `错误: ${e.message}`;
          this.stopPlayback();
        }
      });

      this.currentDecoder = decoder;
      const info = await decoder.ready;
      this.durationMs = info.durationMs ?? 0;
      this.status = `播放中: ${info.sampleRate}Hz`;

      await this.player.play(decoder, info);
      this.applyEqSettings();
      this.applyChannelVolumes();

      this.player.setOnTimeUpdate((pos) => {
        if (!this.isDraggingSeek) this.currentPosMs = pos;
      });

      await decoder.done;
      this.status = '播放结束';
    } catch (err) {
      this.status = `异常: ${(err as BusinessError).message}`;
    } finally {
      this.stopPlayback();
    }
  }

  private async stopPlayback() {
    this.stopBufferingWatchdog();
    await this.player.stop();
    this.currentDecoder = null;
    this.isPlaying = false;
    this.isDecoding = false;
    this.status = '已停止';
    this.currentPosMs = 0;
    this.progressPercent = 0;
  }

  @Builder
  ControlTab() {
    Column() {
      TextInput({ text: this.url, placeholder: '请输入流媒体 URL 或 "test"' })
        .placeholderColor(COLOR_TEXT_SEC).fontColor(COLOR_TEXT_PRI).backgroundColor(COLOR_CARD)
        .caretColor(COLOR_ACCENT).margin({ bottom: 20 }).height(50).borderRadius(12)
        .shadow(ShadowStyle.OUTER_DEFAULT_XS).onChange((v) => this.url = v)

      Row({ space: 15 }) {
        Button(this.isPlaying ? '播放中' : '开始播放')
          .layoutWeight(1).height(56).backgroundColor(this.isPlaying ? '#E3F2FF' : COLOR_ACCENT)
          .fontColor(this.isPlaying ? COLOR_ACCENT : Color.White).fontWeight(FontWeight.Bold)
          .shadow(ShadowStyle.OUTER_DEFAULT_XS).onClick(() => this.playUrl()).enabled(!this.isPlaying)

        Button('停止')
          .width(100).height(56).backgroundColor('#FFF0F0').fontColor(COLOR_WARN)
          .fontWeight(FontWeight.Bold).shadow(ShadowStyle.OUTER_DEFAULT_XS).onClick(() => this.stopPlayback())
      }.width('100%')

      Divider().color(COLOR_DIVIDER).margin({ top: 25, bottom: 25 })

      Column({ space: 12 }) {
        Row() {
          Text('系统状态').fontColor(COLOR_TEXT_SEC).fontSize(13)
          Blank()
          Text(this.isSeeking ? '跳转中...' : (this.isBuffering ? '缓冲中...' : this.status))
            .fontColor(this.isBuffering ? Color.Orange : COLOR_TEXT_PRI).fontSize(15).fontWeight(FontWeight.Medium)
        }.width('100%')
        Row() {
          Text('解码缓冲').fontColor(COLOR_TEXT_SEC).fontSize(13)
          Blank()
          Text(`${this.progressPercent}%`).fontColor(COLOR_ACCENT).fontSize(15).fontWeight(FontWeight.Bold)
        }.width('100%')
      }.padding(20).backgroundColor(COLOR_CARD).borderRadius(16).shadow(ShadowStyle.OUTER_DEFAULT_XS)
    }.padding(24).width('100%').height('100%').justifyContent(FlexAlign.Start)
  }

  @Builder
  MixerTab() {
    Column() {
      Row() {
        Text('声道增益测试').fontSize(18).fontWeight(FontWeight.Bold).fontColor(COLOR_TEXT_PRI)
        Blank()
        Button('重置')
          .fontSize(12).height(28).backgroundColor('#F0F0F0').fontColor(COLOR_TEXT_PRI)
          .onClick(() => {
            this.volLinearLeft = 1.0;
            this.volLinearRight = 1.0;
            this.applyChannelVolumes();
            promptAction.showToast({ message: '系数已重置' });
          })
      }.width('100%').padding({ left: 10, right: 10, bottom: 30 })

      Row({ space: 40 }) {
        this.ChannelFader('左声道 (L)', 'L', COLOR_ACCENT)
        this.ChannelFader('右声道 (R)', 'R', '#FF9500')
      }.justifyContent(FlexAlign.Center).width('100%')

      Text('调节范围: 0.0 - 2.0 (线性)').fontColor(COLOR_TEXT_SEC).fontSize(12).margin({ top: 40 })
    }.padding(24).width('100%').height('100%').justifyContent(FlexAlign.Center)
  }

  @Builder
  ChannelFader(label: string, side: 'L' | 'R', color: string) {
    Column() {
      Text(`${(side === 'L' ? this.volLinearLeft : this.volLinearRight).toFixed(2)}`)
        .fontColor(color).fontSize(16).fontWeight(FontWeight.Bold).margin({ bottom: 15 })

      Slider({
        value: (side === 'L' ? this.volLinearLeft : this.volLinearRight) * 100,
        min: 0,
        max: 200,
        style: SliderStyle.InSet,
        direction: Axis.Vertical,
        reverse: true
      })
        .height(260).trackColor('#E0E0E0').selectedColor(color)
        .onChange((v) => {
          if (side === 'L') {
            this.volLinearLeft = v / 100;
          } else {
            this.volLinearRight = v / 100;
          }
          this.debounceVolumeApply();
        })

      Text(label).fontColor(COLOR_TEXT_SEC).fontSize(13).margin({ top: 15 }).fontWeight(FontWeight.Medium)
    }
  }

  @Builder
  EqualizerTab() {
    Column() {
      Row() {
        Row() {
          Text('模式：').fontSize(13).fontColor(COLOR_TEXT_SEC)
          Row() {
            Text('统一')
              .fontSize(12).fontColor(this.isStereoEqMode ? COLOR_TEXT_SEC : Color.White)
              .backgroundColor(this.isStereoEqMode ? '#F0F0F0' : COLOR_ACCENT)
              .padding({ left: 10, right: 10, top: 4, bottom: 4 }).borderRadius({ topLeft: 12, bottomLeft: 12 })
              .onClick(() => { this.isStereoEqMode = false; this.applyEqSettings(); })
            Text('独立声道')
              .fontSize(12).fontColor(this.isStereoEqMode ? Color.White : COLOR_TEXT_SEC)
              .backgroundColor(this.isStereoEqMode ? COLOR_ACCENT : '#F0F0F0')
              .padding({ left: 10, right: 10, top: 4, bottom: 4 }).borderRadius({ topRight: 12, bottomRight: 12 })
              .onClick(() => { this.isStereoEqMode = true; this.applyEqSettings(); })
          }
        }
        Blank()
        Row() {
          Text(this.currentPresetLabel).fontSize(14).fontColor(COLOR_ACCENT).maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis }).constraintSize({ maxWidth: 100 })
          Image($r('sys.media.ohos_ic_public_arrow_down')).width(12).height(12).fillColor(COLOR_ACCENT).margin({ left: 4 })
        }.padding({ left: 12, right: 12, top: 6, bottom: 6 }).backgroundColor('#E3F2FF').borderRadius(16).bindMenu(this.getEqPresetMenu())
      }.width('100%').justifyContent(FlexAlign.SpaceBetween).margin({ bottom: 20 })

      if (this.isStereoEqMode) {
        Row() {
          Text('正在编辑：').fontSize(12).fontColor(COLOR_TEXT_SEC)
          Row({ space: 10 }) {
            Button('左声道 (L)').fontSize(12).height(28).backgroundColor(!this.editRightChannel ? COLOR_ACCENT : '#F0F0F0')
              .fontColor(!this.editRightChannel ? Color.White : COLOR_TEXT_PRI).onClick(() => this.editRightChannel = false)
            Button('右声道 (R)').fontSize(12).height(28).backgroundColor(this.editRightChannel ? '#FF9500' : '#F0F0F0')
              .fontColor(this.editRightChannel ? Color.White : COLOR_TEXT_PRI).onClick(() => this.editRightChannel = true)
          }
        }.width('100%').justifyContent(FlexAlign.Start).margin({ bottom: 15 })
      }

      Column() {
        Row() {
          ForEach(EQ_BAND_LABELS, (label: string, index: number) => {
            Column() {
              Slider({ value: this.getCurrentGain(index), min: -12, max: 12, step: 1, direction: Axis.Vertical, reverse: true })
                .layoutWeight(1).trackColor('#E0E0E0').selectedColor(this.getEqColor()).onChange((v) => { this.updateGain(index, v); })
              Text(`${Math.round(this.getCurrentGain(index))}`).fontSize(10).fontColor(COLOR_TEXT_PRI).margin({ top: 4, bottom: 2 })
              Text(label).fontSize(9).fontColor(COLOR_TEXT_SEC)
            }.layoutWeight(1).height('100%')
          })
        }.height(240).width('100%')
      }.padding({ top: 20, bottom: 10, left: 10, right: 10 }).backgroundColor(COLOR_CARD).borderRadius(16).shadow(ShadowStyle.OUTER_DEFAULT_XS)
    }.padding(24).width('100%').height('100%')
  }

  @Builder
  getEqPresetMenu() {
    Menu() {
      ForEach(Object.keys(EQ_PRESET_MAP), (key: string) => {
        MenuItem({ content: EQ_PRESET_LABEL[key] }).onClick(() => {
          const presetId = key as EqPresetId;
          this.eqPresetId = presetId;
          this.currentPresetLabel = EQ_PRESET_LABEL[presetId];
          const gains = EQ_PRESET_MAP[presetId];
          if (this.isStereoEqMode) { this.eqGainsLeft = [...gains]; this.eqGainsRight = [...gains]; }
          else { this.eqGainsUnified = [...gains]; }
          this.applyEqSettings();
        })
      })
    }
  }

  private getCurrentGain(index: number): number {
    if (this.isStereoEqMode) { return this.editRightChannel ? this.eqGainsRight[index] : this.eqGainsLeft[index]; }
    return this.eqGainsUnified[index];
  }

  private updateGain(index: number, value: number) {
    if (this.isStereoEqMode) {
      if (this.editRightChannel) { this.eqGainsRight[index] = value; }
      else { this.eqGainsLeft[index] = value; }
    } else { this.eqGainsUnified[index] = value; }
    this.applyEqSettings();
  }

  private getEqColor(): string {
    if (!this.isStereoEqMode) return COLOR_ACCENT;
    return this.editRightChannel ? '#FF9500' : COLOR_ACCENT;
  }

  @Builder
  TabTitle(title: string, index: number) {
    Column() {
      Text(title).fontSize(15).fontWeight(this.currentIndex === index ? FontWeight.Bold : FontWeight.Medium)
        .fontColor(this.currentIndex === index ? COLOR_ACCENT : COLOR_TEXT_SEC).margin({ bottom: 6 }).animation({ duration: 200 })
      Divider().color(COLOR_ACCENT).strokeWidth(3).width(20).opacity(this.currentIndex === index ? 1 : 0).animation({ duration: 200 })
    }.width('100%').height('100%').justifyContent(FlexAlign.Center).backgroundColor(Color.Transparent)
  }

  build() {
    Column() {
      Column() {
        Text(this.message).fontSize(20).fontWeight(FontWeight.Bold).fontColor(COLOR_TEXT_PRI).margin({ top: 15, bottom: 5 }).opacity(0.8)
        Text(this.durationMs > 0 ? `${this.formatTime(this.currentPosMs)} / ${this.formatTime(this.durationMs)}` : this.formatTime(this.currentPosMs))
          .fontSize(36).fontWeight(FontWeight.Bold).fontColor(COLOR_TEXT_PRI).margin({ bottom: 25 }).fontFamily('HarmonyOS Sans Digit')
        Slider({ value: this.isDraggingSeek ? this.seekSliderMs : this.currentPosMs, min: 0, max: this.durationMs > 0 ? this.durationMs : 100 })
          .selectedColor(COLOR_ACCENT).trackColor('#E0E0E0').blockColor(COLOR_ACCENT).onChange(async (v, mode) => {
          this.seekSliderMs = v;
          if (mode === SliderChangeMode.Begin || mode === SliderChangeMode.Moving) { this.isDraggingSeek = true; }
          else if (mode === SliderChangeMode.End || mode === SliderChangeMode.Click) { this.isDraggingSeek = false; await this.doSeek(v); }
        })
      }.width('100%').padding({ top: 50, bottom: 20, left: 24, right: 24 }).backgroundColor(COLOR_BG)

      Tabs({ barPosition: BarPosition.Start, index: this.currentIndex }) {
        TabContent() { this.ControlTab() }.tabBar(this.TabTitle('控制台', 0))
        TabContent() { this.MixerTab() }.tabBar(this.TabTitle('调音台', 1))
        TabContent() { this.EqualizerTab() }.tabBar(this.TabTitle('均衡器', 2))
      }.vertical(false).barHeight(56).barMode(BarMode.Fixed).backgroundColor(COLOR_BG).layoutWeight(1).onChange((index) => { this.currentIndex = index; })
    }.width('100%').height('100%').backgroundColor(COLOR_BG).expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM])
  }
}