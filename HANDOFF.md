# FastFile — 交接说明（给后续 AI / 开发者）

更新日期：2026-09-29（Asia/Shanghai）

> 2026-09-28：已纳入 Git 版本管理；原 8000 行单文件 `src\MainWnd.cpp` 已拆分为 14 个编译单元（见「源码结构」）。
>
> 2026-09-29：完成「第二批～第十二批」共 11 轮修复/打磨（含安装程序、应用图标、右键菜单、崩溃修复、可选文件夹打开接管）。
>
> 2026-09-29（晚）：第十三批（顶部功能区整体改版）+ 第十四批（标签栏改为自绘 `CTabStripUI`，
> 标题行按 Win11/360 规范重排，`Ctrl+Tab` 与拖出标签开窗修复）。DWM Mica Alt 仍默认关闭，
> 原因见「DWM / Mica 现状」。
> 面向使用者的版本记录见 [CHANGELOG.md](CHANGELOG.md)；下面的「开发日志」按批次保留完整细节。

## 开发日志索引（2026-09-29）

| 批次 | 提交 | 主题 |
|---|---|---|
| 第一批 | `864fc2c` | 图标/缩略图锯齿：按 DPI 选真实尺寸的系统图标列表 + GDI+ 高质量缩放 |
| 第二批 | `506b31c` `453292b` | 驱动器磁贴自绘、面包屑自适应、列表视图竖排+自适应列宽、缩略图按长宽比、预览窗格可拖宽 |
| 第三批 | `2934696` `320ccd8` | 平铺文字版式、侧栏/预览分隔条（含加宽热区+光标提示）、严格列排序、矩形进度条 |
| 第四批 | `2b06599` | 三个视图 bug（图标模式驱动器版式 / 切换视图文字重叠 / 真实文件类型）+ **自包含安装程序** |
| 第五批 | `46b64c1` | 关最后一个标签即退出、非详细信息视图文件夹在前、关闭耗时 14s→0.2s |
| 第六批 | `973db58` | 应用图标（System Folder ico）、启动固定「此电脑」 |
| 第七批 | `b8ea2bc` | **进入目录崩溃（DuiLib Invalidate 释放后使用）**：导航改为延迟派发；崩溃日志加调用栈；图标换 Plex hdd-windows |
| 第八批 | `46d67ea` | 右键菜单清理：去掉旧版 PowerShell 动词、第三方「用 X 打开」、空子菜单 |
| 第九批 | `fd1a154` | 驱动器右键改走原生 Shell 菜单；删项后多余分隔线 |
| 第十批 | `61674ab` | 磁盘「属性」作用于选中路径（SHObjectProperties）；磁盘根绑定走桌面+完整路径 |
| 第十一批 | `2cd0b0e` | 可选接管文件夹 / 目录 / 磁盘的默认打开动作；外部路径转发到现有窗口新标签；关闭或卸载恢复 |
| 第十二批 | `b01e569` `707ee98` | 标签切换状态同步；“此电脑”磁盘卡片响应式分列；预览栏最小宽度与元数据列优化；滚动条统一宽度 + 预览导轨并入调宽手柄 |
| 第十三批 | `065fde1` `ea6f6bd` `4d73a16` `30056a5` `d650e91` `32ade3d` `69637df` | 关闭确认框、快速访问原生右键菜单 + 拖动排序、双色命令图标、顶部功能区整体改版（标题栏并入标签行、地址栏在命令栏之上）、收藏栏位置与路径框缩放 |
| 第十四批 | 待提交 | 标签栏改为自绘 `CTabStripUI`；DuiLib XML 骨架按规范重排（32/26/28/28）；窗口按钮改 Shell 字形；`Ctrl+Tab` 修复；拖出标签按源窗口尺寸开窗 |
| 第十五批 | 待提交 | Fluent 密度（36/36/36/40）+ `inset` 消除栏间空隙；收藏栏 Explorer 化并搬走「配置文件」；面包屑首段带此电脑图标；导航名本地化；「含子目录」按需显示；此电脑详情页修正 |

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
- 图标缓存版本：`_v8.png`（HICON → PNG 真透明）；改导出逻辑时升版本并清缓存
- 安装程序：`powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1 -Version 1.0.7`
  （只用系统自带的 .NET `csc.exe`，不需要 Inno/NSIS/WiX；产物在 `dist\`，`dist/` 已在 .gitignore）
- 应用图标：`res\FastFile.ico` + `res\FastFile.rc`（资源 id 1）。**换图标后要 touch 一下 .ico**，
  否则 MSBuild 认为 rc 不需要重编（`Copy-Item` 会保留源文件的旧时间戳）

## 第一批：图标 / 缩略图渲染管线（修锯齿）

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

## 第二批：视图/布局问题（6 项）

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

## 第三批：磁贴文字 / 可拖动分隔条 / 排序 / 进度条

### 平铺视图的文件名
磁贴改成 230×72（设计）：文件名最多两行，大小单独一行，不再被裁掉。
**坑**：DuiLib 开 `multiline` 后文字总是从文本框顶部开始画（`valign` 被忽略），
所以 `BindIconTile`／`TryReuseIconsView` 会用 `MeasureTextWidthPx()` 先估名字占几行，
再按「行数 × 行高」手工算 `textpadding` 的顶部留白，才能看起来垂直居中。

### 左侧栏 / 预览栏都能拖宽度（自带热区 + 光标提示）
**不用 DuiLib 的 sep**，改成自己做命中测试（原因见下）：

1. `HitTestPaneDivider(x,y)` 判断鼠标是否落在分隔线两侧 `±PaneDividerBandPx()` 内
   （8 设计像素、约 12 物理像素，横跨两侧）；
2. `WM_SETCURSOR` 命中就把光标设成 `IDC_SIZEWE`（拖动过程中也保持）；
3. `WM_LBUTTONDOWN` 命中 → `SetCapture` 并记下起始宽度；`WM_MOUSEMOVE` 实时
   `ApplyPaneDragWidth()`（左栏改右边缘、预览栏改左边缘，各自有 min/max）；
   `WM_LBUTTONUP` 释放并 `CapturePaneWidthsIfChanged()` 落盘。

结构上左右两侧都改成“包裹层 + 内容层”两层，因为要改的是包裹层的固定宽度：

```xml
<HorizontalLayout name="left_panel" width="220" padding="8,8,8,8">   <!-- 包裹层 -->
  <VerticalLayout name="left_body" padding="0,0,0,0"> ...左侧内容... </VerticalLayout>
</HorizontalLayout>
<HorizontalLayout name="preview_pane" width="320" padding="24,24,24,24">
  <VerticalLayout name="preview_body" padding="0,0,0,0"> ...预览内容... </VerticalLayout>
</HorizontalLayout>
```

宽度用设计单位持久化在 `left_nav.ini`（`LeftPanelW` / `PreviewW`），
`CapturePaneWidthsIfChanged()` 在 WM_LBUTTONUP 时写入；拖动过程中 200ms 的
`kTimerLayoutSync` 会把预览缩略图按新宽度重取。
`m_pPreviewPane` / `m_pLeftPanel` 的类型是 `CContainerUI*`（包裹层是 HorizontalLayout，
不能再 static_cast 成 CVerticalLayoutUI）。

**为什么不用 DuiLib 的 sep**：①`sepwidth` 只有 `CHorizontalLayoutUI` 实现、
`sepheight` 只有 `CVerticalLayoutUI` 实现（写在别的类型上会被静默忽略）；
②sep 热区**必须完全落在容器的 padding 区域内**，否则被子控件挡住收不到事件，
因此热区被限制在 8px 左右；③sep 的热区不跨两侧，鼠标必须精确压在容器边缘，
实测很不好抓。自己命中测试后热区可以横跨分隔线两边的 24px，而且光标提示同步出现。

调试提示：`WM_SETCURSOR` 的 `lParam` 低字要判 `HTCLIENT`；`GetCursorPos`+`ScreenToClient`
取的是物理坐标（进程需 PerMonitorV2 感知，否则坐标会被 /1.5 虚拟化而对不上）。

### 预览滚动条 / 调宽热区合并成同一条轨道
左侧功能区的滚动条（`dir_tree` 的竖直条）本来就贴着分隔线，所以预览区改成同样的做法：
`preview_pane` 里第一个子控件是真正的 `CScrollBarUI`（`preview_rail`），宽度、轨道色
（`#FFF7F7F7`）和滑块色（`#FFB5B5B5`）都由 `StylePreviewRail()` 按
`UiTokens::SidePaneScrollBarW`（12 @96dpi）设置，和导航滚动条一致；它既是滚动条，也是
“左右调宽”的那条热区——一套命中测试同时管两种手势。

- `IsPreviewScrollBarHit()` 只认这条轨道的矩形；`HitTestPaneDivider()` 不再为预览区返回
  2，于是压在分隔线上的 ±8 设计像素热区取消了。**顺带修掉一个老 bug**：那条热区横跨
  分隔线，把紧贴左侧的文件列表竖直滚动条整条吞掉，`WM_SETCURSOR` 和按下事件都变成调宽。
- `preview_body` 仍保留 DuiLib 自己的竖向滚动条（滚动范围 / 滚轮 / `SetScrollPos` 移子控件
  都靠它），但被 `StylePreviewRail()` 收成 0 宽 + 全透明，所以画面里只有轨道；
  `SyncPreviewRail()` 每次布局（200ms `kTimerLayoutSync`）把 range/pos 镜像到轨道上，
  范围 0 时把轨道滑块色设成 0（否则 DuiLib 会画一条满高的灰色块）。
- 轨道保持 `mouse` 开启，只为把鼠标滚轮透传给 `preview_body`（`CScrollBarUI::DoEvent`
  末尾会 `m_pOwner->DoEvent(event)`）。**别写成 `mouse="false"`**：命中测试里
  `CControlUI::FindControl` 会跳过 `IsMouseEnabled()==false` 的控件，
  `WM_MOUSEWHEEL` 于是落到外层 pane 上，滚轮在轨道上失效。
  按下 / 拖动仍由 `CMainWnd::HandleMessage` 在控件分发之前接管：`m_previewRailGesture` 先待定方向，
  横向拖动 → `ApplyPaneDragWidth(2, …)`，纵向拖动 → 手动 `SetScrollPos`，原地松开
  （没拖动）→ 按滑块上下位置翻页（`PreviewRailThumbRect()` 用 DuiLib 同一套公式算滑块）。
  顺带补上 `::ReleaseCapture()`，之前纵向滚完不释放捕获。

**坑**：`CHorizontalLayoutUI::SetPos` 里 `cxFixed += sz.cx + padding.left + padding.right`，
即包裹层的 padding 会**额外占宽**（`m_cxyFixed.cx` 是控件矩形，padding 不计入其中）。
以前 `preview_pane` 有 24 设计像素 padding，于是预览栏右侧白留了一条 48 设计像素的空档，
分隔线也比实际内容靠左。现在 padding 全部下放到 `preview_body`
（左内缩 = 轨道宽度，其余 24），分隔线位置就等于预览栏真正的左边缘，预览文字与分隔线的
距离、内容宽度都保持原样。

### 分隔线两侧的滚动条宽度
预览导轨刚好贴着文件列表的竖直滚动条，两条一起看就是同一根“柱”。之前列表用
`UiTokens::ScrollBarW = 8`、导航与预览用 `SidePaneScrollBarW = 12`，列表滑块明显更窄。
现在两个 token 都是 12（`ScrollBarW` 是文件视图用、`SidePaneScrollBarW` 给导航 / 预览），
`main.xml` 里的 `Default VScrollBar/HScrollBar` 与 `vscrollbarstyle/hscrollbarstyle` 也同步
改成 12，免得以后又被 Default 属性的 8 覆盖。
另外 `StyleVerticalScrollBar/StyleHorizontalScrollBar` 把轨道底色改成透明（只画滑块，
Windows 11 样式）：两条 `#FFF7F7F7` 轨道紧挨着会连成一条 36 设计像素的浅色带，看上去
比滑块宽一倍，仍然像“宽度不一致”。

### 快速访问：内置行与运行时收藏行对齐
右侧收藏行（`RebuildLeftPinnedFavorites` 里 `new CButtonUI` 的那些）比上面四行偏左 12 物理
像素、行矩形也宽 24 像素。根因是 **DuiLib 的布局会用子控件自己的 padding 内缩它的矩形**：
`CVerticalLayoutUI::SetPos` 里左对齐分支是 `rcCtrl.left = rc.left + rcPadding.left`，
宽度是 `szAvailable.cx - padding.left - padding.right`。内置四行在 `main.xml` 里带
`padding="4,0,4,0"`、主题里又设成 `8,0,8,0`（12 物理像素），而运行时新建的按钮 padding
是 0，于是两边矩形和图标 / 文字基准全都不一样。

现在两边都走 `CMainWnd::ApplyQuickAccessRow()`：行 padding、图标偏移（`NavIconPad`）、
文字 padding（`NavIconPad + NavIconPx + NavIconTextGap`）只写一份，
`ApplyChromeShellIcons` 的 `applyFav` 和 `RebuildLeftPinnedFavorites` 都调它。
实测两条行的 rect 都是 `36..327`、图标 ink 都从 x=49 起，选中高亮也等宽。

### 命令栏双色图标
`main.xml` 里的命令栏按钮不用 Segoe MDL2 单色字形，而是 `GetCommandIconBmp(kind, px, dim)`
用 GDI+ 在 20×20 网格上自绘成 PNG（缓存进 `m_iconCache`）。两套配色取自资源管理器命令栏：

| 状态 | 轮廓 | 蓝色点缀 | 次级浅色 | 更多按钮圆点 |
|---|---|---|---|---|
| 可用（点亮） | `#555555` | `#0078D4` | `#AAAAAA` | `#1B1B1B` |
| 不可用（熄灭） | `#C2C2C2` | `#A3CEEF` | `#E1E1E1` | `#C2C2C2` |

`ApplyCommandIcon()` 按 `IsEnabled()` 选图（DuiLib 的 disabled 状态不会自动替换 foreimage，
必须自己换图），图标尺寸沿用 `UiTokens::ToolbarGlyphPx`：纯图标按钮居中、带文字按钮按
`ToolbarIconPad` 左对齐，`textpadding` 与原来一致。查看图标是资源管理器那种“显示器 + 底座”，
删除图标的内侧两道竖线用次级浅色。

### 收藏栏的垂直位置 / 第一个收藏的左移 / 路径框漏缩放
三个小问题的根因各不相同：

1. **收藏夹紧贴标签栏**：`favorites_strip` 是 `HorizontalLayout`，它把高度撑满收藏栏，
   但内部子控件默认**顶对齐**——所以收藏夹被画在行顶部（实测离标签栏只有 3px，下面却空
   了 40+px）。修法：`favorites_strip` 加 `childvalign="vcenter"`。再把收藏栏的上
   padding 提到 `FavBarPadTop`(12)、下 padding 设 0：因为 DuiLib 会把**子控件 padding
   也计入占位**（同快速访问那节），这样收藏夹正好落在标签栏与地址栏之间（实测上 32px、
   下 29px）。
2. **第一个收藏离“★ 收藏”太远**：`FavLabelW` 72 → 56（`ScaleNamedFixed` 会把它应用到
   `fav_bar_label`，XML 同步改），收藏区整体左移约 24 物理像素。
3. **地址栏路径框又细又小**：`path_host` 的 XML 高度 32 是**物理像素**——它从来没进过
   `ScaleNamedFixed()`，所以 150% 下只有 32px 高，而旁边的搜索框是 `DpiScale(32)`=48px。
   现在两个框都用 `UiTokens::FieldH`(32 设计) 走 `ScaleNamedFixed()`，`SearchBoxH` 也统一
   成 32。

标签字号：新增的 `<Font id="7">` 从 13 调到 14，让标签文字与 32 设计高的标签卡片匹配。

### 标题栏并入标签行（窗口按钮 + 拖动）
`main.xml` 里删掉了 `title_bar`（连同 “FastFile” 标签），把 `minbtn`/`maxbtn`/`restorebtn`/
`closebtn` 移到 `tab_bar` 末尾（名字不变，DuiLib 的最小化/最大化/还原/关闭行为照旧），
标签行右侧不再留 padding，按钮贴住窗口右边缘。

**坑**：`<Window caption="0,0,0,32">` 把顶部 32 设计像素当作标题区，
`WindowImplBase::OnNcHitTest` 对“不是 Button/Option/Text 的控件”返回 `HTCAPTION`——标签行
从第一行开始，于是点标签卡的空白处会变成拖窗口。现在 `CMainWnd::HandleMessage` 自己处理
`WM_NCHITTEST`：命中 `tab_bar` 且不在最外层 sizebox 内时，向上回溯控件链，遇到
Button/Option/Edit/Label 就返回 `HTCLIENT`（标签、×、+、窗口按钮照常点击），否则返回
`HTCAPTION`（拖窗口）。其它区域仍交给 DuiLib，窗口四边缩放不变。
实测：空白处 HTCAPTION、标签/×/+/关闭按钮 HTCLIENT，最大化/还原/关闭按钮都正常。

### 顶部功能区改版（标签卡片 / 地址栏在命令栏之上 / 命令栏白底）
按资源管理器参考重排了 `main.xml` 里的顶部顺序：`tab_bar` → `favorites_bar` →
`address_bar` → `toolbar`。**顺序很重要**：参考里地址栏在命令栏*上方*，命令栏与文件区同为
白底（`#FFFFFF`）并用 1px `#DCDCDC` 分隔，所以 `ApplyUiChromeTokens()` 里原来“所有 band
都用 surface + #E5E5E5”的循环被拆开单独覆盖：

- `tab_bar`：底色 `#FFDBDBDB`（比 chrome surface 深一档的带）+ 底部 1px `#FFCECECE`。
- `toolbar`：底色改成 `ColorContent`（白），其余 band 仍是 surface。
- `path_host` / `search_box`：白底 + 1px `#FFD6D6D6` + 圆角 `FieldRound`(8 @96dpi)。
  **坑**：`MainWnd.Dpi.cpp` 里有个统一把一批控件设成 4px 圆角的循环，原来包含
  `path_host`/`search_box`，会把 XML 里的 8px 静默改回 4px——现在这两个控件从那个循环里
  移出，单独设 `FieldRound`。

标签卡片由 `RebuildTabStrip()` 生成：`TabCardH`(30) 高的按钮，`TabCardRound`(6) 圆角、
标签卡片由 `RebuildTabStrip()` 生成，按 Explorer 的样子分成两态：

- **未选中**：`bkcolor` 透明，只有图标 + 名称 + ×，直接坐在深灰底带上；hover 才给一层
  `ColorTabIdleBg #D2D2D2` 的淡色。
- **当前页**：**卡片挂在包裹层（host）上**而不是按钮上——`CHorizontalLayoutUI` 设
  `bkcolor=ColorTabActive(#F3F3F3)`、`bordersize="1,1,1,0"`（**故意不画底边**，这样卡片
  和下面一行同色融合）、`TabCardRound` 圆角。这样图标、名称和 × 都在同一张卡片里
  （之前卡片只包住按钮，× 落在卡片外）。按钮本身透明、`align="left"`，文字从
  `tabIconPad + tabIconPx + tabTextGap` 开始；字号用新增的 `<Font id="7">`（雅黑 13），
  比正文大一号，接近 Explorer 的标签。

卡片之间的间距靠包裹层的右 padding（`TabCardGap` 5）——注意 DuiLib 的布局里 padding 是
*控件矩形之外*的空间，所以卡片底色不会把间距涂满。
行高与其它 band 一起走 `ScaleNamedFixed()`：`TabBarH` 37、`FavoritesBarH` 32、`AddressBarH` 48、`ToolbarH` 45（对齐参考图的 56/—/72/68 物理像素）；
地址栏的前进/后退/上级/刷新改成 `NavGlyphPx`(20) 的位图图标（原来是 16 号 MDL2 字形），
按钮 40×32；命令栏的图标按钮加宽到 40、分隔线加高到 24，整体更接近参考的疏密。

### 快速访问区高度 + 可拖动的分隔线
两个坑叠在一起：

1. `CVerticalLayoutUI::SetPos` 把**子控件自己的 padding 也算进占位空间**
   （`cyFixed += sz.cy + padding.top + padding.bottom`），而 `UpdateLeftQuickAccessSpacing`
   又把剩余空间对半分到上下 padding。于是快速访问块实际占的高度是
   `2 * 固定高度 - 内容高度`：固定高度 208 设计像素 → 实际占 256。旧代码的
   `LeftQuickMinH + 行数 * NavRowH`（内置四项被算了两遍）又把最小高度推到 320，块被撑得很高，
   而且**已经等于最小高度，往上拖也缩不动**——所以看起来“太高又调不了”。
   现在最小/自动高度都用 `max(LeftQuickMinH, 行数 * NavRowH + 2 * SpaceXs)`，
   并且只在**行数变化时**做一次自动收缩（`QuickFitRows2` 记在 left_nav.ini 里，
   换了键名是为了让旧文件重新适配一次），之后保留用户拖动的高度。
2. DuiLib 自带的 sep 热区只有容器最后几个像素，而可见的分隔线（`left_nav_divider_host`）
   在**控件矩形之下 `padding.bottom`** 处，所以那条线根本抓不到。现在自己做命中测试
   （`HitTestLeftNavDivider`，边界 = `rect.bottom + padding.bottom` 上下各 12 设计像素），
   拖动时高度按 `Δy / 2` 变化，保证分隔线 1:1 跟手；光标为 `IDC_SIZENS`。

### 命令栏按钮的可用/不可用两态
`UpdateCommandBarState()`（在 `UpdateListingStatusTip()` 与 `ApplyCopyUiState()` 里调用）按当前
选择设置 `SetEnabled`：剪切/复制/共享/删除需要选中、重命名需要只选中一个、粘贴需要剪贴板
非空且没有复制任务；新建/排序/查看/更多 始终可用。
图标用 `GetCommandIconBmp(kind, px, dim)` 出两套位图（dim = 把原色向工具栏底色
`#F3F3F3` 混合 62%），`ApplyCommandIcon()` 按 `IsEnabled()` 选图，所以按钮禁用时图标变灰暗、
可用时立刻点亮——和资源管理器一致（DuiLib 的 disabled 状态本来不会替换 foreimage，
必须自己换图）。

### 关闭多个标签页时的确认框
`WM_CLOSE` 里先判 `m_tabs.size() > 1`，走 `ConfirmCloseWithMultipleTabs()`，用户选“取消”
就 `return 0` 把消息吃掉（窗口与标签页都保留），选“关闭”才 `m_closeConfirmed = true`
继续原来的保存 + 关闭流程。

**坑**：`TaskDialogIndirect` 要求进程激活 **comctl32 v6**。`cmake/FastFile.manifest` 原来只有
DPI/兼容性节点，没有 `Microsoft.Windows.Common-Controls 6.0.0.0` 依赖，调用会直接失败
（`FAILED(hr)` → 我们的兜底返回 true，于是“确认框”一闪而过、窗口直接关掉，看起来像没写）。
已在 manifest 里补上 `<dependency>`；DuiLib 自己画控件，所以 v6 只影响系统对话框。
“关闭 / 取消”用自定义按钮 id（1001/1002），所以判断的是 `pressed == 1001`；Esc / 右上角 X
返回的是 IDCANCEL(2)，同样按“取消”处理。

### 快速访问：原生右键菜单 + 拖动排序
四条内置行不再写在 `main.xml` 里，改成运行时统一在 `left_quick_rows` 里建（`RebuildLeftQuickRows`），
模型是 `m_quickRows`（`QuickRow{ isThisPc, builtIn, path, label }`），内置四项与用户固定项
共用一条有序列表。持久化仍然用 `quick_access.txt`，但写的是**完整顺序**：`::ThisPC` 表示
“此电脑”，其余每行一个路径。老文件（只有固定项）会在加载时把缺失的内置项按默认顺序插到
前面，所以升级后顺序不变。

- 行样式仍走 `ApplyQuickAccessRow()`（行 padding / 图标偏移 / 文字 padding 一份）。
- 点击/拖动由 `CMainWnd::HandleMessage` 在控件分发之前接管：`WM_LBUTTONDOWN` 命中行就
  `SetCapture` 并记 `m_quickDragIndex`；`WM_MOUSEMOVE` 超过 4 设计像素进入拖动，按每行中心
  算出目标槽位并 `MoveQuickRow()` 实时重排；`WM_LBUTTONUP` 没拖动就 `ActivateQuickRow()`
  （所以按钮自身的 notify 被忽略：名字前缀 `fav_row_`）。
- 右键走 `ShowQuickRowContextMenu()`：「此电脑」用 `ShowShellBackgroundContextMenu(kThisPcPath)`
  （内部走 FOLDERID_ComputerFolder），文件夹用 `ShowShellContextMenu()`；
  `ShowShellContextMenu` / `TrackPopupShellMenu` 新增 `extraItems/outExtraCmd`，把 FastFile 自己的
  「打开 / 从快速访问中取消固定」追加在 Shell 菜单下方（id 取 9340+，远离 Shell 的
  `idCmdFirst..idCmdLast`），`TrackPopupShellMenu` 命中这些 id 时通过 `outExtraCmd` 回传。
- DPI 变化时 `OnDpiChanged` 会 `RebuildLeftQuickRows()`，因为行是运行时控件、按物理像素排版。

### 排序不再强制文件夹在前
`EntryComesBefore()` 取代了原来"`if (a.isDir != b.isDir) return a.isDir;`"的写法，
`BuildDisplayOrder()` 把文件夹和文件合并后 `stable_sort`。资源管理器本来就只按当前列
排序（文件夹不和文件分组），这样"按修改日期降序"才能把刚保存的文件排在最前面。
三处合并点（详情虚拟列表、渐进填充队列、`FlattenListing`）和图标视图都走同一个函数。

### 驱动器进度条
高度改成 UI 字体的行高（`GetTextMetrics` 实测，约等于文字高度），形状改成矩形
（`FillRect`，不再 RoundRect）。

## 第四批：三个视图 bug + 安装程序

### 超大/大/中图标下的“此电脑”
驱动器磁贴改成自绘文字后，图标视图里也套用了“平铺”的版式（文字在图标右侧 + 进度条 +
说明），看上去又乱又重叠。现在按视图分版式（`TileButtonUI::Layout`）：

| 视图 | 版式 | 内容 |
|---|---|---|
| 平铺 | `TilesDrive` | 盘名 / 进度条 / 灰色「X 可用，共 Y」 |
| 超大 / 大 / 中 | `IconDrive` | **不画进度条**，图标下方居中显示盘名 + 灰色说明 |
| 列表 | `Label` | 交给控件自身画（只有盘名） |

### 从“超大图标”切到“平铺”文字重叠
根因是**两条绑定路径不一致**：新建磁贴走 `BindIconTile`（会清空控件文字并设置自绘版式），
而视图切换时复用的磁贴走 `TryReuseIconsView`——它没做这两件事，于是控件自身的文字
和自绘文字同时画出来。现在两条路径都调用同一个 `ApplyTileText()`，从结构上消除分歧。

### 平铺视图显示真实文件类型
- 新增 `QueryShellTypeNameCached()`：按扩展名缓存 Shell 的类型名，用
  `SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES`（只看扩展名，不碰文件），
  所以 `.iso` 显示「光盘映像文件」、`.jpg` 显示「JPG 文件」，而不是笼统的「文件」；
- 详情视图的「类型」列也换用它；
- 平铺磁贴的文件名按像素宽度折行（最多两行，超出部分把省略号补在**最后一行末尾**），
  不会再出现只剩一个「…」的第三行，也不会和右边文件夹挤在一起。

### 安装程序（installer\）
只依赖 Windows 自带工具（.NET Framework 的 `csc.exe`），无需 Inno/NSIS/WiX：

```powershell
powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
```

`installer\setup.cs` 编译成 `dist\FastFile-Setup-<版本>.exe`，把 `FastFile.exe` 与
`skin\` 作为资源内嵌；安装到 `%LOCALAPPDATA%\Programs\FastFile`（当前用户、免管理员），
建立开始菜单快捷方式与「设置 → 应用」卸载项，并支持 `--quiet/--dir/--uninstall/--cleanup`。
卸载由 `%TEMP%` 中的副本完成（程序自身在安装目录里，不能自己删自己）。
文件版本号在 `installer\setup.cs` 与 `build_installer.ps1 -Version` 两处。

**踩过的坑**：IExpress 在新系统上命令行打包直接退出码 1（连最小 SED 也失败），
所以最终改成 csc 方案；`install.cmd/install.ps1/uninstall.ps1` 是那版残留，留着参考。


## 第五批：标签关闭 / 视图排序分组 / 关闭速度

### 关掉最后一个标签 = 关闭程序
`CloseTab()` 原来在只剩一个标签时只提示「至少保留一个标签」。现在改为
`PostMessage(WM_CLOSE)`，走正常退出流程（会先保存会话）。

### 文件夹分组只在“非详细信息”视图生效
`EntryComesBefore()` 里加了条件：

```cpp
if (m_viewMode != ViewMode::Details && a.isDir != b.isDir)
    return a.isDir && !b.isDir;      // 图标/列表/平铺：文件夹在前
```

详细信息视图保持“严格按点击的那一列排序”，所以按修改日期降序时最新的**文件**能排到
最前面；图标、列表、平铺视图则始终是文件夹在前（跟资源管理器的默认观感一致）。

### 关闭程序要等十几秒（已修）
症状：点标签的 × 之后要 ~14 秒进程才消失（看起来像“关不掉”）。
根因：`StopThumbWorker()` 里 `m_thumbThread.join()` 会一直等后台缩略图线程，
而那个线程可能正卡在 Shell 的视频缩略图提取里（几秒到十几秒）。

修法：
1. `StopThumbWorker()` 改成最多等 1.2 秒，超时就 `detach()`（进程马上就退，
   线程跟着进程一起结束）；
2. `wWinMain` 里的主窗口对象改为 **故意不释放** 的堆对象（注释里写明了原因）——
   这样被 detach 的缩略图/复制线程即使在退出瞬间还在跑，也不会碰到已析构的对象。

实测：`WM_CLOSE` → 0.2 秒进程退出（之前 13.9 秒）。

### 安装程序版本号
版本号不再写在 `setup.cs` 里，而是 `build_installer.ps1 -Version x.y.z` 生成
`installer\version.cs`（`BuildInfo.Version`），所以安装包文件名、注册表
`DisplayVersion`、安装完成提示三处永远一致。


## 第六批：应用图标 + 固定从“此电脑”启动

### 应用图标
1. `res\FastFile.ico`（用户提供的 System Folder 图标，内含 16/32/48/128/256 多尺寸）；
2. `res\FastFile.rc` 里 `1 ICON "FastFile.ico"` —— 资源 id 1 就是资源管理器显示的文件图标；
3. `CMakeLists.txt` 把 `res/FastFile.rc` 加进 target，MSVC 自动调用 rc.exe；
4. **DuiLib 的 `CWindowWnd::RegisterWindowClass()` 把 `wc.hIcon` 设成 NULL**，所以窗口类本身
   没有图标；`CMainWnd::ApplyWindowIcon()`（在 InitWindow 里、`ApplyDpiScaledChrome()` 之后调用）
   用 `LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cy, ...)` 按系统大/小图标尺寸各取一张，
   再 `WM_SETICON` 推给窗口 —— 标题栏、任务栏、Alt-Tab 都用它。
   验证方法：`SendMessage(hWnd, WM_GETICON, ICON_BIG/ICON_SMALL, 0)` 应返回非 0。
5. 安装包（`installer\build_installer.ps1`）加了 `csc /win32icon:res\FastFile.ico`，
   所以 Setup.exe 和开始菜单快捷方式也是同一个图标。

### 每次启动都进入“此电脑”
`LoadSession()` 解析完 ini 后直接丢弃里面的标签路径：

```cpp
    paths.clear(); filters.clear();
    paths[0] = kThisPcPath; count = 1; active = 0;
```

于是**视图模式、预览开关、含子目录、收藏栏、列宽**照旧恢复，但打开的永远是「此电脑」，
不再回到上次的目录；状态栏提示也从「已恢复上次会话」改成「已就绪」。


## 第七批：进入目录时的崩溃（UAF）+ 换图标

### 崩溃：`CControlUI::Invalidate` 访问违例（0xC0000005）
用户报「进入 `C:\Users\JINLONG\图片\GIRLS\刘亦菲`（233 项）时崩溃」。崩溃日志
（`%LOCALAPPDATA%\FastFile\last_crash.txt`）给出 `rva=0x5FDFB`，配合 `FastFile.map`
定位到 DuiLib `CControlUI::Invalidate`；用 dumpbin 反汇编该函数，`+0x1B` 正好是
**第一条虚函数调用**（`call qword ptr [rax+1B8h]`），说明 `this` 指向的内存已不是控件
——典型的释放后使用（野指针）。

根因：**导航会销毁正在派发点击事件的那个控件**。双击目录项/面包屑/树节点/标签 →
事件处理器里直接 `NavigateTo()` → `RefreshListing()` → `RemoveAll()` 把当前控件
（tile / 列表行 / 标签按钮 / 面包屑段 / 树节点）删掉；事件通知返回后 DuiLib 还要继续
操作同一个按钮（清 `UISTATE_PUSHED` → `Invalidate()`）→ 崩溃。项目越大、重建越重，
被释放的内存越容易被新控件复用，所以表现为「偶发」。

修法：`NavigateTo()` 不再同步执行，而是投递 `kMsgDeferredNav`（payload 为
`std::pair<std::wstring,bool>`），真正干活的 `NavigateToNow()` 在下一轮消息里跑。
所有入口（双击、面包屑、树、标签、菜单、地址栏、前进后退）自动受益。

### 崩溃日志增强
除了异常码 / RVA / 模块 / 线程，现在还写：

- `main_tid`：主线程 ID，用来区分 UI 线程崩溃还是后台缩略图线程；
- `stack00..stack39`：`模块名+0x偏移` 的调用栈，配合 `FastFile.map` 可直接反查出
  **调用者函数**（Release 下可能被优化掉几帧，但最近的调用者都在）。

排查套路：`Select-String -Path build\Release\FastFile.map -Pattern '0000000140XXXXXXX'`
（把 stack 里的 FastFile.exe+0x… 加上 0x140000000）。

### 图标（第二次更换）
换成 `Cornmanthe3rd-Plex-System-hdd-windows.ico`（9 档：16/24/32/48/64/72/96/128/256，
全 32bpp）。CMake 里给 `res/FastFile.rc` 加了 `OBJECT_DEPENDS res/FastFile.ico`，
否则换图标后 rc 不会重编、exe 里还是旧图标。
**注意**：`Copy-Item`/`File.Copy` 会保留源文件的修改时间，若把图标换成本身时间戳更旧的
文件，依赖检查仍会认为不需要重编 —— 换完图标 `touch` 一下 `res\FastFile.ico` 或删掉
`build\Release\FastFile.rc.res` 再构建。


## 第八批：右键菜单（去掉 PowerShell / “用 X 打开” / 空子菜单）

### 菜单来源（用户要求核实）
文件夹空白处右键走的就是 **Windows 原生 Shell 接口**：

```
SHGetDesktopFolder → IShellFolder::BindToObject
    → IShellFolder::CreateViewObject(IID_IContextMenu)
    → IContextMenu::QueryContextMenu(CMF_NORMAL|CMF_EXPLORE|CMF_EXTENDEDVERBS)
    → TrackPopupMenuEx + IContextMenu2/3 消息转发（WM_INITMENUPOPUP/DRAWITEM/MEASUREITEM/MENUCHAR）
```

所以「在终端中打开」「在此处打开 PowerShell 窗口」「用 XXX 打开」「授予访问权限」「新建」
这些都是 **Shell / 已安装的 Shell 扩展**给出来的，不是我们写死的（源码里搜不到任何一处
硬编码文本）。我们只额外补了 查看 / 排序方式 / 刷新 / 粘贴。

### 新增 `PruneShellMenu()`
`QueryContextMenu` 之后、`TrackPopupMenuEx` 之前对菜单做一次清理：

1. **预填并清理空子菜单**：对每个 `MF_POPUP` 先调 `IContextMenu2::HandleMenuMsg(
   WM_INITMENUPOPUP)`，若子菜单仍是 0 项就删掉该项（并 `DestroyMenu`）。
   —— Windows 11 对“文件夹背景”的 *授予访问权限* 就是空的（实测背景菜单与
   `SHCreateShellFolderView` 的视图菜单都是 0 项），Explorer 也是直接不显示；
   而 **选中某个文件夹**时该子菜单有内容，所以那种情况下会保留。
2. **屏蔽旧版 PowerShell 动词**：`GCS_VERBW` 得到 verb，`_wcsicmp(verb, L"Powershell")`
   命中就删（Explorer 在存在“在终端中打开”时也会隐藏它）。
3. **屏蔽第三方「用 X 打开」**（仅背景菜单）：菜单文本以“用”开头且含“打开”。
4. 清理因删除而产生的连续/首尾分隔线。

两个入口都调用：背景菜单 `backgroundMenu=true`，选中项菜单 `backgroundMenu=false`
（选中项里「用…打开」是正常的“打开方式”类动词，不动它）。

### 排查手法（以后可复用）
`%TEMP%\ff_bgmenu.txt` / `ff_viewmenu.txt` 那种临时转储最好用
`_wfopen(..., L"w, ccs=UTF-8")` 打开，否则 `fwprintf` 会把中文写成 `?`。
另外：要判断“Shell 到底给了哪些项/动词”，就用 `GetMenuStringW` + `GetCommandString(GCS_VERBW)`；
要比较 Explorer 的菜单，用 `SHCreateShellFolderView` + `IShellView::GetItemObject(SVGIO_BACKGROUND)`
生成一份对照（本项目只用于诊断，不用于实际菜单，因为视图菜单里的查看/排序/刷新会作用到
那个隐藏的 Shell 视图而不是 FastFile 自己）。


## 第九批：驱动器右键走原生菜单 + 分隔线合并

### 磁盘根目录右键没有走 Shell
症状：在「此电脑」里右键某个盘（如 Ventoy (I:)），弹出的是 FastFile 自己的兜底菜单
（打开/复制/删除到回收站/重命名/刷新/显示隐藏的项目），而不是 Windows 原生菜单。

根因：`ShowShellContextMenu()` 只会「绑定父文件夹 + 用叶子名解析子 PIDL」这一种方式；
磁盘根目录没有可用的父路径（`ParentPath(L"I:\\")` 为空，退化成用 "I:\\" 当父目录），
叶子名解析必然失败 → `ok=false` → 调用方回退到 `ShowFallbackContextMenu()`。

修法：加第二套绑定方式 —— **绑定桌面（`SHGetDesktopFolder`）+ 用完整路径解析 PIDL**：

```cpp
    bool ok = !parent.empty() ? bindParentFolder(parent) : false;
    if (!ok) ok = bindDesktopFolder();      // 磁盘根、"shell:" 等
    if (!ok || pidlChildren.empty()) return false;   // 只有两条都失败才用兜底菜单
```

Explorer 把驱动器交给「此电脑」文件夹处理，桌面的 `ParseDisplayName(L"I:\\")` 解析出的是
同一个对象，所以现在拿到的是 Drive 的真实动词（固定到快速访问/管理/包含到库中/复制/
创建快捷方式/属性 …），另外 Shell 扩展（例如本机的 360）也会正常出现。

### 删项后多出一条分隔线
`PruneShellMenu()` 里虽然做了分隔线合并，但那次调用发生在**插入 查看/排序方式/刷新**
之前，插入本身又会和 Shell 原有的分隔线撞在一起。修法：把合并逻辑抽成
`TidyMenuSeparators(HMENU)`，`PruneShellMenu()` 末尾调用一次，**插入完自己的项之后再调用一次**。


## 第十批：磁盘属性弹成“系统关于”

### 已确证的结论
在「此电脑」右键某个盘（Ventoy (I:)）时，我们**确实**拿到并显示了 Shell 的原生磁盘菜单。
把两种绑定方式拿到的菜单逐项打出来对比（`GetMenuStringW`）：

| 绑定方式 | 菜单内容（节选） |
|---|---|
| Computer 文件夹 + 完整路径 | 在新窗口中打开 / 打开 / 固定到快速访问 / **启用 BitLocker** / 打开自动播放 / Bandizip / Defender 扫描 / **授予访问权限** / 还原以前的版本 / 包含到库中 / 格式化 / 复制 / 创建快捷方式 / 属性（21 项）|
| 桌面 + 完整路径 | 在新窗口中打开 / 打开 / 固定到快速访问 / 管理 / 包含到库中 / 复制 / 创建快捷方式 / 属性（13 项）|

两种都是**磁盘**菜单（不是兜底菜单、也不是背景菜单）。所以问题只在「属性」被调用时的目标对象。

### 修法 1：磁盘根目录不走“父目录 + 叶子名”
`ParentPath(L"I:\\")` 为空，旧代码退化成把 `"I:\\"` 当父目录，然后拿叶子名 `"I:"`
去 `ParseDisplayName` —— 这是“驱动器相对路径”，Shell 会**成功**返回一个 PIDL，
于是“假成功”，再也走不到正确的绑定方式。现在明确识别磁盘根路径，直接走
**桌面 + 完整路径**（实测解析出 `[Ventoy (I:)]`，是真正的磁盘对象）。

### 修法 2：「属性」改用文档化 API
菜单里的 offset 动词是由**菜单绑定在哪个文件夹对象上**决定解析结果的；
为了不受这个影响，`属性`（verb == "properties"）现在直接调用

```cpp
SHObjectProperties(m_hWnd, SHOP_FILEPATH, path, nullptr);
```

这样无论菜单是从哪个文件夹对象来的，属性页永远作用在**选中的那个路径**上
（磁盘就是磁盘属性页，文件夹/文件同理）。多选时仍交回 Shell 处理（多项属性页）。

> 另注：在「此电脑」的**空白处**右键 → 属性，打开“系统 - 关于”是 **Windows 自身行为**
> （Explorer 里“此电脑 → 属性”同样是系统页面），那一条不是 bug。


## 第十四批：标签栏自绘（CTabStripUI）+ 标题行规范

目标是把 `skin\main.xml` + `UiTokens.h` 按"Win11 资源管理器标签栏 / 360 密度"的规范重排，
并把标签从"一堆 Button 拼出来"换成一块自绘控件。

### XML 骨架（`skin/main.xml`）

```xml
<Window size="1180,740" sizebox="4,4,4,4" caption="0,0,0,32" mininfo="900,540">
  <VerticalLayout name="outer_gutter" bkcolor="#00000000" padding="0,0,0,0">
  <VerticalLayout name="chrome_root" bkcolor="#00000000" bordersize="0" borderround="0,0">
    <HorizontalLayout name="titlebar" height="32" padding="8,0,0,0" childvalign="vcenter">
      <TabStrip name="tab_strip" />                 <!-- 自绘，吃剩余宽度 -->
      <Button name="btn_tab_add" width="24" height="24" ... />   <!-- + -->
      <Control name="caption_drag" minwidth="40" />  <!-- 弹性空白：拖窗口 -->
      <Button name="minbtn" ... /> <Button name="maxbtn" ... />
      <Button name="restorebtn" visible="false" ... /> <Button name="closebtn" ... />
    </HorizontalLayout>
    <HorizontalLayout name="favorites_bar" height="26" ...>…</HorizontalLayout>
    <HorizontalLayout name="address_bar" height="28" ...>…</HorizontalLayout>
    <HorizontalLayout name="toolbar" height="28" ...>…</HorizontalLayout>
```

密度（`UiTokens.h`，设计像素 @96dpi）：标题行 32 / 标签卡片 30（上方留 2px、底边与下一行
连通）/ 卡片间距 4 / 圆角 6 / 卡片宽 120–190 / 收藏栏 26 / 地址栏 28 / 命令栏 28 /
地址框 24 / 导航按钮 24 / 详细列表行 24 / 导航行 26 / 状态栏 22。

### CTabStripUI（`src/TabStripUI.h` / `.cpp`）

`class CTabStripUI : public CContainerUI`，自己算几何、自己画、自己命中：

| 分组 | 接口 |
|---|---|
| 模型镜像 | `Add / Insert / RemoveAt / Clear / Select / Reorder / FindByPath / SetTabTitle / SetTabIcon / SetActiveTab / GetCount / GetActive` |
| 外观 | `SetDarkMode / SetMetrics(dpi) / SetMaxTabWidth / AnimateAppear` |
| 命中 | `HitTest(POINT) -> {index, part∈{None,Body,Close,Plus,Empty}}`、`IsCaptionDragPoint()` |
| DuiLib 重写 | `GetClass/GetInterface/EstimateSize/SetPos/DoEvent/DoPaint` |

控件只上报意图，模型仍归 `CMainWnd`（`src/MainWnd.Tabs.cpp`）。上报用 `PostMessage`，
带 `POINT` 的两条消息由宿主 `delete` 指针：

```
kMsgTabSelect      = WM_USER+300   wParam = index
kMsgTabClose       = WM_USER+301   wParam = index
kMsgTabReorder     = WM_USER+302   wParam = from, lParam = to
kMsgTabDragOut     = WM_USER+303   wParam = index, lParam = POINT*（屏幕坐标）
kMsgTabContextMenu = WM_USER+304   wParam = index, lParam = POINT*
kMsgTabAdd         = WM_USER+305
```

绘制：GDI+ 抗锯齿。选中卡片用 `BuildTopRoundedPath()` 画"上方圆角 + 底边敞开"的形状，
描边只描上边和左右两条竖边（**不画底边**），所以卡片能和下面的白色收藏/地址区连成一体；
未选中标签不画底、文字 #5C5C5C，悬停给 8–12% 黑/白淡底并显示关闭叉；选中的叉常显，
叉悬停是柔和圆角红底（不是 Win10 的直角大红块）；`+` 在标签右侧，热区 24px。
图标 16px 走 Shell（`GetStockIconBmp` / `GetShellIconBmp`，`SHIL_SMALL`）。
新建标签有 160ms 的短滑入（`SetTimer(this, 1, 16)`）。

### 非客户区命中（`CMainWnd::HandleMessage` 的 `WM_NCHITTEST`）

标题行整行都是"标题栏"，但里面分三种返回：

| 位置 | 返回 |
|---|---|
| `closebtn` / `maxbtn`+`restorebtn` / `minbtn` 的矩形 | `HTCLOSE` / `HTMAXBUTTON` / `HTMINBUTTON` |
| 标签行里 `TabStrip` 之上（命中 part == Body/Close/Plus） | `HTCLIENT`（交回控件自己处理） |
| 标签行其余空白（part == Empty）以及 `caption_drag` | `HTCAPTION`（拖动窗口 / 双击最大化） |
| 窗口四边 `sizeBox` 内 | 继续交给 DuiLib 的默认命中（缩放窗口） |

因为按钮由系统负责（不是 DuiLib 控件）派发，悬停高亮要自己画：
`WM_NCMOUSEMOVE/WM_NCMOUSELEAVE` → `UpdateCaptionButtonHover()`。所以**不要**指望
DuiLib 的 `hotbkcolor` 生效。

但**光返回命中码不等于按钮能用**：这个窗口用 `WM_NCCALCSIZE`/去掉 `WS_CAPTION` 做了
自定义边框，`DefWindowProc` 对 `HTMINBUTTON` / `HTMAXBUTTON` 只会进入内部跟踪循环
（`SendMessage` 过去会直接阻塞住），既不发 `SC_MINIMIZE` / `SC_MAXIMIZE`，也不把
`WM_NCLBUTTONUP` 交给窗口过程（表现就是"点了没反应 + 悬停高亮一闪而过"）。
所以 `CMainWnd::HandleMessage` 现在自己接管 `WM_NCLBUTTONDOWN`：

```cpp
if (uMsg == WM_NCLBUTTONDOWN || uMsg == WM_NCLBUTTONUP) {
    const UINT code = (UINT)wParam;
    if (code == HTMINBUTTON || code == HTMAXBUTTON || code == HTCLOSE) {
        if (uMsg == WM_NCLBUTTONDOWN) {
            if (code == HTMINBUTTON)      SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
            else if (code == HTMAXBUTTON) SendMessage(WM_SYSCOMMAND, IsZoomed(m_hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
            else                          SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
        }
        return 0;   // 别落回 DefWindowProc，否则又开始跟踪循环
    }
    // HTCAPTION / 四边缩放继续交给 DefWindowProc：拖动窗口、双击最大化、Aero Snap 都靠它
}
```

`SC_MAXIMIZE` / `SC_RESTORE` 走 `WindowImplBase::OnSysCommand`，它会在缩放状态变化时
切换 `maxbtn` / `restorebtn` 的可见性，所以"最大化后变成还原字形"是免费的。

### 交互

左键切换、中键关闭（`WM_MBUTTONDOWN` 里按命中索引关）、拖动排序（拖动过程中实时
`Reorder`，越出客户区就 `kMsgTabDragOut`）、拖出后由宿主 `CreateProcess "--new-window"`；
右键菜单五项（关闭 / 关闭右侧 / 关闭其他 / 复制路径 / 在新窗口打开）；
`Ctrl+T` 新建、`Ctrl+W`/`Ctrl+F4` 关闭、`Ctrl+Tab`/`Ctrl+Shift+Tab` 前后循环。
`AddTab()` 增加了 `allowDuplicate` 参数：`Ctrl+T` / "+" 一定新开（资源管理器行为），
来自其它进程的文件夹仍然复用已有标签。

### 拖出标签的窗口几何

`OpenPathInNewWindow()` 会把"落点 + 本窗口外框尺寸"通过 `--geometry=x,y,w,h` 传给新进程；
`wWinMain` 的 `StartupGeometryRequested()` 在 `EnsureDpiLayout()` 之后应用它（有参数就
不再 `CenterWindow()`）。**不要**再用"父进程轮询 `FindWindow` 后 `SetWindowPos`"的写法：
新进程自己的布局还没跑完，尺寸会被它覆盖（实测新窗口会变成系统的级联默认尺寸
1920×997，而不是源窗口的 1770×1110）。

### DWM / Mica 现状（重要）

`ApplyWindowCornerAndPadding()` → `ApplyDwmChrome()`：

- `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`（圆角，一直开启）；
- `DWMWA_USE_IMMERSIVE_DARK_MODE`：按亮色皮肤固定 `FALSE`；
- `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TABBEDWINDOW`（Mica Alt）**只在
  `constexpr bool kShowMicaBackdrop = true` 时才真正启用**，目前是 `false`，
  标题行填 `#FFEDEDED` 平色。

第十五批起 `kShowMicaBackdrop = true`（标题行透出 Mica Alt，其余行自己画实色），
实测文字/图标正常；当初"必须先把标题行改成 32bpp 绘制"的结论已作废，
但仍要注意：不要给标题行设置不透明的 `bkcolor`，否则 Mica 会被盖住。

## 第十五批：Fluent 化（顶栏 / 收藏 / 地址 / 左侧 / 此电脑详情）

目标从"360 紧凑档"改为"只对齐 Win11 资源管理器"，尺寸全部写 96-DPI 逻辑像素，
运行时 `MulDiv(x, dpi, 96)`：

| 行 | 逻辑 | 150% 物理 |
|---|---|---|
| 标题 + 标签（`titlebar`） | 36 | 54 |
| 收藏（`favorites_bar`） | 36 | 54 |
| 地址 + 搜索（`address_bar`） | 36 | 54 |
| 命令栏（`toolbar`） | 40 | 60 |
| 导航 / 目录树一行（`NavRowH` / `TreeRowH`） | 36 | 54 |
| 详细列表行（`DetailsRowH`） | 30 | 45 |
| 状态栏（`StatusBarH`） | 28 | 42 |
| 控件圆角（`RadiusControl`） | 4 | 6 |
| 分割线（`Hairline`，`DpiScaleHairline`） | 1 | **2**（向上取整，避免 1.5px 发虚） |

### 栏间空白（老问题的真凶）

改高之后发现标题 / 收藏 / 地址 / 命令四行之间夹着 6–12 物理像素的空白。原因是 DuiLib
把容器的 **`padding` 计入它在父布局里占用的空间**（`CVerticalLayoutUI::SetPos`:
`cyFixed += sz.cy + padding.top + padding.bottom`），而 `inset` 只缩进子区域、不占位。
所以顶栏这些横向条一律用 `inset`（`ApplyUiChromeTokens` 里的 `setInset`），
**不要再对它们用 `padding`**，否则又会出现缝隙并把整条顶栏撑高。

### 收藏栏（Explorer 规格）

- 尺寸：芯片 28 逻辑高、圆角 4、左右内边距 8、图标→文字 8、芯片间 8、最大宽 168
  （`FavChipH / RadiusControl / FavChipPadX / FavChipIconGap / FavChipGap / FavChipMaxW`）。
- 宽度：先按文字实测宽，再用行宽均分压缩（下限 72 逻辑），不换行、不撑高行；
  水平间距由 `favorites_strip` 的 `childpadding` 提供。
- 图标：`GetShellIconBmp(路径, true, DpiScale(16))` → 150% 下真实 24×24 Shell 图标。
- 空列表：显示 `fav_bar_hint`「拖入文件夹到此处以收藏」，芯片列表隐藏。
- 交互：左键导航、中键/Ctrl+左键新标签、右键菜单（打开/新标签/新窗口/复制路径/取消固定），
  拖入文件夹收藏走 `MainWnd.DragDrop.cpp` 的 `IsOverFavoritesBar`。
- 「配置文件」不再是收藏：删掉了那条 pin（备份 `favorites.txt.bak-20260929`），
  改到命令栏「…」菜单第一项「打开配置文件目录」（`%APPDATA%\FastFile`）。
- TODO：芯片的拖拽排序（快捷访问区已有，收藏栏还没接）。

### 面包屑 / 搜索

- 第一段固定是「此电脑」+ `SIID_DESKTOPPC` 图标（量宽时给图标留位，否则会被裁）。
- 「含子目录」由 `UpdateSearchOptionVisibility()` 控制：搜索框聚焦 / 有词 / 已勾选才显示，
  隐藏时把宽度还给面包屑。

### 左侧导航

- 名称走 `GetShellDisplayName()`（`IShellItem::GetDisplayName(SIGDN_NORMALDISPLAY)`），
  `D:\...\Pictures` 显示为「图片」。收藏芯片的显示名也用它。
- 目录树第一层是盘符（`InitDirectoryTree`），不是 `C:\` 的子目录。

### 此电脑详情

`UpdatePreviewForCurrentFolder()`：This PC 用 `LoadPreviewStockIcon(SIID_DESKTOPPC, px)`，
「类型」= 此电脑，「大小」标签改成「包含」+「N 个驱动器」，时间显示「—」，
不再输出「当前目录概览 / 未选择项目」。磁盘卡片最多三列（`MainWnd.Views.cpp`）。

### DWM Mica Alt（本批开启）

`kShowMicaBackdrop` 改成 `true`：标题行不再铺色，DWM 的 `DWMSBT_TABBEDWINDOW` 背板直接透出，
其余各行照旧自己画实色。**14 批时"extends frame 会让 GDI 文字消失"的判断在本机不成立**——
用真实屏幕截图（`CopyFromScreen`，不是 `PrintWindow`）核对过：标签文字、图标、窗口按钮
字形全部正常。标签条区域由 `CTabStripUI::DoPaint` 画一层 7% 黑的淡色带
（宽度只覆盖标签 + “+”），这样白色选中卡片在浅色背板上仍然分得清，和 Explorer 一致。

### 未做 / 待办

- 收藏芯片的拖拽排序 / Delete 移除。
- 滚动条仍是之前确认过的统一 12 设计像素（新规格提到的"6 物理细轨道"没有采纳，
  因为那是用户上一轮明确要求统一宽度的）。

## 当前顶部结构（自上而下）

1. **标题行 = 标题栏**（`titlebar`，32px）：标签栏（自绘卡片）+“+” + 弹性空白 + 窗口按钮
2. **收藏栏**（`favorites_bar`，26px，白底，与选中标签卡片连通）
3. **地址栏**（`address_bar`，28px）：后退/前进/上级/刷新 + 路径（面包屑↔编辑）+ 搜索 + 含子目录
4. **命令栏**（`toolbar`，28px，白底）：新建/剪切复制…/排序/查看/更多

## 已实现能力（摘要）

- 浏览、后台复制、前台恢复、多选、删除/重命名/新建文件夹
- **复制与移动共用同一套后台任务引擎**：状态栏进度（项数/字节/百分比/当前文件）+ 取消按钮；
  同盘移动走原子改名（瞬时），跨盘移动走"带进度复制 + 删源"，不再弹系统 SHFileOperation 对话框
- **撤销（Ctrl+Z，也挂在「更多」菜单里）**：新建文件夹、移动（整批一次还原）、直达重命名路径
- Shell 右键全部走原生接口（选中项 / 文件夹背景 / **磁盘**三套菜单）+ 隐藏项开关；
  背景菜单额外补 查看 / 排序方式 / 刷新 / 粘贴，并屏蔽 Shell 里多余或空的项
- 左树 + 快速访问、多标签、前进后退；**启动固定进入「此电脑」**（不再恢复上次目录）
- 六种视图、异步缩略图（按长宽比自适应）、拖放、递归搜索
- 右侧预览（元数据 + 图/视频帧），窗格宽度可拖拽，面包屑/缩略图按宽度自适应
- 排序：图标/列表/平铺视图文件夹在前；详细信息视图严格按列排序
- DPI PerMonitorV2；`UiTokens.h` 设计令牌；应用图标见 `res\`
- 会话/设置的持久化、每文件夹视图记忆；安装程序见 `installer\`（当前 1.0.7）
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
| Ctrl+F4 | 关闭当前标签 |
| Ctrl+Tab / Ctrl+Shift+Tab | 下一个 / 上一个标签 |
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
20. **`small`/`near`/`far` 是 Windows 头文件里的宏**（`rpcndr.h` 里 `#define small char`）。
    局部变量叫 `small` 会变成 `HFONT char = ...` 这种诡异编译错误（本项目踩过两次），
    命名请用 `smallIcon` 之类
21. **导航会销毁正在派发点击事件的控件**：`NavigateTo()` 必须走延迟派发
    （`kMsgDeferredNav` → `NavigateToNow()`）。直接在按钮通知里 `RemoveAll()` 会让
    DuiLib 之后对已释放控件调用 `Invalidate()`（0xC0000005，崩在
    `CControlUI::Invalidate` 的第一条虚调用上）
22. **原生菜单里的 offset 动词（含“属性”）由菜单绑定在哪个文件夹对象上决定**：磁盘/
    特殊项不要用 `InvokeCommand(idCmdFirst+n)`，用 `SHObjectProperties(SHOP_FILEPATH, 路径)`
    之类直接指向对象的 API 更稳
23. **Shell 菜单要先转储再决定怎么改**：`GetMenuStringW` 拿文本、`GetCommandString(GCS_VERBW)`
    拿动词。文件夹对象菜单（`CreateViewObject`）与视图菜单（`SHCreateShellFolderView` +
    `SVGIO_BACKGROUND`）内容不同，前者缺少查看/排序/刷新，得自己补
24. **DuiLib 的预翻译阶段会吃掉 `Ctrl+Tab`**：`CPaintManagerUI::TranslateMessage` →
    `PreMessageHandler` 把任何 `WM_KEYDOWN`+`VK_TAB` 当成控件间 Tab 切换并 `return true`，
    消息不会再 `DispatchMessage`，所以 `HandleMessage` 里的 `WM_KEYDOWN` 分支永远收不到它。
    正解是重写 `WindowImplBase::MessageHandler`（预消息过滤器，签名 `bool& bHandled`），
    在里面接管并置 `bHandled = true`（见 `CMainWnd::MessageHandler`）。
25. **`WM_NCHITTEST` 返回 `HTCLOSE/HTMAXBUTTON/HTMINBUTTON` 后，DuiLib 不再收到鼠标事件**：
    这些按钮的悬停/按下高亮要自己在 `WM_NCMOUSEMOVE` 里画（`UpdateCaptionButtonHover`），
    XML 里的 `hotbkcolor` 不会生效
26. **改控件名字要同步改按名查找的代码**：`tab_bar` 改名 `titlebar` 后，
    `ScaleNamedFixed(_T("tab_bar"), …)` 与 `setPad(_T("tab_bar"), …)` 就静默失效了 ——
    表现是"标签行没跟着 DPI 缩放，比其它行矮一截"。改 XML `name` 时用
    `rg "tab_bar|titlebar"` 全文搜一遍
27. **截图窗口时要先 `SetThreadDpiAwarenessContext(PER_MONITOR_AWARE_V2)`**：否则
    `GetWindowRect` 返回被 DPI 虚拟化的尺寸（150% 下 1770×1110 会报成 1180×740），
    按这个尺寸 `PrintWindow` 只会截到左上角一块，最右侧的窗口按钮看起来"消失"了。
    排查"某控件没画出来"之前，先确认截图区域是不是完整窗口
28. **自定义边框窗口的标题按钮必须自己执行命令**：`WM_NCHITTEST` 返回
    `HTMINBUTTON/HTMAXBUTTON/HTCLOSE` 只负责"光标 + 高亮"，真正的动作要由
    `WM_NCLBUTTONDOWN` 里自己发 `WM_SYSCOMMAND`。去掉 `WS_CAPTION` 后
    `DefWindowProc` 不会再为这些命中码发 `SC_*` 命令，反而进入跟踪循环吞消息
    （用 `SendMessage(WM_NCLBUTTONDOWN, HTMINBUTTON)` 测试会直接卡住调用线程，
    这本身就是"进了模态循环"的证据）。另外 `Get-Process ... .MainWindowHandle` 可能
    拿到同进程里的小窗口（如 42×28），量窗口尺寸前先按类名 `FastFile_MainWnd` + 宽度过滤

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
- 安装程序已完成（`installer\`，当前版本 1.0.7）；后续可做自动更新 / 代码签名（现在 exe 无签名，
  首次运行可能触发 SmartScreen）
- 右键菜单的“屏蔽名单”目前是硬编码（PowerShell 动词 + “用 X 打开”），若用户想自定义，
  可放到设置面板里

## 验收口径（这批之后形成习惯）

- 每个界面改动都用 `PrintWindow` 截图 + 1:1 裁剪对比给用户看，而不是只说“改好了”
- 涉及原生 Shell 行为的（右键菜单等），先把 Shell 给的数据转储出来对比，再决定怎么改
- 崩溃一律先看 `%LOCALAPPDATA%\FastFile\last_crash.txt`（现在是异常码 + 模块内 RVA + 调用栈），
  再配合 `build\Release\FastFile.map` 反查函数

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
