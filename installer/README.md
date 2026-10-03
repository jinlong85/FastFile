# FastFile 安装程序

一条命令生成安装包（**不需要任何第三方安装器工具**，只用 Windows 自带的
.NET Framework 编译器 `csc.exe`）：

```powershell
powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1 -BuildDirectory build-ui\Release
```

产物：`dist\FastFile-Setup-<版本>.exe`，当前源码候选为 **1.0.12**（验证状态见根目录 HANDOFF.md）。默认读取根目录 `VERSION`；临时指定版本可用 `-Version 1.2.3`。脚本支持 Windows PowerShell 5 和 PowerShell 7，以及包含空格的路径。

`-BuildDirectory` 指定已有构建产物目录；不传时默认读取 `build\Release`（`-Configuration` 可更改配置）。当前候选构建位于 `build-ui\Release`，打包前应先完成该目录的 Release x64 构建。

可只读验证安装包内嵌版本、文件清单与源文件哈希，不执行安装：

```powershell
powershell -ExecutionPolicy Bypass -File tests\CheckInstallerPayload.ps1 -InstallerPath dist\FastFile-Setup-1.0.12.exe -BuildDirectory build-ui\Release
```

## 安装程序做了什么

双击安装包后（**当前用户安装，不需要管理员**）：

1. 把内嵌的 `FastFile.exe` 与 `skin\` 解压到 `%LOCALAPPDATA%\Programs\FastFile`；
2. 在开始菜单创建 `FastFile` 快捷方式；
3. 写入 `HKCU\...\Uninstall\FastFile`，于是「设置 → 应用」里能看到它并卸载；
4. 顺手把 `uninstall.exe` 放到安装目录，问一句是否立即启动。

命令行参数：

| 参数 | 作用 |
|---|---|
| `--quiet` | 静默安装（不弹窗、不自动启动） |
| `--dir <路径>` | 安装到指定目录（默认 `%LOCALAPPDATA%\Programs\FastFile`） |
| `--uninstall` | 卸载（`uninstall.exe` 默认使用） |
| `--cleanup` | 内部使用：从 `%TEMP%` 重新启动以删除安装目录 |

卸载**不会**删除 `%APPDATA%\FastFile` 里的个人设置（会话、收藏等）。

## 结构

| 文件 | 说明 |
|---|---|
| `setup.cs` | 安装/卸载程序本体（C# 5，`/target:winexe`） |
| `build_installer.ps1` | 打包脚本：收集 `build\<配置>\` 产物 → 生成资源清单 → 调用 `csc.exe` |
| `install.cmd` / `install.ps1` / `uninstall.ps1` | 早期基于 ZIP + IExpress 的方案，保留供参考（当前未使用） |

> 打包时会自动跳过 `*.bak*`、`*.original*`、`*.tmp` 之类的残留文件。
