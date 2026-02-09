/**
 * Free PCM 音频解码库
 *
 * 本库提供了音频解码功能，支持多种音频格式（MP3、FLAC、WAV、AAC、OGG、Opus 等）
 *
 * @module @ospark/free-pcm
 *
 * @example
 * ```typescript
 * import { PcmDecoderTool, AudioRendererPlayer, PcmEqualizer, EqPreset } from '@ospark/free-pcm';
 *
 * // 创建解码器（推荐使用 PcmDecoderTool.getInstance()）
 * const decoderTool = PcmDecoderTool.getInstance();
 * const decoder = decoderTool.createStreamDecoder('/path/to/audio.mp3');
 *
 * // 等待解码器准备
 * const info = await decoder.ready;
 *
 * // 创建播放器
 * const player = new AudioRendererPlayer();
 * await player.play(decoder, info);
 * ```
 */

// 导出工具类
export { AudioDecoderManager } from './src/main/ets/utils/AudioDecoderManager';
export { AudioRendererPlayer } from './src/main/ets/utils/AudioRendererPlayer';
export { PcmDecoderTool } from './src/main/ets/utils/PcmDecoderTool';

// 导出均衡器相关
export { PcmEqualizer, EqPreset } from './src/main/ets/utils/PcmEqualizer';

// 导出类型
export type {
  DecodeAudioProgress,
  PcmStreamInfo,
  PcmStreamDecoderOptions,
  PcmStreamDecoderCallbacks,
  PcmStreamDecoder
} from './src/main/ets/utils/AudioDecoderManager';