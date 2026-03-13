import { COLOR_ACCENT, COLOR_CARD, COLOR_TEXT_PRI, COLOR_TEXT_SEC } from './constants';

export interface PitchTabProps {
  pitchEnabled: boolean;
  pitchSemitones: number;
  onEnabledChange: (enabled: boolean) => void;
  onSemitonesChange: (semitones: number) => void;
  onReset: () => void;
}

@Component
export struct PitchTab {
  @Prop pitchEnabled: boolean = false;
  @Prop pitchSemitones: number = 0;

  onEnabledChange: (enabled: boolean) => void = () => {};
  onSemitonesChange: (semitones: number) => void = () => {};
  onReset: () => void = () => {};

  private clampSemitones(v: number): number {
    let out = Math.round(v);
    if (out < -12) out = -12;
    if (out > 12) out = 12;
    return out;
  }

  @Builder
  build() {
    Scroll() {
      Column({ space: 18 }) {
        Row() {
          Column({ space: 6 }) {
            Text('变调器')
              .fontSize(18)
              .fontWeight(FontWeight.Bold)
              .fontColor(COLOR_TEXT_PRI)
            Text('半音（-12 ~ +12），可运行时调整')
              .fontSize(12)
              .fontColor(COLOR_TEXT_SEC)
          }
          .layoutWeight(1)

          Button('重置')
            .fontSize(12)
            .height(28)
            .backgroundColor('#F0F0F0')
            .fontColor(COLOR_TEXT_PRI)
            .onClick(() => {
              this.onReset();
            })
        }
        .width('100%')
        .padding({ left: 10, right: 10, bottom: 10 })

        Column({ space: 12 }) {
          Row() {
            Text('启用')
              .fontSize(14)
              .fontColor(COLOR_TEXT_SEC)
            Blank()
            Toggle({ type: ToggleType.Switch, isOn: this.pitchEnabled })
              .onChange((v) => {
                this.onEnabledChange(v);
              })
          }
          .width('100%')

          Row() {
            Text('半音偏移')
              .fontSize(14)
              .fontColor(COLOR_TEXT_SEC)
            Blank()
            Text(`${this.pitchSemitones > 0 ? '+' : ''}${this.pitchSemitones}`)
              .fontSize(16)
              .fontColor(COLOR_ACCENT)
              .fontWeight(FontWeight.Bold)
          }
          .width('100%')

          Slider({ value: this.pitchSemitones, min: -12, max: 12, step: 1, style: SliderStyle.InSet })
            .selectedColor(COLOR_ACCENT)
            .trackColor('#E0E0E0')
            .onChange((v) => {
              this.onSemitonesChange(this.clampSemitones(v));
            })

          Row() {
            Text('-12')
              .fontSize(12)
              .fontColor(COLOR_TEXT_SEC)
            Blank()
            Text('0')
              .fontSize(12)
              .fontColor(COLOR_TEXT_SEC)
            Blank()
            Text('+12')
              .fontSize(12)
              .fontColor(COLOR_TEXT_SEC)
          }
          .width('100%')
        }
        .padding(20)
        .backgroundColor(COLOR_CARD)
        .borderRadius(16)
        .shadow(ShadowStyle.OUTER_DEFAULT_XS)
        .width('100%')
      }
      .padding(24)
      .width('100%')
      .justifyContent(FlexAlign.Start)
      .constraintSize({ minHeight: '100%' })
    }
    .scrollable(ScrollDirection.Vertical)
    .scrollBar(BarState.Auto)
  }

}
