#pragma once
// FastFile UI design tokens (96-DPI design units).
// Physical pixels = DpiScale(token). Win11 light theme only. No 360 assets.
// Spacing scale: 4 / 8 / 12 / 16. Outer gutter raised 8 -> 14 (user-confirmed 12-16).
// Phase 2: left nav + details list density aligned with Win11 Explorer.
// Phase 3: preview pane + status bar density (UiTokens / Win11 Explorer).
// Phase 4: empty-preview chrome + scrollbar/nav density polish.
// Phase 5: typography hierarchy + chrome density (command bar 48->40, address 36->32,
//          favorites 30->26, toolbar icons 18->16, section headers no longer smaller than body).

namespace UiTokens {

// ---- Spacing (design px @96 DPI) ----
constexpr int SpaceXs = 4;
constexpr int SpaceSm = 8;
constexpr int SpaceMd = 12;
constexpr int SpaceLg = 16;

constexpr int OuterGutter = 14;     // was 8; target band 12-16
constexpr int InnerPadX = 12;       // chrome band horizontal padding (was ~8)
constexpr int InnerPadY = 4;        // chrome band vertical padding
// ---- Corner radii (Win11 language: containers 8, controls / rows 4) ----
constexpr int ChromeRound = 8;      // window/chrome container (was 10)
constexpr int RadiusControl = 4;    // command-bar buttons, input boxes, list rows, chips
constexpr int GapGroup = SpaceSm;   // toolbar group gaps
constexpr int SepH = 20;            // command-bar separators (bar is 40, buttons 32)
constexpr int HitTabH = 24;
constexpr int TabIconPx = 16;
constexpr int TabMinW = 175;          // Explorer-style tab cards are roomy
constexpr int TabMaxW = 260;
constexpr int TabCloseW = 20;
constexpr int HitBreadcrumbH = 30;      // address text stays vertically centered
constexpr int CmdBtnH = 32;
// Phase 6: Explorer-style chrome - a taller tab strip with tab "cards", a roomier address
// row whose path/search fields are white rounded boxes, and the command bar sitting on the
// white content surface instead of the grey chrome.
constexpr int TabBarH = 37;
constexpr int TabCardH = 32;          // chip inside the strip; its bottom edge = strip bottom
constexpr int TabCardGap = 5;         // gap between chips
constexpr int TabCardRound = 8;       // chip corner radius (Explorer-like)
constexpr int ToolbarH = 45;
constexpr int BreadcrumbBarH = 28;     // legacy; path merged into address_bar
constexpr int BreadcrumbSegPadX = 3;   // compact Explorer-style segment gap
constexpr int BreadcrumbSepW = 12;
constexpr int TitleBarH = 32;
constexpr int FavoritesBarH = 32;
constexpr int FavBarPadY = 2;          // tighter than InnerPadY so chips fit
constexpr int FavChipH = 22;
constexpr int FavIconPx = 16;
constexpr int FavLabelW = 72;          // star + fav left mark
constexpr int FavLabelBaselineLift = 4; // design px to raise the CJK "★ 收藏" label onto the
                                        // Latin baseline of the chips beside it
constexpr int AddressBarH = 48;       // roomier row; the path/search fields stay 32 and centre
constexpr int FieldRound = 8;         // path / search field corner radius
constexpr int AddressBarPadY = 2;
constexpr int SearchBoxH = 28;
constexpr int SearchChkW = 100;        // ☐ 含子目录
// One scrollbar thickness for the whole window: the file views sit right next to the
// preview rail (and the rail sits next to the file list), so a thinner list bar made the
// two columns at the divider visibly different widths. Everything follows the navigation
// pane / function-area scrollbar (user-confirmed reference).
constexpr int ScrollBarW = 12;         // file views (list / tiles)
constexpr int SidePaneScrollBarW = 12; // navigation + preview rail

// ---- Phase 2: left nav / tree (Win11 Explorer density) ----
constexpr int NavSectionHeaderH = 22;  // hidden legacy section labels; retained for layout compatibility
constexpr int NavRowH = 32;            // Quick Access rows + pinned favs
constexpr int TreeRowH = 32;           // roomier drive / folder rows
constexpr int TreeIndent = 14;         // per-level tree indent
constexpr int NavIconPx = 18;
constexpr int NavIconPad = SpaceSm;    // icon left inset
constexpr int NavIconTextGap = 6;      // gap icon -> label
constexpr int NavTextPadR = SpaceXs;
constexpr int NavHeaderPadL = SpaceSm;
constexpr int LeftPanelPad = SpaceSm;
constexpr int LeftNavGripH = 6;
constexpr int LeftNavSepH = SpaceSm;
constexpr int LeftQuickMinH = 160;     // four built-in rows plus breathing room; section label is hidden
constexpr int LeftQuickDefaultH = 160;

// ---- Phase 2: details list ----
constexpr int DetailsHeaderH = 28;
constexpr int DetailsRowH = 28;        // fits SHIL_SMALL 16 + padding
constexpr int DetailsCellPadL = SpaceSm;
constexpr int DetailsIconPx = 16;       // Shell small icon beside Name
constexpr int DetailsIconPadL = SpaceXs; // left inset before icon
constexpr int DetailsIconTextGap = SpaceXs;

// ---- Phase toolbar: Win11 Explorer command-bar density ----
constexpr int ToolbarIconPx = 16;       // denser line glyph; avoid clip with label
constexpr int ToolbarGlyphPx = 16;      // Segoe MDL2 glyph size for command-bar icons
constexpr int ToolbarBtnW = 32;         // icon-only command button
constexpr int ToolbarBtnH = 32;
constexpr int ToolbarTextBtnMinW = 76;  // New/Sort/View: icon + label + chevron
constexpr int ToolbarIconPad = 8;       // left/right pad around toolbar glyphs
constexpr int ToolbarChevronPad = 16;   // room for dropdown chevron
constexpr int ToolbarNavBtnW = 28;      // address-row navigation buttons
constexpr int NavGlyphPx = 20;          // address-row glyph size (roomier row)
constexpr int ToolbarGroupGap = SpaceSm;
constexpr int ToolbarSepPad = SpaceSm;

// ---- Icon / tile padding (cheap shared metrics) ----
constexpr int TilePadCompact = SpaceXs; // list view scroll pad
constexpr int TilePadNormal = 6;        // icon/tile scroll pad
constexpr int TileChildPadList = SpaceXs;
constexpr int TileChildPadMedium = 6;   // between SpaceXs and SpaceSm
constexpr int TileChildPadLarge = SpaceSm;
constexpr int TileChildPadXLarge = 10;

// ---- Font sizes (design) ----
constexpr int FontBody = 12;
constexpr int FontSmall = 11;
constexpr int FontCaption = 10;
constexpr int FontPreviewTitle = 14;   // preview header (DPI-scaled)
constexpr int FontPreviewMeta = 12;    // preview meta rows

// ---- Colors (DuiLib #AARRGGBB strings) ----
inline constexpr const wchar_t* ColorGutter        = L"#FFE8E8E8";
inline constexpr const wchar_t* ColorSurface       = L"#FFF3F3F3";  // chrome bands (unified)
inline constexpr const wchar_t* ColorContent       = L"#FFFFFFFF";  // list / body
inline constexpr const wchar_t* ColorBorder        = L"#FFE5E5E5";
inline constexpr const wchar_t* ColorBorderStrong  = L"#FFE0E0E0";
inline constexpr const wchar_t* ColorSeparator     = L"#FFD0D0D0";
inline constexpr const wchar_t* ColorTextPrimary   = L"#FF1A1A1A";
inline constexpr const wchar_t* ColorTextSecondary = L"#FF5A5A5A";
inline constexpr const wchar_t* ColorTextMuted     = L"#FF8A8A8A";
inline constexpr const wchar_t* ColorTextTabIdle   = L"#FF4A4A4A";
inline constexpr const wchar_t* ColorHover         = L"#FFE5E5E5";
inline constexpr const wchar_t* ColorPressed       = L"#FFD4D4D4";
inline constexpr const wchar_t* ColorActiveFill    = L"#FFFFFFFF";  // active tab / selected chip
inline constexpr const wchar_t* ColorActiveHot     = L"#FFF0F0F0";
// Phase 6 chrome palette (sampled from the Explorer-style reference)
inline constexpr const wchar_t* ColorTabStripBg    = L"#FFDBDBDB";  // tab band behind the cards
inline constexpr const wchar_t* ColorTabIdleBg     = L"#FFD2D2D2";  // inactive tab card
inline constexpr const wchar_t* ColorTabIdleBorder = L"#FFC8C8C8";
inline constexpr const wchar_t* ColorTabActive     = L"#FFF3F3F3";  // active card = chrome surface
inline constexpr const wchar_t* ColorTabActiveHot  = L"#FFF7F7F7";
inline constexpr const wchar_t* ColorTabActiveBorder = L"#FFE5E5E5";
inline constexpr const wchar_t* ColorChromeDivider = L"#FFDCDCDC";  // band separators
inline constexpr const wchar_t* ColorFieldBg       = L"#FFFFFFFF";  // path / search field
inline constexpr const wchar_t* ColorFieldBorder   = L"#FFD6D6D6";
inline constexpr const wchar_t* ColorTransparent   = L"#00FFFFFF";
inline constexpr const wchar_t* ColorDanger        = L"#FFB91C1C";
inline constexpr const wchar_t* ColorDangerHover   = L"#FFFEE2E2";
inline constexpr const wchar_t* ColorDangerPressed = L"#FFFECACA";
inline constexpr const wchar_t* ColorAccentSoft    = L"#FFDBEAFE";
inline constexpr const wchar_t* ColorAccentText    = L"#FF1E40AF";
inline constexpr const wchar_t* ColorScrollTrack   = L"#FFF7F7F7";
inline constexpr const wchar_t* ColorScrollThumb   = L"#FFC4C4C4";
inline constexpr const wchar_t* ColorCloseHot      = L"#FFE81123";

// ---- Phase 3: preview pane + status bar (Win11 Explorer density) ----
constexpr int PreviewPaneW = 344;
constexpr int PreviewPad = 24;            // white details pane: give text an Explorer-like inset
constexpr int PreviewRound = 0;
constexpr int PreviewImageRound = 0;
constexpr int PreviewTitleH = 28;
constexpr int PreviewTitleGap = 14;
constexpr int PreviewImageH = 196;
constexpr int PreviewIconCompactH = 80;  // folders / generic: compact icon area
constexpr int PreviewIconPx = 48;        // folder/generic icon design size
constexpr int PreviewImageGap = 16;
constexpr int PreviewMetaRowH = 22;
constexpr int PreviewMetaLabelW = 84;
constexpr int PreviewActionH = 34;
constexpr int PreviewActionW = 96;
constexpr int PreviewTextGap = SpaceMd;   // before text body
constexpr int PreviewThumbW = PreviewPaneW - 2 * PreviewPad; // 248 @ pad 16
constexpr int PreviewThumbH = 170;

constexpr int StatusBarH = 24;            // was 26; Explorer ~22-24
constexpr int StatusPadX = InnerPadX;     // 12
constexpr int StatusPadY = InnerPadY;     // 4
constexpr int StatusCancelW = 80;
constexpr int StatusCancelH = 20;
// Visual gap around status count separator (design spaces each side of |)
constexpr int StatusCountSepPad = SpaceSm;

// Phase 2: Explorer-like list / nav interaction
inline constexpr const wchar_t* ColorListHover     = L"#FFE8F4FC";
inline constexpr const wchar_t* ColorListSelected  = L"#FFCCE8FF";
inline constexpr const wchar_t* ColorListHeaderBg  = L"#FFF3F3F3";  // match chrome Surface
inline constexpr const wchar_t* ColorNavHover      = L"#FFE8F4FC";  // align with list hover
inline constexpr const wchar_t* ColorNavSelected   = L"#FFCCE8FF";
inline constexpr const wchar_t* ColorNavSection    = L"#FF5A5A5A";  // same size as rows, only lighter
inline constexpr const wchar_t* ColorTreeHotText   = L"#FF1A1A1A";
inline constexpr const wchar_t* ColorTreeSelText   = L"#FF1A1A1A";

// ARGB helpers matching string tokens (for TreeView Set*Color APIs)
constexpr unsigned ArgbTextPrimary   = 0xFF1A1A1Au;
constexpr unsigned ArgbTextSecondary = 0xFF5A5A5Au;
constexpr unsigned ArgbListHover     = 0xFFE8F4FCu;
constexpr unsigned ArgbListSelected  = 0xFFCCE8FFu;
constexpr unsigned ArgbNavSection    = 0xFF6B6B6Bu;

} // namespace UiTokens
