# FastFile

**A tabbed Windows file manager with native file operations and context menus.**

[Download releases](https://github.com/jinlong85/FastFile/releases) · [Report an issue](https://github.com/jinlong85/FastFile/issues/new/choose) · [简体中文](README.md)

FastFile brings multiple folders into one window with tabs, breadcrumbs, bookmarks, a folder tree, search, and a preview pane. Regular folders use the native Windows Shell view, including system context menus, file operation progress, and conflict dialogs.

## Features

- Tabbed browsing, optional reuse of tabs for the same folder, and session restoration.
- Breadcrumb navigation, back/forward history, bookmarks, and a folder tree.
- Copy, cut, paste, drag and drop, rename, delete, and undo/redo for supported operations.
- Six view modes, per-folder view memory, name filtering, and recursive folder search.
- Preview and details panes, adjustable layout density, fonts, and tab sizes.
- Folder association detection and repair, with optional forwarding of newly opened Explorer folder windows.

## Install

Releases target **Windows x64** and default to prerelease status. Check the release notes for its version and validation scope. The application interface is in Chinese; this English README does not imply an English UI.

1. Download `FastFile-Setup-<version>.exe` from the newest entry on the [release page](https://github.com/jinlong85/FastFile/releases). The source archives are not installers.
2. Close the previous FastFile instance before updating, then run the installer. Installation is per user and does not require administrator privileges.
3. Launch FastFile from the Start menu. Use the gear button to open settings.

Installation directory: `%LOCALAPPDATA%\Programs\FastFile`. Uninstall through Windows Settings → Apps. Personal settings in `%APPDATA%\FastFile` are retained.

Installer SHA-256 is supplied in the same release's `SHA256SUMS.txt` and release notes. Match it to the installer version.

## Folder integration

Integration is disabled by default. Open Settings → System integration (`设置 → 系统集成`), select the desired folder and drive associations, and click `修复并应用接管` to apply or repair them. Click `检测接管状态` to inspect the effective handlers and executable paths.

For applications that launch Explorer directly, optionally enable `自动转交新打开的资源管理器文件夹`. Since 1.0.14, **FastFileAgent handles requests in the background**. Closing the main window exits the UI process; the agent launches it again when needed. A new Explorer window is closed only after FastFile confirms the destination folder and selected files; Explorer may briefly appear.

Existing windows, other file managers, multiple tabs, virtual folders, busy windows, selections exceeding 256 items, and windows that cannot be verified are retained. This does not guarantee handling every application or system file picker. Use `恢复 Windows 打开方式`, then `保存`, to restore the previous associations.

## Status and limitations

- Automatic publication requires a Release x64 build, all regression tests, installer payload verification and process ownership checks. Failed checks block publication; see the release notes and handoff records for validation scope.
- The IDM application menu and installation/uninstallation have not yet been manually validated. This version remains a prerelease; see [HANDOFF.md](HANDOFF.md) for the verification record in Chinese and [release documentation](docs/RELEASING.md) for automation rules.
- Archive browsing, network locations, batch renaming, and file content search are not supported. Recursive search results are limited to 4,000 items.
- Permanent deletion cannot be undone; some overwrite or merge operations are excluded from undo history.

## Feedback and development

[Report bugs or request features](https://github.com/jinlong85/FastFile/issues/new/choose). Include the FastFile and Windows versions, reproduction steps, and expected versus actual behavior. For folder integration issues, include the calling application and detection results. Remove personal paths and sensitive data from attachments.

Built with **C++17, Win32, DuiLib, and Windows Shell**. Build and test instructions, shortcuts, and detailed behavior are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) (Chinese). See also the [changelog](CHANGELOG.md), [installer instructions](installer/README.md), and [contribution rules](AGENTS.md).

## License and assets

The source is public, but no project-wide open-source license has been declared. The [DuiLib license](third_party/duilib/LICENSE) applies to that dependency and does not license the entire project.

FastFile is an original implementation. Icons come from Windows Shell or are drawn by the application; no assets or trademarks from third-party proprietary file managers are included.
