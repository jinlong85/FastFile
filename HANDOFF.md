# FastFile — 交接说明（给后续 AI / 开发者）

更新日期：2026-09-29（Asia/Shanghai）

> 2026-09-28：已纳入 Git 版本管理；原 8000 行单文件 `src\MainWnd.cpp` 已拆分为 14 个编译单元（见「源码结构」）。

## 目标

轻量 Windows 文件管理器：接近 **360 文件** 的速度与布局密度，观感贴近 **Windows 11 资源管理器**。  
技术栈：**DuiLib + 原生 C++**（不要改成 WinUI/Electron）。  
**禁止**：反编译 360 安装包；复制 360 图标、商标、皮肤资源。布局/密度可参考，资源必须用 Windows Shell / 自绘。

## 路径

| 项 | 路径 |
|----|------|
| 工程根 | `C:\Users\JINLONG\文档\Grok\FastFile`（可能 junction 到 Documents） |
| 源码 | `src\`（14 个 `MainWnd*.cpp` + `MainWnd.h` + `MainWndInternal.h` + `main.cpp`） |
| 皮肤 | `skin\main.xml`（POST_BUILD 拷到 exe 旁） |
| 可执行文件 | `build\Release\FastFile.exe` |
| DuiLib | `third_party\duilib\` |
| 版本管理 | Git，远程 `https://github.com/jinlong85/FastFile`（分支 `main`） |
| 历史备份 | 已移出工程：`C:\Users\JINLONG\文档\Grok\_FastFile_attic_20260928\` |

## 源码结构（2026-09-28 拆分）

原先约 8000 行的单一 `MainWnd.cpp` 已按职责拆成 14 个编译单元；每个 `.cpp` 只实现 `CMainWnd` 的成员，类声明仍集中在 `MainWnd.h`，行为未变。

| 文件 | 职责 |
|------|------|
| `MainWnd.cpp` | 窗口生命周期、消息路由（Notify / OnClick / HandleMessage）、选择与窗口激活 |
| `MainWnd.Dpi.cpp` | Per-Monitor DPI 缩放（字体、chrome 尺寸、DPI 变更） |
| `MainWnd.Theme.cpp` | UI 令牌、窗口圆角、时间/容量格式化、隐藏项开关 |
| `MainWnd.Util.cpp` | 路径与格式化工具、消息泵、状态栏文字 |
| `MainWnd.Nav.cpp` | 导航、列表刷新、搜索过滤、地址栏与面包屑 |
| `MainWnd.Tree.cpp` | 左侧目录树 |
| `MainWnd.Tabs.cpp` | 标签栏、会话持久化、每文件夹视图记忆 |
| `MainWnd.Favorites.cpp` | 收藏栏、快速访问固定、左栏分隔条 |
| `MainWnd.FileOps.cpp` | 文件操作与后台复制引擎 |
| `MainWnd.Menus.cpp` | 工具栏下拉菜单与 Shell 右键菜单 |
| `MainWnd.Views.cpp` | 视图模式、详细信息、图标/平铺、虚拟化、排序与列宽 |
| `MainWnd.Icons.cpp` | Shell 图标/缩略图提取、图标缓存、缩略图线程 |
| `MainWnd.Preview.cpp` | 预览窗格（图片/文本/视频元数据） |
| `MainWnd.DragDrop.cpp` | OLE 拖放：DropTarget、DragSource、传输 |
| `MainWndInternal.h` | 公共 include 前导 + 跨单元共享的 2 个 inline 辅助函数 |

改代码时的两条规则：

1. 新方法写进职责对应的那个单元；只在单个单元内使用的辅助函数，直接放在该单元顶部的匿名 `namespace` 里。
2. 需要跨单元共享的辅助函数放进 `MainWndInternal.h`（写成 `inline`）。新增 `.cpp` 时，必须同时登记到 `CMakeLists.txt` 和 `vs\FastFile.vcxproj`。

## 构建与运行

- 工具链：CMake + VS 2022 Build Tools，**Release x64**
- 改 `main.xml` 后务必重建或确保 `build\Release\skin\` 与源 skin 同步，否则会「加载资源文件失败」或跑旧皮肤
- 发布后建议：结束 `FastFile` 进程 → 清 `%TEMP%\FastFileIconCache` → 再启动 exe
- 图标缓存版本：`_v7.png`（HICON → PNG 真透明）；改导出逻辑时升版本并清缓存

## 图标 / 缩略图渲染管线（2026-09-29 修锯齿）

症状：任何视图的列表图标、以及右侧预览的缩略图，边缘都有锯齿 / 发糊。
结论：**不是底层（Shell 图标本身清晰），是取图与缩放的渲染问题**。三处原因：

1. **按写死的阈值挑系统图像列表**（旧代码 `cx<=16→SHIL_SMALL /<=32→SHIL_LARGE /
   <=48→SHIL_EXTRALARGE / else SHIL_JUMBO`）。这些常量对应的 **实际像素**随 DPI 放大，
   150% 下是 **24 / 48 / 72 / 384**。于是 24px 的槽位拿到 48px 图标、72px 的槽位拿到
   384px 图标，全部要"硬缩"。现改为遍历四个列表用 `IImageList::GetIconSize()` 读真实
   尺寸，选"最先能覆盖目标的最小列表"（见 `ExtractShellIconSized`）。
2. **`DrawIconEx` 缩放不做高质量重采样**（驱动层近似 StretchBlt）。现在
   `SaveIconToPng` 先按图标**原生尺寸** 1:1 栅格化（`RenderIconToArgbBuffer`），
   尺寸不一致时交给 GDI+ `HighQualityBicubic`（`ResizeArgbBuffer`）。
3. **预览窗格让 DuiLib 去缩放**：`bkimage` 由 `AlphaBlend` 绘制，缩放同样无滤波。
   - 文件夹/通用文件预览原先把 72px 的 PNG 塞进 120px 的框 → **放大**糊掉。现在
     `LoadPreviewShellIcon` 直接按框的尺寸取图（通常拿到 384px JUMBO，再高质量缩到 120）。
   - `ApplyPreviewImageBk` 新增兜底：目标尺寸与 PNG 不一致时先 `ResamplePngToSize()`
     把 PNG 重写成目标像素，再 1:1 贴图。

改这块时的自检：清 `%TEMP%\FastFileIconCache` → 启动 → 看缓存里 PNG 的尺寸分布，
应当出现 24×24（列表/详情）与 120×120（预览）等**恰好等于目标**的尺寸。
另外 `PreviewIconCompactH`(80 设计) 与 `PreviewIconPx`(48 设计) 现在只作下限，
预览小图标实际按 120 物理像素取图。

## 2026-09-29 第二批：视图/布局问题（6 项）

### 1. 此电脑的驱动器磁贴（图1）
`DriveTileButtonUI` 改为自绘：第一行盘名、中间胶囊形占用条、第三行「X 可用，共 Y」。
条高 = 磁贴高/9（随磁贴/DPI 自适应，夹在 7–14 设计像素），文字颜色比盘名浅
(`RGB(0x70,0x70,0x70)`)，说明行用小一号字体。磁贴文案不再写进控件文本
（`BindIconTile` 里对 This PC 清空 label），否则会和自绘文字重叠。

### 2. 平铺视图多出的「文件」二字（图2）
`BindIconTile` / `TryReuseIconsView` 里原本拼 `类型 + 大小`；现在只拼大小，并给平铺磁贴
打开 `multiline`，所以是「名称 / 大小」两行（和资源管理器一致）。

### 3. 地址栏面包屑被裁字（图3）
`RebuildBreadcrumb` 先量出每段文字宽度，再按地址栏**实际宽度**决定显示哪几段：
放不下就从头丢段并用「…」占位（点「…」回到被折叠的父目录），最后一段优先保留，
实在放不下才收缩其宽度并靠 `endellipsis` 收尾。段宽不再写死 220 上限。
注意：**`此电脑` 是伪路径 `::ThisPC`，绝不能进 `NormalizePath`**（`GetFullPathNameW`
会把它变成 `::\ThisPC`，于是面包屑里冒出「本地磁盘 (:)」）；判断要用原始 `m_currentPath`。

### 4. 列表视图顺序 + 完整文件名（图4）
DuiLib 的 `CTileLayoutUI` 增加 `columnfirst` 属性（本项目对 third_party 的最小改动，
默认 false）：行数按**可用高度**算，先竖着填满一列再往右开新列。
`ApplyTileLayoutMetrics` 在列表模式下 `SetColumnFirst(true)`、`EnableScrollBar(false,true)`
（横向滚动条），其它模式还原。列宽由 `MeasureListColumnWidth()` 用 UI 字体量最长文件名
得出（夹在 180–900 设计像素），因此文件名基本不再被省略。
**坑**：TileLayout 里 `iRowIndex` 实际喂给 X 坐标、`iColumnIndex` 喂给 Y 坐标（名字是反的），
写竖向流时必须 `Y = i%rows`、`X = i/rows`。

### 5. D:\Users\<用户> 显示为空（图5）
`ShouldHideByAttributes` 原来把 `HIDDEN | SYSTEM` 一起过滤。资源管理器的规则是
「隐藏项」只按 HIDDEN，「受保护的操作系统文件」才是 HIDDEN+SYSTEM 两个都要。
被重定向的用户文件夹（文档/桌面/下载/图片…在 D 盘上）属性是 **ReadOnly|System**，
于是被误藏。现在只按 `FILE_ATTRIBUTE_HIDDEN` 判断。

### 6. 缩略图不按图片长宽比（图6）
`ExtractShellItemImage` 生成的是「方框 + 透明留白」，新增 `CropPngToContentAlpha()`
裁掉透明边，得到紧贴内容的缩略图；`ApplyTileIconImage` 用 `PngSizeFromFile()`（读 PNG
头，不解码）拿到真实宽高，按比例放进方形图标槽居中——竖图变窄高、横图变宽，图标视图
不再一律方形。图标缓存版本 `_v8.png`（缩略图内容变了，必须升版本）。

### 7. 预览窗格可拖动 + 自适应
- `main.xml`：`preview_pane` 加 `sepwidth="-6" sepimm="true" minwidth="180" maxwidth="760"`。
  **负的 sepwidth 才能把拖拽热区放在左边缘**（正的会放在右边缘）。
- 拖动时不会来 `WM_SIZE`（DuiLib 自己重排），所以新增 200ms 的 `kTimerLayoutSync`
  → `SyncLayoutDependents()`：面包屑宽度变了就重排面包屑；预览窗格宽度变了就
  `ReloadPreviewForWidth()` 重新生成缩略图（`PreviewImageBox()` 按窗格宽度给出绘制尺寸，
  图片/文件夹图标/视频帧都按新宽度重取，不会拉伸模糊或穿模）。
  `m_previewFromSelection` 用来区分「选中项的预览」和「当前目录概览」。

## 2026-09-29 第三批：磁贴文字 / 可拖动分隔条 / 排序 / 进度条

### 平铺视图的文件名
磁贴改成 230×72（设计）：文件名最多两行，大小单独一行，不再被裁掉。
**坑**：DuiLib 开 `multiline` 后文字总是从文本框顶部开始画（`valign` 被忽略），
所以 `BindIconTile`／`TryReuseIconsView` 会用 `MeasureTextWidthPx()` 先估名字占几行，
再按「行数 × 行高」手工算 `textpadding` 的顶部留白，才能看起来垂直居中。

### 左侧栏 / 预览栏都能拖宽度
根因：**分隔条（sep）是容器自己的能力**——只有 `CHorizontalLayoutUI` 实现 `sepwidth`、
只有 `CVerticalLayoutUI` 实现 `sepheight`。之前把 `sepwidth` 写在 `VerticalLayout`
上，属性被静默忽略，所以完全拖不动。

| 想改的方向 | 容器必须是 | 属性 | 热区位置 |
|---|---|---|---|
| 宽度 | HorizontalLayout | `sepwidth` | 正数=右边缘，负数=左边缘 |
| 高度 | VerticalLayout | `sepheight` | 正数=下边缘，负数=上边缘 |

热区**必须落在容器的 padding 区域里**，否则会被子控件挡住、收不到鼠标事件。
因此结构改成两层：

```xml
<HorizontalLayout name="left_panel" width="220" sepwidth="8" padding="8,8,8,8">   <!-- 包裹层 -->
  <VerticalLayout name="left_body" padding="0,0,0,0"> ...左侧内容... </VerticalLayout>
</HorizontalLayout>
<HorizontalLayout name="preview_pane" width="320" sepwidth="-12" padding="24,24,24,24">
  <VerticalLayout name="preview_body" padding="0,0,0,0"> ...预览内容... </VerticalLayout>
</HorizontalLayout>
```

宽度用设计单位持久化在 `left_nav.ini`（`LeftPanelW` / `PreviewW`），
`CapturePaneWidthsIfChanged()` 在 WM_LBUTTONUP 时写入；拖动过程中 200ms 的
`kTimerLayoutSync` 会把预览缩略图按新宽度重取。
`m_pPreviewPane` / `m_pLeftPanel` 的类型是 `CContainerUI*`（包裹层是 HorizontalLayout，
不能再 static_cast 成 CVerticalLayoutUI）。

调试提示：自动化测试点击分隔条时要落在热区**内部**——`PtInRect` 右/下边界是开区间，
差 1px 就会点空，看起来像"拖动功能没实现"。

### 排序不再强制文件夹在前
`EntryComesBefore()` 取代了原来"`if (a.isDir != b.isDir) return a.isDir;`"的写法，
`BuildDisplayOrder()` 把文件夹和文件合并后 `stable_sort`。资源管理器本来就只按当前列
排序（文件夹不和文件分组），这样"按修改日期降序"才能把刚保存的文件排在最前面。
三处合并点（详情虚拟列表、渐进填充队列、`FlattenListing`）和图标视图都走同一个函数。

### 驱动器进度条
高度改成 UI 字体的行高（`GetTextMetrics` 实测，约等于文字高度），形状改成矩形
（`FillRect`，不再 RoundRect）。

## 当前顶部结构（自上而下）

1. 系统标题栏（客户端内已去掉「FastFile 文件管理」自定义标题行）
2. **选项卡**
3. **收藏**（★ 收藏 + 可拖入固定）
4. **工具栏**（新建/剪切复制…/排序/查看/预览开关）
5. **地址栏**（与面包屑合并：默认面包屑；点击进入编辑；Enter 导航；Esc/失焦回面包屑）+ **搜索**（框内占位「搜索」；「含子目录」复选框；无外侧放大镜/清除按钮）

## 已实现能力（摘要）

- 浏览、后台复制、前台恢复、多选、删除/重命名/新建文件夹
- **复制与移动共用同一套后台任务引擎**：状态栏进度（项数/字节/百分比/当前文件）+ 取消按钮；
  同盘移动走原子改名（瞬时），跨盘移动走"带进度复制 + 删源"，不再弹系统 SHFileOperation 对话框
- **撤销（Ctrl+Z，也挂在「更多」菜单里）**：新建文件夹、移动（整批一次还原）、直达重命名路径
- Shell 右键（IContextMenu2/3）、卷标、隐藏项开关
- 左树 + 快速访问、多标签与路径记忆、前进后退
- 六种视图、异步缩略图、拖放、递归搜索
- 右侧预览（元数据 + 图/视频帧）；空选时尽量空白
- DPI PerMonitorV2；`UiTokens.h` 设计令牌
- 详细信息：名称列 Shell 小图标（`bkimage`，勿用 `CControlUI`+`foreimage`）
- 图标：PNG alpha；文件夹/盘符/工具栏用 HICON，勿对文件夹用 `SIIGBF_ICONONLY`（会黑框）

## 快捷键

| 键 | 作用 |
|----|------|
| Ctrl+C / Ctrl+X / Ctrl+V | 复制 / 剪切 / 粘贴（粘贴走后台任务，有进度与取消） |
| Ctrl+Z | 撤销（新建文件夹、移动、直达重命名） |
| Ctrl+A | 全选（详细信息视图与图标视图都支持） |
| Ctrl+Shift+N | 新建文件夹 |
| Ctrl+T / Ctrl+W | 新建标签 / 关闭当前标签 |
| Ctrl+F 或 F3 | 聚焦搜索框并全选 |
| Alt+D | 聚焦地址栏（资源管理器习惯） |
| Alt+←/→、Alt+↑、Backspace | 后退 / 前进 / 上级 / 后退 |
| F2 / F5 | 重命名 / 刷新 |
| Delete / Shift+Delete | 删除到回收站 / 永久删除（不进回收站） |
| Alt+Enter | 显示属性 |
| Esc | 退出地址编辑 / 取消选择 |

## 已知坑

1. **DuiLib `CControlUI` 只认 `bkimage`，不认 `foreimage`**（按钮才有 foreimage）
2. **不透明 BMP + 强制 A=255** → 灰底白框 / 白底黑框；必须 PNG + 真 alpha
3. **`main.xml` 标签不匹配** → MessageBox「加载资源文件失败」后 ExitProcess
4. 预览图宽度须 ≤ `PreviewPaneW - 2*PreviewPad`
5. 每文件夹视图记在 `folder_views.ini`，可能覆盖会话默认视图
6. **地址栏退出编辑态必须交还键盘焦点**：DuiLib 只隐藏容器、原生 Edit 仍持有键盘焦点，
   导致后续所有 `WM_KEYDOWN`（Ctrl+A/C/V/X/Z、Del、F2…）被隐藏输入框吞掉。
   `ExitAddressEditMode()` 里已通过隐藏 edit 控件本身 + `ReturnFocusToFileView()` 修复；
   改动这段时务必保留，`IsEditingText()` 也要继续排除"已退出编辑态的地址框"
7. **关窗必须退出进程**：DuiLib 的 `WindowImplBase::OnClose` 只清 `bHandled`，不会
   `PostQuitMessage`，所以关掉窗口后进程会残留（还会一直占用 `FastFile.exe`，导致重新编译
   报 LNK1104）。已在 `HandleMessage` 的 `WM_DESTROY` 里补上 `PostQuitMessage(0)`
8. **窗口尺寸按显示器工作区钳制**：`MainWnd.Dpi.cpp` 的 `ClampSizeToWorkArea` /
   `ClampRectToWorkArea` 会在"设计尺寸 × DPI"超出工作区时退让，并保证窗口不被推出屏幕。
   本机 150% 缩放（屏幕 2560×1440，工作区 2560×1368）时设计尺寸装得下；只有小工作区
   （如 1366×768 笔电 + 150%）才真正触发
9. **外部脚本量窗口/屏幕坐标前必须声明 DPI 感知**：PowerShell 默认不是 DPI 感知进程，
   `GetWindowRect`/`SetCursorPos` 拿到的是被按 DPI 缩放的虚拟坐标，而 DWM 接口返回物理
   坐标。两者混用会得出"窗口比屏幕还大"之类的错误结论（已踩坑一次），排查窗口问题时
   先调用 `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`
10. **`CLabelUI` 的文字位置由 `textpadding` 决定，不是 `padding`**：`padding` 只参与布局，
    改它不会移动文字。另外 `textpadding` 会缩小文本框，压太多会裁字——要抬升文字又要留足
    行高，得同时把控件高度加上等量（见 `fav_bar_label` 的处理）
11. **左侧区块标题的字体在代码里被二次设置**：`ApplyUiChromeTokens()` 会覆盖 `main.xml`，
    之前它强制 font 3（10px），导致"小标题比正文还小"的层级倒置。改标题样式时两处都要看
12. **文件夹空白处的右键菜单不等同于资源管理器的**：`IShellFolder::CreateViewObject` 拿到的
    只有 Shell 自己的项（在终端中打开/授予访问权限/新建/属性…），资源管理器显示的
    查看/排序方式/刷新 来自"视图"层，必须自己补（见 `ShowShellBackgroundContextMenu`）。
    另外 FastFile 用自己的剪贴板，Shell 看不到，所以"粘贴"也要自己加
13. **DuiLib 控件默认最大尺寸是 9999**（`m_cxyMax`）：给列表占位行设高度时必须同时
    `SetMaxHeight()`，否则高度被静默钳到 9999，滚动范围随之错误。详情视图虚拟化就是
    踩了这个坑（`RebuildDetailsVirtual` / `UpdateDetailsWindow`）
14. **详情视图的选中状态不在列表项上**：行会被回收复用，所以选中记录在 `m_detailsSel`
    （入口索引）里，项索引只在可视窗口内有效。改这块要同步检查：`CollectSelectedItems`、
    `HasFileSelection`、`ClearFileSelection`、`SelectAllItems`、`ITEMCLICK/ITEMSELECT`
    与方向键处理（`DetailsMoveCursor`）
15. **清空列表前必须 `ResetDetailsVirtualState()`**：详情视图缓存了占位行指针，任何
    `m_pFileList->RemoveAll()`（例如切到图标/平铺视图）都会析构它们；后台定时器若再用
    旧指针做虚函数调用就是一次"野调用"崩溃（`0xc0000005`，WER 常报"模块 unknown、
    偏移 0"）。`UpdateDetailsWindow` 里还有一道"指针是否仍属于列表"的校验兜底
16. **别用 `DrawIconEx`/`AlphaBlend` 当缩放器**：两者都无高质量重采样，小尺寸下明显
    发糊。取图要挑"真实尺寸"匹配的系统列表，缩放一律走 GDI+ `HighQualityBicubic`
    （详见「图标 / 缩略图渲染管线」）
17. **`session.ini` 是 UTF-16LE（带 BOM）**：用别的编码重写会让路径变乱码，程序回落到
    默认目录。要改会话状态就让它自己写，或用 `[System.Text.Encoding]::Unicode`
18. **`third_party\duilib` 有本项目的小改动**（不是 submodule，改动随仓库走）：
    `UITileLayout` 增加了 `columnfirst`（竖向优先排列）。升级/替换 DuiLib 时要保留，
    否则列表视图会退回横向排布
19. **DuiLib 的 splitter 只有在控件本身是固定宽/高时才有效**（拖动改的是 `m_cxyFixed`），
    且 `sepwidth/sepheight` 为负表示热区在左侧/上部

## 崩溃排查流程

已内置自诊断，定位一次崩溃只需一分钟：

1. 程序崩了以后看 `%LOCALAPPDATA%\FastFile\last_crash.txt`，里面有异常码、出错地址
   以及它相对哪个模块的 **RVA**；
2. 用构建时生成的 `build\Release\FastFile.map` 反查该 RVA：

```powershell
Select-String -Path build\Release\FastFile.map -Pattern '0001:000<RVA>' |
  Select-Object -First 1
```

   映射行格式：`0001:00034120 ?RebuildDetailsVirtual@CMainWnd@@AEAAXXZ 0000000140035120 f
   MainWnd.Views.obj`，最后一项直接给出源文件。

   若 `last_crash.txt` 显示 `<unknown>`，说明是跳转到了无效地址（典型是已释放对象被
   再次调用），此时看 WER 里最近一次 `Application Error` 事件的偏移，或用 dumpbin
   反汇编该 RVA 附近，看它是"对谁的虚函数调用"：

```powershell
dumpbin /DISASM /NOBYTES build\Release\FastFile.exe > disasm.txt
```

## 会话/配置文件（通常在 `%APPDATA%\FastFile\`）

- `session.ini`、`folder_views.ini`、`favorites.txt`、`left_nav.ini` 等

## 建议下一轮方向（用户曾提过）

- 继续对齐资源管理器 / 360 密度与图标风格（自有 Shell 图标）
- 预览窗格继续打磨（视频首帧质量取决于 Shell 缩略图缓存；可考虑自绘取帧）
- 设置面板（用户明确推迟：等程序成熟后再做"取代资源管理器"）
- 安装包 / 发布流程

## 给新 AI 的工作方式

1. 先读 `UiTokens.h`、`skin/main.xml`，再按功能定位到对应编译单元（见「源码结构」）后修改
2. 改动走 Git：小步提交（`git commit` + `git push`），不要再往工程根堆 `bak_*`
3. 改完：校验 XML 良构 → Release 构建 → 杀进程 → 清图标缓存 → 启动验收
4. 用户界面中文；回复用户可用中文

## 历史备份（已移出工程）

拆分前的 169 个临时产物（10 个 `bak_*` 快照目录、源码 `.bak_*`、一次性补丁脚本、`_inspect` 转储）集中归档在
`C:\Users\JINLONG\文档\Grok\_FastFile_attic_20260928\`，按来源分子目录存放；确认 Git 历史够用后可整体删除。

---
本文档由 FastFile 开发助手生成，便于换 AI 继续开发时粘贴或直接打开。
