import { promptAction } from '@kit.ArkUI';
import { COLOR_ACCENT, COLOR_CARD, COLOR_TEXT_PRI, COLOR_TEXT_SEC } from './constants';
import { DrcPresetId, DRC_PRESET_LABEL, DRC_PRESET_MAP, DrcParams, DrcMeterInfo } from './types';

/**
 * DRC 组件属性
 */
export interface DrcTabProps {
  drcEnabled: boolean;
  drcThresholdDb: number;
  drcRatio: number;
  drcAttackMs: number;
  drcReleaseMs: number;
  drcMakeupDb: number;
  drcPresetLabel: string;
  meterLevel: number;
  meterGain: number;
  meterGr: number;
  onEnabledChange: (enabled: boolean) => void;
  onParamsChange: (threshold: number, ratio: number, attack: number, release: number, makeup: number) => void;
  onPresetChange: (preset: string) => void;
  onDrawMeter: () => void;
}

@Component
export struct DrcTab {
  @Prop drcEnabled: boolean = false;
  @Prop drcThresholdDb: number = -20;
  @Prop drcRatio: number = 4;
  @Prop drcAttackMs: number = 10;
  @Prop drcReleaseMs: number = 100;
  @Prop drcMakeupDb: number = 0;
  @Prop drcPresetLabel: string = '自定义';
  @Prop meterLevel: number = -120;
  @Prop meterGain: number = 0;
  @Prop meterGr: number = 0;
  
  onEnabledChange: (enabled: boolean) => void = () => {};
  onParamsChange: (threshold: number, ratio: number, attack: number, release: number, makeup: number) => void = () => {};
  onPresetChange: (preset: string) => void = () => {};
  onDrawMeter: () => void = () => {};

  // Canvas 上下文
  private canvasSettings: RenderingContextSettings = new RenderingContextSettings(true);
  private canvasCtx: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.canvasSettings);

  // 坐标映射：0dB 在顶部，-60dB 在底部
  private dbToY(db: number, height: number): number {
    const minDb = -60;
    if (db < minDb) return height;
    if (db > 0) return 0;
    return (1 - (db - minDb) / (0 - minDb)) * height;
  }

  private drawDrcMeter() {
    if (!this.canvasCtx) return;

    const w = this.canvasCtx.width;
    const h = this.canvasCtx.height;
    const padding = 10;
    const barWidth = (w - padding * 4) / 3;

    // 1. 背景与网格
    this.canvasCtx.clearRect(0, 0, w, h);
    this.canvasCtx.fillStyle = '#111111';
    this.canvasCtx.fillRect(0, 0, w, h);

    // 画水平 dB 刻度线
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

    // Bar 1: Input Level
    const x1 = padding;
    const y1_top = this.dbToY(this.meterLevel, h);
    const h1 = h - y1_top;

    const grad1 = this.canvasCtx.createLinearGradient(x1, h, x1, 0);
    grad1.addColorStop(0, '#00FF00');
    grad1.addColorStop(0.7, '#FFFF00');
    grad1.addColorStop(1, '#FF0000');

    this.canvasCtx.fillStyle = grad1;
    this.canvasCtx.fillRect(x1, y1_top, barWidth, h1);

    // 阈值线
    const yThres = this.dbToY(this.drcThresholdDb, h);
    this.canvasCtx.strokeStyle = '#FFFFFF';
    this.canvasCtx.lineWidth = 2;
    this.canvasCtx.beginPath();
    this.canvasCtx.moveTo(x1, yThres);
    this.canvasCtx.lineTo(x1 + barWidth, yThres);
    this.canvasCtx.stroke();

    // Bar 2: Gain Reduction
    const x2 = padding * 2 + barWidth;
    let grHeight = 0;

    if (this.meterGr > 0.1) {
      const maxGrDisplay = 24;
      const ratio = Math.min(this.meterGr, maxGrDisplay) / maxGrDisplay;
      grHeight = ratio * h;
    }

    this.canvasCtx.fillStyle = '#FF3B30';
    this.canvasCtx.fillRect(x2, 0, barWidth, grHeight);

    // Bar 3: Output Level
    const x3 = padding * 3 + barWidth * 2;
    let outLevel = this.meterLevel + this.meterGain;
    if (outLevel > 0) outLevel = 0;

    const y3_top = this.dbToY(outLevel, h);
    const h3 = h - y3_top;

    const grad3 = this.canvasCtx.createLinearGradient(x3, h, x3, 0);
    grad3.addColorStop(0, '#007DFF');
    grad3.addColorStop(1, '#00BFFF');

    this.canvasCtx.fillStyle = grad3;
    this.canvasCtx.fillRect(x3, y3_top, barWidth, h3);

    // 文字标签
    this.canvasCtx.fillStyle = '#FFFFFF';
    this.canvasCtx.textAlign = 'center';
    this.canvasCtx.font = '12vp sans-serif bold';

    this.canvasCtx.fillText("IN", x1 + barWidth / 2, h - 10);
    this.canvasCtx.font = '10vp sans-serif';
    this.canvasCtx.fillText(this.meterLevel.toFixed(1), x1 + barWidth / 2, h - 25);

    this.canvasCtx.fillStyle = '#FF9999';
    this.canvasCtx.font = '12vp sans-serif bold';
    this.canvasCtx.fillText("GR", x2 + barWidth / 2, h - 10);
    if (this.meterGr > 0.1) {
      this.canvasCtx.font = '10vp sans-serif';
      this.canvasCtx.fillText(`-${this.meterGr.toFixed(1)}`, x2 + barWidth / 2, 20);
    }

    this.canvasCtx.fillStyle = '#FFFFFF';
    this.canvasCtx.font = '12vp sans-serif bold';
    this.canvasCtx.fillText("OUT", x3 + barWidth / 2, h - 10);
    this.canvasCtx.font = '10vp sans-serif';
    this.canvasCtx.fillText(outLevel.toFixed(1), x3 + barWidth / 2, h - 25);
  }

  private getDrcVal(key: string): number {
    if (key === 'THRES') return this.drcThresholdDb;
    if (key === 'RATIO') return this.drcRatio;
    if (key === 'ATTACK') return this.drcAttackMs;
    if (key === 'RELEASE') return this.drcReleaseMs;
    return this.drcMakeupDb;
  }

  private setDrcVal(key: string, v: number) {
    let threshold = this.drcThresholdDb;
    let ratio = this.drcRatio;
    let attack = this.drcAttackMs;
    let release = this.drcReleaseMs;
    let makeup = this.drcMakeupDb;

    if (key === 'THRES') threshold = Math.round(v);
    else if (key === 'RATIO') ratio = Math.round(v * 2) / 2;
    else if (key === 'ATTACK') attack = Math.round(v * 10) / 10;
    else if (key === 'RELEASE') release = Math.round(v);
    else makeup = Math.round(v);

    this.onParamsChange(threshold, ratio, attack, release, makeup);
  }

  @Builder
  DrcSlider(label: string, key: string, min: number, max: number, unit: string) {
    Column({ space: 5 }) {
      Row() {
        Text(label)
          .fontSize(14)
        Blank()
        Text(`${this.getDrcVal(key)}${unit}`)
          .fontSize(14)
          .fontColor(COLOR_ACCENT)
      }
      .width('100%')
      
      Slider({ value: this.getDrcVal(key), min: min, max: max, style: SliderStyle.InSet })
        .onChange((v) => {
          this.setDrcVal(key, v);
        })
    }
  }

  @Builder
  getDrcPresetMenu() {
    Menu() {
      ForEach(Object.keys(DRC_PRESET_MAP), (key: string) => {
        MenuItem({ content: DRC_PRESET_LABEL[key] }).onClick(() => {
          this.onPresetChange(key);
        })
      })
    }
  }

  @Builder
  build() {
    Scroll() {
      Column() {
        Row() {
          Text('动态范围压缩 (DRC)')
            .fontSize(18)
            .fontWeight(FontWeight.Bold)
          Blank()
          Toggle({ type: ToggleType.Switch, isOn: this.drcEnabled })
            .onChange((v) => {
              this.onEnabledChange(v);
            })
        }
        .width('100%')
        .margin({ bottom: 20 })

        // Canvas 电平表
        Canvas(this.canvasCtx)
          .width('100%')
          .height(180)
          .backgroundColor('#1A1A1A')
          .borderRadius(12)
          .margin({ bottom: 15 })
          .onReady(() => {
            this.drawDrcMeter();
          })

        // 预设选择区
        Row() {
          Text('场景预设：')
            .fontSize(14)
            .fontColor(COLOR_TEXT_SEC)
          Text(this.drcPresetLabel)
            .fontSize(14)
            .fontColor(COLOR_ACCENT)
            .bindMenu(this.getDrcPresetMenu())
        }
        .width('100%')
        .margin({ bottom: 15 })
        .justifyContent(FlexAlign.Start)

        Column({ space: 15 }) {
          this.DrcSlider('阈值 (Threshold)', 'THRES', -60, 0, 'dB')
          this.DrcSlider('比率 (Ratio)', 'RATIO', 1, 20, ':1')
          this.DrcSlider('启动时间 (Attack)', 'ATTACK', 0.1, 200, 'ms')
          this.DrcSlider('释放时间 (Release)', 'RELEASE', 5, 2000, 'ms')
          this.DrcSlider('补偿增益 (Makeup)', 'MAKEUP', -12, 24, 'dB')

          Button('重置 DRC 参数')
            .width('100%')
            .height(45)
            .backgroundColor('#F0F0F0')
            .fontColor(COLOR_TEXT_PRI)
            .margin({ top: 10 })
            .onClick(() => {
              this.onPresetChange(DrcPresetId.Default);
              promptAction.showToast({ message: 'DRC 已重置' });
            })
        }
        .padding(20)
        .backgroundColor(COLOR_CARD)
        .borderRadius(16)

        Text('提示: 中间红色条向下表示压缩量(GR)。')
          .fontSize(12)
          .fontColor(COLOR_TEXT_SEC)
          .margin({ top: 20 })
      }
      .padding(24)
      .width('100%')
      .justifyContent(FlexAlign.Start)
      .constraintSize({ minHeight: '100%' })
    }
    .scrollable(ScrollDirection.Vertical)
  }
}
