import { promptAction, window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import {
  AudioRendererPlayer,
  PcmDecoderTool,
  PcmEqualizer,
  PcmStreamDecoder,
  DecodeAudioProgress,
  PcmStreamDecoderCallbacks
} from '@ospark/free-pcm';
import { fileIo as fs } from '@kit.CoreFileKit';
import { common } from '@kit.AbilityKit';

// --- EQ 相关定义 ---
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

// --- DRC 相关定义 ---
export enum DrcPresetId {
  Default = 'Default',
  Speech = 'Speech',
  Vocal = 'Vocal',
  Night = 'Night',
  Mastering = 'Mastering',
  Punch = 'Punch',
  Broadcast = 'Broadcast',
  Limiter = 'Limiter',
  Crush = 'Crush'
}

const DRC_PRESET_LABEL: Record<string, string> = {
  [DrcPresetId.Default]: '通用标准 (Default)',
  [DrcPresetId.Speech]: '人声对白 (Speech)',
  [DrcPresetId.Vocal]: '流行歌唱 (Pop Vocal)',
  [DrcPresetId.Night]: '深夜模式 (Night)',
  [DrcPresetId.Mastering]: '母带柔化 (Mastering)',
  [DrcPresetId.Punch]: '打击增强 (Punch)',
  [DrcPresetId.Broadcast]: '广播电台 (Broadcast)',
  [DrcPresetId.Limiter]: '安全限幅 (Limiter)',
  [DrcPresetId.Crush]: '毁灭压缩 (Crush)'
};

interface DrcParams {
  threshold: number;
  ratio: number;
  attack: number;
  release: number;
  makeup: number;
}

export const DRC_PRESET_MAP: Record<string, DrcParams> = {
  // 轻微控制，适用大多数场景
  [DrcPresetId.Default]: { threshold: -12, ratio: 2.0, attack: 10, release: 100, makeup: 0 },

  // 快启动快释放，压住爆破音，大幅提升音量，适合新闻/有声书
  [DrcPresetId.Speech]: { threshold: -24, ratio: 4.0, attack: 5, release: 80, makeup: 6 },

  // 歌唱：比对白更慢的启动和释放，保持声音的自然呼吸感
  [DrcPresetId.Vocal]: { threshold: -20, ratio: 3.0, attack: 20, release: 300, makeup: 3 },

  //强力压缩动态，让小声音变大，大声音不吵
  [DrcPresetId.Night]: { threshold: -35, ratio: 10.0, attack: 5, release: 500, makeup: 12 },

  // 母带：低压缩比，高阈值，极慢释放。适合古典/爵士，增加"粘合度"而不破坏动态
  [DrcPresetId.Mastering]: { threshold: -10, ratio: 1.5, attack: 30, release: 400, makeup: 1 },

  // 打击感：较慢的Attack让瞬态(鼓皮声)通过，快速Release制造节奏感
  [DrcPresetId.Punch]: { threshold: -18, ratio: 4.0, attack: 30, release: 50, makeup: 4 },

  // 典型的"像墙一样"的声音，中等Attack保留一点冲击力
  [DrcPresetId.Broadcast]: { threshold: -20, ratio: 6.0, attack: 15, release: 200, makeup: 6 },

  // 阈值设为 -0.5dB (防止削波)，极快启动，用于保护音箱
  [DrcPresetId.Limiter]: { threshold: -0.5, ratio: 20.0, attack: 0.1, release: 50, makeup: 0 },

  // 极限压力测试，用于测试算法在极端情况下的失真/底噪
  [DrcPresetId.Crush]: { threshold: -40, ratio: 20.0, attack: 0.1, release: 30, makeup: 18 }
};

// --- 样式常量 ---
const COLOR_BG = '#F1F3F5';
const COLOR_CARD = '#FFFFFF';
const COLOR_ACCENT = '#007DFF';
const COLOR_WARN = '#FA2A2D';
const COLOR_TEXT_PRI = '#191919';
const COLOR_TEXT_SEC = '#999999';
const COLOR_DIVIDER = '#E5E5E5';

// 补充定义 DRC Meter 接口
interface DrcMeterInfo {
  levelDb: number;
  gainDb: number;
  grDb: number;
}

@Entry
@Component
struct Index {
  @State message: string = '音频实验室';
  @State status: string = '设备就绪';
  @State isDecoding: boolean = false;
  @State isPlaying: boolean = false; // 表示当前是否有声音正在输出
  @State url: string = '';
  @State currentIndex: number = 0;

  // 进度相关
  @State progressPercent: number = 0;
  @State currentPosMs: number = 0;
  @State durationMs: number = 0;
  @State seekSliderMs: number = 0;
  @State isDraggingSeek: boolean = false;
  @State isSeeking: boolean = false;
  @State isBuffering: boolean = false;

  // EQ 状态
  @State isStereoEqMode: boolean = false;
  @State editRightChannel: boolean = false;
  @State eqPresetId: EqPresetId = EqPresetId.Default;
  @State currentPresetLabel: string = EQ_PRESET_LABEL[EqPresetId.Default];
  @State eqGainsUnified: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];
  @State eqGainsLeft: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];
  @State eqGainsRight: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];

  // 混音器状态
  @State volLinearLeft: number = 1.0;
  @State volLinearRight: number = 1.0;

  // DRC 状态
  @State drcEnabled: boolean = false;
  @State drcThresholdDb: number = -20;
  @State drcRatio: number = 4;
  @State drcAttackMs: number = 10;
  @State drcReleaseMs: number = 100;
  @State drcMakeupDb: number = 0;
  @State drcPresetLabel: string = '自定义';

  // 内部对象
  private player: AudioRendererPlayer = new AudioRendererPlayer();
  private decoderTool: PcmDecoderTool = new PcmDecoderTool();
  private eq: PcmEqualizer = new PcmEqualizer();
  private currentDecoder: PcmStreamDecoder | null = null;
  private bufferingTimerId: number = -1;
  private volTimerId: number = -1;
  private drcTimerId: number = -1;
  private lastPosMs: number = 0;
  private lastPosUpdateTs: number = 0;

  // Canvas 上下文
  private canvasSettings: RenderingContextSettings = new RenderingContextSettings(true);
  private canvasCtx: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.canvasSettings);

  // Meter 缓存数据
  private meterLevel: number = -120;
  private meterGain: number = 0;
  private meterGr: number = 0;

  aboutToAppear(): void {
    window.getLastWindow(getContext(this)).then((win) => {
      win.setWindowLayoutFullScreen(true);
      win.setWindowSystemBarProperties({ statusBarContentColor: '#000000' });
    });
  }

  // --- 业务逻辑 ---

  private applyChannelVolumes() {
    if (!this.player || !this.player.setChannelVolumes) return;
    try {
      this.player.setChannelVolumes(this.volLinearLeft, this.volLinearRight);
    } catch (e) {
      console.warn(`Set volume failed: ${e}`);
    }
  }

  private debounceVolumeApply() {
    if (this.volTimerId !== -1) clearTimeout(this.volTimerId);
    this.volTimerId = setTimeout(() => {
      this.applyChannelVolumes();
      this.volTimerId = -1;
    }, 100);
  }

  private applyEqSettings() {
    if (!this.currentDecoder) return;
    try {
      if (this.isStereoEqMode) {
        if (this.player.setEqGainsLR) {
          this.player.setEqGainsLR([...this.eqGainsLeft], [...this.eqGainsRight]);
        }
      } else {
        this.eq.setGainsDb([...this.eqGainsUnified]);
        this.eq.applyToDecoder(this.currentDecoder);
      }
    } catch (e) {
      console.error(`Apply EQ Error: ${e}`);
    }
  }

  private applyDrcSettings() {
    if (!this.currentDecoder) return;
    try {
      if (this.currentDecoder.setDrcEnabled) {
        this.currentDecoder.setDrcEnabled(this.drcEnabled);
      }
      if (this.currentDecoder.setDrcParams) {
        this.currentDecoder.setDrcParams(this.drcThresholdDb, this.drcRatio, this.drcAttackMs, this.drcReleaseMs, this.drcMakeupDb);
      }
    } catch (e) {
      console.warn(`Apply DRC failed: ${e}`);
    }
  }

  private debounceDrcApply() {
    if (this.drcTimerId !== -1) clearTimeout(this.drcTimerId);
    this.drcTimerId = setTimeout(() => {
      this.applyDrcSettings();
      this.drcTimerId = -1;
    }, 100);
  }

  // --- 绘图逻辑 (重构版) ---
  private drawDrcMeter() {
    if (!this.canvasCtx) return;

    const w = this.canvasCtx.width;
    const h = this.canvasCtx.height;
    const padding = 10;
    const barWidth = (w - padding * 4) / 3; // 三条柱子平均分

    // 1. 背景与网格
    this.canvasCtx.clearRect(0, 0, w, h);
    this.canvasCtx.fillStyle = '#111111';
    this.canvasCtx.fillRect(0, 0, w, h);

    // 画水平 dB 刻度线 (0, -6, -12, -24, -48)
    this.canvasCtx.strokeStyle = '#333333';
    this.canvasCtx.lineWidth = 1;
    this.canvasCtx.font = '10vp sans-serif';
    this.canvasCtx.fillStyle = '#666666';
    this.canvasCtx.textAlign = 'right';

    const dbTicks = [0, -6, -12, -24, -48];
    dbTicks.forEach(db => {
      let y = this.dbToY(db, h);
      this.canvasCtx.beginPath();
      this.canvasCtx.moveTo(padding, y);
      this.canvasCtx.lineTo(w - padding, y);
      this.canvasCtx.stroke();
      if (db !== -48) {
        this.canvasCtx.fillText(`${db}`, w - 2, y + 4);
      }
    });

    // -------------------------------------------------
    // Bar 1: Input Level (In) - 从下往上
    // -------------------------------------------------
    const x1 = padding;
    const y1_top = this.dbToY(this.meterLevel, h);
    const h1 = h - y1_top;

    // 创建渐变色 (绿 -> 黄 -> 红)
    const grad1 = this.canvasCtx.createLinearGradient(x1, h, x1, 0);
    grad1.addColorStop(0, '#00FF00'); // Bottom
    grad1.addColorStop(0.7, '#FFFF00');
    grad1.addColorStop(1, '#FF0000'); // Top

    this.canvasCtx.fillStyle = grad1;
    this.canvasCtx.fillRect(x1, y1_top, barWidth, h1);

    // 绘制阈值线 (只在 Input 条上显示)
    const yThres = this.dbToY(this.drcThresholdDb, h);
    this.canvasCtx.strokeStyle = '#FFFFFF';
    this.canvasCtx.lineWidth = 2;
    this.canvasCtx.beginPath();
    this.canvasCtx.moveTo(x1, yThres);
    this.canvasCtx.lineTo(x1 + barWidth, yThres);
    this.canvasCtx.stroke();

    // -------------------------------------------------
    // Bar 2: Gain Reduction (GR) - 从上往下 (红色)
    // -------------------------------------------------
    // meterGr 是正数 (e.g. 1.5)，代表被压了 1.5dB
    // 0dB 在顶部，向下延伸
    const x2 = padding * 2 + barWidth;
    let grHeight = 0;

    if (this.meterGr > 0.1) {
      // 映射：0dB -> y=0, 24dB -> y=h (最大显示24dB衰减)
      const maxGrDisplay = 24;
      const ratio = Math.min(this.meterGr, maxGrDisplay) / maxGrDisplay;
      grHeight = ratio * h;
    }

    this.canvasCtx.fillStyle = '#FF3B30'; // 鲜红
    this.canvasCtx.fillRect(x2, 0, barWidth, grHeight);

    // -------------------------------------------------
    // Bar 3: Output Level (Out) - 从下往上
    // Output ≈ Input + Gain (Makeup - GR)
    // -------------------------------------------------
    const x3 = padding * 3 + barWidth * 2;
    // 计算估算的输出电平
    let outLevel = this.meterLevel + this.meterGain;
    if (outLevel > 0) outLevel = 0; // Clip

    const y3_top = this.dbToY(outLevel, h);
    const h3 = h - y3_top;

    const grad3 = this.canvasCtx.createLinearGradient(x3, h, x3, 0);
    grad3.addColorStop(0, '#007DFF'); // 华为蓝
    grad3.addColorStop(1, '#00BFFF'); // 亮蓝

    this.canvasCtx.fillStyle = grad3;
    this.canvasCtx.fillRect(x3, y3_top, barWidth, h3);

    // -------------------------------------------------
    // 文字标签
    // -------------------------------------------------
    this.canvasCtx.fillStyle = '#FFFFFF';
    this.canvasCtx.textAlign = 'center';
    this.canvasCtx.font = '12vp sans-serif bold';

    // In Label
    this.canvasCtx.fillText("IN", x1 + barWidth/2, h - 10);
    this.canvasCtx.font = '10vp sans-serif';
    this.canvasCtx.fillText(this.meterLevel.toFixed(1), x1 + barWidth/2, h - 25);

    // GR Label
    this.canvasCtx.fillStyle = '#FF9999';
    this.canvasCtx.font = '12vp sans-serif bold';
    this.canvasCtx.fillText("GR", x2 + barWidth/2, h - 10);
    if (this.meterGr > 0.1) {
      this.canvasCtx.font = '10vp sans-serif';
      this.canvasCtx.fillText(`-${this.meterGr.toFixed(1)}`, x2 + barWidth/2, 20); // GR数值显示在顶部
    }

    // Out Label
    this.canvasCtx.fillStyle = '#FFFFFF';
    this.canvasCtx.font = '12vp sans-serif bold';
    this.canvasCtx.fillText("OUT", x3 + barWidth/2, h - 10);
    this.canvasCtx.font = '10vp sans-serif';
    this.canvasCtx.fillText(outLevel.toFixed(1), x3 + barWidth/2, h - 25);
  }

  // 坐标映射：0dB 在顶部，-60dB 在底部
  private dbToY(db: number, height: number): number {
    const minDb = -60;
    if (db < minDb) return height;
    if (db > 0) return 0;
    // 线性映射 dB
    return (1 - (db - minDb) / (0 - minDb)) * height;
  }

  // --- 播放器逻辑 (重构) ---

  private startBufferingWatchdog() {
    if (this.bufferingTimerId !== -1) return;
    this.lastPosMs = this.currentPosMs;
    this.lastPosUpdateTs = Date.now();
    this.bufferingTimerId = setInterval(() => {
      if (this.isSeeking || !this.isPlaying) { this.isBuffering = false; return; }
      const now = Date.now();
      if (this.currentPosMs !== this.lastPosMs) {
        this.lastPosMs = this.currentPosMs;
        this.lastPosUpdateTs = now;
        this.isBuffering = false;
        return;
      }
      if (this.isPlaying) this.isBuffering = (now - this.lastPosUpdateTs) > 800;
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
    if (!this.player || !this.currentDecoder || this.isSeeking) return;
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

  // 核心逻辑：播放/暂停切换
  async togglePlayState() {
    // 1. 如果已经有解码器，处理 暂停/继续
    if (this.currentDecoder) {
      if (this.isPlaying) {
        // 正在播放 -> 暂停
        try {
          await this.player.pause();
          this.isPlaying = false;
          this.status = '已暂停';
          this.stopBufferingWatchdog();
        } catch (e) {
          console.error('Pause failed:', e);
        }
      } else {
        // 暂停中 -> 继续播放
        try {
          await this.player.resume();
          this.isPlaying = true;
          this.status = '播放中';
          this.startBufferingWatchdog();
        } catch (e) {
          console.error('Resume failed:', e);
        }
      }
      return;
    }

    // 2. 如果没有解码器，开始新的播放
    await this.startNewPlayback();
  }

  // 内部方法：初始化并开始新的播放
  private async startNewPlayback() {
    if (this.isDecoding) return;
    let targetUrl = this.url.trim();
    if (!targetUrl) { promptAction.showToast({ message: '请输入地址' }); return; }

    if (targetUrl === "test") {
      try {
        const ctx = getContext(this) as common.UIAbilityContext;
        const fileData = await ctx.resourceManager.getRawFileContent('test.flac');
        const tempPath = ctx.filesDir + '/temp_input.flac';
        const file = fs.openSync(tempPath, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY);
        fs.writeSync(file.fd, fileData.buffer);
        fs.closeSync(file);
        targetUrl = tempPath;
      } catch (e) { promptAction.showToast({ message: '文件读取失败' }); return; }
    }

    this.isPlaying = true;
    this.isDecoding = true;
    this.status = '正在初始化...';
    this.startBufferingWatchdog();

    try {
      const callbacks: PcmStreamDecoderCallbacks = {
        onProgress: (p: DecodeAudioProgress) => {
          if (p.progress >= 0) this.progressPercent = Math.min(100, Math.round(p.progress * 100));
        },
        onError: (e: Error) => {
          // 如果当前解码器还是这个 decoder，才报错 (防止 reset 后报错)
          if (this.currentDecoder && this.status !== '已重置') {
            this.status = `错误: ${e.message}`;
            this.resetPlayback();
          }
        },
        onDrcMeter: (m: DrcMeterInfo) => {
          this.meterLevel = m.levelDb;
          this.meterGain = m.gainDb;
          this.meterGr = m.grDb;
          this.drawDrcMeter();
        }
      };

      const decoder = this.decoderTool.createStreamDecoder(targetUrl, {
        eqEnabled: true,
        eqGainsDb: [...this.eqGainsUnified],
      }, callbacks);

      this.currentDecoder = decoder;
      const info = await decoder.ready;
      this.durationMs = info.durationMs ?? 0;
      this.status = `播放中: ${info.sampleRate}Hz，${info.sampleFormatCode}`;

      this.player.setOnTimeUpdate((pos) => {
        if (!this.isDraggingSeek) this.currentPosMs = pos;
      });

      // 初始化 EQ/DRC/音量
      this.applyEqSettings();
      this.applyChannelVolumes();
      this.applyDrcSettings();

      // 开始播放
      // 注意：play() 返回 promise 表示“开始播放”成功，而非“播放结束”
      await this.player.play(decoder, info);

      // 使用 decoder.done 来等待播放/解码结束，这是你之前逻辑中有效的判断方式
      await decoder.done;

      // 播放正常结束
      // 增加检查：确保当前没有被 Reset (currentDecoder 没有被置空或更换)
      if (this.currentDecoder === decoder) {
        this.status = '播放结束';
        this.isPlaying = false;
        this.currentPosMs = 0;
        this.stopBufferingWatchdog();
      }

    } catch (err) {
      // 只有在没有手动 Reset 的情况下才报启动失败
      if (this.currentDecoder) {
        this.status = `启动失败: ${(err as BusinessError).message}`;
        this.resetPlayback();
      }
    } finally {
      this.isDecoding = false;
    }
  }

  // 重置/停止逻辑：完全销毁
  private async resetPlayback() {
    this.stopBufferingWatchdog();
    try {
      await this.player.stop();
    } catch (e) {
      console.warn('Stop failed:', e);
    }

    this.currentDecoder = null;
    this.isPlaying = false;
    this.isDecoding = false;
    this.status = '已重置';
    this.currentPosMs = 0;
    this.progressPercent = 0;

    if (this.drcTimerId !== -1) {
      clearTimeout(this.drcTimerId);
      this.drcTimerId = -1;
    }
    // 重置 Meter
    this.meterLevel = -120;
    this.meterGain = 0;
    this.meterGr = 0;
    this.drawDrcMeter();
  }

  // --- UI Builders ---

  @Builder
  ControlTab() {
    Scroll() {
      Column() {
        TextInput({ text: this.url, placeholder: '请输入流媒体 URL 或 "test"' })
          .placeholderColor(COLOR_TEXT_SEC).fontColor(COLOR_TEXT_PRI).backgroundColor(COLOR_CARD)
          .caretColor(COLOR_ACCENT).margin({ bottom: 20 }).height(50).borderRadius(12)
          .shadow(ShadowStyle.OUTER_DEFAULT_XS).onChange((v) => this.url = v)
        Row({ space: 15 }) {
          // 按钮 1: 播放控制 (三态：开始 / 暂停 / 继续)
          Button(this.getPlayButtonLabel())
            .layoutWeight(1).height(56).backgroundColor(this.isPlaying ? '#E3F2FF' : COLOR_ACCENT)
            .fontColor(this.isPlaying ? COLOR_ACCENT : Color.White).fontWeight(FontWeight.Bold)
            .shadow(ShadowStyle.OUTER_DEFAULT_XS)
            .onClick(() => this.togglePlayState())
            // .enabled(!this.isDecoding) // 初始化时禁用

          // 按钮 2: 重置 (停止并销毁)
          Button('重置')
            .width(100).height(56).backgroundColor('#FFF0F0').fontColor(COLOR_WARN)
            .fontWeight(FontWeight.Bold).shadow(ShadowStyle.OUTER_DEFAULT_XS)
            .onClick(() => this.resetPlayback())
        }.width('100%')
        Divider().color(COLOR_DIVIDER).margin({ top: 25, bottom: 25 })
        Column({ space: 12 }) {
          Row() {
            Text('运行状态').fontColor(COLOR_TEXT_SEC).fontSize(13)
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
      }.padding(24).width('100%').justifyContent(FlexAlign.Start).constraintSize({ minHeight: '100%' })
    }.scrollable(ScrollDirection.Vertical).scrollBar(BarState.Auto)
  }

  private getPlayButtonLabel(): string {
    if (!this.currentDecoder) {
      return '开始播放';
    }
    return this.isPlaying ? '暂停' : '继续播放';
  }

  @Builder
  MixerTab() {
    Scroll() {
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
      }.padding(24).width('100%').justifyContent(FlexAlign.Start).constraintSize({ minHeight: '100%' })
    }.scrollable(ScrollDirection.Vertical).scrollBar(BarState.Auto)
  }

  @Builder
  ChannelFader(label: string, side: string, color: string) {
    Column() {
      Text(`${(side === 'L' ? this.volLinearLeft : this.volLinearRight).toFixed(2)}`)
        .fontColor(color).fontSize(16).fontWeight(FontWeight.Bold).margin({ bottom: 15 })
      Slider({
        value: (side === 'L' ? this.volLinearLeft : this.volLinearRight) * 100,
        min: 0, max: 200, style: SliderStyle.InSet, direction: Axis.Vertical, reverse: true
      })
        .height(260).trackColor('#E0E0E0').selectedColor(color)
        .onChange((v) => {
          if (side === 'L') this.volLinearLeft = v / 100;
          else this.volLinearRight = v / 100;
          this.debounceVolumeApply();
        })
      Text(label).fontColor(COLOR_TEXT_SEC).fontSize(13).margin({ top: 15 })
    }
  }

  @Builder
  EqualizerTab() {
    Scroll() {
      Column() {
        Row() {
          Text('模式：').fontSize(13).fontColor(COLOR_TEXT_SEC)
          Row() {
            Text('统一').fontSize(12).fontColor(this.isStereoEqMode ? COLOR_TEXT_SEC : Color.White)
              .backgroundColor(this.isStereoEqMode ? '#F0F0F0' : COLOR_ACCENT)
              .padding({ left: 10, right: 10, top: 4, bottom: 4 }).borderRadius({ topLeft: 12, bottomLeft: 12 })
              .onClick(() => { this.isStereoEqMode = false; this.applyEqSettings(); })
            Text('独立').fontSize(12).fontColor(this.isStereoEqMode ? Color.White : COLOR_TEXT_SEC)
              .backgroundColor(this.isStereoEqMode ? COLOR_ACCENT : '#F0F0F0')
              .padding({ left: 10, right: 10, top: 4, bottom: 4 }).borderRadius({ topRight: 12, bottomRight: 12 })
              .onClick(() => { this.isStereoEqMode = true; this.applyEqSettings(); })
          }
          Blank()
          Text(this.currentPresetLabel).fontSize(14).fontColor(COLOR_ACCENT).bindMenu(this.getEqPresetMenu())
        }.width('100%').margin({ bottom: 20 })
        if (this.isStereoEqMode) {
          Row({ space: 10 }) {
            Button('左 (L)').fontSize(12).height(28).backgroundColor(!this.editRightChannel ? COLOR_ACCENT : '#F0F0F0')
              .onClick(() => this.editRightChannel = false)
            Button('右 (R)').fontSize(12).height(28).backgroundColor(this.editRightChannel ? '#FF9500' : '#F0F0F0')
              .onClick(() => this.editRightChannel = true)
          }.width('100%').margin({ bottom: 15 })
        }
        Row() {
          ForEach(EQ_BAND_LABELS, (label: string, index: number) => {
            Column() {
              Slider({ value: this.getCurrentGain(index), min: -12, max: 12, step: 1, direction: Axis.Vertical, reverse: true })
                .layoutWeight(1).trackColor('#E0E0E0').selectedColor(this.getEqColor()).onChange((v) => { this.updateGain(index, v); })
              Text(`${Math.round(this.getCurrentGain(index))}`).fontSize(10).margin({ top: 4 })
              Text(label).fontSize(9).fontColor(COLOR_TEXT_SEC)
            }.layoutWeight(1).height(240)
          })
        }.padding(15).backgroundColor(COLOR_CARD).borderRadius(16).width('100%')
      }.padding(24).width('100%').justifyContent(FlexAlign.Start).constraintSize({ minHeight: '100%' })
    }.scrollable(ScrollDirection.Vertical)
  }

  @Builder
  DrcTab() {
    Scroll() {
      Column() {
        Row() {
          Text('动态范围压缩 (DRC)').fontSize(18).fontWeight(FontWeight.Bold)
          Blank()
          Toggle({ type: ToggleType.Switch, isOn: this.drcEnabled }).onChange((v) => {
            this.drcEnabled = v;
            this.debounceDrcApply();
          })
        }.width('100%').margin({ bottom: 20 })

        // Canvas 电平表 (重新设计：3栏式)
        Canvas(this.canvasCtx)
          .width('100%')
          .height(180) // 增加高度以便更清晰展示
          .backgroundColor('#1A1A1A')
          .borderRadius(12)
          .margin({ bottom: 15 })
          .onReady(() => {
            this.drawDrcMeter();
          })

        // 预设选择区
        Row() {
          Text('场景预设：').fontSize(14).fontColor(COLOR_TEXT_SEC)
          Text(this.drcPresetLabel).fontSize(14).fontColor(COLOR_ACCENT)
            .bindMenu(this.getDrcPresetMenu())
        }.width('100%').margin({ bottom: 15 }).justifyContent(FlexAlign.Start)

        Column({ space: 15 }) {
          this.DrcSlider('阈值 (Threshold)', 'THRES', -60, 0, 'dB')
          this.DrcSlider('比率 (Ratio)', 'RATIO', 1, 20, ':1')
          this.DrcSlider('启动时间 (Attack)', 'ATTACK', 0.1, 200, 'ms')
          this.DrcSlider('释放时间 (Release)', 'RELEASE', 5, 2000, 'ms')
          this.DrcSlider('补偿增益 (Makeup)', 'MAKEUP', -12, 24, 'dB')

          Button('重置 DRC 参数').width('100%').height(45).backgroundColor('#F0F0F0').fontColor(COLOR_TEXT_PRI)
            .margin({ top: 10 }).onClick(() => {
            this.applyDrcPreset(DrcPresetId.Default);
            promptAction.showToast({ message: 'DRC 已重置' });
          })
        }.padding(20).backgroundColor(COLOR_CARD).borderRadius(16)

        Text('提示: 中间红色条向下表示压缩量(GR)。')
          .fontSize(12).fontColor(COLOR_TEXT_SEC).margin({ top: 20 })

      }.padding(24).width('100%').justifyContent(FlexAlign.Start).constraintSize({ minHeight: '100%' })
    }.scrollable(ScrollDirection.Vertical)
  }

  // DRC 滑块组件：使用 Key 模式解决双向绑定问题
  @Builder
  DrcSlider(label: string, key: string, min: number, max: number, unit: string) {
    Column({ space: 5 }) {
      Row() {
        Text(label).fontSize(14)
        Blank()
        Text(`${this.getDrcVal(key)}${unit}`).fontSize(14).fontColor(COLOR_ACCENT)
      }.width('100%')
      Slider({ value: this.getDrcVal(key), min: min, max: max, style: SliderStyle.InSet })
        .onChange((v) => {
          this.setDrcVal(key, v);
          this.drcPresetLabel = '自定义'; // 动了滑块就变自定义
          this.debounceDrcApply();
        })
    }
  }

  @Builder
  getDrcPresetMenu() {
    Menu() {
      ForEach(Object.keys(DRC_PRESET_MAP), (key: string) => {
        MenuItem({ content: DRC_PRESET_LABEL[key] }).onClick(() => {
          this.applyDrcPreset(key as DrcPresetId);
        })
      })
    }
  }

  @Builder
  getEqPresetMenu() {
    Menu() {
      ForEach(Object.keys(EQ_PRESET_MAP), (key: string) => {
        MenuItem({ content: EQ_PRESET_LABEL[key] }).onClick(() => {
          this.eqPresetId = key as EqPresetId; this.currentPresetLabel = EQ_PRESET_LABEL[key];
          const gains = EQ_PRESET_MAP[this.eqPresetId];
          if (this.isStereoEqMode) { this.eqGainsLeft = [...gains]; this.eqGainsRight = [...gains]; } else { this.eqGainsUnified = [...gains]; }
          this.applyEqSettings();
        })
      })
    }
  }

  // --- 逻辑辅助 ---

  private applyDrcPreset(id: DrcPresetId) {
    const p = DRC_PRESET_MAP[id];
    if (p) {
      this.drcThresholdDb = p.threshold;
      this.drcRatio = p.ratio;
      this.drcAttackMs = p.attack;
      this.drcReleaseMs = p.release;
      this.drcMakeupDb = p.makeup;
      this.drcPresetLabel = DRC_PRESET_LABEL[id];
      this.applyDrcSettings();
    }
  }

  private getDrcVal(key: string): number {
    if (key === 'THRES') return this.drcThresholdDb;
    if (key === 'RATIO') return this.drcRatio;
    if (key === 'ATTACK') return this.drcAttackMs;
    if (key === 'RELEASE') return this.drcReleaseMs;
    return this.drcMakeupDb;
  }

  private setDrcVal(key: string, v: number) {
    // 简单的四舍五入或保留一位小数，避免 UI 跳变
    if (key === 'THRES') this.drcThresholdDb = Math.round(v);
    else if (key === 'RATIO') this.drcRatio = Math.round(v * 2) / 2;
    else if (key === 'ATTACK') this.drcAttackMs = Math.round(v * 10) / 10;
    else if (key === 'RELEASE') this.drcReleaseMs = Math.round(v);
    else this.drcMakeupDb = Math.round(v);
  }

  private getCurrentGain(index: number): number {
    return this.isStereoEqMode ? (this.editRightChannel ? this.eqGainsRight[index] : this.eqGainsLeft[index]) : this.eqGainsUnified[index];
  }

  private updateGain(index: number, v: number) {
    const val = Math.round(v);
    if (this.isStereoEqMode) {
      if (this.editRightChannel) this.eqGainsRight[index] = val; else this.eqGainsLeft[index] = val;
    } else this.eqGainsUnified[index] = val;
    this.applyEqSettings();
  }

  private getEqColor(): string {
    return this.isStereoEqMode ? (this.editRightChannel ? '#FF9500' : COLOR_ACCENT) : COLOR_ACCENT;
  }

  @Builder
  TabTitle(title: string, index: number) {
    Column() {
      Text(title).fontSize(14).fontWeight(this.currentIndex === index ? 700 : 400).fontColor(this.currentIndex === index ? COLOR_ACCENT : COLOR_TEXT_SEC)
      Divider().color(COLOR_ACCENT).strokeWidth(3).width(16).opacity(this.currentIndex === index ? 1 : 0).margin({ top: 4 })
    }
  }

  build() {
    Column() {
      Column() {
        Text(this.message).fontSize(20).fontWeight(700).margin({ top: 15, bottom: 5 })
        Text(this.durationMs > 0 ? `${this.formatTime(this.currentPosMs)} / ${this.formatTime(this.durationMs)}` : this.formatTime(this.currentPosMs))
          .fontSize(36).fontWeight(700).margin({ bottom: 25 })
        Slider({ value: this.isDraggingSeek ? this.seekSliderMs : this.currentPosMs, min: 0, max: this.durationMs > 0 ? this.durationMs : 100 })
          .onChange(async (v, mode) => {
            this.seekSliderMs = v;
            if (mode === 0 || mode === 1) this.isDraggingSeek = true;
            else { this.isDraggingSeek = false; await this.doSeek(v); }
          })
      }.width('100%').padding(24).backgroundColor(COLOR_BG)

      Tabs({ index: this.currentIndex }) {
        TabContent() { this.ControlTab() }.tabBar(this.TabTitle('控制', 0))
        TabContent() { this.MixerTab() }.tabBar(this.TabTitle('调音', 1))
        TabContent() { this.EqualizerTab() }.tabBar(this.TabTitle('均衡', 2))
        TabContent() { this.DrcTab() }.tabBar(this.TabTitle('压缩', 3))
      }.layoutWeight(1).onChange((i) => this.currentIndex = i)
    }.width('100%').height('100%').backgroundColor(COLOR_BG)
    .expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM])
  }
}