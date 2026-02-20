import { promptAction } from '@kit.ArkUI';
import { COLOR_BG, COLOR_CARD, COLOR_ACCENT, COLOR_WARN, COLOR_TEXT_PRI, COLOR_TEXT_SEC, COLOR_DIVIDER } from './constants';

/**
 * 控制面板组件属性
 */
export interface ControlTabProps {
  url: string;
  status: string;
  isPlaying: boolean;
  isDecoding: boolean;
  isSeeking: boolean;
  isBuffering: boolean;
  hasDecoder: boolean;
  progressPercent: number;
  onUrlChange: (url: string) => void;
  onTogglePlay: () => void;
  onReset: () => void;
}

@Component
export struct ControlTab {
  @Prop url: string = '';
  @Prop status: string = '设备就绪';
  @Prop isPlaying: boolean = false;
  @Prop isDecoding: boolean = false;
  @Prop isSeeking: boolean = false;
  @Prop isBuffering: boolean = false;
  @Prop hasDecoder: boolean = false;
  @Prop progressPercent: number = 0;
  
  onUrlChange: (url: string) => void = () => {};
  onTogglePlay: () => void = () => {};
  onReset: () => void = () => {};

  private getPlayButtonLabel(): string {
    if (!this.hasDecoder) {
      return '开始播放';
    }
    return this.isPlaying ? '暂停' : '继续播放';
  }

  @Builder
  build() {
    Scroll() {
      Column() {
        TextInput({ text: this.url, placeholder: '请输入流媒体 URL 或 "test"' })
          .placeholderColor(COLOR_TEXT_SEC)
          .fontColor(COLOR_TEXT_PRI)
          .backgroundColor(COLOR_CARD)
          .caretColor(COLOR_ACCENT)
          .margin({ bottom: 20 })
          .height(50)
          .borderRadius(12)
          .shadow(ShadowStyle.OUTER_DEFAULT_XS)
          .onChange((v) => this.onUrlChange(v))

        Row({ space: 15 }) {
          Button(this.getPlayButtonLabel())
            .layoutWeight(1)
            .height(56)
            .backgroundColor(this.isPlaying ? '#E3F2FF' : COLOR_ACCENT)
            .fontColor(this.isPlaying ? COLOR_ACCENT : Color.White)
            .fontWeight(FontWeight.Bold)
            .shadow(ShadowStyle.OUTER_DEFAULT_XS)
            .onClick(() => this.onTogglePlay())

          Button('重置')
            .width(100)
            .height(56)
            .backgroundColor('#FFF0F0')
            .fontColor(COLOR_WARN)
            .fontWeight(FontWeight.Bold)
            .shadow(ShadowStyle.OUTER_DEFAULT_XS)
            .onClick(() => this.onReset())
        }
        .width('100%')

        Divider()
          .color(COLOR_DIVIDER)
          .margin({ top: 25, bottom: 25 })

        Column({ space: 12 }) {
          Row() {
            Text('运行状态')
              .fontColor(COLOR_TEXT_SEC)
              .fontSize(13)
            Blank()
            Text(this.isSeeking ? '跳转中...' : (this.isBuffering ? '缓冲中...' : this.status))
              .fontColor(this.isBuffering ? Color.Orange : COLOR_TEXT_PRI)
              .fontSize(15)
              .fontWeight(FontWeight.Medium)
          }
          .width('100%')

          Row() {
            Text('解码缓冲')
              .fontColor(COLOR_TEXT_SEC)
              .fontSize(13)
            Blank()
            Text(`${this.progressPercent}%`)
              .fontColor(COLOR_ACCENT)
              .fontSize(15)
              .fontWeight(FontWeight.Bold)
          }
          .width('100%')
        }
        .padding(20)
        .backgroundColor(COLOR_CARD)
        .borderRadius(16)
        .shadow(ShadowStyle.OUTER_DEFAULT_XS)
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
