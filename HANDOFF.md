# FastFile — 交接说明（给后续 AI / 开发者）

更新日期：2026-09-28（Asia/Shanghai）

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
- 图标缓存版本：`_v6.png`（HICON → PNG 真透明）；改导出逻辑时升版本并清缓存

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
7. **窗口初始尺寸 = 设计单位 × DPI，未按显示器工作区钳制**：150% 缩放 + 1707×960 屏幕上
   窗口达 1770×1110，右下角（含状态栏）会跑出屏幕。下一步建议按 `SPI_GETWORKAREA` 钳制

## 会话/配置文件（通常在 `%APPDATA%\FastFile\`）

- `session.ini`、`folder_views.ini`、`favorites.txt`、`left_nav.ini` 等

## 建议下一轮方向（用户曾提过）

- 继续对齐资源管理器 / 360 密度与图标风格（自有 Shell 图标）
- 按显示器工作区钳制初始窗口尺寸（见「已知坑」7）
- 安装包、详情视图虚拟化（当前是分批填充 + 8000 项上限）
- 预览/工具栏细节打磨

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
