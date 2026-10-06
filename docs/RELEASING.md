# 自动发行

GitHub Actions 的 [Release 流程](https://github.com/jinlong85/FastFile/actions/workflows/release.yml) 在 `main` 的 VERSION 更新后启动。修改发布流程或准备／测试脚本也会触发，首次加入流程时发行当前版本；可在 Actions 手动运行。

1. 更新根目录 VERSION，使用 `x.y.z`；在 CHANGELOG.md 添加对应 `### x.y.z` 记录。
2. 按 HANDOFF.md 完成适用的本地界面、文件操作、Shell 和安装验证，再提交并推送 main。
3. Windows Server 2025 runner 使用 MSVC 配置 Release x64（当前托管镜像为 Visual Studio 2026），构建并执行全部 CTest；任一步失败都不会发行。
4. 使用静态 C++ 运行库构建，检查界面与代理不依赖外部 MSVC DLL；生成安装包，核对嵌入版本、文件哈希及安装器进程归属，再生成 SHA256SUMS.txt 和中文发行说明。
5. 创建指向该次构建提交的 `v<版本>` 标签和预发布 Release，上传安装包与校验文件。测试成功不能代替第三方应用菜单、安装／卸载及现场视觉验收，默认保留预发布状态。

发布前核对 main 仍为本次构建提交；已被后续提交替代的运行拒绝发行。现有 Release 不覆盖，不重传同版本资产。已有标签但没有 Release 时拒绝自动发行，须先核实标签的提交。更改已发行代码应递增 VERSION。

失败时查看 Actions 日志；成功打包的产物另外保存在 Actions artifact。Windows 托管 runner 的桌面环境若不支持某项 Shell 测试，流程会失败，不能据此声称全部通过或绕过验证。

本地验证准备脚本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CheckReleasePreparation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/PrepareRelease.ps1
```

第二条命令要求 dist 下已存在与 VERSION 相同的安装包。准备脚本不执行安装器。

托管 runner 使用 RUNNER_TEMP 下的长路径作为 TEMP／TMP，避免 8.3 临时路径与 Shell 返回的规范路径不一致；统一测试夹具的扩展名显示，并检查 Explorer 桌面和文件窗口 Shell 初始化完成。环境准备只修改一次性 runner，不修改用户电脑。运行库检查优先使用 CMake 实际选择的编译器旁的 dumpbin，避免新 Visual Studio 的安装查询信息不兼容。编译日志和 CTest 日志无论成功失败均上传为 Verification-Logs artifact。
