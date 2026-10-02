# FastFile — 交接说明（给后续 AI / 开发者）

更新日期：2026-10-02（Asia/Shanghai）

## 接手先读：当前状态与验证入口（2026-10-02）

本节是最新状态入口。下方按日期保留开发历史；旧章节中的「当前」、测试数量、默认打开行为和待办仅代表当时状态，冲突时以本节及随后两节修复记录为准，再核对实际源码。协作要求见 [AGENTS.md](AGENTS.md)，功能说明见 [README.md](README.md)，面向用户的变更见 [CHANGELOG.md](CHANGELOG.md)。

- 技术栈仍为 C++ / Win32 / DuiLib，普通文件区由 Windows ExplorerBrowser 承载。不要改换 UI 框架。
- 本地工作区包含大量未提交修改及未跟踪的源码、测试；接手先检查工作区，不要用 reset / clean 或只复制 Git 已跟踪文件的方式丢弃当前实现。本次未提交或推送代码。
- [VERSION](VERSION) 仍为 **1.0.9**。安装包已于 2026-10-02 16:44 重新生成（`dist/FastFile-Setup-1.0.9.exe`，1,444,864 字节，SHA-256 `4002CEDD2A8B97E71031C59530925B216A2B5E0A7920154DA2301079A3D9FC9A`），包含 2026-10-02 全部设置、视图和打开行为修复。打包前核对常用 exe 哈希等于下述已验证快照且其后无产品源码修改，故未重新构建；用反射读取安装包内嵌资源，FastFile.exe 与 skin/main.xml 哈希与常用产物一致。旧包备份为 `dist/FastFile-Setup-1.0.9.before-20261002.exe`。下方各节「未重建安装包」的说明由此取代。
- **2026-10-02 晚：原生文件操作进度与原生背景菜单（见下一节）已提交源码，但尚未部署到 build/Release。** 候选 `build-ui/Release/FastFile.exe`（1,284,096 字节，SHA-256 `B57D75E406E8B3385451B5D9E3C8E7A94887D4B6C21F2734B5FC13CFF59CF1D0`）已用最终源码构建（日志 `build-ui/nativeops-build.log`）。最终源码的各项 CTest 均已分别通过（主窗口回归 88.40 秒、Shell 浏览器 1.74 秒、其余 5 项见 `build-ui/nativeops-tests.log`），但最后一次 7 项完整运行时，主窗口回归里依赖资源管理器会话撤销服务的检查（第一项为「native Shell undo restores original name」）连锁失败 17 项。用 `git archive HEAD` 在 `%TEMP%\ffhead` 构建的**未修改基线也以同样方式失败 14 项**，说明是环境问题而非本次改动：explorer.exe（PID 9664）留有一个隐藏的「已完成 95%」OperationStatusWindow，此后即使新做一次 FOFX_ADDUNDORECORD 重命名，背景菜单也不再出现「撤销」。按交付规则，在 7 项完整运行通过之前不覆盖常用 exe。接手步骤：先让用户结束该卡住的资源管理器操作或重启资源管理器 / 重新登录，再运行完整 ctest，通过后将 exe / map / skin 复制到 build/Release（运行中的旧窗口先改名为 `FastFile.before-native-fileops.exe` 再复制），核对 SHA-256，并更新本节。
- 上一次完整验证（shell-activation）：Release x64 构建成功，CTest **7/7 通过，106.94 秒**。构建日志为 `build-ui/shell-activation-final-build.log`，测试日志为 `build-ui/shell-activation-final-tests.log`。这些日志和二进制属于本地忽略产物，换机器后需重新生成。
- 测试通过后才将 `build-ui/Release` 的 exe、map 和 skin 更新到常用 `build/Release`。两处 exe 的 SHA-256 已核对一致：`737DD15580C453F8BBE0911FFB701968B149D914A9E8214A03D37789937176AB`。这是本次交付快照，后续重构建应重新核对。
- 交付时保留用户正在运行的旧窗口；旧进程不能热更新。常用路径已有新版文件，但用户需要退出旧窗口并重新启动。旧文件备份为 `build/Release/FastFile.before-shell-activation.exe`，它不是新版启动入口。

### 本轮修改应从哪里读

| 入口 / 文件 | 当前职责与不能退回的行为 |
| --- | --- |
| [src/main.cpp](src/main.cpp) | 解析外部启动参数、单实例查找、WM_COPYDATA 转发；旧缓存 `--open` 的停用重定向只在启动入口处理。明确的 `--shell-folder` 不受历史默认接管停用标记拦截。 |
| [src/MainWnd.cpp](src/MainWnd.cpp) | `kMsgOpenExternalPaths` 接收端直接打开 FastFile；不要重新无条件调用 `RedirectDisabledShellOpen`，否则会再次启动 Explorer。 |
| [src/MainWnd.Settings.cpp](src/MainWnd.Settings.cpp)、[src/MainWnd.Integration.cpp](src/MainWnd.Integration.cpp) | 设置保存与系统集成注册。测试必须核对实际生成的 Directory / Drive 注册命令，不能仅模拟主窗口内部消息。 |
| [src/ShellBrowserHost.cpp](src/ShellBrowserHost.cpp)、[src/ShellBrowserHost.h](src/ShellBrowserHost.h) | 显示前初始化目标视图，列表 / 详细信息统一为 26 逻辑像素行高；处理原生视图导航、激活与右键入口。 |
| [src/MainWnd.Menus.cpp](src/MainWnd.Menus.cpp) | 保留原生 Shell 菜单；文件夹 / 磁盘的新窗口、新标签命令转为 FastFile 新标签，缺少入口时补充中文命令。 |
| [src/MainWnd.Nav.cpp](src/MainWnd.Nav.cpp)、[src/MainWnd.Tabs.cpp](src/MainWnd.Tabs.cpp) | 目标目录视图状态、标签导航与复用；显式新标签强制新建并保留原标签，过期完成通知不能改写当前标签。 |
| [src/ShellFileOperation.cpp](src/ShellFileOperation.cpp)、[src/ShellFileOperation.h](src/ShellFileOperation.h) | 复制 / 移动 / 回收 / 永久删除的 IFileOperation 引擎（含 SHFileOperation 回退）、操作标志生成与进度接收器结果核对。生产标志不得加入 FOF_SILENT / FOF_NOERRORUI / FOFX_NOMINIMIZEBOX；复制 / 移动不得加 FOFX_ADDUNDORECORD。 |
| [src/ShellMenuUtil.h](src/ShellMenuUtil.h) | Shell 菜单分隔线规范化（按 MFT_SEPARATOR / 空文本判断，不按 id）与按 verb 查找菜单项。 |
| [src/MainWnd.FileOps.cpp](src/MainWnd.FileOps.cpp) | StartFileOperation：每项操作一个 STA 后台线程（附着主窗口桌面），完成后 kMsgFileOpFinished 回到 UI 线程写入历史；状态栏只显示结果摘要。 |
| [tests/MainWndRegressionTests.cpp](tests/MainWndRegressionTests.cpp)、[tests/ShellBrowserHostTests.cpp](tests/ShellBrowserHostTests.cpp) | 本轮回归、真实 Shell 视图与菜单、隔离桌面及生产入口多进程启动测试。测试登记见 [CMakeLists.txt](CMakeLists.txt)。 |

### 构建、验证与更新常用程序

在项目根目录使用 VS 2022 x64 工具链，先构建独立候选目录。已有 `build-ui` 缓存的源码路径或生成器不匹配时，使用新的候选目录，勿直接删除未知用途目录。

```powershell
cmake -S . -B build-ui -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw "配置失败" }
cmake --build build-ui --config Release --parallel 8
if ($LASTEXITCODE -ne 0) { throw "构建失败" }
ctest --test-dir build-ui -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "回归失败" }
```

系统右键专项可用 `ctest --test-dir build-ui -C Release -R '^FastFileShellActivationTests$' --output-on-failure` 定位问题，但不替代交付前完整回归。当前七项为 `FastFileMainWndRegressionTests`、`FastFileUiPolishTests`、`FastFileShellActivationTests`、`FastFileFavoritesTests`、`FastFileCoreTests`、`FastFileShellBrowserTests`、`FastFileVersionSource`。主窗口测试使用隔离桌面；启动专项还隔离用户注册表与配置，不为测试切换用户真实系统集成开关。

修复缺陷应先确认回归能复现旧行为，再确认修复后通过。临时恢复旧源码验证后，恢复最终源码并刷新其修改时间，避免增量构建复用旧对象文件。所有相关测试、Release x64 及适用运行验证通过后，才更新常用 exe / map / skin，并核对候选与常用 exe 哈希；失败候选不得覆盖常用程序。安装包更新需另行构建和验证，不等同于复制 exe。

### 尚未完成的本轮验收

- 原生文件操作：在正常桌面复制一个大文件，确认出现资源管理器原生进度窗口（暂停 / 取消 / 剩余时间），同名冲突时出现替换 / 跳过对话框，操作期间 FastFile 窗口可继续浏览；剪切粘贴、拖放、Delete / Shift+Delete 同样检查。自动测试只能在隔离桌面验证调用路径与标志，无法目视确认进度窗口。
- 原生背景菜单：在正常桌面右键文件区空白处，确认 粘贴 / 粘贴快捷方式 / 撤销 / 分组依据 等出现、没有叠在一起的分隔线、查看 / 排序方式 作用于 FastFile 视图；Shift+右键显示扩展项。
- 用户正常桌面上逐项点击系统右键「使用 FastFile 打开」，分别检查已有窗口和完全退出后的首次启动，覆盖文件夹与磁盘。自动测试已覆盖真实注册命令、进程启动与转发；尚未收到用户重启新版后的实际使用确认。
- 正常界面中检查鼠标 / 键盘右键的新标签入口、重复目录和子目录保留原标签，以及列表 / 详细信息往返时首帧间距。相关自动运行回归已通过，本轮未人工逐项点选菜单验收。
- 安装包已重新打包，但未实际安装 / 卸载验证：setup.cs 的安装流程（含 `--quiet --dir`）会结束正在运行的 FastFile 并写入开始菜单快捷方式与当前用户卸载项，不适合在用户正在使用的开发机上测试。仍需在合适时机验证安装路径、注册命令、卸载恢复及安装后运行。

## 原生文件操作进度与原生背景菜单（2026-10-02）

- 需求：复制、移动（剪切 + 粘贴）、粘贴、删除（回收站）及拖放复制 / 移动不再用 FastFile 自己的引擎和状态栏进度文字，改用 Windows 原生进度窗口；文件区空白处右键改用资源管理器原生背景菜单；修复菜单中叠在一起的两条分隔线。
- 引擎：新文件 `src/ShellFileOperation.{h,cpp}`（命名空间 ShellFileOps）。`Perform` 在调用线程（必须是 STA）上同步执行 IFileOperation，失败时回退 SHFileOperationW（同样的低 16 位标志）。进度接收器在 Pre* 回调中响应取消，在 PostCopyItem / PostMoveItem / PostDeleteItem 中记录结果；`Verify` 只把与顶层源匹配且经文件系统确认的结果记入 completed，落到原本已存在的同名目标（替换 / 合并）计入 notUndoable，不进入撤销，避免撤销时删掉原有内容。旧的 CopyProgressSnapshot、后台复制线程、状态栏进度和取消按钮逻辑已删除（取消按钮保持隐藏）。
- 标志（`OperationFlags`）：复制 / 移动为 FOF_NOCONFIRMMKDIR | FOFX_SHOWELEVATIONPROMPT（保留冲突对话框、错误界面和原生进度）；回收为 FOF_NOCONFIRMATION | FOFX_SHOWELEVATIONPROMPT | FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOFX_RECYCLEONDELETE | FOF_WANTNUKEWARNING；永久删除为 FOF_NOCONFIRMATION | FOFX_SHOWELEVATIONPROMPT（FastFile 先自行确认）。`interactive=false` 只供测试（加 SILENT / NOERRORUI / NOCONFIRMATION / RENAMEONCOLLISION，去掉提权与 nuke 提示）；生产路径 `m_fileOpsInteractive` 恒为 true。
- 撤销决策：复制 / 移动**不**加 FOFX_ADDUNDORECORD。资源管理器的「撤销复制」在 Win10/11 上直接永久删除副本（不进回收站，也不询问），并且会与 FastFile 自己的复制撤销（把副本移入隐藏的 `.FastFileUndo-{guid}` 供重做）重复；若写入该记录，后续 Ctrl+Z 撤销回收站删除时会先撤销到这条复制记录，两个历史错位。回收站删除保留 FOFX_ADDUNDORECORD，FastFile 的 ShellDelete 记录通过 InvokeHistory 调用原生撤销，因此 FastFile 历史与资源管理器撤销栈的顺序保持一致。临时给复制加上该标志后，对齐回归按预期失败（见下）；该模拟运行中真实触发了资源管理器的原生撤销，随后资源管理器留下一个卡住的隐藏操作窗口，正是该决策要避免的风险。
- 线程：`StartFileOperation` 为每项操作创建工作线程，SetThreadDesktop 到 UI 线程的桌面，CoInitializeEx(STA)，以主窗口为 owner 调用 Perform，结束后 PostMessage(kMsgFileOpFinished) 携带堆上的 Result。`OnFileOperationFinished` 在 UI 线程 join 线程、更新 m_copyRunning、写历史（复制 / 移动记录、回收为 ShellDelete、永久删除清空重做）、刷新 FastFile 自己的状态（ExplorerBrowser 依靠变更通知自动刷新）。允许多项操作并行；有操作进行时 Ctrl+Z / Ctrl+Y 提示稍后再试。关闭窗口时若仍有操作，询问是否取消剩余操作，确认后在全部结束后再关闭。
- 入口：OnPasteClicked（含 Shift+Insert、背景菜单 FastFile 粘贴）、OnDeleteClicked → DeletePaths、拖放 PerformDropTransfer → TransferWithFileOperation 均走 StartFileOperation。DeleteItems 仍为同步 Perform（供权限测试）。拖到 Shell 文件区本身、Shell 菜单中的其他命令（如粘贴快捷方式、虚拟项目上的粘贴 / 删除）仍由 Windows 自己执行并显示原生进度，FastFile 不再拦截或附加状态栏进度。
- 背景菜单：`BuildShellBackgroundMenu` 优先取当前 ExplorerBrowser 视图的 `GetItemObject(SVGIO_BACKGROUND)`（ShellBrowserHost::CreateBackgroundContextMenu），得到与资源管理器相同的 查看 / 排序方式 / 分组依据 / 刷新 / 粘贴 / 粘贴快捷方式 / 撤销 / 终端 / 新建 / 属性；只有该文件夹未显示在 Shell 视图时（此电脑快速行、搜索结果）才回退 CreateViewObject，并补上 查看 / 排序方式 / 刷新 / 粘贴。原生 view / arrange 子菜单替换为 FastFile 的子菜单（标签与位置不变，被替换的 Shell 子菜单在菜单结束后才销毁，避免句柄复用），以保持记忆视图、26 像素行距和表头逻辑。撤销 / 重做项的可用状态和标签跟随 FastFile 历史；verb undo / redo / refresh / paste（剪贴板含文件系统项目且为当前文件夹时）以及项目菜单的 delete（全部为文件系统路径时，Shift 为永久删除）由 `HandleRoutedShellVerb` 交给 FastFile，其他 verb 原样交给 Windows。CMF_EXTENDEDVERBS 只在按住 Shift 时加入。原有 FastFile 背景菜单命令 id 移到 0xFE00 起，避开 Shell 的 1..0x7FFF 范围。
- 分隔线根因：旧 TidyMenuSeparators 只把 id==0 视为分隔线；Win11 背景菜单使用 0、-1、0x7FFD、0x7FFE、0x7FFC 等 id，「授予访问权限」空子菜单被剪除后 0 与 0x7FFD 两条分隔线相邻，即截图里的双线。`ShellMenuUtil::NormalizeSeparators` 按 MFT_SEPARATOR（及无文本、非子菜单、非自绘项）判断，去掉开头、结尾和连续分隔线并递归子菜单；背景菜单与项目菜单显示前统一调用。
- 回归：ShellBrowserHostTests 新增混合 id 分隔线规范化单元测试，以及真实视图背景菜单含 paste / groupby / properties verb、规范化后无叠线。主窗口回归新增 CheckShellMenus（TidyMenuSeparators 合成菜单；BuildShellBackgroundMenu 来自视图、含原生 paste / properties / groupby、FastFile 查看 / 排序子菜单仍在、无叠线；refresh 路由、pastelink / properties 不拦截）、CheckFileOperationEngine（四种操作的生产 / 测试标志；临时目录内的复制、同目录复制重命名、移动、取消、回收、永久删除及 SHFileOperation 回退结果），并在快捷键测试中确认 Ctrl+V / Shift+Insert / Ctrl+X→V / Delete / Shift+Delete / 批量粘贴都经 IFileOperation、标志不含 SILENT / NOERRORUI / NOMINIMIZEBOX、状态栏无百分比；新增「回收后 FastFile 复制，先撤销复制、再撤销回收能还原」的对齐检查。删除和拖放改为异步后，测试改为等待操作结束。
- 修复前复现：临时恢复按 id==0 判断分隔线、背景菜单只用 CreateViewObject、复制标志加 FOF_SILENT | FOF_ALLOWUNDO | FOFX_ADDUNDORECORD 后，Shell 浏览器测试 1 项失败，主窗口回归 23 项失败（分隔线、背景菜单来源 / paste / groupby、生产标志、对齐撤销等），日志 `build-ui/nativeops-before-tests.log`、`build-ui/nativeops-before-build.log`。恢复最终源码并刷新修改时间后重新构建（源码中无 BEFORE-FIX 残留）。
- 验证状态：最终源码 Release x64 构建成功（`build-ui/nativeops-build.log`）。最终源码的主窗口回归单独运行通过（88.40 秒），Shell 浏览器测试及其余 5 项在 `build-ui/nativeops-tests.log` 中通过；该日志中的主窗口回归失败为上面所述资源管理器撤销服务卡住引起，未修改的 HEAD 基线同样失败。因此**本次没有更新 build/Release**（常用 exe 仍为 `737DD155…176AB`），也未在正常桌面启动新版截图（启动链路由 FastFileShellActivationTests 在隔离桌面覆盖；直接启动候选 exe 会通过单实例转发到用户已打开的窗口，所以没有这样做）。未重建安装包。

## 系统右键「使用 FastFile 打开」转发修复（2026-10-02）

- 用户实际状态：Directory / Drive 的 `FastFile.SettingsOpen` 命令正确指向常用 FastFile.exe，参数 `--shell-folder`；但历史 `FolderHandlerEnabled=0` 与新的 `IntegrationFolders=1` 同时存在。新进程绕过仅针对旧 `--open` 的启动重定向后，通过 WM_COPYDATA 转发给已有窗口；已有窗口的 kMsgOpenExternalPaths 又无条件调用 RedirectDisabledShellOpen，主动启动 Explorer。这是上一轮内部打开 / 新标签测试遗漏的外部启动链路。
- 接收端现在直接 BringToForeground / OpenExternalPaths。旧缓存 `--open` 的停用判断仍在启动入口处理，不影响明确的 `--shell-folder`、裸路径和 FastFile 窗口之间的转发。未改用户的真实注册表开关、目录默认值、文件关联或视图间距。
- 新增第 7 项 CTest：FastFileShellActivationTests。测试从生产 ApplySystemIntegration 读取注册命令，经真实 ShellExecuteEx 的 FastFile.SettingsOpen verb 启动独立进程；测试进程编译同一 src/main.cpp 生产入口（改名入口函数），只增加隔离桌面 / 注册表的启动准备和 Create 完成标记，不替代参数解析、单实例查找、WM_COPYDATA 或主窗口打开逻辑。覆盖目录、磁盘根、子目录向已有窗口转发并退出，以及无已有窗口时创建 FastFile、打开准确目录并正常关闭保存。历史停用标记为 0；真实用户集成状态保持原值。
- 原接收端恢复后，目录 / 磁盘 / 子目录出现 3 个预期失败，首次启动通过：`build-ui/shell-activation-before-tests.log`（21.49 秒）。修复后专项通过。首次启动测试等待 Create 完成才关闭窗口，避免测试提前关闭尚在初始化的窗口造成假失败。
- 主窗口回归改用隔离桌面，避免共享桌面的焦点 / 输入影响。剪切后的剪贴板断言在 1 秒内等待剪贴板可读取，并严格检查移动标记、项目数和准确路径；不重新发送剪切命令，避免剪贴板观察器短暂占用导致即时读取假失败。
- 先在 build-ui 构建测试版；最终 Release x64 构建及全部 7 项 CTest 通过（106.94 秒），日志 `build-ui/shell-activation-final-build.log`、`build-ui/shell-activation-final-tests.log`。通过后才将已验证的 exe / map / skin 复制至 build/Release，并校验 exe SHA-256 与候选完全相同。运行中的旧窗口保留，旧文件为 `build/Release/FastFile.before-shell-activation.exe`；用户需退出旧窗口后启动新版，旧进程不能热更新。未重建安装包；未人工逐项点击系统右键菜单，真实注册命令和多进程启动 / 接收 / 首次启动由上述自动运行验证覆盖。

## 非紧凑文件视图与原生菜单新标签（2026-10-02）

- 间距根因：原有 26 逻辑像素加宽只针对 FVM_LIST，在主窗口处理异步导航完成后才应用；新 Shell 视图先显示默认窄行，FVM_DETAILS 则一直使用原行高。并非一个全局紧凑开关即可修复。
- 列表 / 详细信息共用 26 逻辑像素行高。进入目标目录前保存其记忆模式，在 OnViewCreated（视图显示前）设置模式、表头、间距和图标。重复应用同模式不再先恢复窄行；DPI 改变仍重建间距。详细信息保留原生文本、列、选择 / 焦点绘制，真实 Shell 小图标在项目后绘制阶段补画。Navigate 提前记录目标路径，失败时还原，确保同步创建回调中的详细信息列和原始分组快照属于目标目录。
- 菜单根因：已有内部路由只识别 open / explore，遗漏 opennewwindow；主文件区还直接运行 Shell 自有菜单。现在文件区的 NM_RCLICK、WM_CONTEXTMENU（含键盘入口）统一走主窗口的 IContextMenu2/3 菜单。文件系统目录 / 磁盘的 opennewwindow / opennewtab 改为 FastFile 新标签，原标签保留；原生菜单没有提供该命令时补上「在新选项卡中打开」。显式新标签绕过同目录复用和子目录沿当前标签导航策略，异步完成不会切回旧同路径标签。过期导航通知不再改写当前标签。普通文件、压缩文件及不支持的虚拟选择保留原生处理；未改用户系统集成开关或文件关联。
- 回归覆盖统一行高与真实图标、重复应用的间距稳定性、目录往返时不经主窗口完成处理也已具备正确间距、目标分组快照、实际鼠标右键通知及键盘菜单入口、真实磁盘 / 目录菜单的补充入口、新窗口 / 新标签命令（含大小写）、磁盘 / 重复目录 / 子目录的新标签及原标签保留、完成后选中状态、文件 / 无效混合选择保护、过期通知和真实 Shell 导航复用。
- 临时恢复旧列表独有间距和遗漏新窗口命令后，两项专项出现预期失败，见 `build/noncompact-menu-before-tests.log`；单独取消显示前初始化后，目录往返专项复现失败，见 `build/noncompact-first-view-before-tests.log`。恢复源文件时刷新修改时间，避免增量构建继续使用复现版对象文件；最终源码不含诊断输出或复现开关。
- 最终 Release x64 构建及全部 6 项 CTest 通过（104.96 秒），日志 `build/noncompact-menu-build.log`、`build/noncompact-menu-final-tests.log`。包括真实主窗口 / Shell 浏览器自动运行验证；未做人工逐项点选菜单验收。常用 `build/Release/FastFile.exe` 已更新；运行中的旧窗口保留，旧文件移为 `build/Release/FastFile.before-noncompact-menu.exe`，需重新启动新版才能使用修复。未重建安装包。

## 标签栏取消自动横向动画（2026-10-02）

- 根因：Add / Insert 给标签记录创建时间并启动 16ms 定时器，绘制时在 160ms 内横移最多 10 逻辑像素；重建标签栏会让全部标签重新播放。绘制位移与关闭按钮 / 点击区域的最终位置也短暂不一致。
- 删除创建时间、动画状态、动画入口、定时器及横向绘制位移；新增、插入、切换、标题更新、重排和关闭标签直接显示最终布局。保留宽度算法、滚轮滚动、选中标签进入视野、末尾 +、栏高和分界线。
- 回归在 96 / 144 / 192 DPI 比较首帧和 180ms 后的全部绘制像素及布局，覆盖上述操作以及溢出标签的手动滚动。移除旧测试将创建时间强制清零的操作，避免掩盖动画。
- 临时恢复旧创建时间和横向绘制位移后，专项在三个 DPI 出现 3 个预期失败，日志 `build-ui/tab-motion-before-tests.log`；恢复修复源码重新编译后专项通过，日志 `build-ui/tab-motion-final-tests.log`。
- Release x64 构建及全部 6 项 CTest 通过（103.22 秒），日志 `build/tab-no-animation-build.log`、`build/tab-no-animation-tests.log`；常用 `build/Release/FastFile.exe` 已更新。实际窗口截图确认新版可运行；人工新增标签检查被用户输入及窗口最小化打断，未完成此项人工操作验收。真实主窗口运行回归及多 DPI 绘制检查已通过。未重建安装包。

## FastFile 内部磁盘 / 目录打开（2026-10-02）

- 根因：`ShellBrowserHost::DefaultCommand` 只处理普通文件，遇到 `SFGAO_FOLDER` 返回 `S_FALSE`，嵌入 Shell 文件区继续执行系统默认目录命令，可能启动 Explorer。旧回归只断言目录返回 `S_FALSE`，未验证真实主窗口内部激活，漏掉了这一行为。
- 文件系统目录和磁盘默认命令现返回 `S_OK`，通过独立 `kMsgShellFolderOpen` 将路径交给主窗口；在回调退出后导航，更新当前标签、历史、树与详情，避免在 Shell 激活回调中重建视图。内部打开不依赖系统接管开关，也不调用注册表目录打开命令。多目录激活的后续路径交给标签入口处理。
- 导航区 / 快捷区的 Shell 菜单保留原文案和图标，只将文件系统目录的标准 `open` / `explore` 命令路由为内部导航；其他菜单命令和普通文件关联保留。ZIP 等具有 `SFGAO_FOLDER` 的实际文件与不支持的虚拟对象继续交给 Shell 处理。
- 回归补上真实磁盘选择、无外部启动且收到内部请求、两种接管配置状态下主窗口进入磁盘、普通目录内部激活、目录菜单路由、普通文件和属性命令不被接管、压缩目录行为。
- 交付验证：常用 `build/Release/FastFile.exe` 已更新，Release x64 构建和全部 6 项 CTest 通过（93.41 秒），日志 `build/internal-folder-build.log`、`build/internal-folder-tests.log`。使用独立配置实际双击「软件 (D:)」，原 FastFile 窗口 / 当前标签进入 D: 根目录，面包屑、树及右侧详情同步，磁盘内容正常显示，窗口列表未新增 Windows Explorer。未改用户真实系统接管选项；未重建安装包。


## 2026-10-02 设置功能

- 新增 `FastFileSettings.h` 与 `MainWnd.Settings.cpp`，分别维护偏好数据、UTF-16 原子保存、四页 Win32 设置对话框及即时外观更新；编译单元已登记 CMake 和 VS 项目。命令栏右端齿轮沿用自绘抗锯齿图标与 32 逻辑像素热区。
- 设置页：常规与标签（启动位置、外部窗口、复用、关闭确认）、外观（密度、导航字号、栏高、标签宽度、导航滚动条）、浏览（视图、记忆、排序、分组）、系统集成（菜单、目录磁盘默认入口、桌面此电脑、恢复）。布局尺寸仍为 96 DPI 逻辑单位。默认 29 高度、150% 标签宽度、12 导航字号保持已有外观。
- `%APPDATA%\FastFile\settings.ini` 不包含能开启系统集成的字段。原会话 / 收藏 / 快捷 / 文件夹视图配置继续使用各自文件；设置窗口取消不修改配置。外部新路径使用新标签，已有路径是否复用由设置控制；内部连续目录导航保持既有行为。
- 可选集成默认关闭，用当前用户 `Software\Classes` 的私有 `FastFile.SettingsOpen` 命令；仅 Directory / Drive 接管默认，Folder 通用虚拟命名空间默认保持原值；此电脑使用单独 CLSID。新命令参数为 `--shell-folder`，不受历史 `--open` 停用重定向影响。
- 原默认值（存在性、类型及原始字节）备份到 `Software\FastFile\IntegrationBackupV1`。关闭时只恢复仍指向本程序的默认项；同名外来私有命令拒绝覆盖。失败尝试事务恢复。恢复能处理命令已写入、状态旗标尚未写入的中断情形。图片和视频关联完全不改；显式启动 Explorer 的其他应用和系统文件对话框不被接管。
- 卸载新增 `--restore-integration` 无窗口恢复入口，安装器与 PowerShell 卸载脚本在恢复失败时停止卸载。安装器源码已独立编译、脚本语法已检查；本次未重建或实际安装 / 卸载安装包。
- 新回归覆盖配置往返、边界值、失败保存、启动模式、复用设置、外观更新、四页实际对话框与保存 / 取消、隔离注册表的独立开关及恢复、Shell 分组与返回原分组、齿轮多 DPI 热区。系统集成测试用进程级注册表隔离，不开启用户真实接管。
- 交付验证：常用 `build/Release/FastFile.exe` 已更新，Release x64 和全部 6 项 CTest 通过（95.04 秒），日志 `build/settings-build.log`、`build/settings-tests.log`。四页真实 Win32 对话框运行测试覆盖保存 / 取消；早期取消回归遇到初始化可见性时序，增加初始化及显示检查、回收定时器后最终全套通过。人工窗口检查齿轮入口及常规页面；其他页面交互由上述运行测试完成。Shell 分组使用真实 ExplorerBrowser 检查，默认入口恢复使用隔离注册表检查。未启用用户真实默认接管，也未实际运行安装 / 卸载流程；安装器源码编译与卸载脚本语法通过。未重建安装包。


> 当前开发基线：最新安装包版本记录为 **1.0.9**。近期完成第十七批的文件区键盘导航、命令栏双色图标，以及第十八批的 Fluent 悬停滚动条。仓库根目录 `VERSION` 是版本号唯一来源，CMake 与安装脚本均从此读取。
>
> 2026-09-28：已纳入 Git 版本管理；原 8000 行单文件 `src\MainWnd.cpp` 已拆分为多个职责单元（见「源码结构」）。
>
> 主要能力已覆盖浏览、标签、多视图、搜索、文件操作、Shell 集成、预览和安装；当前建议重点转向回归验证、测试覆盖和发布可靠性。已知功能边界见 README 的「已知限制」。
> 面向使用者的版本记录见 [CHANGELOG.md](CHANGELOG.md)；下面的「开发日志」按批次保留完整细节。

## 命令栏间距、图标与文件区边界（2026-10-02）

- 用户反馈命令栏拥挤、图标锯齿，文件分组标题像侵入工具栏。命令按钮原 childpadding 为 0，标签按钮宽 76；现增加 4 逻辑像素间距，新建 / 排序 / 查看宽 88，图标与文字距离由 4 改为 6。图标仍为 16 逻辑像素，按钮热区仍高 32，工具栏仍高 40；标签和收藏栏的 29 高度不变。
- 命令图标沿用现有原创矢量路径，先按最终物理尺寸的 4 倍绘制，再用 GDI+ 高质量缩小至准确的当前 DPI 像素，DuiLib 1:1 显示，不放大 16px 位图。过滤后的 RGB 固定 #1A1A1A，只保留过滤 Alpha 表示边缘覆盖；否则双三次插值在透明边缘可能改变 RGB。禁用图标最后统一乘 40% Alpha。缓存键和文件版本升为 v6，避免复用旧图。
- 根因：当前 DuiLib 的 PaintBorder 外层判断仅检查 left>0，因此 `0,0,0,1` 的底边配置不绘制。本轮不改第三方内核，改用独立 `command_body_divider` 实色控件（1 逻辑像素向上取整、#D0D0D0），并给 body_host 增加 8 逻辑像素顶部 inset，让左侧导航、主文件区和右侧详情共享顶部留白；Shell 分组 / 缩略图和滚动布局保持原生。
- 回归覆盖 96 / 144 / 192 DPI 的命令按钮间距、标签按钮宽度、分隔线的实际绘制像素和文件区 inset，以及命令图标物理尺寸、边缘 Alpha 层次、精确正文色和禁用透明度。恢复旧间距 / 宽度 / 无分隔 / 无留白的行为后专项出现 22 个预期失败（`build-ui/command-before-tests.log`）；修复后的图标专项通过。
- 交付：常用 `build/Release/FastFile.exe` 已更新，Release x64 和全部 6 项 CTest 通过（83.96 秒），日志 `build/command-spacing-build.log`、`build/command-spacing-tests.log`；像素回归调用完整 `Paint` 入口设置绘制区域，而非直接 DoPaint。150% 实际下载目录检查清晰水平分隔线、「昨天」和「上周」分组标题在线下且留白充足、命令间距增加；选中 12.png 后剪切 / 复制 / 重命名 / 删除正常启用，右侧 PNG 图像、2048×2048 分辨率及预览同步，查看按钮正常打开菜单。原运行文件保留为 `build/Release/FastFile.before-command-spacing.exe`，未强制关闭旧窗口；未重建安装包。

## 标签与收藏栏收紧（2026-10-02）

- 用户要求两栏高度缩为原来的 80%；由 36 改为 29 逻辑像素（36×0.8 取整）。XML 初始高度、DPI 运行时高度、标题栏系统按钮和 caption 命中矩形同步修改；+ 的宽度继续为 32，但高度适配 29，避免超出标题行。
- 文字保持 12、Shell 图标保持 16 逻辑像素；收藏芯片、星标热区和空栏提示统一为 25 逻辑像素高，保留上下各 2 的留白。星标宽度仍 32，因此图形尺寸及收藏首项横向位置不变。标签宽度、关闭槽、浅色分界线、首标签贴边和末尾 + 保持。
- 新回归覆盖 96 / 144 / 192 DPI 的两栏、窗口按钮、caption 热区、收藏按钮和提示高度、字体与图标尺寸，以及短竖线绘制和 + 不越界。恢复旧高度后专项出现 27 个预期失败（`build-ui/compact-before-tests.log`），修复后的初次 UI 专项通过。
- 交付：常用 `build/Release/FastFile.exe` 已更新；Release x64 构建和全部 6 项 CTest 通过（84.34 秒），日志 `build/compact-bands-build.log`、`build/compact-bands-tests.log`。150% 实际新窗口检查标签/收藏两栏收紧、星标与六个收藏短名居中且无裁切，随后选中 D: 并 Ctrl+T 打开第二标签，确认两标签同高、图标和关闭按钮居中、短竖线及末尾 + 正常。原正在运行的可执行文件留存为 `build/Release/FastFile.before-compact-bands.exe`，未强制关闭用户窗口；未重建安装包。

## 原生文件夹菜单与标签边缘（2026-10-02）

- 用户要求取消本程序对系统文件夹默认打开方式的管理。旧恢复逻辑仍注册 `FastFile.WindowsExplorer` 并将其设为 Folder / Directory / Drive 的默认命令，导致右键菜单出现「使用 Windows 文件资源管理器打开」。现删除接管菜单及注册入口，启动时仅清理历史上由本程序拥有的 `FastFile.open` / `FastFile.WindowsExplorer` 命令和默认值，移除用户层覆盖后继承 Windows 本身的设置，不再安装自定义 Explorer 命令。其他工具的默认项、菜单项及同名但指向其他程序的命令保留。此前关于可选接管系统文件夹的记录由本节取代。
- 文件双击仍遵循 Windows 当前有效关联，保留上一轮默认图片打开修复；本轮不修改任何文件格式的默认应用。
- 标签标题栏 XML 和运行时左 inset 同时改为 0，第一张标签贴合窗口边缘；标签间增加居中的 16 逻辑像素高、1 物理像素宽淡色竖线，浅色 #CFCFCF、深色 #505050。既有宽度增加 50%、36 逻辑行高、关闭槽和末尾 + 的布局保持。
- 回归覆盖旧两种默认命令的清理、无关默认项与同名第三方命令保护，以及 96 / 144 / 192 DPI 下首标签边缘和浅深主题分界线的实际绘制像素。临时恢复旧绘制后 UI 专项出现 3 个预期失败，恢复修复后通过；日志 `build-ui/tab-separator-before-tests.log`。
- 常用 `build/Release/FastFile.exe` 已更新，Release x64 构建与全部 6 项 CTest 通过（84.11 秒），日志 `build/native-menu-tabs-final-build.log`、`build/native-menu-tabs-final-tests.log`。实际启动新版后，当前用户 Folder / Directory / Drive 的两个历史命令键及默认覆盖均已清除，窗口首标签无左侧空白。实际右键菜单和多标签视觉验收被用户鼠标操作打断，未声称这两项运行检查完成；自动绘制和注册表回归已通过。
- 为保留用户正在使用的旧窗口，在其运行期间将旧可执行文件重命名为 `build/Release/FastFile.before-native-menu-tabs.exe`，再生成常用路径的新版本；旧窗口未被强制关闭，不能热更新，需要用户改用新窗口。未重建安装包。

## 历史工作区记录：UI 精致度（2026-10-01）

- 默认文件打开修复：嵌入视图原 `ICommDlgBrowser::OnDefaultCommand` 一直返回 S_FALSE，文件双击/Enter 交给旧式视图默认命令；普通 ShellExecuteEx 空 verb 调用也在开发机复现默认照片程序已存在却弹选择框，必须传入官方查询的生效关联；旧 DuiLib 列表、搜索结果和应用菜单另固定调用 open。文件激活现在读取触发视图的实际选择，通过官方 `IApplicationAssociationRegistration::QueryCurrentDefault(AT_FILEEXTENSION, AL_EFFECTIVE)` 查询当前生效 ProgID，再将动态查询结果作为 `SEE_MASK_CLASSNAME` 交给 `ShellExecuteExW` 执行默认操作（空 lpVerb），不手动读取/写入 UserChoice、不拼接打开程序命令；查询不到默认关联时保留普通 Shell 调用及系统选择窗口；`SEE_MASK_NOASYNC` 保持 STA 启动完成所需的宿主生命周期。OnDefaultCommand、原生 LVN_ITEMACTIVATE 和非编辑态 Enter 共用同一处理；取消/失败也返回 S_OK 防止视图再次打开。文件夹、虚拟对象和空选择仍返回 S_FALSE，保留 Shell 原生导航。搜索/旧列表/应用菜单共用默认打开函数；旧列表失败不再显示「已打开」。不修改用户默认关联，显式右键「打开方式」保留原生菜单行为。
- 默认打开回归：真实 Shell 文件选择传入记录启动器，检查只启动准确选中文件、使用官方查询的有效关联/默认操作、无关联时保留系统回退、取消后不再回落、空选择不启动、磁盘/文件夹仍交给 Shell 导航。
- 默认打开交付验证：常用 `build/Release/FastFile.exe` 已更新；Release x64 和全部 6 项 CTest 通过（88.12 秒，`build/default-open-final-build.log`、`build/default-open-final-tests.log`）。补上空白区域激活通知检查后，最终构建及受影响 Shell 专项再次通过（`build/default-open-acceptance-build.log`、`build/default-open-acceptance-tests.log`）。正常用户环境实测 Enter：`12.png` 直接进入 Windows「照片」，JPEG 直接进入已设置的 XnView MP，均不显示选择窗口；没有修改默认关联或代点「始终」。普通调用的失败已实测复现，最终有效关联调用成功。开发机用户操作导致早期窗口验收被中断，之后完成上述实际检查；临时诊断代码已移除。未重建安装包。


- 媒体比例修复（取代下方原生图像列表绘制记录）：之前把 Shell 的正方形图像列表槽按同样的目标宽高绘制，媒体内容随槽被压成方形。大/超大图标改用 `IShellItemImageFactory` 的原始 `THUMBNAILONLY` 位图，按实际位图宽高等比缩小并居中，保持 128/160 逻辑像素槽和统一行高；不裁切、不放大小图标。项目在 ITEMPREPAINT 一次完成，避免原生多行名称残留。
- 关联程序标识从 Shell 的 TypeOverlay 资源获取；无该设置时查询默认打开程序的 Shell 图标，显式空 TypeOverlay 则不画。标识单独绘制，不与媒体共同缩放，尺寸最多 20 逻辑像素且不超过实际源图标尺寸；不改变文件关联。原生图像列表的胶片边框不再参与缩略图绘制。缓存随项目图像清理并释放 HICON。
- 比例回归：生成 120×240 竖图、240×120 横图、120×120 方图，逐项检查两种视图实际绘制像素的比例和居中；合成测试窗口在切换视图后恢复宿主布局，避免 0×0 子窗口导致误判。关联标识回归用进程隔离的注册表检查 TypeOverlay、缓存复用/释放及显式空值，不修改真实用户关联。
- 本次交付验证：常用 `build/Release/FastFile.exe` 已更新；Release x64 构建和全部 6 项 CTest 通过（83.84 秒），日志 `build/aspect-build.log`、`build/aspect-tests.log`。150% 下载目录实际检查大/超大图标横向视频等比显示、系统播放器标识、整格选中与右侧同一视频；`12.png` 图片预览和 2048×2048 分辨率正确。使用独立配置验收，未重建安装包。


- 标签默认宽度增加 50%：自然内容宽度（图标、文字、固定关闭槽及边距）乘 1.5，默认宽度范围由 80～240 改为 120～360 逻辑像素；选中/未选中仍同高、同宽，实际图标/文字/关闭槽尺寸保持原值，既有拥挤时压缩及横向滚动算法不变，+ 继续紧跟末标签。回归检查测量值与原内容宽度的 1.5 倍、选中切换不改变宽度、36 逻辑行高及 + 的位置。
- 媒体默认 Windows 缩略图：大/超大图标项目从原生 Shell 文件视图 `LVSIL_NORMAL` 图像列表绘制，保留系统缩略图装饰和关联程序 TypeOverlay；缩略图不可用时继续使用 Shell 工厂图像/类型图标回退。原生图像列表可能比固定槽大，用 `ImageList_DrawIndirect` 的 `ILD_SCALE` 缩小到槽内，不放大较小图标，不自己拼贴播放器图标或读写用户文件关联。相片/视频/音频通过 Windows perceived type 判定，其他文件、目录和磁盘保留原路径。
- 新媒体加载注意：完全跳过原生项目绘制会让新枚举的文件始终停留在类型图标。现 ITEMPREPAINT 允许 Shell 正常加载并请求 ITEMPOSTPAINT，再绘制固定槽、单行名称及整格选中底；测试显示实际窗口并验证新生成图片的颜色内容及原生图像列表绘制。右侧图片优先获取 Windows Shell 缩略图（失败再解码原图），视频继续 Shell 缩略图；右侧详情、分辨率及预览圆角/边距保持原行为。打开程序标识的有无遵循 Windows 当前关联及 TypeOverlay 设置。
- 应用图标：用户指定 `D:\Icons\图标美化\Komfort-Zone-Unibody-Hds-Bootcamp.ico` 原样复制到 `res/FastFile.ico`，资源 ID 1 保持不变，包含 16/32/48/128/256 尺寸。同步更新 `.rc` 源以触发 MSBuild 资源编译。回归加载实际 FastFile.exe 资源，逐项比较图标尺寸及图像字节与 ICO 源，避免只复制文件而未更新程序。
- 标签/媒体/图标验证：常用 `build/Release/FastFile.exe` 已更新，Release x64 及全部 6 项 CTest 通过（82.58 秒），日志 `build/media-tabs-build.log`、`build/media-tabs-tests.log`。150% 下载目录测试窗口已观察到视频胶片边框和系统播放器标识；最终可见窗口回归验证新生成图片确实加载缩略图、右侧优先 Shell 图像且分辨率保留、固定网格选中颜色与导航/滚动行为、标签自然宽度乘 1.5 和 + 跟随。ICO 源与仓库资源 SHA256 一致，最终 exe 全部图标资源字节比对通过。最终常用窗口已启动；用户正在操作其他窗口，未继续抢占焦点做截图。未重建安装包。

- 删除权限交互：旧用户删除路径使用 `SHFileOperation` 并设置 `FOF_NOERRORUI | FOF_SILENT`，屏蔽权限不足时的系统交互；其十进制 120 为旧 Shell 返回码 `DE_ACCESSDENIEDSRC`（0x78），不是 Win32 `GetLastError` 的 120。普通删除还额外显示了自定义确认框。用户删除现改用 `IFileOperation`，设置主窗口为 owner，不关闭系统错误/进度 UI，允许权限提示；普通删除设置 `FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD | FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING`，取消例行确认但保留不能回收时的警告。Shift+Delete 保留明确的永久删除确认，执行仍走同一 Shell 接口。
- 删除完成处理：检查 HRESULT 与 `GetAnyOperationsAborted`；取消/跳过按非成功处理，批量中实际删除成功的路径仍加入历史并刷新列表，保留的路径不加入撤销记录。输入解析失败不会执行已排队部分。内部撤销副本清理继续走专用无交互临时目录清理接口，不让清理产生用户权限窗口。工具栏、Delete/Ctrl+D、Shift+Delete 和应用删除菜单共用这一入口。
- 删除回归更新：普通删除不出现 FastFile 确认框且可撤销/重做，永久删除取消仍保留文件；配置断言覆盖回收标志、撤销标志、无法回收警告、错误/进度 UI 未被抑制、两种删除允许权限提示。交互回归入口 `FastFileMainWndRegressionTests --delete-permission-check` / `--delete-partial-check` 仅使用自身临时目录的 ACL 测试文件，需在系统权限对话框选择「取消」，之后验证保留文件与完成路径并恢复临时 ACL；不对用户的 C: 根目录 DLL 执行删除。
- 删除交付验证：常用 `build/Release/FastFile.exe` 已更新，Release x64 和全部 6 项 CTest 通过（79.57 秒），日志 `build/delete-shell-build.log`、`build/delete-shell-tests.log`。实际运行两种 ACL 交互回归均显示 Windows 原生「你需要提供管理员权限才能删除此文件」及「继续 / 跳过 / 取消」。单项取消后保留文件、完成数 0；批量取消保留受保护项、此前普通项进入回收站且完成数 1；两次回归 0 failures，临时 ACL 已恢复。未代用户点击管理员「继续」或执行 UAC 授权，因此提权后的成功删除未作运行验收；用户的两个根目录 DLL 保留。未重建安装包。

- 列表视图增加上下间距：仅 `FVM_LIST` 使用 26 逻辑像素高的透明图标槽（150% 约 39～40 物理像素行高）。原生 Shell 列表负责项目排列和交互；项目绘制保留 Shell 名称、原字体、原尺寸小图标、叠加标记及剪切变淡，并绘制整行选中底和焦点框。普通 `LVM_SETICONSPACING` 对列表行高无效；Shell 图标槽尺寸输入按 96 DPI 单位传入。Shell 枚举和排序可能在内部恢复小图标列表，绘制前检查并恢复行高；单张透明占位图列表不会覆盖保存的 Shell 图标来源。切换视图、重新绑定列表和销毁时恢复原 Shell 图标列表，销毁本程序的透明占位列表。回归覆盖真实列表行高、原物理尺寸 Shell 图标实际绘制、Shell 内部刷新后间距与图标保留、详细信息视图间距恢复。
- 列表间距交付：常用 `build/Release/FastFile.exe` 已更新；Release x64 构建和全部 6 项 CTest 通过（77.41 秒），日志 `build/list-spacing-build.log`、`build/list-spacing-tests.log`。150% 实际窗口复核 C: 根目录列表，文件夹上下留白增加、Shell 图标正常，选中 Program Files 时右侧名称与路径同步正确。未重建安装包。

- 磁盘大图标边缘：实测 Shell `GetImage(ICONONLY)` 返回的 C: 192 / 240 像素位图中分别有约 465 / 689 个半透明像素 RGB 通道超过 Alpha，直接 `AlphaBlend(AC_SRC_ALPHA)` 导致边缘断线或彩色溢出。缓存前将具有此特征的位图转换为预乘 Alpha，并清除全透明像素中的 RGB；已预乘图像和不透明图片不重复乘 Alpha。回归覆盖直 Alpha、已预乘、透明脏 RGB、不透明像素，以及真实 Shell 磁盘位图。
- 「此电脑」原生平铺行高固定为 64 逻辑像素（150% 为 96 物理像素），增加上下磁盘项目的留白；图标大小、标题、容量条和容量文案继续由 Shell 绘制。切换视图前恢复原生尺寸，保留宽度配置，反复切换不累计增加。真实 Shell 平铺行高和重复设置均有回归。
- 平铺接口注意：Shell 列表包装层会把 `LVM_SETTILEVIEWINFO` 输入再按 DPI 缩放，因此传入 96 DPI 单位；读取的项目矩形 / TileViewInfo 为物理像素。固定高度时同时固定为原项目宽度（允许取整误差 1 物理像素），避免自动宽度重算而减少列数；Shell 返回的附加私有 flags 不直接 OR 到 `LVTVIF_FIXEDSIZE` 中。
- 磁盘边缘与平铺交付：常用 `build/Release/FastFile.exe` 已更新；Release x64 构建、全部 6 项 CTest 通过（79.05 秒），日志 `build/icon-alpha-build.log`、`build/icon-alpha-tests.log`。150% 实际窗口检查大图标与超大图标的磁盘斜边、圆角不再有原来的断线 / 杂色；切到平铺后保持多列布局，上下项目留白增加，标题 / 容量条 / 可用空间文案无裁切。未重建安装包。

- 导航字体收紧：用户同意快速访问和目录树统一从 13 改为 12 逻辑像素（150% 为 18 物理像素），共享字体 4 改为 Segoe UI 常规体，中文通过 Windows 字体链接回退；开发机 Segoe UI 字体链接包含 Microsoft YaHei UI。XML 初始配置和 DPI 重注册同步修改，行高、图标尺寸、正文色、点击区域保持原值。100% / 150% / 200% 回归覆盖字号、字体、常规字重与行高不变；修改前六项新断言失败。
- 导航字体验收：常用 `build/Release/FastFile.exe` 已更新，Release x64 构建和全部 6 项 CTest 通过（77.37 秒）；日志 `build/nav-font-build.log`、`build/nav-font-tests.log`。150% 实际窗口确认「此电脑 / 文档 / 图片」、`Win11x64 (C:)`、`AMD`、`Program Files` 及 `Program Files (x86)` 正常显示，中英文垂直居中、字号一致，没有方框或裁切；左侧行高、图标和滚动条保持原尺寸。未重建安装包。
- 完整回归暴露既有剪贴板恢复问题：保存 `OleGetClipboard` 的实时对象、清空剪贴板后将该对象恢复并 `OleFlushClipboard`，会在 ole32 中异常退出。测试现先枚举格式并取出独立 STGMEDIUM 数据到 Shell 数据对象，再执行剪贴板测试及恢复；应用文件操作不变。

- 本轮最终交付：常用 `build/Release/FastFile.exe` 已重新构建（2026-10-01 19:49）。Release x64 和全部 6 项 CTest 通过（77.88 秒），日志为 `build/nav-capture-canonical-build.log`、`build/nav-capture-canonical-tests.log`。150% 实际窗口复测：树滑块拖动松开后选中 `12.png`，鼠标无按键移到滑块并沿轨道下移，树位置保持不变；滚轮仍可滚动。常态 / 悬停滑块为 12 / 18 物理像素，点击区域为 18 物理像素。再次切换另一张图片时，右侧预览、PNG 本地化类型和 2048 × 2048 分辨率均随选中项更新。未重建安装包。

- 滚动条复测时发现，树焦点残留会让 `UpdatePreviewForSelection` 通过通用文件操作选择逻辑取到当前目录；预览现直接读取可见 Shell 列表选择，树键盘文件操作仍保留原目标规则。补充树焦点残留时文件预览不变的回归。Shell 大图标绘制只处理 `LVCDI_ITEM`，在原生项目矩形不与视口相交时跳过缩略图，避免将分组通知或不可见项目当成文件取图；两项均有回归覆盖。

- 用户复测补充：常用 `build/Release/FastFile.exe` 当时仍是 12:34 的旧产物，上一轮只提供了 `build-ui/Release` 的独立测试版，宽度修改没有进入常用路径。另发现丢失 BUTTONUP / 捕获转移后的滚动状态未清理：DuiLib 的逻辑点击对象、捕获标记和滚动定时器可能残留，导致回到滚动条后纯悬停也滚动。补齐窗口 CAPTURECHANGED / CANCELMODE / KILLFOCUS 取消，滚动控件在无左键的移动 / 定时器消息中清理手势；DuiLib 仅对指定控件清除逻辑捕获，不释放别的原生窗口捕获，正常释放先清逻辑标记以避开同步通知重入。

- 左侧树滚动条后续修复：常态滑块 8、悬停 / 拖动 12、实际点击区域始终保留 12 逻辑像素（150% 为 12 / 18 / 18 物理像素）。原实现仅向内容区覆盖绘制宽滑块，点击区域仍为窄轨；窗口还会将滚动条按下识别成文件拖拽。现将滚动条手势交回 DuiLib 独占，拖动中不收窄，并在每个鼠标移动消息立即更新滚动位置，避免快速释放丢失 50ms 定时器尚未处理的位移。主区和预览条的既有常态宽度保留。
- 滚动条修复验证：回归修复前复现点击区域过窄、滚动条按下启动文件拖拽识别、捕获中收窄三个失败；修复后专项通过，另覆盖快速拖动立即更新、移出轨道继续拖动和释放结束捕获。Release x64 构建与全部 6 项 CTest 通过（89.00 秒，`build/nav-scroll-tests.log`）；真实窗口上下拖动左侧树滑块，鼠标横向移出轨道后仍能滚动，释放正常，无文件拖拽图标。截图 `build/nav-scroll-acceptance.jpg`。

- 大 / 超大图标沿用原生 Shell 浏览器和缩略图工厂，通过 Win32 兼容列表绘制固定 128 / 160 逻辑像素图像槽、单行居中文件名、整格浅蓝选中和浅灰悬停。Shell 格距接口会内部处理 DPI，传入 96 DPI 格距；图像请求和实际绘制使用窗口物理像素，避免 150% 二次放大。其他视图保留 Shell 绘制、详细信息四列表头和原生文件操作。
- 预览边距 16、圆角 4、标题间距 12；类型从 Shell 注册的 MUI 资源和类型信息取得，路径优先保留父目录及文件名。无选中时继续统计真实子项。命令图标以 16 逻辑像素生成，正文色 #1A1A1A；禁用图标整体 Alpha 为 40%。
- 滚动条静止宽 4 逻辑像素（150% 为 6 物理像素）、#C4C4C4，悬停加宽；Shell 自身的凹边移除，分隔交给 DuiLib 的 1 逻辑像素 #E5E5E5 边。树和快捷项统一 13 逻辑像素，导航完成时重放 PIDL 展开、嵌套滚入与单处浅灰高亮；首次启动立即绘出包含「图片」的默认快捷行。
- 标签选中和未选中同为 36 逻辑像素，图标圆角 2；保留原宽度算法、关闭槽、末标签旁的 +、星标短名收藏和目录搜索占位。
- 专项测试 `FastFileUiPolishTests` 覆盖 96 / 144 / 192 DPI；Shell 测试增加真实 DPI 格距、选中滚入不丢选择、整格颜色与无凹边回归。运行验收使用独立 `build-ui/runtime-profile`，不改用户常用配置。测试版位于 `build-ui/Release/FastFile.exe`；本次未重建安装包，安装包版本记录仍为上文的 1.0.9。
- 验证：Release x64 构建成功，全部 6 项 CTest 通过（81.18 秒）。150% 实际「刘诗诗」目录验证大图标固定槽、1 (583).jpg 整格选中与右侧同图、类型「JPEG 图像」、分辨率 5304 × 7952、树展开至该目录；「图片」快捷单处浅灰高亮、无选中命令变淡与真实计数、星标短名收藏、标签等高及末尾 + 均完成运行检查。日志在 `build/ui-final-tests.log`，截图在 `build/ui-acceptance-150.jpg`。

## 常用快捷键与撤销 / 重做修复（2026-10-01）

- Ctrl+Z 原因：原生文件列表键盘消息不经过主窗口，且原撤销记录未包含复制、新建目录、原生 Shell 重命名和回收站删除。统一在 DuiLib 消息预处理入口转发宿主命令，文本编辑窗口保留自身的剪贴板和撤销行为；文件区剩余按键仍交给 Shell。
- 当前会话撤销 / 重做覆盖复制、整批移动、新建文件夹、应用重命名、原生重命名和回收站删除。复制撤销将副本移入同一目标目录的隐藏临时目录，重做移回；清除重做历史或退出时清理暂存。目标同名冲突不会覆盖；非空新建目录不直接删除；未完成操作保留历史以便重试。
- 原生 Shell 操作通过背景菜单的 canonical undo / redo 调用。即使指定 NOASYNC 仍可能异步完成，因此计时器确认实际路径后才推进历史；将预期重命名通知单独排除，避免延迟通知被误认为新操作并清空重做栈。
- 补齐 Ctrl+Y / Ctrl+Shift+Z、Ctrl+Insert / Shift+Insert、Ctrl+Shift+C、Ctrl+D、Ctrl+R、Ctrl+N、Ctrl+F4、Ctrl+数字选标签、Ctrl+E / F3、F4 地址历史、F6 / Shift+F6、Ctrl+Shift+E、Alt+P / Alt+Shift+P、Ctrl+Shift+1…8、F11 最大化 / 还原、导航树方向键和数字键盘加减。小图标与内容视图追加枚举，保持旧配置数值兼容。完整清单见 README。
- 验证：Release x64 构建及全部 5 项 CTest 通过；回归覆盖上述按键入口、原生异步撤销 / 重做顺序、多项和文件夹复制、同名重做冲突、非空目录撤销保护、剪贴板与文本编辑保护、树操作及 8 个视图模式。
- 150% 真实窗口验收：复制 → 撤销 → 重做、F2 原生重命名 → 撤销 → 重做均成功，源与目标 SHA256 一致；Ctrl+Shift+6 显示四列表头，Ctrl+Shift+4 隐藏表头；Ctrl+E、Ctrl+L、Escape、F4 地址历史均正常。
- 安装包 1.0.9 已重新生成；内嵌 FastFile.exe 和 skin/main.xml 的 SHA256 均与本次 Release 产物一致。

## 当前功能区、地址栏与搜索修复（2026-09-30）

- 系统快捷键修复：原生 ExplorerBrowser 文件列表拥有独立子窗口，Ctrl+C / X / V、Delete 等消息未经过主窗口；仅调用 Shell 视图 TranslateAccelerator 也未执行宿主文件命令。主窗口现注册 DuiLib ITranslateAccelerator，在消息分派前将 Shell 文件区快捷键转入现有操作入口，其他按键继续交给原生视图；Shell 重命名 Edit 保留文字编辑键。判断编辑状态时优先检查实际 HWND 焦点，避免搜索框残留的 DuiLib 焦点吞掉文件快捷键。Ctrl+A 同步改为选中原生 Shell 项目，未选中旧自绘列表。
- 复制 / 剪切此前仅保存内部 m_clipboard，现发布 Windows CF_HDROP + Preferred DropEffect；粘贴每次读取系统剪贴板，支持资源管理器互通，避免剪贴板已清空后粘贴旧内部数据。命令栏粘贴状态监听 WM_CLIPBOARDUPDATE。既有后台复制 / 移动、删除确认与回收站操作保留。
- 自动回归：在独占临时目录创建真实 Shell 浏览器，通过 DuiLib 消息入口验证 Ctrl+A、复制与粘贴内容、剪切移动标志及结果、Delete 取消 / 确认、Shift+Delete、系统空剪贴板及重命名编辑保护；测试保存并还原原剪贴板。Release x64 构建及全部 5 项 CTest 通过。
- 真实窗口验收：150% 隔离配置中选中文本文件，Ctrl+C → 切换收藏目标目录 → 点空列表 → Ctrl+V，目标文件与源文件 SHA256 一致；Ctrl+A 选中原生项目，Delete 显示确认且取消保留文件，F2 进入 Shell 编辑框，框内 Ctrl+A 全选文件名，Escape 退出。安装包已重建并核对内嵌程序及皮肤与 Release 产物一致。

- 系统文件夹开关原先要求 Folder / Directory / Drive 三项全部指向 FastFile 才勾选，部分残留关联因此被显示为未启用；现在逐项读取当前用户和合并 Classes 的有效默认动作，任一 FastFile 关联均标记启用。启用确认列出实际打开程序，重复启用可修复本程序旧关联。取消即为三类对象设置独立的 Windows Explorer 动作，不依赖缺失的旧备份，也不覆盖第三方原有命令。关联变更使用 SHChangeNotify + SHCNF_FLUSH 通知 Shell。
- 已打开的 Windows「此电脑」视图实测会缓存旧盘符动作，关联通知和根目录刷新并不能保证立即丢弃。因此显式取消时保存 FolderHandlerEnabled=0，启动入口在单实例转发前检查关联专用 `--open` 调用，已有窗口的外部路径消息接收端也检查关闭状态，直接交给 explorer.exe；启用时清除这项关闭状态。普通启动、应用内导航和拖出新窗口保持 FastFile 行为。
- 地址栏行高 48、内部字段高 36、圆角 6、左右内边距 10（均为逻辑像素）；四枚导航图标缩为 16，改为浅灰并用双倍分辨率绘制。收藏栏高度不变。
- 搜索统一沿用既有结果列表。旧隐藏逻辑向当前视图查询 IOleWindow 失败，原生浏览器父窗口仍盖住结果；现在通过 IShellView 获取并隐藏实际浏览器容器，导航完成后也重放可见状态。清空恢复原生视图。
- 更多菜单的隐藏项目开关此前未同步 Shell 枚举。浏览器站点提供 ICommDlgBrowser2，通过 SHOWALLFILES 枚举并在 IncludeObject 按本程序设置筛选，不修改 Windows 全局隐藏设置。回归覆盖隐藏文件显示与隐藏两个方向。
- 回归覆盖真实搜索输入通知、匹配 / 无匹配 / 清空、浏览器窗口实际显隐、字段尺寸和圆角，以及隔离注册表中的部分残留关联、第三方默认检测、重复启用和无备份恢复 Windows。
- 验证：Release x64 构建与全部 5 项 CTest 通过。150% 真实窗口确认地址栏样式、下载目录搜索显示匹配文件且清空恢复目录；启用后的外部目录进入 FastFile 标签，取消后的普通目录、C 盘以及缓存旧动作的 Windows「此电脑」双击均交给 Windows 文件资源管理器，FastFile 不新增 C: 标签。最终保留开关取消状态，Folder / Directory / Drive 均默认 Windows。

## 当前 Shell 视图与导航回归修复（2026-09-30）

- 下级目录复用：AddTab 在同目录已有标签检查后，对规范化路径按目录分隔符边界判断是否为当前目录后代；新增目标属于当前目录下级时改走当前标签导航并记录后退历史。连续子目录、收藏及外部打开入口共用规则；已有目标标签仍优先激活，类似 Program Files / Program Files-Other 的同前缀相邻目录仍可另开标签。回归覆盖多级连续打开、后退、已有子目录标签优先和目录边界。Release x64 与全部 5 项 CTest 通过；使用隔离 APPDATA 真实窗口验证收藏连续打开 Root → Level1 → Level2 始终一个标签，后退回 Level1，Root-Other 可新增标签，再点 Level1 复用已有标签。
- 标签文字由 14 调整为 12 逻辑像素（150% 下 21 → 18 物理像素），采用独立 FontTab token；XML 初始字体与 DPI 重注册保持一致，继续使用微软雅黑 UI 常规字重。标签高度、图标、关闭槽不变，宽度测量和绘制共用字体 7。新增 100% / 150% / 200% 字体度量回归，覆盖 Program Files、此电脑和图片的文字宽高，并沿用标签切换几何稳定性检查。字号回归修复前 12 项断言失败，修复后 Release x64 与全部 5 项 CTest 通过；150% 真实窗口验证 Program Files 完整显示、此电脑比例协调、切换无位移。
- 盘符根与标签唯一性：旧关联命令 `--open "%1"` 遇到 `C:\` 时，末尾反斜杠会让命令行解析结果变成 `C:"`；去掉引号后 `GetFullPathNameW("C:")` 解析到该盘工作目录（本次为 Release）。`NormalizePath` 现在先将裸盘符补成根目录，再解析完整路径，兼容已启用的旧关联；新关联使用 `--open "%1\."` 避免末尾反斜杠转义引号。移除 `allowDuplicate`，所有 AddTab 入口均复用已有目录；普通导航和 Shell 导航完成也优先激活已有标签，比较时统一绝对路径、尾分隔符和大小写。根路径与重复打开回归在修复前六项失败，修复后全部通过；Release x64 和 5 项测试通过。150% 真实窗口验证双击 C 盘进入 `C:\`，再次双击切回同一 C: 标签，点击「+」不产生重复。
- 标签切换位移回归：`ActivateTab` 原先每次调用 `RebuildTabStrip`，清空后重新 `Add` 全部标签，导致全部重播 160 毫秒滑入动画，并在逐项添加时钳制横滚位置。现在已有标签切换只调用 `SetActiveTab`，新增 / 关闭等结构变化仍重建。主窗口测试在切换后立即检查矩形、宽度、滚动位置和动画时间戳；修复前四次失败，修复后通过。Release x64 构建、全部 5 项测试及 150% 真实窗口来回点击验证通过。
- 列头只随 `ViewMode::Details`（原生 `FVM_DETAILS`）显示；`ShellBrowserHost::SetViewMode` 设置 `FWF_NOCOLUMNHEADER | FWF_NOHEADERINALLVIEWS`，普通目录详细信息固定名称、修改日期、类型、大小四列。导航完成后即使路径与当前路径相同，也重新应用模式，避免异步创建的新视图漏掉设置。分组标题仍由 Shell 内容区绘制。
- 详情 0/0 的根因：原生目录浏览提前返回，未填充旧自绘列表的 `m_listingDirs / m_listingFiles`，而详情仍读该缓存。现在独立枚举当前目录；枚举失败显示无法读取，不冒充空目录。单项详情读取该项 Shell 属性，多项显示「已选择 N 项」，此电脑显示实际驱动器数。
- 原生选择变化订阅 `DShellFolderViewEvents / DISPID_SELECTIONCHANGED`；清空选择时先检查 `SVGIO_SELECTION` 项数，规避部分 Shell 提供者对空选择返回失败而遗留旧详情。未选中时恢复当前目录详情和禁用操作按钮。
- 树同步入口为 `NavigateToNow`、`ActivateTab`、`OnShellBrowserNavigation`；收藏、面包屑、快捷入口沿用这些导航链。按 PIDL 祖先展开，并补齐已知文件夹的虚拟 UserFiles PIDL 省略的盘符祖先；布局完成后 `RevealSyncedTreeNode` 滚动嵌套树视口。快捷目标只亮快捷行，其余目录只亮树，导航底色 #E8E8E8。
- 标签先按文字理想宽分配（最大 240 逻辑像素），紧张时优先压缩长标签，全部到下限后横滚；28 逻辑像素关闭槽始终计入，选中不加宽。「+」保持末标签后。文字测量与绘制使用同一 DPI 字体；标题走 Shell 本地化显示名。
- 新增主窗口回归测试，覆盖真实 Shell 选择通知、目录计数、列头模式、嵌套树可见性、快捷单处高亮、命令状态、标签宽度和本地化标题；核心和 Shell 控件测试同步补充。
- 验证：Release x64 构建成功，全部 5 项 CTest 通过。开发机 150% 真实窗口验证 Program Files 详细信息 ↔ 大图标（大图标无表头）、74 个子文件夹、Battle.net 详情绑定、D: 树节点可见、还原窗口完整 Program Files 标签、下载浅灰单处高亮、Pictures 标签显示「图片」；此电脑显示 7 个驱动器，分组保留。保留星标收藏、芯片短名和收藏行高度。

## 当前收藏栏实现（2026-09-30）

- 左侧 `btn_favorite_toggle` 是原生 DuiLib 自绘五角星按钮：16 逻辑像素图形、32×32 热区，垂直居中；默认 #5C5C5C 细线空心，悬停 #1A1A1A + 4 逻辑像素圆角浅底，已收藏路径为 #C7A300 实心。
- 单击复用 `PinFavorite` / `UnpinFavorite`，导航和切换标签复用 `UpdateFavoritesHighlight` 同步状态；「此电脑」保持空心且不接受收藏。
- 星按钮右侧保留 8 逻辑像素间距，芯片可用宽度扣除左右内边距、星热区和间距；芯片保持自然宽度，溢出滚动。无芯片时显示灰色「拖入文件夹到此处」。
- 星与芯片共用 `%APPDATA%\FastFile\favorites.json`，格式为有序路径字符串数组（UTF-8 JSON）；缺少 JSON 时导入旧 UTF-16 `favorites.txt`，保留旧文件。先写临时文件，再替换 JSON。
- 新增 `tests/FavoritesTests.cpp`：JSON 中文 / UNC / 转义 / Unicode 往返、损坏输入拒绝，以及 100% / 150% / 200% 星按钮颜色、空心 / 实心、圆角及尺寸回归。
- 验证：Release x64 构建、全部 4 项 CTest 通过；真实窗口验证收藏 / 取消、标签同步、重启持久化、首项添加及末项取消恢复空栏提示。独立配置验证目录位于 `build/favorites-runtime-profile`。

## 开发日志索引（截至 2026-09-30）

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
| 第十四批 | `29f1d8b`、`32ade3d`、`d650e91` 等 | 自绘 `CTabStripUI`、Explorer 式标题行、窗口按钮与标签交互修复 |
| 第十五批 | `84cdb13`、`a4626cf` 等 | Fluent 顶栏与收藏栏、Mica、面包屑和此电脑详情 |
| 第十六批 | `53a14c4`、`3f2628b`、`9e0f2e0` 等 | 标签溢出滚动、树同步、每目录视图记忆、收藏文案与布局修复 |
| 第十七批 | `d28af3b`、`82015b0` | 文件区键盘导航与命令栏双色图标 |
| 第十八批 | `cf3651d` | Fluent 悬停滚动条（细轨、悬浮加宽、邻近命中） |

## 目标

轻量 Windows 文件管理器：接近 **360 文件** 的速度与布局密度，观感贴近 **Windows 11 资源管理器**。  
技术栈：**DuiLib + 原生 C++**（不要改成 WinUI/Electron）。  
**禁止**：反编译 360 安装包；复制 360 图标、商标、皮肤资源。布局/密度可参考，资源必须用 Windows Shell / 自绘。

## 路径

| 项 | 路径 |
|----|------|
| 工程根 | `C:\Users\JINLONG\文档\Grok\FastFile`（可能 junction 到 Documents） |
| 源码 | `src\`（15 个 `MainWnd*.cpp`，另有 `TabStripUI.*`、`FluentScrollBarUI.*` 和共享头文件） |
| 皮肤 | `skin\main.xml`（POST_BUILD 拷到 exe 旁） |
| 可执行文件 | `build\Release\FastFile.exe` |
| DuiLib | `third_party\duilib\` |
| 版本管理 | Git，远程 `https://github.com/jinlong85/FastFile`（分支 `main`） |
| 历史备份 | 已移出工程：`C:\Users\JINLONG\文档\Grok\_FastFile_attic_20260928\` |

## 源码结构（截至 2026-09-30）

原先约 8000 行的单一 `MainWnd.cpp` 已按职责拆分；目前有 15 个 `MainWnd*.cpp` 编译单元，另有自绘标签栏和滚动条控件。`CMainWnd` 声明仍集中在 `MainWnd.h`。

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
| `MainWnd.Integration.cpp` | 系统文件夹打开关联、外部路径与单实例集成 |
| `MainWnd.Views.cpp` | 视图模式、详细信息、图标/平铺、虚拟化、排序与列宽 |
| `MainWnd.Icons.cpp` | Shell 图标/缩略图提取、图标缓存、缩略图线程 |
| `MainWnd.Preview.cpp` | 预览窗格（图片/文本/视频元数据） |
| `MainWnd.DragDrop.cpp` | OLE 拖放：DropTarget、DragSource、传输 |
| `TabStripUI.cpp` | 自绘标签栏控件及标签命中、绘制和交互 |
| `FluentScrollBarUI.cpp` | 悬停加宽的 Fluent 滚动条控件 |
| `MainWndInternal.h` | 公共 include 前导 + 跨单元共享的 2 个 inline 辅助函数 |

改代码时的两条规则：

1. 新方法写进职责对应的那个单元；只在单个单元内使用的辅助函数，直接放在该单元顶部的匿名 `namespace` 里。
2. 需要跨单元共享的辅助函数放进 `MainWndInternal.h`（写成 `inline`）。新增 `.cpp` 时，必须同时登记到 `CMakeLists.txt` 和 `vs\FastFile.vcxproj`。

## 构建与运行

- 工具链：CMake + VS 2022 Build Tools，**Release x64**
- 改 `main.xml` 后务必重建或确保 `build\Release\skin\` 与源 skin 同步，否则会「加载资源文件失败」或跑旧皮肤
- 发布后建议：结束 `FastFile` 进程 → 清 `%TEMP%\FastFileIconCache` → 再启动 exe
- 图标缓存版本：`_v8.png`（HICON → PNG 真透明）；改导出逻辑时升版本并清缓存
- 安装程序：`powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1`（默认从根目录 `VERSION` 读取版本）
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
版本号由仓库根目录 `VERSION` 提供，CMake 项目元数据和安装包名称均从该文件读取；仅在特殊打包场景才显式传入 `build_installer.ps1 -Version` 覆盖值。

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
版本号不写在 `setup.cs` 里；打包脚本默认从根目录 `VERSION` 读取，也支持显式覆盖，并生成
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
- 当前尚未实现：收藏芯片拖拽排序和 Delete 快捷移除（快速访问区已有独立排序交互）。

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

### 本批记录的待办（部分已由后续批次完成）

- 收藏芯片拖拽排序 / Delete 移除仍未实现，见上文。
- 滚动条后续已在第十八批改为静止 4、悬停 8 逻辑像素；不再是本节当时记录的固定 12 逻辑像素。

## 第十六批：标签条 / 树同步 / 收藏文案

### 标签宽度与滚动（P0-1）

`CTabStripUI::RecalcRects()` 不再 `avail / n` 均分（那是"8 个标签各显示 1 个字"的根因），
改成：`MeasureTabWidth(i)` 用 `GetTextExtentPoint32W` 量标题 → 夹到
`[TabMinW(120) | TabSelMinW(148) 选中, TabMaxW(200)]` → 累加得到 `m_contentW`；
放不下时启用 `m_scrollX`：

- 滚轮 / Shift+滚轮 = 水平滚动（`UIEVENT_SCROLLWHEEL`，步长 `TabMinW/2`）；
- `EnsureTabVisible()` 在 `Add(activate) / Select / SetActiveTab` 时把选中标签滚进视野；
- `ClampScroll()` 负责边界；`"+`" 放在 `min(内容末, 条右边界-plusW)`，不会被系统按钮盖住。
- **必须给绘制加自身矩形裁剪**：DuiLib 只把控件裁到父容器，滚动后标签会画到系统按钮上
  （`DoPaint` 里 `g.SetClip(m_rcItem ∩ rcClip, CombineModeIntersect)`）。

### 标签条宽度

`caption_drag` 从 `minwidth="40"` 改成 `width="40"`：原来它是第二个"可伸缩子控件"，
和 TabStrip 平分了标题行，导致 "+" 右边空出半行。现在只有 TabStrip 可伸缩，铺满到按钮前。

### 选中卡片连体（P0-2）

两个原因：① `SetMetrics()` 把 strip 的最小高设成 26 逻辑（比 36 行矮），`childvalign="vcenter"`
让它居中 → 卡片悬在行中间；现在 `SetMinHeight/SetFixedHeight(TabBarH)`，strip 占满整行。
② 卡片只画到 `rc.bottom - 0.5`，底部留一条灰；现在选中卡片 `card.bottom += 1`，
画到下一行里去，和收藏行无缝。

### DWM 边框

extends frame 之后 DWM 会自己画 1–2 物理像素边框（四个边都出现深色发丝线）。
`ApplyDwmChrome()` 里加 `DWMWA_BORDER_COLOR = DWMWA_COLOR_NONE`（34 / 0xFFFFFFFE）关掉。

### 树同步（P0-3）

- `OpenQuickAccessTab()` 以前 `m_suspendTreeSync = true` 包住 `AddTab()`，从收藏进入目录
  时树完全不动 —— 这是"内容在 E:\软件、树停在 F:\下载"的根因。已删除该暂停。
- `ActivateTab()` 现在也 `SyncTreeToPath()`（标签激活同样要同步）。
- `SyncTreeToPath()` 末尾新增"滚进视野"：`CListUI::EnsureVisible()` 只认顶层项，树节点是嵌套的，
  所以用节点自己的 `GetPos()` 与树矩形求像素差，再 `m_pDirTree->Scroll(0, dy)`。

### 每目录视图模式

`NavigateToNow()` 会用 `LoadFolderViewForPath()` 应用该目录记住的视图模式，
`ActivateTab()` 以前没有 —— 于是 `folder_views.ini` 里 `e:\软件=1`（大图标）却被渲染成
上一个标签的「平铺」。现在两个入口都会应用（`UpdateViewModeButtons()` 后 `RefreshListing()`）。

### 收藏芯片文案

`CleanFavoriteLabel()`：名字同时含中日韩字符和拉丁字母时，只保留开头的中文串
（≥2 字），例如「绝密较量.Jue mi jiao liang」→「绝密较量」、「太平年.Swords into
Plowshares」→「太平年」；纯中文 / 纯英文 / 中文+数字不动。在 `LoadFavorites()`
和 `PinFavorite()` 两处都调用（读入和保存都会清洗）。
芯片宽度由 `RefitFavoritesChips()` 在 `SyncLayoutDependents()`（200ms 定时器）里按行宽
重新均分 —— 启动时 `favorites_bar` 还没有尺寸，早期版本会把所有芯片压到最小宽。

## 当前顶部结构（截至 1.0.9，自上而下）

1. **标题行 = 标题栏**（`titlebar`，36 逻辑像素）：自绘标签栏、“+”和窗口按钮
2. **收藏栏**（`favorites_bar`，36 逻辑像素）
3. **地址栏**（`address_bar`，36 逻辑像素）：导航按钮、面包屑/路径编辑、搜索
4. **命令栏**（`toolbar`，40 逻辑像素）

## 第十七批：视图键盘导航 + 命令栏图标

本批加入文件区键盘导航与命令栏双色图标。此前尚未完成的滚动条悬停加宽，已由第十八批实现。

已验收行为（不要回退）：标签最小宽 120 + 溢出横滚、单标签与收藏行连体且「+」紧贴最后
可见标签（4 逻辑间距、不靠系统按钮）、树与路径同步、每目录视图模式、收藏芯片中文短名
清洗、地址栏点空白进 Edit（带 `#FF0078D4` 焦点框）、「此电脑」不再双高亮、细滚动条
（`ScrollBarW/SidePaneScrollBarW = 4` 逻辑）、磁盘条 6 逻辑圆角 + 20%/10% 橙红阈值。
**本批新增（已截图验收）**：图标/平铺/列表视图的键盘导航；命令栏 10 个图标重画为
资源管理器的实心双色风格。

### 视图键盘导航（本批实现）

- 焦点在文件区（`file_icons` 子树，或 `ReturnFocusToFileView()` 把焦点给了
  `m_pIconTiles` 本身）时，`CMainWnd::HandleMessage` 的 `WM_KEYDOWN` 分支接管
  ←/→/↑/↓、PageUp/PageDown、Home/End、Enter/Space，**早于** DuiLib 把
  Space/Enter 当成按钮点击（`CButtonUI::DoEvent` 会 `Activate()`）。
- 步进量取 `CTileLayoutUI::GetColumns()/GetRows()`（`SetPos` 里算出来的真实网格）。
  图标/平铺是行优先：`index = row*cols + col`；列表视图是列优先：
  `index = col*rows + row`（`SetColumnFirst(true)`）。
- 光标 = **当前获得焦点的 tile**（每次移动都会 `SetFocus`），`m_iconAnchor` 只记 Shift
  连选的起点。这是本批修掉的一个真 bug：早先版本用 anchor 当光标，Shift 连选后
  再按 Ctrl+方向键会跳回锚点。
- `IconEnsureVisible` 用子控件 `GetPos()` 与 `m_pIconTiles->GetPos()` 求交集算出滚动量，
  再 `SetScrollPos`（子控件坐标已是"已滚动"后的窗口坐标，直接比即可）。
- 新增成员：`IsIconViewFocused / IconCursorIndex / IconMoveTo / IconNavigate /
  IconPageMove / IconEnsureVisible / IconActivateCursor`（都在 `MainWnd.Views.cpp`）。

### 命令栏图标重画（本批实现）

`MainWnd.Icons.cpp` 的 `DrawCommandIcon`：描边从 `px/12` 加粗到 `px/9`（≈1.7 逻辑像素，
对齐 Explorer）；圆角 1.6 个网格单位；复制/粘贴的蓝色纸张改用"浅蓝 tint 填充 + 蓝描边"
（tint = `0x26` alpha 的 accent），剪切手柄圆环放大到 2.9 并带 tint，垃圾桶改成上宽下窄
梯形，共享箭头改成实心三角，排序箭头加大，**查看由"显示器"改成资源管理器现在的四条
横线图标**（自上而下逐渐变粗）。图标仍 16 逻辑、热区仍 32×32 逻辑。
改了图形一定要 bump `GetCommandIconBmp` 里的 PNG 文件名版本（现在是 `_v2`），否则
`%LOCALAPPDATA%\FastFile` 里的旧图标缓存会被继续复用。

## 第十八批：滚动条 hover 加宽（Fluent 悬浮滑块，1.0.9）

用户已确认口径（4→8 逻辑 = 150% 下 6→12 物理、悬停带浅灰圆角轨道、鼠标靠近列表
右边缘就展开、树与预览导轨同参数），本批做完。**不要动**：`CTabStripUI::RecalcRects()`
的"按标题实测宽度 + 夹紧 [120|148,200] + 溢出横滚"是上一轮按用户纠正重做的，禁止退回
`avail/n` 均分。

### 实现（`src/FluentScrollBarUI.h/.cpp`）

- `CFluentScrollBarUI : CScrollBarUI`，只重写 `DoPaint`：静止画细的圆角滑块（4 逻辑，
  颜色 = `thumbcolor`），展开时滑块加宽到 8 逻辑、颜色压暗 14，并补一条圆角轨道
  （轨道色 = `bkcolor`，没有轨道的文件区用 `#FFF0F0F0`）。拖拽中（`UISTATE_PUSHED`）
  再压暗一档。沿轴长度、圆角半径都用控件宽度算，短滑块也保证是胶囊而不是圆点。
- **滑块画在控件矩形之外**（overlay 式）：DuiLib 的滑块宽度就是控件宽度
  （`CScrollBarUI::SetPos`：`m_rcThumb.right = rc.left + m_cxyFixed.cx`），所以加宽不能
  从布局拿空间，否则文件区/树/预览会永久少掉几个像素。安全性来自三点，改这块前务必确认：
  1. `CRenderEngine::DrawColor()` 对 `color <= 0x00FFFFFF` 直接 return → 容器不会把
     悬浮出来的那一条盖回去（注意：透明 bkcolor 是 `0x00FFFFFF` **不是 0**，判颜色一定要
     看 alpha，否则"有轨道但什么都没画"）；
  2. `CContainerUI::DoPaint()` 在子控件之后才画 `m_pVerticalScrollBar` → 不会被兄弟控件盖；
  3. 展开时额外 `m_pManager->Invalidate(溢出后的矩形)`，否则 update region 裁剪会把
     悬浮部分切掉（`IntersectRect` 为空的控件会被整个跳过）。
- 方向：右侧/底部的条（文件区、树、横向条）`SetDockFar(true)`，向左/上溢出；预览导轨在
  `preview_pane` 的**左边缘**，向左会出父容器被裁掉，所以 `SetDockFar(false)` 向右溢出。
- 颜色完全沿用原来的 `bkcolor` / `thumbcolor`（`GetBkColor()` / `GetThumbColor()`），
  所以 `SyncPreviewRail()` 里"不可滚动就把 thumbcolor 设 0"的逻辑继续有效（0 = 不画滑块）。

### 挂接（3 处）

- **创建**：vendored DuiLib 里 `CContainerUI::EnableScrollBar()` 与 `CDialogBuilder`
  都是写死的 `new CScrollBarUI`，没有 setter。加了一个全局工厂
  （`UIScrollBar.h/.cpp` 的 `SetScrollBarUICreator` / `CreateScrollBarUIInstance`，
  默认仍是 `new CScrollBarUI`），FastFile 在 `CMainWnd` **构造函数**里注册——必须在
  构造函数里，`InitWindow()` 已经晚于 `OnCreate` 的皮肤加载（`preview_rail` 那时已经建好）。
- **参数**：`ApplyFluentScrollBar(sb, dockFar)` 在 `StyleVerticalScrollBar` /
  `StyleHorizontalScrollBar` / `StyleSidePaneScrollBars` / `StylePreviewRail` 四处调用，
  设 `SetRailMetrics(DpiScale(ScrollBarW), DpiScale(ScrollBarHoverW))` + dock 方向。
- **悬停**：DuiLib 自带的 `UISTATE_HOT` 只在指针压在那 6 物理像素上才算，资源管理器的
  行为是"靠近边缘就展开"。所以 `CMainWnd::HandleMessage` 的 `WM_MOUSEMOVE` 调
  `UpdateFluentScrollBarHover()`（把 file_list / file_icons / dir_tree 的竖横条 + 预览
  导轨一起做命中，最近的赢，其余的收起），`WM_MOUSELEAVE` 全部收起（**不 return**，
  免得吞掉 DuiLib 自己的 leave 清理）。命中范围 = 条本身，或它贴着的那一侧 6 逻辑像素内。

### 还没做（可选，未开工）

- 展开的过渡动画（现在是瞬时）。要做得多一个定时器和逐帧重绘；口径没提，先不做。
- 横向滚动条（详情视图列超宽时出现）走的是同一套代码，但没有专门截图验证过。

## 回归截图脚本要点（踩过坑，越往后越新）

`Start-Process` 起来的新进程会被单实例转发吃掉并立即退出，
不要按新进程 PID 找窗口；直接枚举已运行实例的 `FastFile_MainWnd`（宽度 > 800）取 HWND。
改宽前必须先 `ShowWindow(SW_RESTORE)` 并确认 `IsZoomed=false`（在最大化态 `SetWindowPos`
会截到命令行）；抓屏前确认屏幕未锁。四张必测：还原 1 标签 / 还原 8 标签 / 最大化 / 还原后改宽。

1. **DPI**：屏幕是 150% 缩放，发鼠标事件的 PowerShell 进程如果是 DPI-unaware，
   `ClientToScreen`/`SetCursorPos` 会算错（点到的位置和预期差一截）——脚本开头要
   `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`。
2. **焦点**：启动时 DuiLib 的焦点在标签栏的「+」（`btn_tab_add`），此时方向键不会落在
   文件区；要点击某个 tile 把焦点送进 `file_icons`（`Alt+D` 再退出也行，但见第 3 条）。
3. **输入法**：中文 IME 会把 `\` 打成「、」，脚本没法用地址栏输入 Windows 路径
   （`D:\Program Files` → `D:、ProgramFiles`）。用鼠标导航（双击盘符卡 → 双击列表行）
   或改用 `/` 分隔符。
4. **修饰键**：发 Shift/Ctrl 组合要用 `keybd_event` 并带真实扫描码，否则 `GetKeyState`
   看不到修饰键状态（`GetKeyState` 读的是消息队列状态，`PostMessage` 不会更新它）。
5. **句柄**：`Process.MainWindowHandle` 会失效（进程还有隐藏的顶层窗口），失效后
   `DwmGetWindowAttribute` 返回全 0 矩形、截图构造 Bitmap 直接抛异常。按窗口类名
   `FastFile_MainWnd` 用 `FindWindowW` 取 HWND，并在截图前重试。
6. **条的位置别靠眼估**：读截图里某一行的像素颜色（`Bitmap.GetPixel`）来定位滚动条矩形，
   左面板背景 `#F3F3F3` 和轨道 `#F7F7F7` 只差 4，肉眼和阈值都容易混。


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
- 会话/设置的持久化、每文件夹视图记忆；安装程序见 `installer\`（源码已到 1.0.9）
- 图标/平铺/列表视图键盘导航（方向键 + Home/End + PageUp/PageDown + Shift/Ctrl 连选 +
  Enter/Space 打开，自动滚动到可见）
- 滚动条 hover 加宽：静止是 4 逻辑细轨，鼠标靠近列表/树/预览导轨就展开到 8 逻辑的
  圆角滑块 + 浅灰圆角轨道（悬浮在内容之上，布局不动）
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
| ↑/↓/←/→ | 文件区光标移动（图标/平铺按列步进；列表视图上下走行、左右跳列） |
| Home / End、PageUp / PageDown | 跳到首/尾、翻一屏（图标 / 平铺 / 列表 / 详细信息都支持） |
| Shift/ Ctrl + 方向键 | 连选一段 / 在现有选择上增减（图标与详细信息视图） |
| Enter / Space | 打开光标所在项目（文件夹进入、文件用默认程序打开） |

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
    旧版 FastFile 曾仅用内部剪贴板；当前已发布系统 CF_HDROP，与 Shell 粘贴互通。
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

- `session.ini`、`folder_views.ini`、`favorites.json`、`left_nav.ini` 等（旧 `favorites.txt` 仅兼容导入）

## 历史测试与当时待办（2026-09-30）

以下保留当时记录，不是当前测试清单；最新七项测试、设置功能和未完成人工验收见文首「接手先读」。重新安排待办前先检查后续修复和当前源码。

- **2026-09-30 当前开发状态（未发布）**：普通目录文件区已改由 `src/ShellBrowserHost.*` 承载 Windows `ExplorerBrowser`，FastFile 继续负责主窗口、标签、导航栏、左侧目录树、预览与命令栏。普通文件区由系统提供选择、原生上下文菜单和重命名交互；FastFile 命令栏 / F2 调用 `IFolderView2::DoRename()`。普通名称筛选使用 Shell 文件夹筛选；递归搜索保留自绘结果视图。`README.md` 已按此更新。该改造尚未发布，不应写入 1.0.9 的已发布变更。
- **本次验证**：Release x64 主程序构建通过；CTest 3/3 通过，包括新加的 `FastFileShellBrowserTests`，它在隐藏测试窗口中创建真实 Shell 浏览器并验证导航、视图模式、排序和刷新。当前会话没有可供 Computer Use 选择的 Windows 应用窗口，因此 FastFile 主窗口内的鼠标二次单击重命名、F2、右键菜单和递归搜索切换尚未完成交互验收；交付前应在应用窗口做这组手工回归。构建输出另有 DuiLib 头文件代码页警告，未阻止构建。
- 已建立 CTest：`FastFileCoreTests` 覆盖路径、文件名规则和文件大小格式；`FastFileShellBrowserTests` 验证 Shell 浏览器宿主；`FastFileVersionSource` 校验 CMake、根目录 `VERSION` 与安装脚本的版本来源一致。
- 2026-09-30 回归记录：Release x64 构建和两项 CTest 通过；隔离窗口验证了复制、同盘移动、F2 重命名与 Ctrl+Z 撤销、新建文件夹与撤销 / 取消，以及 Ctrl+T / Ctrl+Tab / Ctrl+W 标签流程。F2 原先调用 Shell rename 动词会失败，现改用应用自有输入框 + `RenameItem`，并已手工验证成功与撤销。
- 2026-09-30 右键重命名修复：部分 Shell 项目的 `IContextMenu` 没有提供 Rename 动词。单选文件 / 文件夹时，在 Shell 原生菜单后追加 FastFile 自己的「重命名」命令，复用 F2 的输入框与撤销逻辑。隔离窗口已确认菜单显示、右键入口重命名成功且 Ctrl+Z 可撤销；手工回归步骤：单选项目 → 右键确认菜单底部「重命名」→ 输入新名称 → 确认 → Ctrl+Z 确认恢复原名。
- 当前自动回归仍未覆盖复制 / 移动任务取消、失败提示以及 Fluent 滚动条悬停状态；后续应补充这些场景。代码更新需按根目录 `AGENTS.md` 的要求，为变更补充测试并在交付前运行相关测试和验证。
- 1.0.9 已覆盖主要浏览、文件操作、Shell 集成、预览和外观功能；稳定性迭代已开始。当前未发布改造先完成主窗口交互验收，再扩充文件操作取消 / 失败回归及滚动条视觉验收。
- 当前仍可规划的产品功能：压缩包浏览、网络位置、批量重命名、内容搜索（限制见 README）。
- 尚未完成的交互待办：收藏栏芯片拖拽排序 / Delete 移除；动画滚动条过渡为可选项。
- 当前验证缺口：横向滚动条沿用同一控件实现，但尚无专门截图验收记录；应纳入下一轮回归。
- 发布能力后续可考虑设置面板、自动更新和代码签名。是否做这些功能取决于产品发布目标；当前安装器源码已到 1.0.9。

## 验收口径（这批之后形成习惯）

- 代码或功能更新必须新增 / 更新相应测试，并运行所有受影响测试；交付前通过 Release x64 构建及适用的运行验证。若验证失败或无法执行，需说明原因和未验证范围，不能报告为全部完成（详见根目录 `AGENTS.md`）。
- 每个界面改动都用 `PrintWindow` 截图 + 1:1 裁剪对比给用户看，而不是只说“改好了”
- 涉及原生 Shell 行为的（右键菜单等），先把 Shell 给的数据转储出来对比，再决定怎么改
- 崩溃一律先看 `%LOCALAPPDATA%\FastFile\last_crash.txt`（现在是异常码 + 模块内 RVA + 调用栈），
  再配合 `build\Release\FastFile.map` 反查函数

## 给新 AI 的工作方式

1. 按任务需要读取 `UiTokens.h`、`skin/main.xml` 和对应编译单元；详细历史经验见本文件。
2. 改动走 Git：小步提交（`git commit` + `git push`），不要再往工程根堆 `bak_*`。
3. 每次代码 / 功能更新都补充或更新测试；交付前运行受影响测试、校验 XML、做 Release x64 构建，并对界面 / Shell 行为做适用的运行验收。失败时修复并重验；无法执行的项如实报告。
4. 用户界面中文；回复用户可用中文。

## 历史备份（已移出工程）

拆分前的 169 个临时产物（10 个 `bak_*` 快照目录、源码 `.bak_*`、一次性补丁脚本、`_inspect` 转储）集中归档在
`C:\Users\JINLONG\文档\Grok\_FastFile_attic_20260928\`，按来源分子目录存放；确认 Git 历史够用后可整体删除。

---
本文档由 FastFile 开发助手生成，便于换 AI 继续开发时粘贴或直接打开。
