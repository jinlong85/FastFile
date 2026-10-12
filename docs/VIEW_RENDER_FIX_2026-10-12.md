# 视图图标修复与缩略图测量

## 第二轮：首次打开时私有列表存在但槽位为空（最新候选）

用户在已安装的首轮候选（程序SHA256 2BFEFDF9...）上仍看到文档目录多数图标缺失。日志显示约330ms已完成31项枚举且视图尺寸正常；不支持把该次问题归因于目录枚举缓慢。

新增真实Shell绘制回归：提供4096个有效但未填充的私有图像槽位，检查绘出的图标区域像素。原实现返回有效索引9但图标有效像素为0，测试失败（build-ui/empty-slot-before-test.log）。为避免把未填充槽位的黑色背景误当成图标，像素断言排除纯黑、纯白和无效像素；正常图标检查继续保留。

修改ResolveItemIcon：不再从DefView私有列表或LVN_GETDISPINFO索引选取自绘图标，直接按实际Shell项目/PIDL取得系统小图标。私有列表的存在与索引有效性不再决定图标就绪。保留既有文字、行高、标签和星标绘制，不加入自动F5/目录Refresh。

Release x64全目标构建成功；QuickAccess、ViewSwitch、AsyncThumb、UiPolish、ShellBrowser五套相关回归全部通过（55.87秒，build-ui/empty-slot-tests.log），含列表和详细信息的空槽位像素回归。首轮记录中的全量综合/集成超时仍未解决，不宣称全量通过；未再运行真实接管/安装/卸载。

重新生成dist/FastFile-Setup-1.0.25.exe，三文件内嵌校验通过。安装包SHA256 B0E27B7407DB03574409F80DDA8313684286E50DD37CA4996C869BF9876164A3；主程序SHA256 3B1F2D0EAFA89C079CC475B64AF3B3C80DD1C3F600F449BCDC74C5DBA80AF31D。首轮修复包备份为FastFile-Setup-1.0.25.before-empty-slot-fix-20261012.exe。此次没有替换已安装版；截图中的文档目录需要新安装版最终复测，缩略图性能不视为已解决。

下方为首轮记录，哈希/验证范围不代表本轮最新产物。

范围：用户反馈切换视图后文件夹图标需要F5才出现、缩略图比Explorer滞后。保留现有Shell浏览架构，本轮不改系统接管/卸载，不改已安装程序。

## 已复现缺陷

ShellBrowserHost::ResolveItemIcon在DefView私有小图像列表尚未可用时，把ListView返回的私有iImage用于系统小图像列表。索引即使在系统列表范围内也不能证明对应同一图标，因此旧测试的img>=0断言不足。

新增回归：使用真实临时目录的Shell项目，通过测试子类模拟“私有列表未就绪、ListView返回槽0”，将解析结果与该项目通过SHGetFileInfo取得的系统索引和句柄比较。修复前该断言失败（build-ui/view-icons-before-fix.log），修复后专项通过。这确认该缺陷，但不代表用户目录的所有F5问题均已覆盖。

修复：私有列表存在时才使用其iImage或LVN_GETDISPINFO索引；没有合法私有列表时，按实际Shell项目/PIDL解析系统图标。LVM_SETIMAGELIST传入空句柄时同步清空缓存的私有句柄，避免继续使用已移除的列表。保留已有临时占位列表识别规则。

## 缩略图测量

保留原STA后台线程和LRU策略，记录请求排队、实际提取、结果到UI接收的最大耗时。计数在UI接收时更新，未增加同步提取或UI等待；专项输出queue/extract/delivery三个值。值是各阶段独立最大值，不一定来自同一个请求，也不是完整首屏耗时。

首次修复后夹具测量：16个提取结果，最大排队62ms、提取16ms、UI接收16ms。夹具不能代表视频、大目录、网络或冷系统缓存，更没有与原生Explorer做同目录受控对比。缩略图性能结论仍待用户提供实际程序与目录、媒体类型后复测，不声称提速完成。

## 构建与验证记录

- 改动前已有ViewSwitch/AsyncThumb专项均通过，暴露旧断言的覆盖不足。
- 新增回归在旧实现上失败，随后修复通过ViewSwitch和AsyncThumb。
- ShellBrowser专项发现临时占位列表兼容问题，撤回该部分改动后重新构建全部Release x64目标成功。
- 完整CTest（build-ui/view-icons-regression.log）：14个启用套件中12个通过，MainWnd综合套件180秒超时（超时前有5个撤销/重命名断言失败）、Integration套件90秒超时；两个真实Explorer接管套件禁用。不能报告全量通过。
- 用HEAD版本ShellBrowserHost.cpp替换渲染实现进行对照构建（候选源码已保留并恢复）：`--shortcuts-only`同样5个撤销/重命名断言失败，Integration同样90秒超时。日志build-ui/view-icons-baseline-shortcuts.log、view-icons-baseline-integration.log。这确认该组断言与集成超时在旧渲染实现也存在，不代表全部其他问题已经定位。
- 对照完成后重新编译候选源码，最终ViewSwitch/AsyncThumb/ShellBrowser三个相关专项全部通过（41.67秒，build-ui/view-icons-final-tests.log）；Release全部构建成功（build-ui/view-icons-final-build.log），git diff --check通过。最终夹具最大排队/提取/UI接收47/16/32ms，不作为真实目录性能承诺。未执行真实接管、安装/卸载。
- 最终候选FastFile.exe SHA256：2BFEFDF9B5F3BFFEF6BF46063DCDF88D33E93538F96E1FF0EA53568B894E131E。
- 当前源码候选仍为1.0.25。按用户要求重新生成dist/FastFile-Setup-1.0.25.exe，内嵌主程序、代理、皮肤共3文件与候选构建哈希全部核对通过；安装包SHA256为1CFF880EC36C708FD52CA9B9C8D76E042EBF9ABFCD12E3742A6DB6B191EA6DEA。原包保留为FastFile-Setup-1.0.25.before-view-fix-20261012.exe。未执行安装、未发布或替换已安装版；全量回归限制仍然适用。候选程序与skin目录位于cmake-build-release-static-x64/Release。

构建使用VS2022自带CMake3.31，避免PATH中的CMake4.4与既有生成目录3.31模块混用错误；DuiLib原有编码/typedef警告仍存在。检查结果和最终验收范围以本轮日志为准，不使用HANDOFF历史“通过”代替当前验证。
