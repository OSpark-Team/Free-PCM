/**
 * @file 音频解码管理器
 * @description 基于单例模式实现，封装了 OpenHarmony 原生音频解码能力，支持 Rawfile、本地文件及 URL 的 PCM 解码。
 */

import { common } from '@kit.AbilityKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { fileUri, picker } from '@kit.CoreFileKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import testNapi from 'liblibrary.so';

const TAG = 'AudioDecoderManager';
const DOMAIN = 0x3200;

/** 解码进度信息 */
export interface DecodeAudioProgress {
  /** 进度百分比 (0~1)，若为 -1 则表示时长未知 */
  progress: number;
  /** 当前解码到的时间戳 (ms) */
  ptsMs: number;
  /** 音频总时长 (ms) */
  durationMs: number;
}

/** PCM 流基本信息 */
export interface PcmStreamInfo {
  /** 采样率 (Hz) */
  sampleRate: number;
  /** 声道数 */
  channelCount: number;
  /** 采样格式 (如 's16le', 's32le') */
  sampleFormat: string;
  /** 采样格式编码: 1=S16LE, 3=S32LE */
  sampleFormatCode: number;
  /** 总时长 (ms) */
  durationMs: number;
}

/** 流式解码器配置项 */
export interface PcmStreamDecoderOptions {
  /** 目标采样率 */
  sampleRate?: number;
  /** 目标声道数 */
  channelCount?: number;
  /** 目标比特率 */
  bitrate?: number;
  /** 采样格式：1=S16LE, 3=S32LE，默认 S16LE */
  sampleFormat?: number;
  /** 内部环形缓冲区大小 (Byte) */
  ringBytes?: number;
  /** 是否启用均衡器 */
  eqEnabled?: boolean;
  /** 均衡器 10 段增益配置 (dB) */
  eqGainsDb?: number[];
}

/** 解码器回调函数集合 */
export interface PcmStreamDecoderCallbacks {
  /** 进度回调 */
  onProgress?: (p: DecodeAudioProgress) => void;
  /** 错误回调 */
  onError?: (e: Error) => void;
}

/** 流式 PCM 解码器接口 */
export interface PcmStreamDecoder {
  /** 解码器就绪后的 Promise，返回流信息 */
  ready: Promise<PcmStreamInfo>;
  /** 解码完成或完全停止后的 Promise */
  done: Promise<void>;
  /**
   * 填充 PCM 数据到指定缓冲区
   * @param buffer 目标 ArrayBuffer
   * @returns 实际填充的字节数
   */
  fill: (buffer: ArrayBuffer) => number;
  /** 关闭解码器并释放资源 */
  close: () => void;
  /** 设置均衡器开关状态 */
  setEqEnabled: (enabled: boolean) => void;
  /** 设置均衡器增益 */
  setEqGains: (gainsDb: number[]) => void;
}

/**
 * 音频解码管理器类
 * @class
 */
export class AudioDecoderManager {
  private static instance: AudioDecoderManager;
  private context: common.Context | null = null;
  private downloadUri: string = '';

  private constructor() {}

  /**
   * 获取 AudioDecoderManager 单例
   * @returns {AudioDecoderManager}
   */
  public static getInstance(): AudioDecoderManager {
    if (!AudioDecoderManager.instance) {
      AudioDecoderManager.instance = new AudioDecoderManager();
    }
    return AudioDecoderManager.instance;
  }

  /**
   * 初始化应用上下文
   * @param {common.Context} context - Ability 或应用上下文
   */
  public initContext(context: common.Context): void {
    this.context = context;
  }

  /**
   * 获取当前缓存的上下文
   * @returns {common.Context | null}
   */
  public getContext(): common.Context | null {
    return this.context;
  }

  /**
   * 初始化并获取用户选择的下载目录 URI
   * @returns {Promise<boolean>} 是否成功获取目录
   */
  public async initDownloadDirectory(): Promise<boolean> {
    if (!this.context) {
      hilog.error(DOMAIN, TAG, 'initDownloadDirectory: Context not initialized');
      return false;
    }

    try {
      const documentViewPicker = new picker.DocumentViewPicker(this.context);
      const documentSaveOptions = new picker.DocumentSaveOptions();
      documentSaveOptions.pickerMode = picker.DocumentPickerMode.DOWNLOAD;

      const documentSaveResult = await documentViewPicker.save(documentSaveOptions);
      this.downloadUri = documentSaveResult[0];
      return true;
    } catch (err) {
      const error = err as BusinessError;
      hilog.error(DOMAIN, TAG, 'initDownloadDirectory failed: %{public}s', error.message);
      return false;
    }
  }

  /**
   * 从 Rawfile 解码音频文件并保存至下载目录
   * @param {string} rawfileName - rawfile 目录下的文件名
   * @param {string} outputFileName - 输出的目标文件名 (PCM)
   * @returns {Promise<boolean>} 解码任务是否成功触发并完成
   */
  public async decodeFromRawfile(rawfileName: string, outputFileName: string): Promise<boolean> {
    if (!this.context || !this.downloadUri) {
      hilog.error(DOMAIN, TAG, 'decodeFromRawfile: Context or DownloadDir not ready');
      return false;
    }

    try {
      // 读取 Rawfile 
      const fileData = await this.context.resourceManager.getRawFileContent(rawfileName);

      // 建立临时输入文件
      const tempInputPath = this.context.filesDir + '/temp_input_' + rawfileName;
      const inputFile = fs.openSync(tempInputPath, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY);
      fs.writeSync(inputFile.fd, fileData.buffer);
      fs.closeSync(inputFile);

      // 准备输出路径
      const outputPath = new fileUri.FileUri(this.downloadUri + '/' + outputFileName).path;

      // 调用 NAPI 核心解码
      const success = testNapi.decodeAudio(tempInputPath, outputPath);

      // 清理临时输入文件
      fs.unlinkSync(tempInputPath);

      return success;
    } catch (err) {
      const error = err as BusinessError;
      hilog.error(DOMAIN, TAG, 'decodeFromRawfile error: %{public}s', error.message);
      return false;
    }
  }

  /**
   * 直接同步解码本地文件
   * @param {string} inputPath - 输入文件绝对路径
   * @param {string} outputPath - 输出 PCM 文件绝对路径
   * @returns {boolean}
   */
  public decodeFile(inputPath: string, outputPath: string): boolean {
    return testNapi.decodeAudio(inputPath, outputPath);
  }

  /**
   * 从网络 URL 异步解码音频至下载目录
   * @param {string} url - 音频资源 URL
   * @param {string} outputFileName - 输出的目标文件名 (PCM)
   * @param {Function} [onProgress] - 可选的进度回调函数
   * @returns {Promise<boolean>}
   */
  public async decodeFromUrl(
    url: string,
    outputFileName: string,
    onProgress?: (p: DecodeAudioProgress) => void
  ): Promise<boolean> {
    if (!this.context || !this.downloadUri) {
      hilog.error(DOMAIN, TAG, 'decodeFromUrl: Context or DownloadDir not ready');
      return false;
    }

    try {
      const outputPath = new fileUri.FileUri(this.downloadUri + '/' + outputFileName).path;
      // 调用 NAPI 异步解码
      return await testNapi.decodeAudioAsync(url, outputPath, onProgress);
    } catch (err) {
      const error = err as BusinessError;
      hilog.error(DOMAIN, TAG, 'decodeFromUrl error: %{public}s', error.message);
      return false;
    }
  }

  /**
   * 创建一个流式 PCM 解码器对象
   * @description 适用于与 AudioRenderer 的 'writeData' 回调配合使用，实现边解码边播放。
   * @param {string} inputPathOrUri - 本地路径或网络 URL
   * @param {PcmStreamDecoderOptions} [options] - 解码配置项
   * @param {PcmStreamDecoderCallbacks} [callbacks] - 进度与错误回调
   * @returns {PcmStreamDecoder}
   */
  public createPcmStreamDecoder(
    inputPathOrUri: string,
    options?: PcmStreamDecoderOptions,
    callbacks?: PcmStreamDecoderCallbacks
  ): PcmStreamDecoder {
    // 调用 NAPI 创建解码器
    return testNapi.createPcmStreamDecoder(inputPathOrUri, options, callbacks) as PcmStreamDecoder;
  }
}

export default AudioDecoderManager.getInstance();