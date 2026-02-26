/**
 * FFT 频谱分析（用于 entry 可视化）
 * - FFT size: 1024
 * - Window: Hann
 * - Output bars: 16 (log-spaced)
 *
 * 设计：
 * - 音频回调线程：只 push PCM（轻量，不做 FFT）
 * - UI/定时器线程：每 33ms computeBars()，取最新窗口做 FFT + 分桶
 */

export interface FftSpectrumOptions {
  fftSize?: number;       // default 1024
  barCount?: number;      // default 16
  sampleRate?: number;    // default 48000 (will be set from streamInfo)
  sampleFormatCode?: number; // 1=S16LE, 3=S32LE
  channelCount?: number;  // default 2

  // dynamics tuning
  minRelDb?: number;      // default -55
  gamma?: number;         // default 0.6
  attack?: number;        // default 0.75
  release?: number;       // default 0.25
  peakHoldFrames?: number;// default 10
  gravity?: number;       // default 0.03

  // spectral tilt compensation
  tiltDbPerOct?: number;  // default 0 (e.g. +4.5)
  tiltRefHz?: number;     // default 1000
}

function clamp01(v: number): number {
  if (!Number.isFinite(v)) return 0;
  if (v < 0) return 0;
  if (v > 1) return 1;
  return v;
}

/** In-place radix-2 Cooley–Tukey FFT (complex) */
function fftRadix2(re: Float32Array, im: Float32Array): void {
  const n = re.length;
  // bit-reversal permutation
  let j = 0;
  for (let i = 1; i < n; i++) {
    let bit = n >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
    if (i < j) {
      const tr = re[i]; re[i] = re[j]; re[j] = tr;
      const ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }

  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2.0 * Math.PI / len;
    const wlenRe = Math.cos(ang);
    const wlenIm = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let wRe = 1.0;
      let wIm = 0.0;
      const half = len >> 1;
      for (let k = 0; k < half; k++) {
        const uRe = re[i + k];
        const uIm = im[i + k];
        const vRe = re[i + k + half] * wRe - im[i + k + half] * wIm;
        const vIm = re[i + k + half] * wIm + im[i + k + half] * wRe;

        re[i + k] = uRe + vRe;
        im[i + k] = uIm + vIm;
        re[i + k + half] = uRe - vRe;
        im[i + k + half] = uIm - vIm;

        // w *= wlen
        const nextWRe = wRe * wlenRe - wIm * wlenIm;
        const nextWIm = wRe * wlenIm + wIm * wlenRe;
        wRe = nextWRe;
        wIm = nextWIm;
      }
    }
  }
}

export class FftSpectrumAnalyzer {
  private fftSize: number;
  private barCount: number;

  private sampleRate: number;
  private sampleFormatCode: number;
  private channelCount: number;

  // ring buffer of mono float samples
  private ring: Float32Array;
  private ringWrite: number = 0;
  private ringFilled: number = 0;

  // window + fft buffers
  private window: Float32Array;
  private re: Float32Array;
  private im: Float32Array;

  // magnitude temp (half spectrum)
  private mag: Float32Array;

  // bars output + dynamics
  private bars01: Float32Array;       // smoothed bar level 0..1
  private peaks01: Float32Array;      // peak cap 0..1
  private peakHold: Int16Array;       // frames remaining

  private tmpDbWeighted: Float32Array; // per-bar weighted dB temp

  private level01: number = 0;

  private minRelDb: number;
  private gamma: number;
  private attack: number;
  private release: number;
  private peakHoldFrames: number;
  private gravity: number;

  // precomputed bin ranges for each bar
  private barBinStart: Int32Array;
  private barBinEnd: Int32Array;

  private tiltDbPerOct: number;
  private tiltRefHz: number;
  private tiltDbByBar: Float32Array;

  constructor(options?: FftSpectrumOptions) {
    this.fftSize = Math.max(256, Math.min(4096, (options?.fftSize ?? 1024) | 0));
    // ensure power of two
    let p2 = 1;
    while (p2 < this.fftSize) p2 <<= 1;
    this.fftSize = p2;

    this.barCount = Math.max(4, Math.min(64, (options?.barCount ?? 16) | 0));

    this.sampleRate = options?.sampleRate ?? 48000;
    this.sampleFormatCode = options?.sampleFormatCode ?? 1;
    this.channelCount = Math.max(1, Math.min(2, (options?.channelCount ?? 2) | 0));

    // ring holds 4 FFT windows worth (for overlap)
    this.ring = new Float32Array(this.fftSize * 4);
    this.window = new Float32Array(this.fftSize);
    this.re = new Float32Array(this.fftSize);
    this.im = new Float32Array(this.fftSize);
    this.mag = new Float32Array(this.fftSize >> 1);

    this.bars01 = new Float32Array(this.barCount);
    this.peaks01 = new Float32Array(this.barCount);
    this.peakHold = new Int16Array(this.barCount);
    this.tmpDbWeighted = new Float32Array(this.barCount);

    this.barBinStart = new Int32Array(this.barCount);
    this.barBinEnd = new Int32Array(this.barCount);

    this.tiltDbPerOct = Number.isFinite(options?.tiltDbPerOct) ? (options!.tiltDbPerOct as number) : 0;
    this.tiltRefHz = Number.isFinite(options?.tiltRefHz) ? (options!.tiltRefHz as number) : 1000;
    if (this.tiltRefHz <= 0) this.tiltRefHz = 1000;
    this.tiltDbByBar = new Float32Array(this.barCount);

    this.minRelDb = Number.isFinite(options?.minRelDb) ? (options!.minRelDb as number) : -55;
    this.gamma = Number.isFinite(options?.gamma) ? (options!.gamma as number) : 0.6;
    this.attack = Number.isFinite(options?.attack) ? (options!.attack as number) : 0.75;
    this.release = Number.isFinite(options?.release) ? (options!.release as number) : 0.25;
    this.peakHoldFrames = Number.isFinite(options?.peakHoldFrames) ? (options!.peakHoldFrames as number) : 10;
    this.gravity = Number.isFinite(options?.gravity) ? (options!.gravity as number) : 0.03;

    this.recomputeBins();
  }

  public reset(): void {
    this.ring.fill(0);
    this.ringWrite = 0;
    this.ringFilled = 0;
    this.bars01.fill(0);
    this.peaks01.fill(0);
    this.peakHold.fill(0);
    this.level01 = 0;
  }

  public setStreamFormat(sampleRate: number, sampleFormatCode: number, channelCount: number): void {
    if (Number.isFinite(sampleRate) && sampleRate > 0) {
      if (this.sampleRate !== sampleRate) {
        this.sampleRate = sampleRate;
        this.recomputeBins();
      }
    }
    this.sampleFormatCode = sampleFormatCode;
    this.channelCount = Math.max(1, Math.min(2, (channelCount | 0)));
  }

  /**
   * Enable/disable spectral tilt compensation at runtime.
   * @remarks
   * - This only recomputes per-bar tilt weights; FFT buffers are unchanged.
   */
  public setTilt(enabled: boolean, tiltDbPerOct: number = 4.5, tiltRefHz: number = 1000): void {
    if (!enabled) {
      this.tiltDbPerOct = 0;
      this.tiltRefHz = tiltRefHz > 0 ? tiltRefHz : 1000;
      this.recomputeTilt();
      return;
    }
    this.tiltDbPerOct = Number.isFinite(tiltDbPerOct) ? tiltDbPerOct : 4.5;
    this.tiltRefHz = (Number.isFinite(tiltRefHz) && tiltRefHz > 0) ? tiltRefHz : 1000;
    this.recomputeTilt();
  }

  public getTiltEnabled(): boolean {
    return this.tiltDbPerOct !== 0;
  }

  /**
   * Push PCM chunk into ring as mono float samples.
   * Keep it light: no allocations.
   */
  public pushPcm(pcmBuffer: ArrayBuffer, bytesWritten: number): void {
    if (!pcmBuffer || bytesWritten <= 0) return;
    const usable = Math.min(bytesWritten, pcmBuffer.byteLength);
    if (usable <= 0) return;
    const ch = this.channelCount;
    const bytesPerSample = (this.sampleFormatCode === 3) ? 4 : 2;
    const frameBytes = bytesPerSample * ch;
    if (usable < frameBytes) return;

    if (bytesPerSample === 2) {
      const view = new Int16Array(pcmBuffer, 0, Math.floor(usable / 2));
      // view is interleaved; take L only (fast) or average L/R (better)
      if (ch === 1) {
        for (let i = 0; i < view.length; i++) {
          this.pushSample(view[i] / 32768);
        }
      } else {
        for (let i = 0; i + 1 < view.length; i += 2) {
          const x = (view[i] + view[i + 1]) / 65536; // average then normalize
          this.pushSample(x);
        }
      }
      return;
    }

    const view32 = new Int32Array(pcmBuffer, 0, Math.floor(usable / 4));
    if (ch === 1) {
      for (let i = 0; i < view32.length; i++) {
        this.pushSample(view32[i] / 2147483648);
      }
    } else {
      for (let i = 0; i + 1 < view32.length; i += 2) {
        const x = (view32[i] + view32[i + 1]) / 4294967296;
        this.pushSample(x);
      }
    }
  }

  private pushSample(x: number): void {
    const idx = this.ringWrite;
    this.ring[idx] = x;
    this.ringWrite = (idx + 1) % this.ring.length;
    if (this.ringFilled < this.ring.length) this.ringFilled++;
  }

  /**
   * Compute bars from latest FFT window.
   * Call at ~30fps from UI/timer.
   */
  public computeBars(): void {
    if (this.ringFilled < this.fftSize) {
      return;
    }

    // gather latest fftSize samples into window (oldest->newest)
    const ringLen = this.ring.length;
    let start = this.ringWrite - this.fftSize;
    if (start < 0) start += ringLen;

    for (let i = 0; i < this.fftSize; i++) {
      const srcIdx = (start + i) % ringLen;
      const w = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (this.fftSize - 1))); // Hann
      const v = this.ring[srcIdx] * w;
      this.re[i] = v;
      this.im[i] = 0;
    }

    fftRadix2(this.re, this.im);

    // magnitude (power)
    const half = this.fftSize >> 1;
    for (let i = 0; i < half; i++) {
      const r = this.re[i];
      const ii = this.im[i];
      // power spectrum
      this.mag[i] = r * r + ii * ii;
    }

    // bars: log bins with dB mapping
    // Use RELATIVE dB (per-frame) to avoid "everything pegged" on loud masters.
    const minRelDb = this.minRelDb; // dynamic range below the current frame peak
    const maxRelDb = 0;
    const eps = 1e-12;

    // Compute per-frame peak on WEIGHTED bars (Approach A) so tilt is consistent with normalization.
    let framePeakDbWeighted = -1e9;

    for (let b = 0; b < this.barCount; b++) {
      const s = this.barBinStart[b];
      const e = this.barBinEnd[b];
      let sum = 0;
      let cnt = 0;
      for (let k = s; k < e; k++) {
        sum += this.mag[k];
        cnt++;
      }
      const mean = cnt > 0 ? (sum / cnt) : 0;
      const db = 10 * Math.log10(mean + eps);
      const dbWeighted = db + this.tiltDbByBar[b];
      if (dbWeighted > framePeakDbWeighted) framePeakDbWeighted = dbWeighted;

      this.tmpDbWeighted[b] = dbWeighted;
    }

    if (!Number.isFinite(framePeakDbWeighted)) {
      framePeakDbWeighted = -1e9;
    }

    // Second pass: map weighted dB -> 0~1 bars with dynamics
    for (let b = 0; b < this.barCount; b++) {
      const dbWeighted = this.tmpDbWeighted[b];
      const relDb = dbWeighted - framePeakDbWeighted;
      const norm = (relDb - minRelDb) / (maxRelDb - minRelDb);

      // perceptual shaping (gamma) to keep mid-level motion visible
      const target = clamp01(Math.pow(clamp01(norm), this.gamma));

      // dynamics: attack/release on bar level
      const curBar = this.bars01[b];
      const atk = this.attack;
      const rel = this.release;
      const next = (target > curBar) ? (curBar + (target - curBar) * atk) : (curBar + (target - curBar) * rel);
      this.bars01[b] = next;

      // peak hold + gravity (cap)
      const pk = this.peaks01[b];
      if (next >= pk) {
        this.peaks01[b] = next;
        this.peakHold[b] = this.peakHoldFrames;
      } else {
        const hold = this.peakHold[b];
        if (hold > 0) {
          this.peakHold[b] = hold - 1;
        } else {
          // gravity fall
          const g = this.gravity;
          let cap = pk - g;
          if (cap < next) cap = next;
          if (cap < 0) cap = 0;
          this.peaks01[b] = cap;
        }
      }
    }

    // overall level (0~1): use peak bar with smoothing
    let m = 0;
    for (let i = 0; i < this.barCount; i++) {
      const v = this.bars01[i];
      if (v > m) m = v;
    }
    this.level01 = this.level01 + (m - this.level01) * (m > this.level01 ? 0.7 : 0.25);
  }

  /** bars 0~1 (copy) */
  public getBars01(): number[] {
    return Array.from(this.bars01);
  }

  /** peaks 0~1 (copy) */
  public getPeaks01(): number[] {
    return Array.from(this.peaks01);
  }

  public getLevel01(): number {
    return this.level01;
  }

  private recomputeBins(): void {
    // log-spaced bins from ~60Hz to ~16kHz
    const minHz = 60;
    const maxHz = Math.min(16000, this.sampleRate / 2);
    const half = this.fftSize >> 1;

    for (let b = 0; b < this.barCount; b++) {
      const t0 = b / this.barCount;
      const t1 = (b + 1) / this.barCount;
      const f0 = minHz * Math.pow(maxHz / minHz, t0);
      const f1 = minHz * Math.pow(maxHz / minHz, t1);

      let k0 = Math.floor((f0 * this.fftSize) / this.sampleRate);
      let k1 = Math.floor((f1 * this.fftSize) / this.sampleRate);

      if (k0 < 1) k0 = 1;
      if (k1 < k0 + 1) k1 = k0 + 1;
      if (k1 > half) k1 = half;
      if (k0 > half - 1) k0 = half - 1;

      this.barBinStart[b] = k0;
      this.barBinEnd[b] = k1;
    }

    this.recomputeTilt();
  }

  private recomputeTilt(): void {
    // based on current bin ranges and sampleRate
    const minHz = 60;
    const maxHz = Math.min(16000, this.sampleRate / 2);
    for (let b = 0; b < this.barCount; b++) {
      const t0 = b / this.barCount;
      const t1 = (b + 1) / this.barCount;
      const f0 = minHz * Math.pow(maxHz / minHz, t0);
      const f1 = minHz * Math.pow(maxHz / minHz, t1);
      const fc = Math.sqrt(f0 * f1);
      if (this.tiltDbPerOct !== 0) {
        const oct = Math.log2(fc / this.tiltRefHz);
        this.tiltDbByBar[b] = this.tiltDbPerOct * oct;
      } else {
        this.tiltDbByBar[b] = 0;
      }
    }
  }
}
