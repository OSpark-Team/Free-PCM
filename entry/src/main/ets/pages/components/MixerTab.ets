import { promptAction } from '@kit.ArkUI';
import { COLOR_ACCENT, COLOR_TEXT_PRI, COLOR_TEXT_SEC } from './constants';

/**
 * 混音器组件属性
 */
export interface MixerTabProps {
  volLinearLeft: number;
  volLinearRight: number;
  onVolumeChange: (left: number, right: number) => void;
}

@Component
export struct MixerTab {
  @Prop volLinearLeft: number = 1.0;
  @Prop volLinearRight: number = 1.0;
  
  onVolumeChange: (left: number, right: number) => void = () => {};

  @Builder
  ChannelFader(label: string, side: string, color: string) {
    Column() {
      Text(`${(side === 'L' ? this.volLinearLeft : this.volLinearRight).toFixed(2)}`)
        .fontColor(color)
        .fontSize(16)
        .fontWeight(FontWeight.Bold)
        .margin({ bottom: 15 })

      Slider({
        value: (side === 'L' ? this.volLinearLeft : this.volLinearRight) * 100,
        min: 0,
        max: 200,
        style: SliderStyle.InSet,
        direction: Axis.Vertical,
        reverse: true
      })
        .height(260)
        .trackColor('#E0E0E0')
        .selectedColor(color)
        .onChange((v) => {
          if (side === 'L') {
            this.onVolumeChange(v / 100, this.volLinearRight);
          } else {
            this.onVolumeChange(this.volLinearLeft, v / 100);
          }
        })

      Text(label)
        .fontColor(COLOR_TEXT_SEC)
        .fontSize(13)
        .margin({ top: 15 })
    }
  }

  @Builder
  build() {
    Scroll() {
      Column() {
        Row() {
          Text('声道增益测试')
            .fontSize(18)
            .fontWeight(FontWeight.Bold)
            .fontColor(COLOR_TEXT_PRI)
          Blank()
          Button('重置')
            .fontSize(12)
            .height(28)
            .backgroundColor('#F0F0F0')
            .fontColor(COLOR_TEXT_PRI)
            .onClick(() => {
              this.onVolumeChange(1.0, 1.0);
              promptAction.showToast({ message: '系数已重置' });
            })
        }
        .width('100%')
        .padding({ left: 10, right: 10, bottom: 30 })

        Row({ space: 40 }) {
          this.ChannelFader('左声道 (L)', 'L', COLOR_ACCENT)
          this.ChannelFader('右声道 (R)', 'R', '#FF9500')
        }
        .justifyContent(FlexAlign.Center)
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
