import { COLOR_CARD, COLOR_TEXT_SEC } from './constants';

export interface SpectrumBarsProps {
  /** 0~1 band levels */
  bands01: number[];
  /** 0~1 peak caps (optional) */
  peaks01?: number[];
  /** overall 0~1 (optional label) */
  level01?: number;
  isPlaying: boolean;
  heightVp?: number;
}

function clamp01(v: number): number {
  if (!Number.isFinite(v)) return 0;
  if (v < 0) return 0;
  if (v > 1) return 1;
  return v;
}

@Component
export struct SpectrumBars {
  @Prop bands01: number[] = [];
  @Prop peaks01: number[] = [];
  @Prop level01: number = 0;
  @Prop isPlaying: boolean = false;
  @Prop heightVp: number = 140;

  private canvasSettings: RenderingContextSettings = new RenderingContextSettings(true);
  private canvasCtx: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.canvasSettings);

  private drawTimerId: number = -1;
  private smoothBands: number[] = [];

  aboutToDisappear(): void {
    this.stop();
  }

  private start(): void {
    if (this.drawTimerId !== -1) return;
    const intervalMs = 33; // ~30fps
    this.drawTimerId = setInterval(() => {
      this.draw();
    }, intervalMs);
  }

  private stop(): void {
    if (this.drawTimerId !== -1) {
      clearInterval(this.drawTimerId);
      this.drawTimerId = -1;
    }
  }

  private draw(): void {
    const ctx = this.canvasCtx;
    if (!ctx) return;
    const w = ctx.width;
    const h = ctx.height;
    if (w <= 0 || h <= 0) return;

    const bands = this.bands01 ?? [];
    const peaks = this.peaks01 ?? [];
    const n = bands.length;

    // ensure smooth buffer
    if (this.smoothBands.length !== n) {
      this.smoothBands = new Array(n).fill(0);
    }

    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = '#111111';
    ctx.fillRect(0, 0, w, h);

    // bars area
    const paddingX = 12;
    const paddingY = 14;
    const innerW = Math.max(0, w - paddingX * 2);
    const innerH = Math.max(0, h - paddingY * 2);

    // subtle grid
    ctx.strokeStyle = '#222222';
    ctx.lineWidth = 1;
    for (let i = 1; i < 4; i++) {
      const y = paddingY + (innerH * i) / 4;
      ctx.beginPath();
      ctx.moveTo(paddingX, y);
      ctx.lineTo(paddingX + innerW, y);
      ctx.stroke();
    }

    if (n <= 0) {
      ctx.fillStyle = '#888888';
      ctx.font = '10vp sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('No PCM', w / 2, h / 2);
      return;
    }

    const gap = Math.max(2, Math.floor(innerW * 0.01));
    const barW = Math.max(2, Math.floor((innerW - gap * (n - 1)) / n));
    const maxBarH = innerH;

    const grad = ctx.createLinearGradient(0, paddingY + innerH, 0, paddingY);
    grad.addColorStop(0, '#00C853');
    grad.addColorStop(0.65, '#FFEB3B');
    grad.addColorStop(1, '#FF5252');
    ctx.fillStyle = grad;

    let x = paddingX;
    for (let i = 0; i < n; i++) {
      const target = clamp01(bands[i]);
      const cur = this.smoothBands[i] ?? 0;
      const atk = 0.45;
      const rel = 0.18;
      const next = (target > cur) ? (cur + (target - cur) * atk) : (cur + (target - cur) * rel);
      this.smoothBands[i] = next;

      const barH = Math.floor(next * maxBarH);
      const y = paddingY + innerH - barH;
      // rounded bars via rect + small radius approximation
      ctx.fillRect(x, y, barW, barH);

      // peak cap (if provided)
      const pk = i < peaks.length ? clamp01(peaks[i]) : -1;
      if (pk >= 0) {
        const capH = 3;
        const capY = paddingY + innerH - Math.floor(pk * maxBarH) - capH;
        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect(x, capY, barW, capH);
        ctx.fillStyle = grad;
      }
      x += barW + gap;
    }

    // label
    ctx.fillStyle = '#AAAAAA';
    ctx.font = '10vp sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText(this.isPlaying ? `SPECTRUM ${n}` : 'IDLE', paddingX, 12);

    ctx.textAlign = 'right';
    ctx.fillText(`${Math.round(clamp01(this.level01 ?? 0) * 100)}%`, w - paddingX, 12);
  }

  build() {
    Column({ space: 8 }) {
      Row() {
        Text('频段柱形')
          .fontSize(14)
          .fontColor(COLOR_TEXT_SEC);
        Blank();
        Text(this.isPlaying ? '播放中' : '已暂停')
          .fontSize(14)
          .fontColor(this.isPlaying ? Color.White : COLOR_TEXT_SEC);
      }.width('100%')

      Canvas(this.canvasCtx)
        .width('100%')
        .height(this.heightVp)
        .backgroundColor('#111111')
        .borderRadius(12)
        .onReady(() => {
          this.start();
        })
    }
    .padding(16)
    .backgroundColor(COLOR_CARD)
    .borderRadius(16)
    .shadow(ShadowStyle.OUTER_DEFAULT_XS)
  }
}
