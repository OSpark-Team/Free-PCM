import { promptAction, window } from "@kit.ArkUI";
import { BusinessError } from "@kit.BasicServicesKit";
import { hilog } from '@kit.PerformanceAnalysisKit';
import {
  AudioRendererPlayer,
  AudioRendererPlayerDebugOptions,
  PcmTapOptions,
  PcmDecoderTool,
  PcmEqualizer,
  PcmStreamDecoder,
  PcmStreamDecoderOptions,
  DecodeAudioProgress,
  PcmStreamDecoderCallbacks,
} from "@ospark/free-pcm";
import { fileIo as fs } from "@kit.CoreFileKit";
import { common } from "@kit.AbilityKit";

import {
  ControlTab,
  MixerTab,
  EqualizerTab,
  DrcTab,
  SpectrumBars,
  COLOR_BG,
  COLOR_ACCENT,
  COLOR_TEXT_SEC,
  EqPresetId,
  EQ_PRESET_LABEL,
  EQ_PRESET_MAP,
  DrcPresetId,
  DRC_PRESET_LABEL,
  DRC_PRESET_MAP,
  DrcMeterInfo,
  FftSpectrumAnalyzer,
} from "./components";

@Entry
@Component
struct Index {
  // === 基础状态 ===
  @State message: string = "音频实验室";
  @State status: string = "设备就绪";
  @State isDecoding: boolean = false;
  @State isPlaying: boolean = false;
  @State url: string = "";
  @State currentIndex: number = 0;

  // === 进度相关 ===
  @State progressPercent: number = 0;
  @State currentPosMs: number = 0;
  @State durationMs: number = 0;
  @State seekSliderMs: number = 0;
  @State isDraggingSeek: boolean = false;
  @State isSeeking: boolean = false;
  @State isBuffering: boolean = false;

  // === EQ 状态 ===
  @State isStereoEqMode: boolean = false;
  @State editRightChannel: boolean = false;
  @State eqPresetId: EqPresetId = EqPresetId.Default;
  @State currentPresetLabel: string = EQ_PRESET_LABEL[EqPresetId.Default];
  @State eqGainsUnified: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];
  @State eqGainsLeft: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];
  @State eqGainsRight: number[] = [...EQ_PRESET_MAP[EqPresetId.Default]];

  // === 混音器状态 ===
  @State volLinearLeft: number = 1.0;
  @State volLinearRight: number = 1.0;

  // === DRC 状态 ===
  @State drcEnabled: boolean = false;
  @State drcThresholdDb: number = -20;
  @State drcRatio: number = 4;
  @State drcAttackMs: number = 10;
  @State drcReleaseMs: number = 100;
  @State drcMakeupDb: number = 0;
  @State drcPresetLabel: string = "自定义";

  // === 内部对象 ===
  private player: AudioRendererPlayer = new AudioRendererPlayer();
  private decoderTool: PcmDecoderTool = new PcmDecoderTool();
  private eq: PcmEqualizer = new PcmEqualizer();
  private currentDecoder: PcmStreamDecoder | null = null;
  private bufferingTimerId: number = -1;
  private debugTimerId: number = -1;
  private volTimerId: number = -1;
  private drcTimerId: number = -1;
  private lastPosMs: number = 0;
  private lastPosUpdateTs: number = 0;

  // === Meter 数据 ===
  @State meterLevel: number = -120;
  @State meterGain: number = 0;
  @State meterGr: number = 0;

  // === 可视化（PCM Tap） ===
  @State vizLevel01: number = 0;
  @State vizBands01: number[] = new Array(64).fill(0);
  @State vizPeaks01: number[] = new Array(64).fill(-1);

  @State tiltEnabled: boolean = true;

  private spectrum: FftSpectrumAnalyzer = new FftSpectrumAnalyzer({
    fftSize: 1024,
    barCount: 16,

    analyserFftSize: 256,
    analyserMinDecibels: -90,
    analyserMaxDecibels: -20,
    analyserSmoothing: 0.75,
    analyserStride: 2,

    gamma: 0.4, // 原 0.55，稍微压低让中低能量更可见
    attack: 0.65, // 原 0.82，稍微放慢上升，避免跳动过于激烈
    release: 0.12, // 原 0.18，稍微加快下落，显得更灵动
    peakHoldFrames: 18, // 原 12，峰值多停留一会儿，体感更好看
    gravity: 0.018, // 原 0.025，峰值下落慢一点更优雅

    tiltDbPerOct: 3.5, // 原 4.5，稍微减弱倾斜补偿，高频不会压太死
    tiltRefHz: 1000,
  });
  private vizTimerId: number = -1;

  @State enableVizTap: boolean = true;

  private startVizTimer(): void {
    if (this.vizTimerId !== -1) return;
    this.vizTimerId = setInterval(() => {
      this.spectrum.computeBars();

      const bars = this.spectrum.getBars01();
      const dstN = 64;
      if (!this.vizBands01 || this.vizBands01.length !== dstN) {
        this.vizBands01 = new Array(dstN).fill(0);
      }
      // 16 bars → 64 bars 插值
      for (let i = 0; i < dstN; i++) {
        const x = (i / (dstN - 1)) * (bars.length - 1);
        const i0 = Math.floor(x);
        const i1 = Math.min(bars.length - 1, i0 + 1);
        const t = x - i0;
        this.vizBands01[i] = bars[i0] * (1 - t) + bars[i1] * t;
      }

      this.vizLevel01 = this.spectrum.getLevel01();
    }, 33);
  }

  private stopVizTimer(): void {
    if (this.vizTimerId !== -1) {
      clearInterval(this.vizTimerId);
      this.vizTimerId = -1;
    }
  }

  private attachVizTap(): void {
    this.player.setOnPcmData(
      (pcmBuf, bytesWritten, streamInfo) => {
        this.spectrum.setStreamFormat(
          streamInfo.sampleRate,
          streamInfo.sampleFormatCode,
          streamInfo.channelCount,
        );
        this.spectrum.pushPcm(pcmBuf, bytesWritten);
      },
      { minIntervalMs: 16 },
    );
    this.spectrum.setTilt(this.tiltEnabled, 4.5, 1000);
    this.startVizTimer();
  }

  private detachVizTap(): void {
    this.player.setOnPcmData(null);
    this.stopVizTimer();
  }

  // === 生命周期 ===
  aboutToAppear(): void {
    window.getLastWindow(getContext(this)).then((win) => {
      win.setWindowLayoutFullScreen(true);
      win.setWindowSystemBarProperties({ statusBarContentColor: "#000000" });
    });
  }

  // === 业务逻辑 ===

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
        this.currentDecoder.setDrcParams(
          this.drcThresholdDb,
          this.drcRatio,
          this.drcAttackMs,
          this.drcReleaseMs,
          this.drcMakeupDb,
        );
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

  // === 播放器逻辑 ===

  private startBufferingWatchdog() {
    if (this.bufferingTimerId !== -1) return;
    this.lastPosMs = this.currentPosMs;
    this.lastPosUpdateTs = Date.now();
    this.bufferingTimerId = setInterval(() => {
      if (this.isSeeking || !this.isPlaying) {
        this.isBuffering = false;
        return;
      }
      const now = Date.now();
      if (this.currentPosMs !== this.lastPosMs) {
        this.lastPosMs = this.currentPosMs;
        this.lastPosUpdateTs = now;
        this.isBuffering = false;
        return;
      }
      if (this.isPlaying) this.isBuffering = now - this.lastPosUpdateTs > 800;
    }, 200);
  }

  private stopBufferingWatchdog() {
    if (this.bufferingTimerId !== -1) {
      clearInterval(this.bufferingTimerId);
      this.bufferingTimerId = -1;
    }
    this.isBuffering = false;
  }

  private startDebugTimer(): void {
    if (this.debugTimerId !== -1) return;
    this.debugTimerId = setInterval(() => {
      const pStats = this.player.getDebugStats();
      hilog.info(0x0000, 'free-pcm-debug',
        `writeData calls=${pStats.writeDataCalls} valid=${pStats.writeDataValid} invalid=${pStats.writeDataInvalid} lastBytes=${pStats.writeDataLastBytes} maxCostMs=${pStats.writeDataMaxCostMs} underflow=${pStats.underflowCount} (+${pStats.underflowDelta}) jank=${pStats.audioJankEvents}`);

      const d = this.currentDecoder;
      if (d && d.getDebugStats) {
        try {
          const s = d.getDebugStats();
          hilog.info(0x0000, 'free-pcm-debug',
            `native writeCalls=${s.writeCalls} writeZero=${s.writeZero} availLast=${s.availLast} writeMaxUs=${s.writeMaxUs} dspLastUs=${s.dspLastUs} dspMaxUs=${s.dspMaxUs}`);
        } catch (e) {
          // ignore
        }
      }
    }, 1000);
  }

  private stopDebugTimer(): void {
    if (this.debugTimerId !== -1) {
      clearInterval(this.debugTimerId);
      this.debugTimerId = -1;
    }
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
    if (!Number.isFinite(ms) || ms < 0) return "00:00";
    const totalSec = Math.floor(ms / 1000);
    const min = Math.floor(totalSec / 60);
    const sec = totalSec % 60;
    return `${min.toString().padStart(2, "0")}:${sec.toString().padStart(2, "0")}`;
  }

  // === 核心逻辑：播放/暂停切换 ===
  async togglePlayState() {
    if (this.currentDecoder) {
      if (this.isPlaying) {
        try {
          await this.player.pause();
          this.isPlaying = false;
          this.status = "已暂停";
          this.stopBufferingWatchdog();
        } catch (e) {
          console.error("Pause failed:", e);
        }
      } else {
        try {
          await this.player.resume();
          this.isPlaying = true;
          this.status = "播放中";
          this.startBufferingWatchdog();
        } catch (e) {
          console.error("Resume failed:", e);
        }
      }
      return;
    }
    await this.startNewPlayback();
  }

  // === 内部方法：初始化并开始新的播放 ===
  private async startNewPlayback() {
    if (this.isDecoding) return;
    let targetUrl = this.url.trim();
    if (!targetUrl) {
      promptAction.showToast({ message: "请输入地址" });
      return;
    }

    if (targetUrl.startsWith("test")) {
      // 获取扩展名
      const ext = targetUrl.split(".").pop();
      try {
        const ctx = getContext(this) as common.UIAbilityContext;
        const fileData = await ctx.resourceManager.getRawFileContent(`test.${ext}`);
        const tempPath = ctx.filesDir + `/test.${ext}`;
        const file = await fs.open(tempPath, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY);
        await fs.write(file.fd, fileData.buffer);
        await fs.close(file);
        targetUrl = tempPath;
      } catch (e) {
        promptAction.showToast({ message: "文件读取失败" });
        return;
      }
    }

    this.isPlaying = true;
    this.isDecoding = true;
    this.status = "正在初始化...";
    this.startBufferingWatchdog();

    try {
      const callbacks: PcmStreamDecoderCallbacks = {
        onProgress: (p: DecodeAudioProgress) => {
          if (p.progress >= 0) this.progressPercent = Math.min(100, Math.round(p.progress * 100));
        },
        onError: (e: Error) => {
          if (this.currentDecoder && this.status !== "已重置") {
            this.status = `错误: ${e.message}`;
            this.resetPlayback();
          }
        },
        onDrcMeter: (m: DrcMeterInfo) => {
          this.meterLevel = m.levelDb;
          this.meterGain = m.gainDb;
          this.meterGr = m.grDb;
        },
      };

      const decoder = this.decoderTool.createStreamDecoder(
        targetUrl,
        (() => {
          const opts: PcmStreamDecoderOptions = {
            qosLevel: -2,
            eqEnabled: true,
            eqGainsDb: [...this.eqGainsUnified],
          };
          return opts;
        })(),
        callbacks,
      );

      this.currentDecoder = decoder;

      const dbgOpts: AudioRendererPlayerDebugOptions = {
        measureWriteDataCost: true,
        underflowSampleIntervalMs: 1000,
        enableAudioJankWatcher: false,
      };
      this.player.setDebugEnabled(true, dbgOpts);
      if (decoder.setDebugEnabled) {
        try {
          decoder.setDebugEnabled(true);
        } catch (e) {
          // ignore
        }
      }
      this.startDebugTimer();
      const info = await decoder.ready;
      this.durationMs = info.durationMs ?? 0;
      this.status = `播放中: ${info.sampleRate}Hz，${info.sampleFormatCode}，${info.channelCount}ch`;

      this.player.setOnTimeUpdate((pos) => {
        if (!this.isDraggingSeek) this.currentPosMs = pos;
      });

      if (this.enableVizTap) {
        this.attachVizTap();
      } else {
        this.detachVizTap();
      }

      this.applyEqSettings();
      this.applyChannelVolumes();
      this.applyDrcSettings();

      await this.player.play(decoder, info);
      await decoder.done;

      if (this.currentDecoder === decoder) {
        this.status = "播放结束";
        this.isPlaying = false;
        this.currentPosMs = 0;
        this.vizLevel01 = 0;
        this.vizBands01 = new Array(64).fill(0);
        this.vizPeaks01 = new Array(64).fill(-1);
        this.spectrum.reset();
        this.detachVizTap();
        this.stopBufferingWatchdog();
        this.stopDebugTimer();
      }
    } catch (err) {
      if (this.currentDecoder) {
        this.status = `启动失败: ${(err as BusinessError).message}`;
        this.resetPlayback();
      }
    } finally {
      this.isDecoding = false;
    }
  }

  // === 重置/停止逻辑 ===
  private async resetPlayback() {
    this.stopBufferingWatchdog();
    this.stopDebugTimer();
    try {
      await this.player.stop();
    } catch (e) {
      console.warn("Stop failed:", e);
    }

    this.currentDecoder = null;
    this.isPlaying = false;
    this.isDecoding = false;
    this.status = "已重置";
    this.currentPosMs = 0;
    this.progressPercent = 0;

    if (this.drcTimerId !== -1) {
      clearTimeout(this.drcTimerId);
      this.drcTimerId = -1;
    }

    this.meterLevel = -120;
    this.meterGain = 0;
    this.meterGr = 0;
    this.vizLevel01 = 0;
    this.vizBands01 = new Array(64).fill(0);
    this.vizPeaks01 = new Array(64).fill(-1);
    this.spectrum.reset();

    this.detachVizTap();
  }

  // === Tab 标题 ===
  @Builder
  TabTitle(title: string, index: number) {
    Column() {
      Text(title)
        .fontSize(14)
        .fontWeight(this.currentIndex === index ? 700 : 400)
        .fontColor(this.currentIndex === index ? COLOR_ACCENT : COLOR_TEXT_SEC)
      Divider()
        .color(COLOR_ACCENT)
        .strokeWidth(3)
        .width(16)
        .opacity(this.currentIndex === index ? 1 : 0)
        .margin({ top: 4 })
    };
  }

  // === 主界面构建 ===
  build() {
    Column() {
      // 头部区域
      Column() {
        Text(this.message)
          .fontSize(20)
          .fontWeight(700)
          .margin({ top: 15, bottom: 5 })
        Text(
          this.durationMs > 0
            ? `${this.formatTime(this.currentPosMs)} / ${this.formatTime(this.durationMs)}`
            : this.formatTime(this.currentPosMs),
        )
          .fontSize(36)
          .fontWeight(700)
          .margin({ bottom: 25 })
        Slider({
          value: this.isDraggingSeek ? this.seekSliderMs : this.currentPosMs,
          min: 0,
          max: this.durationMs > 0 ? this.durationMs : 100,
        })
          .onChange(async (v, mode) => {
            this.seekSliderMs = v;
            if (mode === 0 || mode === 1) {
              this.isDraggingSeek = true;
            } else {
              this.isDraggingSeek = false;
              await this.doSeek(v);
            }
          })
      }
      .width("100%")
      .padding(24)
      .backgroundColor(COLOR_BG)

      // 可视化区域（Canvas）
      SpectrumBars({
        bands01: this.vizBands01,
        peaks01: this.vizPeaks01,
        renderBarCount: 64,
        level01: this.vizLevel01,
        isPlaying: this.isPlaying,
        heightVp: 140,
      })
        .width("100%")
        .padding({ left: 24, right: 24, bottom: 12 })

      // 可选：频谱倾斜补偿开关
      Row() {
        Text("Tilt 补偿 (+4.5dB/oct)")
          .fontSize(14)
          .fontColor(COLOR_TEXT_SEC)
        Blank()
        Toggle({ type: ToggleType.Switch, isOn: this.tiltEnabled })
          .onChange((v) => {
            this.tiltEnabled = v;
            // safe to toggle at runtime; just recompute weights
            this.spectrum.setTilt(this.tiltEnabled, 4.5, 1000);
          })
      }
      .width("100%")
      .padding({ left: 24, right: 24, bottom: 12 })

      Row() {
        Text('频谱采样')
          .fontSize(14)
          .fontColor(COLOR_TEXT_SEC)
        Blank()
        Toggle({ type: ToggleType.Switch, isOn: this.enableVizTap })
          .onChange((v) => {
            this.enableVizTap = v;
            if (this.enableVizTap) {
              this.attachVizTap();
            } else {
              this.detachVizTap();
            }
          })
      }
      .width('100%')
      .padding({ left: 24, right: 24, bottom: 12 })

      // Tabs 区域
      Tabs({ index: this.currentIndex }) {
        TabContent() {
          ControlTab({
            url: this.url,
            status: this.status,
            isPlaying: this.isPlaying,
            isDecoding: this.isDecoding,
            isSeeking: this.isSeeking,
            isBuffering: this.isBuffering,
            hasDecoder: this.currentDecoder !== null,
            progressPercent: this.progressPercent,
            onUrlChange: (url: string) => {
              this.url = url;
            },
            onTogglePlay: () => {
              this.togglePlayState();
            },
            onReset: () => {
              this.resetPlayback();
            },
          })
        }.tabBar(this.TabTitle("控制", 0))
        TabContent() {
          MixerTab({
            volLinearLeft: this.volLinearLeft,
            volLinearRight: this.volLinearRight,
            onVolumeChange: (left, right) => {
              this.volLinearLeft = left;
              this.volLinearRight = right;
              this.debounceVolumeApply();
            },
          })
        }.tabBar(this.TabTitle("调音", 1))
        TabContent() {
          EqualizerTab({
            isStereoEqMode: this.isStereoEqMode,
            editRightChannel: this.editRightChannel,
            currentPresetLabel: this.currentPresetLabel,
            eqGainsUnified: this.eqGainsUnified,
            eqGainsLeft: this.eqGainsLeft,
            eqGainsRight: this.eqGainsRight,
            onStereoModeChange: (isStereo) => {
              this.isStereoEqMode = isStereo;
              this.applyEqSettings();
            },
            onEditChannelChange: (isRight) => {
              this.editRightChannel = isRight;
            },
            onGainsChange: (unified, left, right) => {
              this.eqGainsUnified = unified;
              this.eqGainsLeft = left;
              this.eqGainsRight = right;
              this.currentPresetLabel = "自定义";
              this.applyEqSettings();
            },
          })
        }.tabBar(this.TabTitle("均衡", 2))
        TabContent() {
          DrcTab({
            drcEnabled: this.drcEnabled,
            drcThresholdDb: this.drcThresholdDb,
            drcRatio: this.drcRatio,
            drcAttackMs: this.drcAttackMs,
            drcReleaseMs: this.drcReleaseMs,
            drcMakeupDb: this.drcMakeupDb,
            drcPresetLabel: this.drcPresetLabel,
            meterLevel: this.meterLevel,
            meterGain: this.meterGain,
            meterGr: this.meterGr,
            onEnabledChange: (enabled) => {
              this.drcEnabled = enabled;
              this.debounceDrcApply();
            },
            onParamsChange: (threshold, ratio, attack, release, makeup) => {
              this.drcThresholdDb = threshold;
              this.drcRatio = ratio;
              this.drcAttackMs = attack;
              this.drcReleaseMs = release;
              this.drcMakeupDb = makeup;
              this.drcPresetLabel = "自定义";
              this.debounceDrcApply();
            },
            onPresetChange: (preset) => {
              const p = DRC_PRESET_MAP[preset];
              if (p) {
                this.drcThresholdDb = p.threshold;
                this.drcRatio = p.ratio;
                this.drcAttackMs = p.attack;
                this.drcReleaseMs = p.release;
                this.drcMakeupDb = p.makeup;
                this.drcPresetLabel = DRC_PRESET_LABEL[preset];
                this.applyDrcSettings();
              }
            },
            onDrawMeter: () => {},
          })
        }.tabBar(this.TabTitle("压缩", 3))
      }
      .layoutWeight(1)
      .onChange((i) => (this.currentIndex = i))
    }
    .width("100%")
    .height("100%")
    .backgroundColor(COLOR_BG)
    .expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM]);
  }
}
