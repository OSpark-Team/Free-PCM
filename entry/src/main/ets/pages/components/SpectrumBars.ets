import { COLOR_CARD, COLOR_TEXT_SEC } from './constants';

export interface SpectrumBarsProps {
  /** 0~1 band levels */
  bands01: number[];
  /** 0~1 peak caps (optional) */
  peaks01?: number[];
  /** target bar count for rendering (interpolate bands to this count) */
  renderBarCount?: number;
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

interface BindPeak {
  bands: number[];
  peaks: number[];
}

@Component
export struct SpectrumBars {
  @Prop bands01: number[] = [];
  @Prop peaks01: number[] = [];
  @Prop renderBarCount: number = 64;
  @Prop level01: number = 0;
  @Prop isPlaying: boolean = false;
  @Prop heightVp: number = 140;

  private canvasSettings: RenderingContextSettings = new RenderingContextSettings(true);
  private canvasCtx: CanvasRenderingContext2D = new CanvasRenderingContext2D(this.canvasSettings);

  private drawTimerId: number = -1;
  private smoothBands: number[] = [];

  private interpBands: number[] = [];
  private interpPeaks: number[] = [];

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

  private hermite(p0: number, p1: number, p2: number, p3: number, t: number): number {
    // Catmull-Rom as cubic Hermite (tension=0.5)
    const m1 = 0.5 * (p2 - p0);
    const m2 = 0.5 * (p3 - p1);
    const t2 = t * t;
    const t3 = t2 * t;
    const h00 = 2 * t3 - 3 * t2 + 1;
    const h10 = t3 - 2 * t2 + t;
    const h01 = -2 * t3 + 3 * t2;
    const h11 = t3 - t2;
    return h00 * p1 + h10 * m1 + h01 * p2 + h11 * m2;
  }

  private sampleCubic(arr: number[], x: number): number {
    const n = arr.length;
    if (n === 0) return 0;
    if (n === 1) return clamp01(arr[0]);
    const i1 = Math.floor(x);
    const t = x - i1;
    const i0 = Math.max(0, i1 - 1);
    const i2 = Math.min(n - 1, i1 + 1);
    const i3 = Math.min(n - 1, i1 + 2);
    const p0 = arr[i0];
    const p1 = arr[Math.min(n - 1, i1)];
    const p2 = arr[i2];
    const p3 = arr[i3];
    return clamp01(this.hermite(p0, p1, p2, p3, t));
  }

  private buildInterpolated(): BindPeak {
    const src = this.bands01 ?? [];
    const srcPeaks = this.peaks01 ?? [];
    const srcN = src.length;
    const dstN = Math.max(4, Math.min(256, Math.floor(this.renderBarCount ?? 64)));

    if (this.interpBands.length !== dstN) this.interpBands = new Array(dstN).fill(0);
    if (this.interpPeaks.length !== dstN) this.interpPeaks = new Array(dstN).fill(0);

    if (srcN <= 1) {
      const v = srcN === 1 ? clamp01(src[0]) : 0;
      const p = srcPeaks.length === 1 ? clamp01(srcPeaks[0]) : -1;
      for (let i = 0; i < dstN; i++) {
        this.interpBands[i] = v;
        this.interpPeaks[i] = p;
      }
      return { bands: this.interpBands, peaks: this.interpPeaks };
    }

    // map dst index to src index range [0, srcN-1]
    const scale = (srcN - 1) / (dstN - 1);
    for (let i = 0; i < dstN; i++) {
      const x = i * scale;
      this.interpBands[i] = this.sampleCubic(src, x);
      // peaks: cubic too, but never below band
      if (srcPeaks.length === srcN) {
        const pk = this.sampleCubic(srcPeaks, x);
        this.interpPeaks[i] = pk < this.interpBands[i] ? this.interpBands[i] : pk;
      } else {
        this.interpPeaks[i] = -1;
      }
    }

    return { bands: this.interpBands, peaks: this.interpPeaks };
  }

  private draw(): void {
    const ctx = this.canvasCtx;
    if (!ctx) return;
    const w = ctx.width;
    const h = ctx.height;
    if (w <= 0 || h <= 0) return;

    const built = this.buildInterpolated();
    const bands = built.bands;
    const peaks = built.peaks;
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

    // segmented background (low/mid/high)
    const segW = innerW / 3;
    ctx.fillStyle = '#121018';
    ctx.fillRect(paddingX, paddingY, segW, innerH);
    ctx.fillStyle = '#10161A';
    ctx.fillRect(paddingX + segW, paddingY, segW, innerH);
    ctx.fillStyle = '#121A10';
    ctx.fillRect(paddingX + segW * 2, paddingY, segW, innerH);

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
