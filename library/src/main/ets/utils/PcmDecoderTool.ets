import AudioDecoderManager, {
  PcmStreamDecoder,
  PcmStreamDecoderCallbacks,
  PcmStreamDecoderOptions
} from './AudioDecoderManager';

export class PcmDecoderTool {
  public createStreamDecoder(
    inputPathOrUri: string,
    options?: PcmStreamDecoderOptions,
    callbacks?: PcmStreamDecoderCallbacks
  ): PcmStreamDecoder {
    return AudioDecoderManager.createPcmStreamDecoder(inputPathOrUri, options, callbacks);
  }
}
