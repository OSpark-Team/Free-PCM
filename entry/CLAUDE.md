# Entry 模块 - 测试应用

> 最后更新时间：2026-02-08 17:34:47

---

## 变更记录 (Changelog)

### 2026-02-08 17:34:47
- 初始化模块文档
- 记录 UI 结构与交互逻辑

---

[根目录](../CLAUDE.md) > **entry**

---

## 模块职责

`entry` 是 Free PCM 的测试应用模块，提供：
- **功能验证**：测试 library 模块的所有核心功能
- **UI 演示**：展示音频解码、播放、均衡器的实际使用
- **交互测试**：实时调整均衡器、控制播放进度
- **开发调试**：帮助开发者快速验证修改效果

---

## 入口与启动

### 模块入口
- **Ability 入口**：`src/main/ets/entryability/EntryAbility.ets`
- **主页面**：`src/main/ets/pages/Index.ets`
- **模块类型**：`entry`（可独立运行的 HarmonyOS 应用）

### 启动流程
```
EntryAbility.onCreate()
    ↓
EntryAbility.onWindowStageCreate()
    ↓
windowStage.loadContent('pages/Index')
    ↓
Index 页面显示
```

---

## 对外接口

### 1. EntryAbility

应用入口 Ability，负责：
- 初始化应用上下文
- 设置颜色模式
- 加载主页面

```typescript
export default class EntryAbility extends UIAbility {
  onCreate(want: Want, launchParam: LaunchParam): void;
  onDestroy(): void;
  onWindowStageCreate(windowStage: WindowStage): void;
  onWindowStageDestroy(): void;
  onForeground(): void;
  onBackground(): void;
}
```

### 2. Index 主页面

主页面提供以下交互功能：

**UI 组件：**
- 音频流 URL 输入框
- 播放进度条
- 均衡器预设选择按钮（9 种预设）
- 均衡器频段滑块（10 个频段）
- 播放/停止控制按钮

**核心方法：**
```typescript
struct Index {
  // 播放音频流
  async playUrl(): void;

  // 停止播放
  async stopPlayback(): void;

  // 更新播放进度
  private updateProgress(p: DecodeAudioProgress): void;

  // 格式化时间显示
  private formatTime(seconds: number): string;
}
```

---

## 关键依赖与配置

### 模块依赖（oh-package.json5）

```json5
{
  "dependencies": {
    "@okysu/free-pcm": "file:../library"
  }
}
```

依赖本地 library 模块作为音频解码库。

### OpenHarmony Kit 使用

- `@kit.AbilityKit` - Ability 和应用生命周期
- `@kit.ArkUI` - UI 组件（PromptAction）
- `@kit.BasicServicesKit` - 错误处理

### 模块配置（src/main/module.json5）

```json5
{
  "module": {
    "name": "entry",
    "type": "entry",
    "mainElement": "EntryAbility",
    "deviceTypes": ["phone", "tablet"],
    "pages": "$profile:main_pages",
    "abilities": [
      {
        "name": "EntryAbility",
        "srcEntry": "./ets/entryability/EntryAbility.ets",
        "exported": true,
        "skills": [...]
      }
    ]
  }
}
```

**权限需求：**
- `ohos.permission.INTERNET` - 支持网络音频流解码

---

## 数据模型

### EqPresetId - 均衡器预设枚举

```typescript
enum EqPresetId {
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
```

### EqPreset - 均衡器预设数据

```typescript
interface EqPreset {
  id: EqPresetId;      // 预设 ID
  label: string;       // 显示标签
  gainsDb: number[];    // 10 个频段的增益值
}
```

### 均衡器预设列表

```typescript
export const EQ_PRESET_LIST: EqPreset[] = [
  { id: 'Default', label: '标准 (Default)', gainsDb: [...] },
  { id: 'Ballads', label: '民谣 (Ballads)', gainsDb: [...] },
  { id: 'Chinese', label: '中国风 (Chinese)', gainsDb: [...] },
  { id: 'Classical', label: '古典 (Classical)', gainsDb: [...] },
  { id: 'Dance', label: '舞曲 (Dance)', gainsDb: [...] },
  { id: 'Jazz', label: '爵士 (Jazz)', gainsDb: [...] },
  { id: 'Pop', label: '流行 (Pop)', gainsDb: [...] },
  { id: 'RnB', label: 'R&B', gainsDb: [...] },
  { id: 'Rock', label: '摇滚 (Rock)', gainsDb: [...] }
];
```

### 频段标签

```typescript
export const EQ_BAND_LABELS: string[] = [
  '31Hz', '62Hz', '125Hz', '250Hz', '500Hz',
  '1kHz', '2kHz', '4kHz', '8kHz', '16kHz'
];
```

---

## 测试与质量

### 测试结构

```
entry/src/
├── test/                # 单元测试
│   ├── LocalUnit.test.ets
│   └── List.test.ets
└── ohosTest/            # 设备测试
    └── ets/test/
        ├──Ability.test.ets
        └── List.test.ets
```

### 测试框架
- **Hypium** (@ohos/hypium) - OpenHarmony 测试框架
- **Hamock** (@ohos/hamock) - Mock 框架

### 手动测试场景

**功能验证：**
1. 输入有效的音频 URL（支持 http/https）
2. 选择均衡器预设（如 Pop、Rock）
3. 手动调整各频段增益
4. 控制播放/停止
5. 观察播放进度和状态提示

**异常测试：**
1. 空的 URL 输入
2. 无效的 URL（404 错误）
3. 网络中断情况
4. 播放过程中切换预设

---

## 常见问题 (FAQ)

### Q1: 如何播放本地音频文件？

**A:** 当前实现仅支持 URL 输入。如需支持本地文件：
1. 使用文件选择器获取文件路径
2. 将路径传递给 `createStreamDecoder`
3. 或者添加文件选择按钮到 UI

```typescript
// 示例代码（未实现）
const filePicker = new picker.DocumentViewPicker(context);
const result = await filePicker.select(options);
const path = result[0].uri;
const decoder = decoderTool.createStreamDecoder(path);
```

### Q2: 均衡器调整会实时生效吗？

**A:** 是的。调整滑块或切换预设时：
1. 更新内部均衡器状态
2. 调用 `equalizer.applyToDecoder(decoder)`
3. 立即应用到正在播放的音频

### Q3: 支持哪些音频格式？

**A:** 由 library 模块决定，支持：
- MP3、FLAC、WAV、AAC、OGG、Opus 等

### Q4: 如何调试播放问题？

**A:**
1. 查看页面顶部的状态提示文本
2. 使用 DevEco Studio 的日志工具查看 `hilog` 输出
3. 检查网络连接状态
4. 验证 URL 是否可访问

### Q5: 应用崩溃后如何恢复？

**A:**
- 应用会自动重启
- 播放状态会重置为"待机状态"
- 需要重新输入 URL 并点击"开始播放"

---

## 相关源代码文件

### ArkTS 源文件
- `src/main/ets/entryability/EntryAbility.ets` - 应用入口
- `src/main/ets/entrybackupability/EntryBackupAbility.ets` - 备份恢复
- `src/main/ets/pages/Index.ets` - 主页面（205 行）

### 配置文件
- `oh-package.json5` - 模块依赖
- `build-profile.json` - 构建配置
- `src/main/module.json5` - 模块元信息
- `obfuscation-rules.txt` - 混淆规则

### 资源文件
- `src/main/resources/base/element/string.json` - 字符串资源
- `src/main/resources/base/element/color.json` - 颜色资源
- `src/main/resources/base/element/float.json` - 浮点资源
- `src/main/resources/base/media/layered_image.json` - 图标配置
- `src/main/resources/base/profile/main_pages.json` - 页面路由配置

### Mock 配置
- `src/mock/Libentry.mock.ets` - Mock 配置

---

## UI 交互流程

```
1. 输入 URL
   ↓
2. 点击"开始播放"
   ↓
3. 创建 PcmStreamDecoder
   ↓
4. 等待 decoder.ready
   ↓
5. 创建 AudioRendererPlayer
   ↓
6. 调用 player.play()
   ↓
7. 实时更新进度条
   ↓
8. 可选：调整均衡器
   ├─ 切换预设按钮
   └─ 拖动频段滑块
   ↓
9. 点击"停止"结束播放
```

---

## 覆盖率缺口

- [ ] 读取测试文件（`src/test/*.test.ets`）
- [ ] 读取设备测试文件（`src/ohosTest/ets/test/*.test.ets`）
- [ ] 分析备份恢复逻辑（`EntryBackupAbility.ets`）
- [ ] 查看 Mock 配置（`Libentry.mock.ets`）
- [ ] 分析资源文件结构