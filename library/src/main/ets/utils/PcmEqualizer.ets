import type { PcmStreamDecoder } from "./AudioDecoderManager";

interface Preset {
  /** 标准预设 - 平直响应，无频率调整 */
  Default: readonly [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

  /** 民谣预设 - 增强人声和吉他 */
  Ballads: readonly [3, 5, 2, -4, 1, 2, -3, 1, 4, 5];

  /** 华语流行预设 - 优化人声和旋律 */
  Chinese: readonly [0, 0, 2, 0, 0, 4, 4, 2, 2, 5];

  /** 古典音乐预设 - 平衡各频段，突出细节 */
  Classical: readonly [2, 3, 2, 1, 0, 0, -5, -5, -5, -6];

  /** 舞曲预设 - 增强低频和高频，提升节奏感 */
  Dance: readonly [4, 3, 2, -3, 0, 0, 5, 4, 2, 0];

  /** 爵士预设 - 优化中频，突出乐器 */
  Jazz: readonly [2, 0, 2, 3, 6, 5, -1, 3, 4, 4];

  /** 流行音乐预设 - 增强低频和高频 */
  Pop: readonly [5, 2, 1, -1, -5, -5, -2, 1, 2, 4];

  /** R&B 预设 - 增强低频和中高频 */
  RnB: readonly [1, 4, 5, 3, -2, -2, 2, 3, 5, 5];

  /** 摇滚预设 - 增强低频和高频，提升冲击力 */
  Rock: readonly [6, 4, 4, 2, 0, 1, 3, 3, 5, 4];
}

/**
 * 均衡器预设常量对象
 *
 * @remarks
 * 每个预设包含 10 个频段的增益值（单位：dB）：
 *
 * - **Default** - 标准预设，平直响应，所有频段增益均为 0 dB
 * - **Ballads** - 民谣预设，增强人声和吉他频段（低中频段 +3~5dB）
 * - **Chinese** - 华语流行预设，优化人声和旋律（中高频段 +2~4dB）
 * - **Classical** - 古典音乐预设，平衡各频段突出细节，衰减中高频段（-5~-6dB）
 * - **Dance** - 舞曲预设，增强低频和高频，提升节奏感和冲击力
 * - **Jazz** - 爵士预设，优化中频突出乐器（中高频段 +3~6dB）
 * - **Pop** - 流行音乐预设，增强低频和高频（低频 +5dB，高频 +4dB）
 * - **RnB** - R&B 预设，增强低频和中高频（低频 +1~5dB，中高频 +2~5dB）
 * - **Rock** - 摇滚预设，增强低频和高频提升冲击力（低频 +6dB，高频 +4dB）
 *
 * 使用方式非常简单，直接通过预设常量访问：
 *
 * @example
 * ```typescript
 * import { EqPreset, PcmEqualizer } from '@ospark/free-pcm';
 *
 * const equalizer = new PcmEqualizer();
 *
 * // 应用流行音乐预设
 * equalizer.setGainsDb(EqPreset.Pop);
 *
 * // 应用摇滚预设
 * equalizer.setGainsDb(EqPreset.Rock);
 *
 * // 应用标准预设
 * equalizer.setGainsDb(EqPreset.Default);
 * ```
 */
export const EqPreset: Preset = {
  /** 标准预设 - 平直响应，无频率调整 */
  Default: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  /** 民谣预设 - 增强人声和吉他 */
  Ballads: [3, 5, 2, -4, 1, 2, -3, 1, 4, 5],
  /** 华语流行预设 - 优化人声和旋律 */
  Chinese: [0, 0, 2, 0, 0, 4, 4, 2, 2, 5],
  /** 古典音乐预设 - 平衡各频段，突出细节 */
  Classical: [2, 3, 2, 1, 0, 0, -5, -5, -5, -6],
  /** 舞曲预设 - 增强低频和高频，提升节奏感 */
  Dance: [4, 3, 2, -3, 0, 0, 5, 4, 2, 0],
  /** 爵士预设 - 优化中频，突出乐器 */
  Jazz: [2, 0, 2, 3, 6, 5, -1, 3, 4, 4],
  /** 流行音乐预设 - 增强低频和高频 */
  Pop: [5, 2, 1, -1, -5, -5, -2, 1, 2, 4],
  /** R&B 预设 - 增强低频和中高频 */
  RnB: [1, 4, 5, 3, -2, -2, 2, 3, 5, 5],
  /** 摇滚预设 - 增强低频和高频，提升冲击力 */
  Rock: [6, 4, 4, 2, 0, 1, 3, 3, 5, 4],
};

/**
 * 10 段图形均衡器工具类
 *
 * 此类提供了音频均衡器的完整功能：
 * - 10 个固定频段的增益控制（31Hz ~ 16kHz）
 * - 9 种预设风格
 * - 自定义增益设置
 * - 应用到解码器
 *
 * @remarks
 * - 每个频段的增益范围：-24 ~ +24 dB
 * - 频段采用 ISO 标准频率：31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz
 * - 使用 RBJ peaking EQ biquad 滤波器实现
 * - 支持单声道和立体声
 *
 * @example
 * ```typescript
 * import { PcmEqualizer, EqPreset } from '@ospark/free-pcm';
 *
 * // 创建均衡器实例
 * const equalizer = new PcmEqualizer();
 *
 * // 应用预设（推荐方式）
 * equalizer.setGainsDb(EqPreset.Pop);
 *
 * // 设置自定义增益
 * equalizer.setGainsDb([5, 4, 3, 1, 0, -1, 1, 3, 4, 5]);
 *
 * // 获取当前增益
 * const gains = equalizer.getGainsDb();
 * console.log('当前增益:', gains);
 * ```
 */
export class PcmEqualizer {
  private gainsDb: number[] = EqPreset.Default.slice();

  /**
   * 获取当前 10 段增益值
   *
   * @returns 10 个增益值的数组副本（单位：dB）
   *
   * @remarks
   * - 返回的是数组的副本，修改不会影响内部状态
   * - 索引 0-9 对应频率：31Hz, 62Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz
   *
   * @example
   * ```typescript
   * const gains = equalizer.getGainsDb();
   * console.log(`31Hz 增益: ${gains[0]} dB`);
   * console.log(`16kHz 增益: ${gains[9]} dB`);
   * ```
   */
  public getGainsDb(): number[] {
    return this.gainsDb.slice();
  }

  /**
   * 设置 10 段增益值
   *
   * @param gainsDb - 10 个增益值的数组（单位：dB）
   *
   * @remarks
   * - 数组长度必须为 10
   * - 每个值会被限制在 -24 ~ +24 dB 范围内
   * - 索引 0-9 对应频率：31Hz, 62Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz
   *
   * @throws
   * - 数组长度不为 10
   *
   * @example
   * ```typescript
   * // 设置为平直响应
   * equalizer.setGainsDb([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
   *
   * // 设置为 V型响应（增强低频和高频）
   * equalizer.setGainsDb([5, 4, 3, 1, 0, -1, 1, 3, 4, 5]);
   *
   * // 增强低音
   * equalizer.setGainsDb([6, 5, 4, 2, 0, 0, 0, 0, 0, 0]);
   *
   * // 应用预设（推荐方式）
   * import { EqPreset } from '@ospark/free-pcm';
   * equalizer.setGainsDb(EqPreset.Pop);
   * ```
   */
  public setGainsDb(gainsDb: number[]): void {
    if (!gainsDb || gainsDb.length !== 10) {
      throw new Error("EQ 增益必须是包含 10 个数字的数组");
    }
    this.gainsDb = gainsDb.map((v) => clamp(v, -24, 24));
  }

  /**
    * 设置单个频段的增益值
    *
    * @param index - 频段索引（0-9）
    * @param gainDb - 增益值（单位：dB，范围：-24 ~ +24）
    * @returns 更新后的增益数组副本
    *
    * @remarks
    * - 频段索引对应频率：0=31Hz, 1=62Hz, 2=125Hz, 3=250Hz, 4=500Hz, 5=1kHz, 6=2kHz, 7=4kHz, 8=8kHz, 9=16kHz
    * - 此方法会更新内部增益状态并返回新的增益数组副本
    *
    * @throws
    * - 索引不在 0-9 范围内
    *
    * @example
    * ```typescript
    * // 将第一个频段（31Hz）增益设置为 +6 dB
    * equalizer.setBandGain(0, 6);
    *
    * // 将第五个频段（500Hz）增益设置为 -3 dB
    * equalizer.setBandGain(4, -3);
    *
    * // 获取更新后的增益数组
    * const gains = equalizer.getGainsDb();
    * console.log('更新后的增益:', gains);
    * ```
    */
  public setBandGain(index: number, gainDb: number): number[] {
    if (index < 0 || index >= 10) {
      throw new Error("EQ band index out of range");
    }
    const next = this.gainsDb.slice();
    next[index] = clamp(gainDb, -24, 24);
    this.gainsDb = next;
    return this.getGainsDb();
  }

  /**
   * 将当前均衡器设置应用到解码器
   *
   * @param decoder - PCM 流解码器实例
   *
   * @remarks
   * - 此方法会自动启用解码器的均衡器
   * - 将当前增益值应用到解码器
   * - 如果 decoder 为 null，此方法不执行任何操作
   *
   * @example
   * ```typescript
   * import { PcmEqualizer, EqPreset } from '@ospark/free-pcm';
   * import { PcmDecoderTool } from '@ospark/free-pcm';
   *
   * const equalizer = new PcmEqualizer();
   * equalizer.setGainsDb(EqPreset.Jazz);
   *
   * const decoder = PcmDecoderTool.getInstance().createStreamDecoder('/path/to/audio.mp3');
   * await decoder.ready;
   *
   * // 应用均衡器设置
   * equalizer.applyToDecoder(decoder);
   * ```
   */
  public applyToDecoder(decoder: PcmStreamDecoder | null): void {
    if (!decoder) {
      return;
    }
    decoder.setEqEnabled(true);
    decoder.setEqGains(this.gainsDb);
  }
}

/**
 * 限制数值在指定范围内
 *
 * @param value - 输入值
 * @param min - 最小值
 * @param max - 最大值
 * @returns 限制后的值
 *
 * @private
 */
function clamp(value: number, min: number, max: number): number {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}
