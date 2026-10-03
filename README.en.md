# FastFile

**A tabbed Windows file manager with native file operations and context menus.**

[Download 1.0.12 prerelease](https://github.com/jinlong85/FastFile/releases/tag/v1.0.12) · [Download installer](https://github.com/jinlong85/FastFile/releases/download/v1.0.12/FastFile-Setup-1.0.12.exe) · [Report an issue](https://github.com/jinlong85/FastFile/issues/new/choose) · [简体中文](README.md)

FastFile brings multiple folders into one window with tabs, breadcrumbs, bookmarks, a folder tree, search, and a preview pane. Regular folders use the native Windows Shell view, including system context menus, file operation progress, and conflict dialogs.

## Features

- Tabbed browsing, optional reuse of tabs for the same folder, and session restoration.
- Breadcrumb navigation, back/forward history, bookmarks, and a folder tree.
- Copy, cut, paste, drag and drop, rename, delete, and undo/redo for supported operations.
- Six view modes, per-folder view memory, name filtering, and recursive folder search.
- Preview and details panes, adjustable layout density, fonts, and tab sizes.
- Folder association detection and repair, with optional forwarding of newly opened Explorer folder windows.

## Install

The current download is **1.0.12 prerelease for Windows x64**. The application interface is in Chinese; this English README does not imply an English UI.

1. Download `FastFile-Setup-1.0.12.exe` from the [release page](https://github.com/jinlong85/FastFile/releases/tag/v1.0.12). The source archives are not installers.
2. Close the previous FastFile instance before updating, then run the installer. Installation is per user and does not require administrator privileges.
3. Launch FastFile from the Start menu. Use the gear button to open settings.

Installation directory: `%LOCALAPPDATA%\Programs\FastFile`. Uninstall through Windows Settings → Apps. Personal settings in `%APPDATA%\FastFile` are retained.

Installer SHA-256:

```text
BB698D76A0B5363E1D7555D68A9AB183D7E7612C8D6449548C2DE902312554A1
```

## Folder integration

Integration is disabled by default. Open Settings → System integration (`设置 → 系统集成`), select the desired folder and drive associations, and click `修复并应用接管` to apply or repair them. Click `检测接管状态` to inspect the effective handlers and executable paths.

For applications that launch Explorer directly, optionally enable `自动转交新打开的资源管理器文件夹`. **FastFile must remain running.** A new Explorer window is closed only after FastFile confirms the destination folder and selected files; Explorer may briefly appear.

Existing windows, other file managers, multiple tabs, virtual folders, busy windows, selections exceeding 256 items, and windows that cannot be verified are retained. This does not guarantee handling every application or system file picker. Use `恢复 Windows 打开方式`, then `保存`, to restore the previous associations.

## Status and limitations

- Release x64 build, all 13 automated tests, and installer payload verification passed, including live Explorer folder opening and file selection forwarding.
- The IDM application menu and installation/uninstallation have not yet been manually validated. This version remains a prerelease; see [HANDOFF.md](HANDOFF.md) for the verification record in Chinese.
- Archive browsing, network locations, batch renaming, and file content search are not supported. Recursive search results are limited to 4,000 items.
- Permanent deletion cannot be undone; some overwrite or merge operations are excluded from undo history.

## Feedback and development

[Report bugs or request features](https://github.com/jinlong85/FastFile/issues/new/choose). Include the FastFile and Windows versions, reproduction steps, and expected versus actual behavior. For folder integration issues, include the calling application and detection results. Remove personal paths and sensitive data from attachments.

Built with **C++17, Win32, DuiLib, and Windows Shell**. Build and test instructions, shortcuts, and detailed behavior are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) (Chinese). See also the [changelog](CHANGELOG.md), [installer instructions](installer/README.md), and [contribution rules](AGENTS.md).

## License and assets

The source is public, but no project-wide open-source license has been declared. The [DuiLib license](third_party/duilib/LICENSE) applies to that dependency and does not license the entire project.

FastFile is an original implementation. Icons come from Windows Shell or are drawn by the application; no assets or trademarks from third-party proprietary file managers are included.
