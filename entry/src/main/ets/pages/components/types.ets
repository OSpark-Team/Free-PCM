/**
 * EQ 相关定义
 */
export enum EqPresetId {
  Default = 'Default',
  Ballads = 'Ballads',
  Chinese = 'Chinese',
  Classical = 'Classical',
  Dance = 'Dance',
  Jazz = 'Jazz',
  Pop = 'Pop',
  RnB = 'RnB',
  Rock = 'Rock'
}

export const EQ_PRESET_LABEL: Record<string, string> = {
  [EqPresetId.Default]: '标准 (Default)',
  [EqPresetId.Ballads]: '民谣 (Ballads)',
  [EqPresetId.Chinese]: '中国风 (Chinese)',
  [EqPresetId.Classical]: '古典 (Classical)',
  [EqPresetId.Dance]: '舞曲 (Dance)',
  [EqPresetId.Jazz]: '爵士 (Jazz)',
  [EqPresetId.Pop]: '流行 (Pop)',
  [EqPresetId.RnB]: 'R&B',
  [EqPresetId.Rock]: '摇滚 (Rock)'
};

export const EQ_PRESET_MAP: Record<string, number[]> = {
  [EqPresetId.Default]: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  [EqPresetId.Ballads]: [3, 5, 2, -4, 1, 2, -3, 1, 4, 5],
  [EqPresetId.Chinese]: [0, 0, 2, 0, 0, 4, 4, 2, 2, 5],
  [EqPresetId.Classical]: [2, 3, 2, 1, 0, 0, -5, -5, -5, -6],
  [EqPresetId.Dance]: [4, 3, 2, -3, 0, 0, 5, 4, 2, 0],
  [EqPresetId.Jazz]: [2, 0, 2, 3, 6, 5, -1, 3, 4, 4],
  [EqPresetId.Pop]: [5, 2, 1, -1, -5, -5, -2, 1, 2, 4],
  [EqPresetId.RnB]: [1, 4, 5, 3, -2, -2, 2, 3, 5, 5],
  [EqPresetId.Rock]: [6, 4, 4, 2, 0, 1, 3, 3, 5, 4]
};

export const EQ_BAND_LABELS: string[] = ['31', '62', '125', '250', '500', '1k', '2k', '4k', '8k', '16k'];

/**
 * DRC 相关定义
 */
export enum DrcPresetId {
  Default = 'Default',
  Speech = 'Speech',
  Vocal = 'Vocal',
  Night = 'Night',
  Mastering = 'Mastering',
  Punch = 'Punch',
  Broadcast = 'Broadcast',
  Limiter = 'Limiter',
  Crush = 'Crush'
}

export const DRC_PRESET_LABEL: Record<string, string> = {
  [DrcPresetId.Default]: '通用标准 (Default)',
  [DrcPresetId.Speech]: '人声对白 (Speech)',
  [DrcPresetId.Vocal]: '流行歌唱 (Pop Vocal)',
  [DrcPresetId.Night]: '深夜模式 (Night)',
  [DrcPresetId.Mastering]: '母带柔化 (Mastering)',
  [DrcPresetId.Punch]: '打击增强 (Punch)',
  [DrcPresetId.Broadcast]: '广播电台 (Broadcast)',
  [DrcPresetId.Limiter]: '安全限幅 (Limiter)',
  [DrcPresetId.Crush]: '毁灭压缩 (Crush)'
};

export interface DrcParams {
  threshold: number;
  ratio: number;
  attack: number;
  release: number;
  makeup: number;
}

export const DRC_PRESET_MAP: Record<string, DrcParams> = {
  [DrcPresetId.Default]: { threshold: -12, ratio: 2.0, attack: 10, release: 100, makeup: 0 },
  [DrcPresetId.Speech]: { threshold: -24, ratio: 4.0, attack: 5, release: 80, makeup: 6 },
  [DrcPresetId.Vocal]: { threshold: -20, ratio: 3.0, attack: 20, release: 300, makeup: 3 },
  [DrcPresetId.Night]: { threshold: -35, ratio: 10.0, attack: 5, release: 500, makeup: 12 },
  [DrcPresetId.Mastering]: { threshold: -10, ratio: 1.5, attack: 30, release: 400, makeup: 1 },
  [DrcPresetId.Punch]: { threshold: -18, ratio: 4.0, attack: 30, release: 50, makeup: 4 },
  [DrcPresetId.Broadcast]: { threshold: -20, ratio: 6.0, attack: 15, release: 200, makeup: 6 },
  [DrcPresetId.Limiter]: { threshold: -0.5, ratio: 20.0, attack: 0.1, release: 50, makeup: 0 },
  [DrcPresetId.Crush]: { threshold: -40, ratio: 20.0, attack: 0.1, release: 30, makeup: 18 }
};

/**
 * DRC Meter 接口
 */
export interface DrcMeterInfo {
  levelDb: number;
  gainDb: number;
  grDb: number;
}
