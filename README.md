# FastFile — 轻量文件管理器（P0 脚手架）

基于 **DuiLib + C++ / Win32** 的轻量文件管理器起步工程。本仓库为全新脚手架，**未安装、未引用、未逆向任何 360 软件或二进制**；仅嵌入开源 DuiLib 源码树中的库本体。

## 技术选型

| 项 | 选择 | 说明 |
|----|------|------|
| UI | DuiLib | XML 皮肤 + DirectUI，体积小、Win32 原生 |
| 语言 | C++17 | MSVC |
| 构建 | CMake（优先） / VS 解决方案（回退） | 生成 Win32 GUI exe |
| 第三方 | `third_party/duilib` | 从 [duilib/duilib](https://github.com/duilib/duilib) master ZIP 嵌入；**仅含 DuiLib 库与许可证，不含各 Demo（含 360SafeDemo）** |

## 目录结构

```
FastFile/
  CMakeLists.txt          # 主构建（静态链接 DuiLib）
  FastFile.sln            # VS 回退解决方案
  README.md
  skin/main.xml           # 主窗口皮肤：标题 / 地址栏 / 列表占位 / 状态栏
  src/main.cpp            # WinMain 入口
  src/MainWnd.h|.cpp      # 主窗口 + P0 TODO 桩
  third_party/duilib/     # 已嵌入的 DuiLib 源码
  vs/                     # 手写 vcxproj（无 CMake 时可用）
```

## P0 范围（已完成）

1. CMake + MSVC Win32 GUI 工程骨架  
2. 嵌入 DuiLib 源码（可离线编译库部分）  
3. 最小主窗口皮肤：标题、地址栏占位、列表占位、状态文字  
4. 代码内 TODO/桩：虚拟化目录列举、后台复制队列、`SetForegroundWindow` 激活  
5. 本中文 README  

**未做（留给 P1+）**：真实目录枚举、虚拟列表、复制队列实现、安装包等。

## 如何打开 / 构建

### 当前本机工具链状态（脚手架创建时探测）

本 MSI 机器创建脚手架时：**未检测到** Visual Studio / Build Tools / MSVC / CMake / git（仅有 winget）。  
因此此处无法现场完成一次真实编译；请先安装工具链后再构建。

推荐用 winget 安装（需你本机确认执行）：

```powershell
winget install --id Kitware.CMake -e
winget install --id Git.Git -e
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

或安装 **Visual Studio 2022 Community**，勾选「使用 C++ 的桌面开发」。

### 方式 A：CMake + VS 生成器（推荐）

在「x64 Native Tools Command Prompt for VS 2022」或已加载 MSVC 环境的 PowerShell 中：

```powershell
cd "$env:USERPROFILE\文档\Grok\FastFile"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成的 exe 与 `skin\` 会落在 `build\Release\`（或对应配置目录）旁（POST_BUILD 会复制皮肤）。

若只有 VS 2019：把生成器改成 `"Visual Studio 16 2019"`。

### 方式 B：直接打开解决方案

1. 双击打开 `FastFile.sln`  
2. 配置选 **Release | x64**（或 Win32）  
3. 生成解决方案  
4. 运行前确认输出目录下有 `skin\main.xml`（项目已设置复制）

### 方式 C：无 CMake 时仅用 vs/ 工程

`vs\FastFile.vcxproj` 引用 `third_party\duilib\DuiLib\DuiLib_Static.vcxproj`，用 Visual Studio 打开 `FastFile.sln` 即可。

## 运行说明

- 工作目录需能找到 `skin\main.xml`（相对 exe 的 `skin\`）。  
- 关闭按钮控件名为 DuiLib 约定的 `closebtn`。  
- P0 列表行为 XML 占位行，点击「转到 / 刷新」只会更新状态栏 TODO 提示。

## 许可证

- 本脚手架代码：可按项目后续决定（当前为内部草稿）。  
- DuiLib：见 `third_party/duilib/LICENSE` 与 `duilib license.txt`（BSD 风格开源许可）。

## 明确不做

- 不安装 360 软件  
- 不逆向 360 或其它闭源文件管理器二进制  
- 不把 upstream 的 `360SafeDemo` 打进本仓库  