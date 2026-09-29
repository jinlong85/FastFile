# FastFile — 交接说明（给后续 AI / 开发者）

更新日期：2026-09-29（Asia/Shanghai）

> 2026-09-28：已纳入 Git 版本管理；原 8000 行单文件 `src\MainWnd.cpp` 已拆分为 14 个编译单元（见「源码结构」）。
>
> 2026-09-29：完成「第二批～第十二批」共 11 轮修复/打磨（含安装程序、应用图标、右键菜单、崩溃修复、可选文件夹打开接管）。
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
| 第十二批 | 待提交 | 标签切换状态同步；“此电脑”磁盘卡片响应式分列；预览栏最小宽度与元数据列优化 |

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
