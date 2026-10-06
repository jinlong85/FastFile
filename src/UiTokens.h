#pragma once
// FastFile UI design tokens (96-DPI design units).
// Physical pixels = DpiScale(token). Win11 light theme only. No 360 assets.
// Spacing scale: 4 / 8 / 12 / 16. Outer gutter raised 8 -> 14 (user-confirmed 12-16).
// Phase 2: left nav + details list density aligned with Win11 Explorer.
// Phase 3: preview pane + status bar density (UiTokens / Win11 Explorer).
// Phase 4: empty-preview chrome + scrollbar/nav density polish.
// Phase 5: typography hierarchy + chrome density (command bar 48->40, address 36->32,
//          favorites 30->26, toolbar icons 18->16, section headers no longer smaller than body).
// Cmd-bar alignment (phase 1 of the Explorer metrics pass): command bar + address row follow
//          values measured on Win11 Explorer at 150% (bar 48, buttons 32 on a 48 pitch,
//          1x32 separators, 32px r4 address/search boxes, 12px nav glyphs).

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
constexpr int SepH = 32;            // command-bar separators: 1 x 32 like Explorer (bar 48, buttons 32)
constexpr int HitTabH = 29;           // compact tab-row height; plus width stays 32
constexpr int TabIconPx = 16;
constexpr int TabMinW = 180;          // fixed default width at 96 DPI
constexpr int TabSelMinW = TabMinW;   // active and idle tabs reserve the same close slot
constexpr int TabMaxW = TabMinW;
// This PC drive cards (Explorer): thin rounded capacity bar with a warning ramp.
constexpr int DriveBarH = 6;            // logical height of the usage bar
constexpr int DriveBarRound = 2;
constexpr int DriveLowFreeWarnPct = 20; // < 20% free -> orange
constexpr int DriveLowFreeRedPct = 10;  // < 10% free -> red
constexpr int TabCloseW = 20;
constexpr int HitBreadcrumbH = 28;      // fits the 32px address box minus its 2px insets
constexpr int CmdBtnH = 32;
constexpr int Hairline = 1;           // 1 design px; DpiScaleHairline() rounds *up* so a
                                      // divider lands on 2 physical px at 150% instead of
                                      // blurring across 1.5
// Phase 6: Explorer-style chrome - a taller tab strip with tab "cards", a roomier address
// row whose path/search fields are white rounded boxes, and the command bar sitting on the
// white content surface instead of the grey chrome.
// Phase 7 (Fluent pass): every chrome row follows Win11 Explorer, not the 360 density:
// Tabs and favorites now use 80% height; address/command/navigation keep their sizes.
constexpr int TabBarH = 29;          // round(36 * 0.8), 96-DPI units
constexpr int TabCardH = TabBarH;     // active and idle cards both fill the row
constexpr int TabCardGap = 4;         // gap between chips
constexpr int TabCardRound = 6;       // chip corner radius (Explorer-like)
constexpr int ToolbarH = 48;          // Explorer: 48 total = hairline + white body + hairline
constexpr int BreadcrumbBarH = 36;     // legacy; path merged into address_bar
constexpr int BreadcrumbSegPadX = 3;   // compact Explorer-style segment gap
constexpr int BreadcrumbSepW = 12;
constexpr int TitleBarH = TabBarH;
constexpr int FavoritesBarH = 29;     // round(36 * 0.8), matches compact tabs
constexpr int FavBarPadY = 2;          // legacy; kept for compatibility
constexpr int FavBarPadTop = 2;        // vertically centered compact favorites buttons
constexpr int FavBarPadBottom = 2;
constexpr int FavChipH = 25;          // 2px vertical padding inside the 29px row
constexpr int FavIconPx = 16;
constexpr int FavStarHitSize = 32;     // icon-only toggle width; height follows FavChipH
constexpr int FavStarGap = 8;         // toggle -> first chip / empty hint
constexpr int FavChipPadX = 8;         // chip inner left/right padding
constexpr int FavChipIconGap = 8;      // icon -> label gap
constexpr int FavChipGap = 8;          // between chips
constexpr int FavChipMaxW = 168;       // DT_END_ELLIPSIS beyond this
constexpr int AddressBarH = 48;       // Explorer address row
constexpr int FieldRound = 4;          // Explorer address/search box corner radius
constexpr int FieldH = 32;             // Explorer address/search box height
constexpr int AddressBarPadY = (AddressBarH - FieldH) / 2;  // 8: boxes centred in the row
constexpr int AddressBarPadL = 7;      // first nav glyph lands 7 + (40 - 12) / 2 = 21 from the edge
constexpr int AddressBarPadR = 12;
constexpr int AddressNavGap = 10;      // Refresh hit box -> address box (Explorer box starts at 201)
constexpr int AddressSearchGap = 8;    // address box -> search box
constexpr int SearchBoxH = FieldH;
// Search box width follows the row: clamp(SearchBoxMinW, SearchBoxRowPct% of row, SearchBoxMaxW)
// (Explorer is 435 wide when maximized). main.xml carries the minimum as its design width.
constexpr int SearchBoxMinW = 240;
constexpr int SearchBoxMaxW = 435;
constexpr int SearchBoxRowPct = 30;
constexpr int SearchBoxW = SearchBoxMinW;
constexpr int SearchGlyphPx = 11;      // Segoe Fluent E721 magnifier
constexpr int SearchGlyphPadR = 13;    // glyph right edge -> box right edge
constexpr int SearchGlyphGap = 8;      // text -> glyph
constexpr int FieldPadL = 10;          // text inset inside the address/search boxes
constexpr int SearchChkW = 100;        // ☐ 含子目录
// One scrollbar thickness for the whole window: the file views sit right next to the
// preview rail (and the rail sits next to the file list), so a thinner list bar made the
// two columns at the divider visibly different widths. Everything follows the navigation
// pane / function-area scrollbar (user-confirmed reference).
// Fluent pass: thin rail (4 logical = 6 physical at 150%), matching Explorer's overlay bar.
constexpr int ScrollBarW = 4;          // file views (list / tiles)
constexpr int SidePaneScrollBarW = 4;  // navigation + preview rail
constexpr int NavScrollBarW = 8;       // visible navigation thumb
constexpr int NavScrollBarHitW = 12;   // full reserved drag target, also hover width
// Fluent pass: the rail stays thin while idle and widens toward the content on hover
// (Explorer's overlay bar). The overhang is painted outside the control rect, so the
// layout keeps using ScrollBarW and nothing shifts when the pointer arrives.
constexpr int ScrollBarHoverW = 8;          // 8 logical = 12 physical at 150%
constexpr int ScrollBarHoverMargin = 6;     // pointer distance that triggers the widening

// ---- Phase 2: left nav / tree (Win11 Explorer density) ----
constexpr int NavSectionHeaderH = 22;  // hidden legacy section labels; retained for layout compatibility
constexpr int NavRowH = 36;           // Quick Access rows + pinned favs (Explorer)
constexpr int TreeRowH = 36;           // drive / folder rows
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
constexpr int DetailsHeaderH = 32;
constexpr int DetailsRowH = 30;        // Win11 Explorer details row
constexpr int DetailsCellPadL = SpaceSm;
constexpr int DetailsIconPx = 16;       // Shell small icon beside Name
constexpr int DetailsIconPadL = SpaceXs; // left inset before icon
constexpr int DetailsIconTextGap = SpaceXs;

// ---- Phase toolbar: Win11 Explorer command-bar density ----
// Cmd-bar alignment pass: measured from Win11 Explorer at 150% (values are logical px).
constexpr int ToolbarIconPx = 16;       // Explorer command icons are 16 logical px
constexpr int ToolbarGlyphPx = 16;      // Segoe Fluent glyph em for command-bar icons
constexpr int ToolbarBtnW = 40;         // icon-only command button: 40 wide + 8 gap = 48 pitch
constexpr int ToolbarBtnH = 32;
constexpr int ToolbarTextBtnMinW = 84;  // New/Sort/View: 12 + icon 16 + 8 + label 24 + 7 + chevron 5 + 12
constexpr int ToolbarIconPad = 12;      // button edge -> icon (label buttons)
constexpr int ToolbarIconLabelGap = 8;  // icon -> label
constexpr int ToolbarLabelChevronGap = 7;
constexpr int ToolbarChevronEm = 8;     // E972 (ChevronDownSmall) at an 8px em = 5.3 x 3.0 ink
constexpr int ToolbarChevronW = 5;
constexpr int ToolbarChevronPad = ToolbarLabelChevronGap + ToolbarChevronW + ToolbarIconPad; // text right pad
constexpr int ToolbarPadL = 6;          // window edge -> first icon = 6 + 12 = 18 (Explorer 18)
constexpr int ToolbarPadR = 6;          // settings gear mirrors the left edge
constexpr int ToolbarSepMargin = 6;     // separator <-> neighbour hit box; ink -> separator = 18
constexpr int ToolbarSortPad = 4;       // separator -> Sort hit box (Explorer ink 16-17 from the line)
constexpr int ToolbarLabelGap = 5;      // Sort -> View
constexpr int ToolbarMorePad = 3;       // separator -> More hit box (dots 17 from the line)
constexpr int ToolbarIconDropY = 1;     // Explorer icons sit ~1px below the bar centre
constexpr int ToolbarNavBtnW = 40;      // address-row navigation hit boxes, 48 pitch
constexpr int NavBtnGap = 8;
constexpr int NavGlyphPx = 12;          // Explorer back/forward/up/refresh glyph size
constexpr int ToolbarGroupGap = SpaceSm;
constexpr int ToolbarItemGap = 8;      // between icon-only command hit areas
constexpr int BodyTopGap = 8;          // group headers belong below the command divider
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
constexpr int FontNav = 12;
constexpr int FontTab = 12;           // compact regular text within the compact tab card
constexpr int FontSmall = 11;
constexpr int FontCaption = 10;
constexpr int FontPreviewTitle = 16;   // preview header (DPI-scaled)
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
inline constexpr const wchar_t* ColorFieldBg       = L"#FFFCFCFB";  // path / search field (Explorer)
inline constexpr const wchar_t* ColorFieldBorder   = L"#FFFCFCFB";  // idle border = fill (no visible border)
inline constexpr const wchar_t* ColorFieldFocus    = L"#FF0078D4";  // address edit mode accent border
// Command bar (measured Explorer palette)
inline constexpr const wchar_t* ColorCmdLine       = L"#FFE0E0E0";  // hairlines above/below the bar
inline constexpr const wchar_t* ColorCmdSeparator  = L"#FFF0F0F0";  // 1x32 group separators
inline constexpr const wchar_t* ColorCmdText       = L"#FF1B1B1B";
inline constexpr const wchar_t* ColorCmdTextDisabled = L"#FFA3A3A3";
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
constexpr int PreviewPaneW = 280;         // Fluent pass: details pane stays ~1/5 of the window
constexpr int PreviewPad = 16;            // white details pane: give text an Explorer-like inset
constexpr int PreviewRound = 0;
constexpr int PreviewImageRound = 4;
constexpr int PreviewTitleH = 28;
constexpr int PreviewTitleGap = 12;
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

constexpr int StatusBarH = 28;            // Win11 Explorer status row
constexpr int StatusPadX = InnerPadX;     // 12
constexpr int StatusPadY = InnerPadY;     // 4
constexpr int StatusCancelW = 80;
constexpr int StatusCancelH = 20;
// Visual gap around status count separator (design spaces each side of |)
constexpr int StatusCountSepPad = SpaceSm;

// Phase 2: Explorer-like list / nav interaction
inline constexpr const wchar_t* ColorListHover     = L"#FFE8F4FC";
inline constexpr const wchar_t* ColorListSelected  = L"#FFE0EEF9";
inline constexpr const wchar_t* ColorListHeaderBg  = L"#FFF3F3F3";  // match chrome Surface
inline constexpr const wchar_t* ColorNavHover      = L"#FFF0F0F0";
inline constexpr const wchar_t* ColorNavSelected   = L"#FFE8E8E8";
inline constexpr const wchar_t* ColorNavSection    = L"#FF5A5A5A";  // same size as rows, only lighter
inline constexpr const wchar_t* ColorTreeHotText   = L"#FF1A1A1A";
inline constexpr const wchar_t* ColorTreeSelText   = L"#FF1A1A1A";

// ARGB helpers matching string tokens (for TreeView Set*Color APIs)
constexpr unsigned ArgbTextPrimary   = 0xFF1A1A1Au;
constexpr unsigned ArgbTextSecondary = 0xFF5A5A5Au;
constexpr unsigned ArgbListHover     = 0xFFE8F4FCu;
constexpr unsigned ArgbListSelected  = 0xFFE0EEF9u;
constexpr unsigned ArgbNavSection    = 0xFF6B6B6Bu;
constexpr unsigned ArgbCmdLine       = 0xFFE0E0E0u;
constexpr unsigned ArgbCmdSeparator  = 0xFFF0F0F0u;
constexpr unsigned ArgbCmdText       = 0xFF1B1B1Bu;
constexpr unsigned ArgbCmdTextDisabled = 0xFFA3A3A3u;
constexpr unsigned ArgbFieldBg       = 0xFFFCFCFBu;
// Command icons: grey layer + blue accent; disabled = the whole icon at 36% (C2C2C2 / A3CEEF).
constexpr unsigned ArgbCmdIcon       = 0xFF555555u;
constexpr unsigned ArgbCmdAccent     = 0xFF0078D4u;
constexpr unsigned ArgbCmdMore       = 0xFF1B1B1Bu;
constexpr unsigned CmdDisabledAlpha  = 92u;          // 36% of 255
constexpr unsigned ArgbCmdChevron    = 0xFF777777u;
constexpr unsigned ArgbCmdChevronDisabled = 0xFFB0B0B0u;
constexpr unsigned ArgbNavGlyph      = 0xFF1A1A1Au;
constexpr unsigned ArgbNavGlyphDisabled = 0xFFA2A2A0u;
constexpr unsigned ArgbSearchGlyph   = 0xFF1A1A1Au;
// Segoe Fluent Icons code points (Explorer): Back / Forward / Up / Refresh / Search / chevron.
constexpr wchar_t GlyphNavBack = 0xE72B;
constexpr wchar_t GlyphNavForward = 0xE72A;
constexpr wchar_t GlyphNavUp = 0xE74A;
constexpr wchar_t GlyphNavRefresh = 0xE72C;
constexpr wchar_t GlyphSearch = 0xE721;
constexpr wchar_t GlyphChevronDown = 0xE972;  // ChevronDownSmall: E70D's compact, heavier twin

// Search box width for a given address-row width (all physical px).
constexpr int SearchBoxWidthFor(int rowPx, int minPx, int maxPx) {
    const int want = rowPx * SearchBoxRowPct / 100;
    return want < minPx ? minPx : (want > maxPx ? maxPx : want);
}

} // namespace UiTokens
