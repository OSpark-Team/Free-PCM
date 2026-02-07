/**
 * Free PCM 音频解码库
 *
 * 本库提供了音频解码功能，支持多种音频格式（MP3、FLAC、WAV、AAC、OGG、Opus 等），
 * 并将音频解码为 PCM 格式供播放或进一步处理。
 * 
 */

/**
 * 音频解码进度信息
 */
export type DecodeAudioProgress = {
  /**
   * 解码进度
   * - 0~1 表示已知时长下的进度百分比
   * - -1 表示时长未知（可用 ptsMs 做展示）
   */
  progress: number;
  /** 已解封装/解码到的时间戳（毫秒） */
  ptsMs: number;
  /** 总时长（毫秒），0 表示未知 */
  durationMs: number;
};

/**
 * PCM 流信息
 *
 * 在解码器准备完成后返回，用于创建 AudioRenderer
 */
export type PcmStreamInfo = {
  /** 采样率（Hz） */
  sampleRate: number;
  /** 声道数（1=单声道，2=立体声） */
  channelCount: number;
  /**
   * 采样格式
   * - 's16le': 16位小端PCM（当前仅支持此格式）
   * - 'unknown': 未知格式
   */
  sampleFormat: 's16le' | 'unknown';
  /** 音频时长（毫秒），0 表示未知 */
  durationMs: number;
};

/**
 * PCM 流解码器选项
 *
 * 用于配置解码器的行为
 */
export type PcmStreamDecoderOptions = {
  /**
   * 可选：采样率（Hz）
   * - 0 或不传表示自动从媒体流获取
   * - 建议使用 44100 或 48000
   */
  sampleRate?: number;

  /**
   * 可选：声道数
   * - 0 或不传表示自动从媒体流获取
   * - 1 = 单声道，2 = 立体声
   */
  channelCount?: number;

  /**
   * 可选：比特率（bps）
   * - 0 或不传表示不设置（推荐）
   * - 通常不需要手动设置此参数
   */
  bitrate?: number;

  /**
   * 内部 PCM 环形缓冲区大小（字节）
   * - 默认 512KB
   * - 增大此值可减少播放卡顿，但会增加内存占用
   */
  ringBytes?: number;

  /**
   * 创建时是否启用均衡器（默认 false）
   * - 启用后会在解码时实时应用均衡器
   */
  eqEnabled?: boolean;

  /**
   * 创建时设置均衡器初始 10 段增益（单位 dB）
   * - 数组长度必须为 10
   * - 每个值的范围：-24 ~ +24 dB
   * - 索引 0-9 对应频率：31Hz, 62Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz
   *
   * @example
   * eqGainsDb: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] // 平直响应
   * eqGainsDb: [5, 4, 3, 1, 0, -1, 1, 3, 4, 5] // V型响应
   */
  eqGainsDb?: number[];
};

/**
 * PCM 流解码器回调函数
 *
 * 用于监听解码过程中的事件
 */
export type PcmStreamDecoderCallbacks = {
  /**
   * 解码进度回调
   *
   * @param p - 进度信息
   *
   * @example
   * ```typescript
   * onProgress: (p) => {
   *   if (p.progress >= 0) {
   *     console.log(`解码进度: ${(p.progress * 100).toFixed(1)}%`);
   *   } else {
   *     console.log(`已解码: ${p.ptsMs}ms`);
   *   }
   * }
   * ```
   */
  onProgress?: (p: DecodeAudioProgress) => void;

  /**
   * 解码错误回调
   *
   * @param e - 错误对象，包含 stage、code、message 等属性
   *
   * @example
   * ```typescript
   * onError: (e) => {
   *   console.error(`解码失败 [${e.stage}]: ${e.message}`);
   * }
   * ```
   */
  onError?: (e: Error & { stage?: string; code?: number }) => void;
};

/**
 * PCM 流解码器接口
 *
 * 用于流式解码音频并配合 AudioRenderer 实现拉取式播放
 */
export type PcmStreamDecoder = {
  /**
   * 准备完成后返回流信息
   *
   * 用于创建 AudioRenderer，包含采样率、声道数等参数
   *
   * @remarks
   * 必须在创建 AudioRenderer 之前等待此 Promise resolve
   *
   * @example
   * ```typescript
   * decoder.ready.then((info) => {
   *   const audioRenderer = audio.createAudioRenderer({
   *     samplingRate: info.sampleRate,
   *     channelCount: info.channelCount,
   *     sampleFormat: audio.AudioSampleFormat.SAMPLE_FORMAT_S16LE,
   *   });
   * });
   * ```
   */
  ready: Promise<PcmStreamInfo>;

  /**
   * 解码完成（EOS）或失败
   *
   * - resolve: 解码完成（所有数据已写入环形缓冲区）
   * - reject: 解码失败或被取消
   *
   * @remarks
   * 即使 done resolve，环形缓冲区中可能仍有数据，需要继续读取直到空
   *
   * @example
   * ```typescript
   * decoder.done.then(() => {
   *   console.log('解码完成');
   * }).catch((err) => {
   *   console.error('解码失败:', err);
   * });
   * ```
   */
  done: Promise<void>;

  /**
   * 填充 AudioRenderer.on('writeData') 提供的 ArrayBuffer
   *
   * 这是解码器的核心方法，用于拉取 PCM 数据
   *
   * @param buffer - AudioRenderer 提供的缓冲区
   * @returns 本次写入的"有效 PCM 字节数"（不足的部分会自动补 0）
   *
   * @remarks
   * - 如果环形缓冲区为空，返回 0
   * - 如果环形缓冲区数据不足，自动补零
   * - 此方法是线程安全的，从音频渲染线程调用
   *
   * @example
   * ```typescript
   * audioRenderer.on('writeData', (buffer: ArrayBuffer) => {
   *   const bytesWritten = decoder.fill(buffer);
   *   // bytesWritten 是实际写入的有效字节数
   * });
   * ```
   */
  fill: (buffer: ArrayBuffer) => number;

  /**
   * 请求停止解码
   *
   * - 调用后，done 将 resolve
   * - 环形缓冲区会被标记为 EOS，继续读取直到空
   * - 此方法是异步的，不会立即停止
   *
   * @example
   * ```typescript
   * // 停止解码
   * decoder.close();
   * ```
   */
  close: () => void;

  /**
   * 启用/禁用均衡器
   *
   * @param enabled - true=启用，false=禁用（默认）
   *
   * @remarks
   * - 均衡器是 10 段图形均衡器
   * - 可以在解码过程中实时切换
   *
   * @example
   * ```typescript
   * decoder.setEqEnabled(true);  // 启用均衡器
   * decoder.setEqEnabled(false); // 禁用均衡器
   * ```
   */
  setEqEnabled: (enabled: boolean) => void;

  /**
   * 设置 10 段均衡器增益（单位 dB，索引 0-9 对应 31Hz~16kHz）
   *
   * @param gainsDb - 10 个增益值的数组，每个值范围 -24 ~ +24 dB
   *
   * @remarks
   * - 索引 0-9 对应频率：31Hz, 62Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz
   * - 增益值会被限制在 -24 ~ +24 dB 范围内
   * - 可以在播放过程中实时调整
   *
   * @example
   * ```typescript
   * // 设置为平直响应
   * decoder.setEqGains([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
   *
   * // 设置为 V型响应（增强低频和高频）
   * decoder.setEqGains([5, 4, 3, 1, 0, -1, 1, 3, 4, 5]);
   *
   * // 增强低音
   * decoder.setEqGains([6, 5, 4, 2, 0, 0, 0, 0, 0, 0]);
   * ```
   */
  setEqGains: (gainsDb: number[]) => void;
};

/**
 * 解码音频文件为 PCM 格式（同步接口）
 *
 * @deprecated 测试接口。工程实践请使用 {@link createPcmStreamDecoder} + AudioRenderer.on('writeData')
 *
 * @param inputPath - 输入音频文件路径或 URL（支持 http(s)://...）
 * @param outputPath - 输出 PCM 文件路径
 * @param sampleRate - 可选：采样率（Hz），0 或不传表示自动从媒体流获取
 * @param channelCount - 可选：声道数，0 或不传表示自动从媒体流获取
 * @param bitrate - 可选：比特率（bps），0 或不传表示不设置（推荐）
 * @returns 解码是否成功
 *
 * @remarks
 * - 默认参数：采样率 44100Hz，声道数 2（立体声），比特率不设置
 * - 此方法是同步的，会阻塞调用线程
 * - 输出格式为 S16LE（16位小端PCM）
 *
 * @example
 * ```typescript
 * // 使用默认参数（44100Hz, 2声道）
 * const success = decodeAudio('/path/to/input.mp3', '/path/to/output.pcm');
 *
 * @example
 * // 指定采样率和声道数
 * const success = decodeAudio('/path/to/input.mp3', '/path/to/output.pcm', 48000, 1);
 *
 * @example
 * // 指定所有参数
 * const success = decodeAudio('/path/to/input.mp3', '/path/to/output.pcm', 44100, 2, 128000);
 * ```
 */
export const decodeAudio: (
  inputPath: string,
  outputPath: string,
  sampleRate?: number,
  channelCount?: number,
  bitrate?: number
) => boolean;

/**
 * 异步解码音频文件为 PCM 格式（不会阻塞 UI 线程）
 *
 * @deprecated 测试接口。工程实践请使用 {@link createPcmStreamDecoder} + AudioRenderer.on('writeData')
 *
 * @param inputPathOrUri - 输入音频文件路径或 URL（支持 http(s)://...）
 * @param outputPath - 输出 PCM 文件路径
 * @param onProgress - 可选：进度回调
 * @param sampleRate - 可选：采样率（Hz），0 或不传表示自动从媒体流获取
 * @param channelCount - 可选：声道数，0 或不传表示自动从媒体流获取
 * @param bitrate - 可选：比特率（bps），0 或不传表示不设置（推荐）
 * @returns Promise<boolean> 解码是否成功
 *
 * @remarks
 * - 此方法在后台线程执行，不会阻塞 UI
 * - 进度回调在主线程调用，可以安全更新 UI
 * - 输出格式为 S16LE（16位小端PCM）
 *
 * @example
 * ```typescript
 * const success = await decodeAudioAsync(
 *   '/path/to/audio.mp3',
 *   '/path/to/output.pcm',
 *   (p) => {
 *     if (p.progress >= 0) {
 *       console.log(`进度: ${(p.progress * 100).toFixed(1)}%`);
 *     }
 *   },
 *   44100, // 采样率
 *   2      // 声道数
 * );
 * ```
 */
export const decodeAudioAsync: (
  inputPathOrUri: string,
  outputPath: string,
  onProgress?: (p: DecodeAudioProgress) => void,
  sampleRate?: number,
  channelCount?: number,
  bitrate?: number
) => Promise<boolean>;

/**
 * 创建一个"流式 PCM 解码器"
 *
 * 这是推荐的解码方式，配合 AudioRenderer.on('writeData') 拉取式回调使用
 *
 * @param inputPathOrUri - 输入音频文件路径或 URL（支持 http(s)://...）
 * @param options - 可选：解码器配置选项
 * @param callbacks - 可选：回调函数（onProgress、onError）
 * @returns PcmStreamDecoder 解码器对象
 *
 * @remarks
 * - 解码在后台线程执行，不会阻塞 UI
 * - 使用环形缓冲区在解码线程和播放线程之间传递数据
 * - 支持 10 段均衡器，可在播放过程中实时调整
 *
 * @example
 * ```typescript
 * // 创建解码器
 * const decoder = createPcmStreamDecoder(
 *   '/path/to/audio.mp3',
 *   {
 *     sampleRate: 44100,
 *     channelCount: 2,
 *     ringBytes: 1024 * 1024, // 1MB 环形缓冲区
 *     eqEnabled: true,
 *     eqGainsDb: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
 *   },
 *   {
 *     onProgress: (p) => {
 *       console.log(`进度: ${(p.progress * 100).toFixed(1)}%`);
 *     },
 *     onError: (e) => {
 *       console.error(`错误: ${e.message}`);
 *     }
 *   }
 * );
 *
 * // 等待解码器准备完成
 * const info = await decoder.ready;
 *
 * // 创建 AudioRenderer
 * const audioRenderer = audio.createAudioRenderer({
 *   samplingRate: info.sampleRate,
 *   channelCount: info.channelCount,
 *   sampleFormat: audio.AudioSampleFormat.SAMPLE_FORMAT_S16LE,
 * });
 * await audioRenderer.start();
 *
 * // 设置 writeData 回调，从解码器拉取 PCM 数据
 * audioRenderer.on('writeData', (buffer: ArrayBuffer) => {
 *   decoder.fill(buffer);
 * });
 *
 * // 等待解码完成
 * await decoder.done;
 *
 * // 停止播放
 * await audioRenderer.stop();
 * ```
 */
export const createPcmStreamDecoder: (
  inputPathOrUri: string,
  options?: PcmStreamDecoderOptions,
  callbacks?: PcmStreamDecoderCallbacks
) => PcmStreamDecoder;