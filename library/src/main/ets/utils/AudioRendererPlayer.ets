/**
 * 音频播放器类 - 封装 AudioRenderer 实现拉取式音频播放
 *
 * 此类配合 PcmStreamDecoder 实现流式音频播放：
 * 1. 创建 AudioRenderer
 * 2. 设置 writeData 回调从解码器拉取 PCM 数据
 * 3. 提供播放控制（播放、暂停、停止、音量等）
 *
 * @remarks
 * - 此类是库的高级封装，简化了 AudioRenderer 的使用
 * - 自动处理采样率、声道数的映射
 * - 提供音量、播放速度等控制功能
 *
 * @example
 * ```typescript
 * import { AudioRendererPlayer } from '@ospark/free-pcm';
 * import freePcm from '@ospark/free-pcm';
 *
 * const player = new AudioRendererPlayer();
 * const decoder = freePcm.createPcmStreamDecoder('/path/to/audio.mp3');
 *
 * // 等待解码器准备
 * const info = await decoder.ready;
 *
 * // 开始播放
 * await player.play(decoder, info);
 *
 * // 设置音量
 * await player.setVolume(0.5); // 50% 音量
 *
 * // 暂停播放
 * await player.pause();
 *
 * // 恢复播放
 * await player.resume();
 *
 * // 停止播放
 * await player.stop();
 * ```
 */

import { audio } from '@kit.AudioKit';
import { BusinessError } from '@kit.BasicServicesKit';

import type { PcmStreamDecoder, PcmStreamInfo } from './AudioDecoderManager';

export class AudioRendererPlayer {
  private renderer: audio.AudioRenderer | null = null;
  private decoder: PcmStreamDecoder | null = null;
  private streamInfo: PcmStreamInfo | null = null;

  // 位置追踪和时间更新相关
  private timeUpdateTimerId: number = -1;
  private isPlaying_: boolean = false;
  private onTimeUpdateCallback: ((positionMs: number) => void) | null = null;

  /**
   * 映射采样率到系统枚举值
   *
   * @param sr - 采样率（Hz）
   * @returns 对应的 AudioSamplingRate 枚举值
   *
   * @remarks
   * 支持的采样率：8000, 11025, 12000, 16000, 22050, 24000,
   * 32000, 44100, 48000, 64000, 88200, 96000, 176400, 192000
   *
   * @private
   */
  private mapSamplingRate(sr: number): audio.AudioSamplingRate {
    switch (sr) {
      case 8000: return audio.AudioSamplingRate.SAMPLE_RATE_8000;
      case 11025: return audio.AudioSamplingRate.SAMPLE_RATE_11025;
      case 12000: return audio.AudioSamplingRate.SAMPLE_RATE_12000;
      case 16000: return audio.AudioSamplingRate.SAMPLE_RATE_16000;
      case 22050: return audio.AudioSamplingRate.SAMPLE_RATE_22050;
      case 24000: return audio.AudioSamplingRate.SAMPLE_RATE_24000;
      case 32000: return audio.AudioSamplingRate.SAMPLE_RATE_32000;
      case 44100: return audio.AudioSamplingRate.SAMPLE_RATE_44100;
      case 48000: return audio.AudioSamplingRate.SAMPLE_RATE_48000;
      case 64000: return audio.AudioSamplingRate.SAMPLE_RATE_64000;
      case 88200: return audio.AudioSamplingRate.SAMPLE_RATE_88200;
      case 96000: return audio.AudioSamplingRate.SAMPLE_RATE_96000;
      case 176400: return audio.AudioSamplingRate.SAMPLE_RATE_176400;
      case 192000: return audio.AudioSamplingRate.SAMPLE_RATE_192000;
      default: return audio.AudioSamplingRate.SAMPLE_RATE_48000;
    }
  }

  /**
   * 映射声道数到系统枚举值
   *
   * @param count - 声道数（1 或 2）
   * @returns 对应的 AudioChannel 枚举值
   *
   * @private
   */
  private mapChannels(count: number): audio.AudioChannel {
    if (count === 1) {
      return audio.AudioChannel.CHANNEL_1;
    }
    return audio.AudioChannel.CHANNEL_2;
  }

  /**
   * 开始播放音频
   *
   * @param decoder - PCM 流解码器实例
   * @param info - PCM 流信息（包含采样率、声道数等）
   * @returns Promise<void> 播放开始后 resolve
   *
   * @remarks
   * - 此方法会先停止当前播放（如果正在播放）
   * - 创建 AudioRenderer 并设置音频参数
   * - 注册 writeData 回调，从解码器拉取 PCM 数据
   * - 自动启动 AudioRenderer 开始播放
   *
   * @throws
   * - AudioRenderer 创建失败
   * - AudioRenderer 启动失败
   */
  public async play(decoder: PcmStreamDecoder, info: PcmStreamInfo): Promise<void> {
    await this.stop();

    this.decoder = decoder;
    this.streamInfo = info;

    const audioStreamInfo: audio.AudioStreamInfo = {
      samplingRate: this.mapSamplingRate(info.sampleRate),
      channels: this.mapChannels(info.channelCount),
      sampleFormat: info.sampleFormatCode === 3
        ? audio.AudioSampleFormat.SAMPLE_FORMAT_S32LE
        : audio.AudioSampleFormat.SAMPLE_FORMAT_S16LE,
      encodingType: audio.AudioEncodingType.ENCODING_TYPE_RAW
    };
    const audioRendererInfo: audio.AudioRendererInfo = {
      usage: audio.StreamUsage.STREAM_USAGE_MUSIC,
      rendererFlags: 0
    };
    const audioRendererOptions: audio.AudioRendererOptions = {
      streamInfo: audioStreamInfo,
      rendererInfo: audioRendererInfo
    };

    try {
      this.renderer = await audio.createAudioRenderer(audioRendererOptions);

      // API 12+: writeData callback can return VALID/INVALID.
      this.renderer.on('writeData', (buffer: ArrayBuffer) => {
        const d = this.decoder;
        if (!d) {
          return audio.AudioDataCallbackResult.INVALID;
        }

        // Preferred on API 12+: only consume ring when we can fully fill.
        if (d.fillForWriteData) {
          const n = d.fillForWriteData(buffer);
          return n > 0 ? audio.AudioDataCallbackResult.VALID : audio.AudioDataCallbackResult.INVALID;
        }

        // Fallback (legacy): consumes and pads with zeros, always treated as valid.
        d.fill(buffer);
        return audio.AudioDataCallbackResult.VALID;
      });
      await this.renderer.start();
      this.isPlaying_ = true;
      this.startTimeUpdate();
    } catch (err) {
      const e = err as BusinessError;
      throw new Error(`AudioRenderer start failed: ${e.message}`);
    }
  }

  /**
   * 获取音频流时长
   *
   * @returns 音频时长（毫秒），未知时返回 0
   *
   * @remarks
   * 时长来源于解码器的信息，可能为 0（未知）
   */
  public getDurationMs(): number {
    return this.streamInfo?.durationMs ?? 0;
  }

  /**
   * 获取当前播放位置
   *
   * @returns 当前播放位置（毫秒），未播放时返回 0
   *
   * @remarks
   * 位置来自解码器的 getPosition() 方法
   */
  public getCurrentPosition(): number {
    if (!this.decoder) {
      return 0;
    }
    return this.decoder.getPosition();
  }

  /**
   * 跳转到指定播放位置
   *
   * @param positionMs - 目标位置（毫秒）
   * @returns Promise<void> 跳转完成后 resolve
   *
   * @remarks
   * - 支持任意位置跳转（包括往前跳转）
   * - 支持本地文件和 URL 流式播放
   * - 跳转精度目标：< 50ms
   *
   * @throws
   * - 解码器未初始化
   * - 目标位置无效
   */
  public async seekTo(positionMs: number): Promise<void> {
    if (!this.decoder) {
      throw new Error('Decoder not initialized');
    }

    if (positionMs < 0) {
      throw new Error('positionMs must be >= 0');
    }

    const wasPlaying = this.isPlaying_;

    // 1. 暂停播放
    if (wasPlaying) {
      await this.pause();
    }

    // 2. 刷新 AudioRenderer 缓冲区
    await this.flush();

    // 3. 调用解码器 Seek
    // Prefer async variant so URL seeks can wait until post-seek data is ready.
    if (this.decoder.seekToAsync) {
      await this.decoder.seekToAsync(positionMs);
    } else {
      this.decoder.seekTo(positionMs);
    }

    // 4. 如果之前在播放，恢复播放
    if (wasPlaying) {
      await this.resume();
    }
  }

  /**
   * 设置时间更新回调
   *
   * @param callback - 时间更新回调函数，接收当前播放位置（毫秒）
   *
   * @remarks
   * - 默认每 100ms 触发一次
   * - 可以通过 setTimeUpdateEnabled() 控制启用/禁用
   * - 播放开始时自动启用，暂停/停止时自动禁用
   */
  public setOnTimeUpdate(callback: (positionMs: number) => void): void {
    this.onTimeUpdateCallback = callback;
  }

  /**
   * 启用或禁用时间更新事件
   *
   * @param enabled - 是否启用时间更新
   * @param intervalMs - 更新间隔（毫秒），默认 100ms
   *
   * @remarks
   * - 默认启用，播放时自动触发
   * - intervalMs 建议：50ms~200ms
   * - 设置为 false 会停止时间更新
   */
  public setTimeUpdateEnabled(enabled: boolean, intervalMs: number = 100): void {
    if (enabled && this.isPlaying_) {
      this.startTimeUpdate(intervalMs);
    } else {
      this.stopTimeUpdate();
    }
  }

  /**
   * 启动时间更新定时器
   *
   * @param intervalMs - 更新间隔（毫秒），默认 100ms
   * @private
   */
  private startTimeUpdate(intervalMs: number = 100): void {
    this.stopTimeUpdate();
    this.timeUpdateTimerId = setInterval(() => {
      if (this.decoder && this.onTimeUpdateCallback) {
        const position = this.decoder.getPosition();
        this.onTimeUpdateCallback(position);
      }
    }, intervalMs);
  }

  /**
   * 停止时间更新定时器
   * @private
   */
  private stopTimeUpdate(): void {
    if (this.timeUpdateTimerId !== -1) {
      clearInterval(this.timeUpdateTimerId);
      this.timeUpdateTimerId = -1;
    }
  }

  /**
   * 获取当前播放状态
   *
   * @returns AudioState 枚举值，未创建时返回 null
   *
   * @remarks
   * 可能的状态：
   * - STATE_NEW: 新建
   * - STATE_PREPARED: 准备就绪
   * - STATE_RUNNING: 正在播放
   * - STATE_PAUSED: 已暂停
   * - STATE_STOPPED: 已停止
   * - STATE_RELEASED: 已释放
   */
  public getState(): audio.AudioState | null {
    return this.renderer?.state ?? null;
  }

  /**
   * 获取当前音量
   *
   * @returns 音量值（0.0 ~ 1.0），未创建时返回 null
   *
   * @remarks
   * - 0.0 = 静音
   * - 1.0 = 最大音量
   * - 默认值通常是 1.0
   */
  public getVolume(): number | null {
    if (!this.renderer) {
      return null;
    }
    return this.renderer.getVolume();
  }

  /**
   * 设置音量
   *
   * @param volume - 音量值（0.0 ~ 1.0）
   * @returns Promise<void> 设置完成后 resolve
   *
   * @remarks
   * - 0.0 = 静音
   * - 1.0 = 最大音量
   * - 值会被限制在 0.0 ~ 1.0 范围内
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public async setVolume(volume: number): Promise<void> {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    await this.renderer.setVolume(volume);
  }

  /**
   * 平滑渐变音量到目标值
   *
   * @param volume - 目标音量（0.0 ~ 1.0）
   * @param durationMs - 渐变时长（毫秒）
   *
   * @remarks
   * - 音量会在指定时间内平滑地从当前值渐变到目标值
   * - 渐变是线性的
   * - 适用于淡入淡出效果
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public setVolumeWithRamp(volume: number, durationMs: number): void {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    this.renderer.setVolumeWithRamp(volume, durationMs);
  }

  /**
   * 获取当前播放速度
   *
   * @returns 播放速度倍率，未创建时返回 null
   *
   * @remarks
   * - 1.0 = 正常速度
   * - 2.0 = 2 倍速
   * - 0.5 = 0.5 倍速（慢放）
   * - 支持范围：0.25 ~ 4.0
   */
  public getSpeed(): number | null {
    if (!this.renderer) {
      return null;
    }
    return this.renderer.getSpeed();
  }

  /**
   * 设置播放速度
   *
   * @param speed - 播放速度倍率（0.25 ~ 4.0）
   *
   * @remarks
   * - 1.0 = 正常速度
   * - 2.0 = 2 倍速
   * - 0.5 = 0.5 倍速（慢放）
   * - 支持范围：0.25 ~ 4.0
   * - 变速播放不会改变音调（如果系统支持）
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public setSpeed(speed: number): void {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    this.renderer.setSpeed(speed);
  }

  /**
   * 暂停播放
   *
   * @returns Promise<void> 暂停完成后 resolve
   *
   * @remarks
   * - 暂停后可以通过 resume() 恢复播放
   * - 暂停不会释放资源，可以快速恢复
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public async pause(): Promise<void> {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    await this.renderer.pause();
    this.isPlaying_ = false;
    this.stopTimeUpdate();
  }

  /**
   * 恢复播放
   *
   * @returns Promise<void> 恢复完成后 resolve
   *
   * @remarks
   * - 从暂停状态恢复播放
   * - 如果未暂停，此方法会启动播放
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public async resume(): Promise<void> {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    await this.renderer.start();
    this.isPlaying_ = true;
    this.startTimeUpdate();
  }

  /**
   * 排空播放缓冲区
   *
   * @returns Promise<void> 排空完成后 resolve
   *
   * @remarks
   * - 等待已写入的数据全部播放完成
   * - 通常在停止播放前调用
   * - 不影响播放状态
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public async drain(): Promise<void> {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    await this.renderer.drain();
  }

  /**
   * 刷新播放缓冲区
   *
   * @returns Promise<void> 刷新完成后 resolve
   *
   * @remarks
   * - 立即丢弃缓冲区中未播放的数据
   * - 通常在跳转或错误处理时调用
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public async flush(): Promise<void> {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    await this.renderer.flush();
  }

  /**
   * 获取 AudioRenderer 建议的最小缓冲区大小
   *
   * @returns 最小缓冲区大小（字节），未创建时返回 0
   *
   * @remarks
   * - 这是 AudioRenderer 建议的最小缓冲区大小
   * - 可以根据此值调整环形缓冲区大小
   */
  public getBufferSizeSync(): number {
    if (!this.renderer) {
      return 0;
    }
    return this.renderer.getBufferSizeSync();
  }

  /**
   * 获取音频时间
   *
   * @returns 基于单调纳秒系统定时器的音频时间（纳秒）
   *
   * @remarks
   * - 用于精确的音频同步
   * - 时间基于系统单调时钟，不会受系统时间调整影响
   *
   * @throws
   * - AudioRenderer 未初始化
   */
  public async getAudioTimeNs(): Promise<number> {
    if (!this.renderer) {
      throw new Error('AudioRenderer not initialized');
    }
    return await this.renderer.getAudioTime();
  }

  /**
   * 停止播放并释放资源
   *
   * @returns Promise<void> 停止完成后 resolve
   *
   * @remarks
   * - 停止解码器
   * - 停止并释放 AudioRenderer
   * - 清理所有内部状态
   * - 调用后需要重新调用 play() 才能播放
   */
  public async stop(): Promise<void> {
    // 停止时间更新定时器
    this.stopTimeUpdate();
    this.isPlaying_ = false;

    try {
      if (this.decoder) {
        this.decoder.close();
      }
    } catch (err) {
      // ignore
    } finally {
      this.decoder = null;
    }

    try {
      if (this.renderer) {
        await this.renderer.stop();
        await this.renderer.release();
      }
    } catch (err) {
      // ignore
    } finally {
      this.renderer = null;
      this.streamInfo = null;
    }
  }

  /**
   * 获取AudioRenderer实例
   *
   * @returns <audio.AudioRenderer> AudioRenderer实例
   */
  public getRenderer(): audio.AudioRenderer | null {
    return this.renderer;
  }

  /**
   * 获取解码器实例
   *
   * @returns <PcmStreamDecoder> 解码器实例
   */
  public getDecoder(): PcmStreamDecoder | null {
    return this.decoder;
  }

  /**
   * 获取PCM流信息
   *
   * @returns <PcmStreamInfo> PCM流信息
   */
  public getStreamInfo(): PcmStreamInfo | null {
    return this.streamInfo;
  }
}
