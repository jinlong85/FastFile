# FastFile — 轻量 Windows 文件管理器

用 **DuiLib + 原生 C++ / Win32** 写的文件管理器。观感贴近 Windows 11 资源管理器，布局密度参考 360 文件；所有图标资源来自 Windows Shell 或自绘。

> **免责声明**：本仓库为原创实现。**未安装、未引用、未反编译任何 360 软件或二进制**，不使用其图标、商标或皮肤资源；仅在 `third_party/duilib` 中嵌入了开源 DuiLib 的库本体（BSD 风格许可，见该目录下 LICENSE）。
>
> 开发交接、历史踩坑与工作方式见 [HANDOFF.md](HANDOFF.md)。

## 功能现状

**浏览与导航**

- 多标签页（新建/关闭/路径记忆）、前进后退、上级、刷新
- 地址栏与面包屑合一：默认面包屑，点击进入编辑，Enter 导航，Esc/失焦回到面包屑
- 会话恢复（上次打开的标签与目录）、每文件夹独立视图记忆
- 左侧：快速访问 + 目录树；快速访问与树之间为可拖拽分割线（位置持久化）

**列表与视图**

- 六种视图：超大图标 / 大图标 / 中等图标 / 列表 / 详细信息 / 平铺
- **视图虚拟化**：详细信息视图只保留可视窗口内的行（滚动时复用行控件），10 万项的目录也只占用固定内存；图标/平铺视图仍是"每项一个控件"，上限 8000 项
- 异步缩略图（图标视图）
- 详细信息：Shell 小图标 + 名称/修改日期/类型/大小，列宽与排序持久化
- 搜索：即时过滤 + 「含子目录」递归搜索

**文件操作**

- 复制 / 剪切 / 粘贴，**后台复制**带状态栏进度与「取消」
- **移动**同样走后台任务：同盘为原子改名（瞬时），跨盘为「带进度复制 + 删源」，不再弹系统对话框
- 删除到回收站 / 永久删除（Shift+Delete，带确认）、重命名、新建文件夹
- 拖放：拖到文件夹移动、拖到收藏栏固定、支持从资源管理器拖入
- **撤销（Ctrl+Z）**：新建文件夹、移动（整批还原）、直达重命名；「更多」菜单里也有

**系统集成**

- Shell 右键菜单（IContextMenu2/3，含二级子菜单）、属性、共享
- 空白处右键 = 资源管理器同款结构：查看 / 排序方式 / 刷新 + Shell 自身项（在终端中打开、授予访问权限、新建、属性…）
- 单实例：重复启动会激活已有窗口；关闭窗口即退出进程

**外观**

- Per-Monitor DPI v2；设计令牌集中在 `src/UiTokens.h`（间距 4/8/12/16、字号、颜色、圆角）
- Win11 浅色 chrome、命令栏 40px 密度、容器 8 / 控件 4 圆角
- 收藏栏：按文件名实测宽度排布，可在「查看」菜单折叠

## 快捷键

| 键 | 作用 |
|----|------|
| Ctrl+C / X / V | 复制 / 剪切 / 粘贴 |
| Ctrl+Z | 撤销（新建文件夹、移动、重命名） |
| Ctrl+A | 全选（图标视图与详细信息都支持） |
| Ctrl+Shift+N | 新建文件夹 |
| Ctrl+T / Ctrl+W | 新建标签 / 关闭当前标签 |
| Ctrl+F 或 F3 | 聚焦搜索框 |
| Alt+D | 聚焦地址栏 |
| Alt+← / → / ↑、Backspace | 后退 / 前进 / 上级 / 后退 |
| F2 / F5 | 重命名 / 刷新 |
| Delete / Shift+Delete | 删除到回收站 / 永久删除 |
| Alt+Enter | 显示属性 |
| Esc | 退出地址编辑 / 取消选择 |

## 构建

工具链：**CMake ≥ 3.16 + Visual Studio 2022（x64）**，Release 配置。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物在 `build\Release\FastFile.exe`；构建后会自动把 `skin\` 拷到 exe 旁（运行时按 `<exe>\skin\main.xml` 找皮肤）。也可以直接打开根目录的 `FastFile.sln`（VS 回退工程）。

改完皮肤或代码后的验收流程、以及必须清图标缓存的场景，见 [HANDOFF.md](HANDOFF.md)。

## 目录结构

```
FastFile/
  CMakeLists.txt          # 主构建（静态链接 DuiLib）
  FastFile.sln            # VS 回退解决方案
  skin/main.xml           # 主窗口皮肤（设计尺寸为 96 DPI 基准，运行时按 DPI 缩放）
  src/
    main.cpp              # WinMain、DPI 感知、单实例
    MainWnd.h             # CMainWnd 声明 + UiTokens 引用
    MainWndInternal.h     # 各编译单元共用的 include 前导与共享辅助函数
    MainWnd*.cpp          # 按职责拆分的 14 个编译单元（窗口/DPI/主题/工具/导航/树/标签/收藏/文件操作/菜单/视图/图标/预览/拖放）
    UiTokens.h            # 设计令牌（间距、字号、颜色、圆角）
  third_party/duilib/     # 嵌入的 DuiLib 源码（仅库本体与许可）
  vs/                     # 手写 vcxproj（无 CMake 时可用）
  scripts/                # 工具脚本
  cmake/                  # manifest 等构建辅助文件
```

## 配置与数据

运行时数据在 `%APPDATA%\FastFile\`：`session.ini`（标签/视图/排序等会话状态）、`folder_views.ini`（每文件夹视图）、`favorites.txt`、`left_nav.ini`（左栏分割位置）。图标缓存位于 `%TEMP%\FastFileIconCache`，异常时可删除后重启。

## 已知限制

- 图标/平铺视图不是虚拟化的：上限 8000 项，超过会截断并在状态栏提示（详细信息视图无此限制，上限 10 万项）。
- 递归搜索上限 4000 项；图标缩略图上限 400 张。
- 窗口初始尺寸按显示器工作区钳制：小工作区（如 1366×768 + 150% 缩放）下会等比退让，界面元素可能被压缩。
- 暂不支持：压缩包内浏览、网络位置、批量重命名、内容搜索、安装包。

## 许可证

- 本项目代码：内部草稿，后续可自行决定。
- DuiLib：见 `third_party/duilib/LICENSE` 与 `duilib license.txt`（BSD 风格开源许可）。

## 明确不做

- 不安装 360 软件，不反编译任何闭源文件管理器二进制。
- 不把 upstream 的 `360SafeDemo` 或任何 360 资源打进本仓库。
