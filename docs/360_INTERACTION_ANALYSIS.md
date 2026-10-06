# 文件管理器交互分析与 FastFile 代理设计

检查日期：2026-10-04。以下观察来自用户电脑已有组件，属于只读进程、注册表、文件元数据和 PE 导入表检查；没有复制第三方代码、图标、商标或皮肤。

## 本机 360 组件的实测证据

| 观察 | 能支持的结论 | 不能支持的结论 |
| --- | --- | --- |
| `360FileService` 服务自动启动，LocalSystem、Session 0，路径 `C:\Windows\SysWOW64\360FileService.exe`，文件版本 1.0.0.1050 | 有独立的后台服务组件 | 服务单独负责全部文件夹请求拦截 |
| 本机目录包含 `360FileBrowser.exe`、`360FileBrowser64.exe`、`360FileService.exe`，检查时没有浏览器界面进程 | 服务与界面具有独立进程生命周期 | 关闭按钮在所有版本中都结束界面进程 |
| 服务导入 `CreateNamedPipeW`、`ConnectNamedPipe`、`DisconnectNamedPipe`、`CreateProcessAsUserW` 和服务控制接口 | 具备管道通信及启动用户进程的能力；符合后台服务配合用户界面的架构 | 具体管道名、消息格式、调用时序及真实运行分支 |
| 界面导入 `CreateFileW`、`SetNamedPipeHandleState`、窗口消息、ShellExecuteEx、CoCreateInstance | 具备管道客户端、窗口通信和 Shell 交互能力 | 所有请求都走管道；必然使用特定 COM 接口或全局钩子 |
| 当时 Explorer 中未发现带 360 名称的 DLL，未发现 explorer.exe 的 IFEO 重定向项 | 本次检查没有发现这两种接管方式的直接证据 | 能排除动态加载、无品牌名称模块或其他接管机制 |

本机目录名为 `D:\Program Files\去360模块版1.0.0.1080`，属于修改版分发，不能把本机行为当作官方最新版实现。没有获取官方源码，也没有证据确定其 Shell 请求拦截算法。可靠结论是“后台组件与界面分离”；关于管道负责唤醒和请求转发属于结合导入表作出的架构推断。

## Windows 对实现的约束

Windows 服务在 Session 0 中运行，不能直接与当前用户界面交互。微软建议需要交互的服务通过 IPC 协调用户会话中的独立进程，而不是用交互式 LocalSystem 服务访问桌面：[Interactive Services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services)。

FastFile 的需求全部发生在当前用户桌面，没有需要系统服务权限的工作。因此采用当前用户代理进程，避免引入服务安装、管理员权限、Session 0 到用户会话的桥接以及多用户服务授权。它仍需后台进程运行；完全没有后台接收者时，只能接收已注册的标准 Shell 关联请求，不能持续发现应用直接启动的 Explorer 窗口。

## FastFile 1.0.14 的交互链路

1. 标准文件夹、磁盘和此电脑入口通过现有用户级 Shell 关联直接启动或激活 `FastFile.exe`。
2. 独立 `FastFileAgent.exe` 随用户登录启动；默认接管启用时，启动界面也会检查代理是否已启动。旧 `--background` 命令兼容为启动代理后退出，不创建隐藏界面。
3. 代理不初始化 DuiLib、不创建交互窗口。在 STA 中枚举 Shell 窗口，并在启动时记录已有 Explorer 窗口。基线窗口不会被转交；可读取的新窗口目标及选择需稳定至少一秒。IShellWindows::Item 明确返回 S_FALSE 的空记录可以跳过，其他读取失败保持保守处理；返回语义见 [IShellWindows::Item](https://learn.microsoft.com/en-us/windows/win32/api/exdisp/nf-exdisp-ishellwindows-item)。
4. 代理确认接收窗口的类名及可执行文件路径属于同一安装目录。没有界面时使用明确的可执行文件路径与 `--new-window --shell-folder` 启动界面，避免另一安装目录实例截走请求。冷启动目录通过命令行仅投递一次；已有界面才接收 Open 消息，避免关闭标签复用时生成重复标签。
5. 请求与确认通过有长度上限的 UTF-16 `WM_COPYDATA` 消息传递；传递数据中没有跨进程指针，接收时复制并验证终止符、大小和项目数量。该消息的复制要求见 [WM_COPYDATA](https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-copydata)。接收请求与完成加载是两个不同结果。
6. 完成确认要求：没有未完成的外部路径解析、实际导航成功且匹配目标、主窗口可见且未最小化、当前原生文件列表有非零尺寸、所选文件确实存在于当前视图并已选中。
7. 确认后重新读取原 Explorer 的进程、目录、选择和忙碌状态，并用 UI Automation 检查多标签。只有安全检查全部通过才投递 `WM_CLOSE`，不终止 explorer.exe。超时二十秒、无法读取、多标签、正在交互或源目录改变等情形保留原窗口。
8. 关闭 FastFile 界面走正常退出逻辑；代理保留接收能力。停用默认接管会恢复拥有的关联、移除拥有的登录项，代理发现停用后退出。后台代理采用用户会话及配置目录作用域的互斥量防止重复启动。

安装包包含 `FastFile.exe`、`FastFileAgent.exe` 和皮肤。升级／卸载仅结束目标安装目录中的这两个进程。诊断日志位于配置目录下 `explorer-agent.log`，记录发现、启动、请求接收、完成确认、保留原窗口和退出；日志限制约 2 MiB。

## 验证边界

回归覆盖协议拒绝、导航未完成／不可见视图不确认、真实文件选择、界面关闭退出、代理无交互窗口、单实例、停用退出和真实 Explorer 跨进程转交。最终 CMake 和独立 Visual Studio Release x64 构建均通过，完整 CTest 14/14 通过（235.53 秒），安装包三个文件的版本／哈希及安装器进程归属回归通过。详细日志与交付范围见 [HANDOFF.md](../HANDOFF.md)。

此架构仍可能看到 Explorer 短暂出现；它检测已经创建的窗口，不承诺零闪现。夸克首次文件区空白的先前记录显示导航很快完成而列表尺寸为零，新增回归稳定复现了外层 ExplorerBrowserControl 尺寸正常、内层 SHELLDLL_DefView 和 SysListView32 为零的状态：外层尺寸未变时 SetRect 不重新布置新内层视图。代码在该状态下请求容器执行正常 WM_SIZE 布局，并保持严格完成确认。受控回归已经覆盖这一故障，但尚未复测云盘本体，不能据此宣称已修复夸克的全部空白问题。云盘本体菜单、登录后启动、安装／卸载仍需在候选实际安装后验收。
