import { COLOR_ACCENT, COLOR_CARD, COLOR_TEXT_PRI, COLOR_TEXT_SEC } from './constants';
import { EqPresetId, EQ_PRESET_LABEL, EQ_PRESET_MAP, EQ_BAND_LABELS } from './types';

/**
 * 均衡器组件属性
 */
export interface EqualizerTabProps {
  isStereoEqMode: boolean;
  editRightChannel: boolean;
  currentPresetLabel: string;
  eqGainsUnified: number[];
  eqGainsLeft: number[];
  eqGainsRight: number[];
  onStereoModeChange: (isStereo: boolean) => void;
  onEditChannelChange: (isRight: boolean) => void;
  onGainsChange: (unified: number[], left: number[], right: number[]) => void;
}

@Component
export struct EqualizerTab {
  @Prop isStereoEqMode: boolean = false;
  @Prop editRightChannel: boolean = false;
  @Prop currentPresetLabel: string = '标准 (Default)';
  @Prop eqGainsUnified: number[] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  @Prop eqGainsLeft: number[] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  @Prop eqGainsRight: number[] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  
  onStereoModeChange: (isStereo: boolean) => void = () => {};
  onEditChannelChange: (isRight: boolean) => void = () => {};
  onGainsChange: (unified: number[], left: number[], right: number[]) => void = () => {};

  private getCurrentGain(index: number): number {
    return this.isStereoEqMode 
      ? (this.editRightChannel ? this.eqGainsRight[index] : this.eqGainsLeft[index]) 
      : this.eqGainsUnified[index];
  }

  private getEqColor(): string {
    return this.isStereoEqMode 
      ? (this.editRightChannel ? '#FF9500' : COLOR_ACCENT) 
      : COLOR_ACCENT;
  }

  private updateGain(index: number, v: number) {
    const val = Math.round(v);
    let newUnified = [...this.eqGainsUnified];
    let newLeft = [...this.eqGainsLeft];
    let newRight = [...this.eqGainsRight];
    
    if (this.isStereoEqMode) {
      if (this.editRightChannel) {
        newRight[index] = val;
      } else {
        newLeft[index] = val;
      }
    } else {
      newUnified[index] = val;
    }
    this.onGainsChange(newUnified, newLeft, newRight);
  }

  @Builder
  getEqPresetMenu() {
    Menu() {
      ForEach(Object.keys(EQ_PRESET_MAP), (key: string) => {
        MenuItem({ content: EQ_PRESET_LABEL[key] }).onClick(() => {
          const gains = EQ_PRESET_MAP[key];
          if (this.isStereoEqMode) {
            this.onGainsChange([...gains], [...gains], [...gains]);
          } else {
            this.onGainsChange([...gains], this.eqGainsLeft, this.eqGainsRight);
          }
        })
      })
    }
  }

  @Builder
  build() {
    Scroll() {
      Column() {
        Row() {
          Text('模式：')
            .fontSize(13)
            .fontColor(COLOR_TEXT_SEC)
          
          Row() {
            Text('统一')
              .fontSize(12)
              .fontColor(this.isStereoEqMode ? COLOR_TEXT_SEC : Color.White)
              .backgroundColor(this.isStereoEqMode ? '#F0F0F0' : COLOR_ACCENT)
              .padding({ left: 10, right: 10, top: 4, bottom: 4 })
              .borderRadius({ topLeft: 12, bottomLeft: 12 })
              .onClick(() => { this.onStereoModeChange(false); })
            
            Text('独立')
              .fontSize(12)
              .fontColor(this.isStereoEqMode ? Color.White : COLOR_TEXT_SEC)
              .backgroundColor(this.isStereoEqMode ? COLOR_ACCENT : '#F0F0F0')
              .padding({ left: 10, right: 10, top: 4, bottom: 4 })
              .borderRadius({ topRight: 12, bottomRight: 12 })
              .onClick(() => { this.onStereoModeChange(true); })
          }
          
          Blank()
          
          Text(this.currentPresetLabel)
            .fontSize(14)
            .fontColor(COLOR_ACCENT)
            .bindMenu(this.getEqPresetMenu())
        }
        .width('100%')
        .margin({ bottom: 20 })

        if (this.isStereoEqMode) {
          Row({ space: 10 }) {
            Button('左 (L)')
              .fontSize(12)
              .height(28)
              .backgroundColor(!this.editRightChannel ? COLOR_ACCENT : '#F0F0F0')
              .onClick(() => this.onEditChannelChange(false))
            
            Button('右 (R)')
              .fontSize(12)
              .height(28)
              .backgroundColor(this.editRightChannel ? '#FF9500' : '#F0F0F0')
              .onClick(() => this.onEditChannelChange(true))
          }
          .width('100%')
          .margin({ bottom: 15 })
        }

        Row() {
          ForEach(EQ_BAND_LABELS, (label: string, index: number) => {
            Column() {
              Slider({
                value: this.getCurrentGain(index),
                min: -12,
                max: 12,
                step: 1,
                direction: Axis.Vertical,
                reverse: true
              })
                .layoutWeight(1)
                .trackColor('#E0E0E0')
                .selectedColor(this.getEqColor())
                .onChange((v) => { this.updateGain(index, v); })
              
              Text(`${Math.round(this.getCurrentGain(index))}`)
                .fontSize(10)
                .margin({ top: 4 })
              
              Text(label)
                .fontSize(9)
                .fontColor(COLOR_TEXT_SEC)
            }
            .layoutWeight(1)
            .height(240)
          })
        }
        .padding(15)
        .backgroundColor(COLOR_CARD)
        .borderRadius(16)
        .width('100%')
      }
      .padding(24)
      .width('100%')
      .justifyContent(FlexAlign.Start)
      .constraintSize({ minHeight: '100%' })
    }
    .scrollable(ScrollDirection.Vertical)
  }
}
