// RoCheats Monitor
// ---------------------------------------------------------------------------
// A small Windows tray app. It polls the public WEAO API and raises a native
// desktop notification whenever the status of a tracked product changes.
//
//   * no installer, no runtime dependencies (static MinGW-w64 build)
//   * settings + last known statuses live in %APPDATA%\RoCheatsMonitor\settings.ini
//   * the only network request it makes is GET <WEAO>/api/status/exploits
//   * the whole interface is drawn by the app itself (see canvas.h)
//
// Build: ./build.sh

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <wincodec.h>

#include "canvas.h"
#include "json.h"

// A few constants that older SDK headers may lack.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef NIIF_LARGE_ICON
#define NIIF_LARGE_ICON 0x00000020
#endif
#ifndef NIF_SHOWTIP
#define NIF_SHOWTIP 0x00000080
#endif
#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const wchar_t* const APP_NAME    = L"RoCheats Monitor";
static const wchar_t* const APP_VERSION = L"2.2";
static const wchar_t* const WND_CLASS   = L"RoCheatsMonitor.MainWindow";
static const wchar_t* const MUTEX_NAME  = L"Local\\RoCheatsMonitor.SingleInstance";
static const wchar_t* const RUN_KEY     = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* const RUN_VALUE   = L"RoCheatsMonitor";

// WEAO asks third-party tools to identify themselves with this user agent.
static const wchar_t* const USER_AGENT = L"WEAO-3PService";
static const wchar_t* const API_URLS[] = {
    L"https://weao.xyz/api/status/exploits",
    L"https://whatexpsare.online/api/status/exploits",
    L"https://weao.gg/api/status/exploits",
};

static const wchar_t* const DEFAULT_PRODUCTS =
    L"Volt,Wave,Potassium,Isaeva,Matcha,Serotonin,Codex,Lucid,Axis";
enum { MAX_PRODUCTS = 16, SEG_COUNT = 6 };
static const size_t MAX_BODY = 8u * 1024u * 1024u;
static const int INTERVALS[SEG_COUNT] = { 1, 2, 5, 10, 15, 30 };  // minutes
static const int RETRY_SECONDS = 60;

static const DWORD WND_STYLE = WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX;
static const DWORD WND_EXSTYLE = WS_EX_APPWINDOW;

enum : UINT { WM_APP_TRAY = WM_APP + 1, WM_APP_FETCHED, WM_APP_SHOW, WM_APP_RBX };
enum : UINT_PTR { TIMER_POLL = 1, TIMER_TRAY = 2, TIMER_ANIM = 3, TIMER_CLOCK = 4, TIMER_RBX = 5 };
enum { TRAY_ID = 1 };
enum { ID_OPEN = 1001, ID_CHECK, ID_TEST, ID_EXIT };

// Everything the mouse can point at.
enum {
    HIT_NONE = 0,
    HIT_BRAND,    // the app mark in the title bar or on the rail
    HIT_TB_SETTINGS,
    HIT_TB_MIN,
    HIT_TB_CLOSE,
    HIT_NAV0,     // rail: status
    HIT_NAV1,     // rail: settings
    HIT_CHECK,
    HIT_SHEET_CLOSE,
    HIT_TEST,
    HIT_SCRIM,  // the dimmed dashboard beside the settings panel
    HIT_SHEET,  // empty part of the settings panel
    HIT_TGL0,   // + 0 notifications, + 1 sound, + 2 start with Windows
    HIT_DENS0,  // + 0 comfortable, + 1 compact
    HIT_ALERT0, // + 0 status, + 1 versions, + 2 detection, + 3 Roblox
    HIT_SEG0 = HIT_ALERT0 + 4,         // + index into INTERVALS
    HIT_FILTER0 = HIT_SEG0 + SEG_COUNT,  // + 0 all, 1 updated, 2 not updated
    HIT_SWATCH0 = HIT_FILTER0 + 3,     // + 0..5 scenes, 6 picture, 7 colour
    HIT_THEME0 = HIT_SWATCH0 + 8,      // + 0 glass, 1 flat
    HIT_ACCENT0 = HIT_THEME0 + 2,      // + 0..5 accent presets
    HIT_PICPICK = HIT_ACCENT0 + 6,     // "choose a picture" button
    HIT_ROW0 = HIT_PICPICK + 1,        // + product index
    HIT_BELL0 = HIT_ROW0 + MAX_PRODUCTS,
    HIT_COUNT = HIT_BELL0 + MAX_PRODUCTS
};

// Palette: near-black base, one blue accent, three status colours.
namespace pal {
// Cool, slightly green-grey glass with a cyan accent: the same family as the
// Unnamed Enhancements workspace, so the two apps look like siblings.
const uint32_t Bg        = 0x0A0C0D;
const uint32_t Card      = 0x141617;
const uint32_t CardHot   = 0x1B1E1F;
const uint32_t Row       = 0x15181A;
const uint32_t RowHot    = 0x1D2122;
const uint32_t Line      = 0x24282A;
const uint32_t LineHot   = 0x3A4042;
const uint32_t Panel     = 0x101314;
const uint32_t Rail      = 0x0D1011;
const uint32_t Text      = 0xEDEAE4;
const uint32_t Text2     = 0xA3A6A1;
const uint32_t Text3     = 0x808680;
const uint32_t Text4     = 0x5A615C;
uint32_t Accent          = 0x7DEEFF;  // a setting: six preset accents
const uint32_t AccentHot = 0xFFFFFF;
const uint32_t Violet    = 0x7C5CFF;
const uint32_t Green     = 0x3DD68C;
const uint32_t Red       = 0xF2616D;
const uint32_t Amber     = 0xF0B243;
const uint32_t Track     = 0x20242E;
const uint32_t ToggleOff = 0x2A2E3A;
const uint32_t CloseHot  = 0xE5484D;
const uint32_t White     = 0xFFFFFF;
const uint32_t Black     = 0x000000;
}  // namespace pal

// Layout in logical pixels (100 % scale). S() converts to device pixels.
static const double kW = 780, kTitleH = 52, kPad = 20;
static const double kRailW = 56, kRailMin = 780;
static const double kHeroH = 150, kToolbarH = 66, kRowH = 50, kGap = 8;
// Density only changes metrics, never content: compact simply spends less air.
static bool Compact();  // defined after Settings
static double DensHero() { return Compact() ? 124.0 : kHeroH; }
static double DensRow() { return Compact() ? 36.0 : kRowH; }
static double DensGap() { return Compact() ? 5.0 : kGap; }
static double DensToolbar() { return Compact() ? 54.0 : kToolbarH; }
static const double kPanelW = 278, kMinH = 740;
static const int LOGO_TILE = 96, LOGO_COUNT = 10;
static const int SWATCHES = 8;  // six scenes, a picture and a colour

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
struct Product {
    std::wstring name;        // shown in the UI (upper case)
    std::wstring key;         // lower case, matched against the API "title"
    bool notify = true;       // per-product notification switch
    int updated = -1;         // last known: -1 unknown, 0 not updated, 1 updated
    int detected = -1;        // last known: -1 unknown, 0 undetected, 1 detected
    std::wstring version;
    std::wstring kind;        // "Executor", "External" or empty
    ULONGLONG updatedAt = 0;  // when its developers last updated it (seconds since 1601, UTC)
    std::wstring rbx;         // Roblox client build it supports, as the feed reports it
    bool tracked = true;      // false once a successful poll did not list it
};

struct Settings {
    std::wstring background = L"aurora";  // aurora, midnight, ember, forest, carbon, custom
    std::wstring imagePath;               // a picture of your own (background=image)
    std::wstring bgColor = L"4c8dff";     // accent for a colour of your own (background=colour)
    std::wstring theme = L"glass";        // glass or flat
    std::wstring accent = L"7deeff";      // the one colour that tints the whole app
    int intervalMin = 5;
    bool notifications = true;
    bool sound = true;
    std::wstring density = L"comfortable";  // comfortable or compact
    bool alertUpdates = true;    // status flips
    bool alertVersions = true;   // a new version string for the same status
    bool alertDetection = true;  // Hyperion flags it or clears it
    bool alertRoblox = true;     // a new Roblox client build
    bool trayHintShown = false;
    std::wstring apiUrl;  // optional override, tried before the built-in URLs
};

// Roblox build each product supports, keyed by product key, refreshed with the feed.
static std::vector<std::pair<std::wstring, std::wstring>> g_lastRbxByProduct;

struct FetchItem {
    std::wstring title, version, platform, kind, rbx;
    ULONGLONG updatedAt = 0;
    bool updated = false;
    bool hasDetected = false;
    bool detected = false;
};

struct FetchResult {
    bool ok = false;
    bool rateLimited = false;
    int retryAfterSec = 0;
    std::vector<FetchItem> items;
};

struct FetchParams {
    HWND hwnd = nullptr;
    std::vector<std::wstring> urls;
};

struct Change {
    int index;  // product index
    int kind;   // 0 = update status, 1 = detection, 2 = new version string
    bool now;   // new value
};

// A value that eases towards a target.
// Temporarily overrides how quickly things feel: slower while the window is
// resizing (so it never lags behind the pointer) and faster on Windows, where
// a longer animation reads as sluggish.
static float g_transition = 1.0f;

struct Anim {
    float v = 0.0f, to = 0.0f;
    void set(float value) { v = to = value; }
    void target(float value) { to = value; }
    void jump(float value) { v = to = value; }
    // Snaps at the end, so nothing is left a fraction of a pixel out of place.
    bool step(float dt, float rate) {
        if (v == to) return false;
        const float k = std::min(0.999f, 1.0f - std::exp(-dt * rate * g_transition));
        v += (to - v) * k;
        if (std::fabs(to - v) < 0.002f) v = to;
        return true;
    }
};

// Same, but on a fixed clock rather than an exponential settle: the splash
// has to hand over at a known moment, not creep towards one.
struct Timed {
    float v = 0.0f, to = 0.0f;
    void target(float value) { to = value; }
    void jump(float value) { v = to = value; }
    bool step(float dt, float seconds) {
        if (v == to) return false;
        if (seconds <= 0.0f) { v = to; return true; }
        const float stepv = dt / seconds;
        if (v < to) v = std::min(to, v + stepv);
        else v = std::max(to, v - stepv);
        return true;
    }
};

// Eased 0..1 progress: the shape most transitions use.
static float Ease(float t) {
    t = cv::Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

// An off-screen bitmap that both the rasteriser and GDI (for text) can draw on.
struct Layer {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    cv::Surface s;
};

static HINSTANCE g_inst = nullptr;
static HWND g_hwnd = nullptr;
static HICON g_iconSmall = nullptr, g_iconLarge = nullptr;
static UINT g_msgTaskbarCreated = 0;
static double g_scale = 1.0;
static Settings g_set;

static bool Compact() { return g_set.density == L"compact"; }
static void SaveCustomisation();  // defined next to the ini helpers
static bool AlertOn(int n) {
    switch (n) {
        case 0: return g_set.alertUpdates;
        case 1: return g_set.alertVersions;
        case 2: return g_set.alertDetection;
        default: return g_set.alertRoblox;
    }
}
static void AlertSet(int n, bool on) {
    switch (n) {
        case 0: g_set.alertUpdates = on; break;
        case 1: g_set.alertVersions = on; break;
        case 2: g_set.alertDetection = on; break;
        default: g_set.alertRoblox = on; break;
    }
}
static std::vector<Product> g_products;
static std::wstring g_iniPath;

static bool g_fetching = false;       // a request is in flight
static bool g_everSucceeded = false;  // at least one good response this session
static bool g_lastFailed = false;
static std::wstring g_failText;
static std::wstring g_lastChecked;    // formatted local time of the last good response
static DWORD g_lastManualTick = 0;
static bool g_hasManualTick = false;
static ULONGLONG g_pollStart = 0, g_pollDue = 0;  // tick counts framing the wait for the next check
// Roblox itself: the client build every product is measured against. Polled on its
// own fast clock because Roblox ships builds without warning.
static std::wstring g_rbxVersion;     // "0.747.0.7471481"
static std::wstring g_rbxUpload;      // "version-4ea8e85481e34f46"
static float g_rbxPulse = 0.0f;       // 1 -> 0 after a new build lands
static bool g_rbxFetching = false;

static std::wstring g_lastChange;     // most recent status change, e.g. "VOLT updated"
static ULONGLONG g_lastChangeAt = 0;  // when it was seen (seconds since 1601, UTC)

static bool g_autostart = false;
static bool g_trayAdded = false, g_trayV4 = false;
static int g_trayRetries = 0;
static int g_savedX = INT_MIN, g_savedY = INT_MIN;
static bool g_roundCorners = false;  // Windows 11 rounds the window's corners
static bool g_systemBorder = false;   // Windows 11 also draws the border

// Interface state
static Layer g_dash;   // the dashboard, redrawn on every paint
static Layer g_front;  // what is shown: the dashboard plus the settings panel
static std::vector<uint32_t> g_bg;  // cached window background
static std::vector<RECT> g_dirty;   // parts of the dashboard that must be redrawn

static int g_hot = HIT_NONE, g_pressed = HIT_NONE;
static bool g_mouseTracking = false;
static bool g_sheetOpen = false;
static int g_filter = 0;  // 0 all, 1 updated, 2 not updated

static Anim g_hov[HIT_COUNT];        // hover highlight per element
static Anim g_tgl[3];                // switch knobs
static Anim g_alertTgl[4];           // the four notification switches
static Anim g_seg;                   // highlight position in the interval picker
static Anim g_sheet;                 // 0 = settings closed, 1 = open
// The panel is taller than the window on short screens, so it scrolls: the
// wheel moves `to`, and `y` chases it so the motion eases instead of jumping.
static double g_scrollTo = 0.0, g_scrollY = 0.0;
static const double kPanelContentH = 872.0;  // logical height of everything in the panel
static float g_flash[MAX_PRODUCTS];   // 1 -> 0 after a product changed status
static Anim g_rowPos[MAX_PRODUCTS];   // 0..1 while a row slides to its new slot
static float g_rowFrom[MAX_PRODUCTS];  // where it slid from, in slot positions
static Anim g_fade;                   // fades the dashboard in and out
static float g_bgFade = 1.0f;         // 0 -> 1 after the background changes
static bool g_layoutNext = false;     // resize the window once the rows settle
static std::vector<uint32_t> g_bgPrev;  // the outgoing background during a cross-fade
static float g_spin = 0.0f;          // rotation of the refresh icon, in turns
static Anim g_splash;                 // 0 -> 1 while the wordmark sits over the frame
static bool g_splashOn = false;        // the wordmark is still on screen
static double g_splashElapsed = 0.0;   // seconds since the wordmark came up
static bool g_animOn = false;
// Repaint bookkeeping for the paint path: an animation tick only redraws what
// it actually changed. The panel is hundreds of thousands of pixels, so asking
// for the whole window on every tick is what made it cost a core.
static void InvalidateFront(const RECT* r);
static RECT g_tickDirty[24];
static int g_tickDirtyCount = 0;
static void TickDirty(const RECT& r) {
    if (g_tickDirtyCount < 24) g_tickDirty[g_tickDirtyCount++] = r;
    // Only the panel is large; everything else invalidates itself by control id.
    if (r.right - r.left > 0) InvalidateFront(&r);
}
static void TickDirtyClear() { g_tickDirtyCount = 0; }
static int g_bgW = 0, g_bgH = 0, g_bgGen = 0;
static LARGE_INTEGER g_qpf, g_animLast;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static int S(double v) { return (int)std::floor(v * g_scale + 0.5); }
static float U(double v) { return (float)(v * g_scale); }
static int Max(int a, int b) { return a > b ? a : b; }
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t')) --b;
    return s.substr(a, b - a);
}

static std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

static std::wstring Upper(std::wstring s) {
    if (!s.empty()) CharUpperBuffW(&s[0], (DWORD)s.size());
    return s;
}

static bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }

// Text from the network: drop control characters, trim, cap the length.
static std::wstring Clean(const std::wstring& in, size_t maxLen) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in)
        if (c >= 0x20 && c != 0x7F) out += c;
    out = Trim(out);
    if (out.size() > maxLen) {
        out.resize(maxLen);
        if (!out.empty() && IsHighSurrogate(out.back())) out.pop_back();
    }
    return out;
}

// Product names double as INI section names, so keep them tame.
static std::wstring CleanName(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        if (c < 0x20 || c == 0x7F || c == L'[' || c == L']' || c == L'=' || c == L';') continue;
        out += c;
    }
    out = Trim(out);
    if (out.size() > 24) out.resize(24);
    return Trim(out);
}

// Fits `s` into `maxChars` characters, ending with an ellipsis when cut.
static std::wstring Clip(const std::wstring& s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars == 0) return std::wstring();
    size_t n = maxChars - 1;
    if (n > 0 && IsHighSurrogate(s[n - 1])) --n;
    return s.substr(0, n) + L"\u2026";
}

static std::wstring NowTime() {
    wchar_t buf[64];
    if (!GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, buf, 64))
        return std::wstring();
    return buf;
}

static std::wstring IntervalShort(int minutes) { return std::to_wstring(minutes) + L"m"; }

static ULONGLONG NowSecs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000000ULL;
}

static bool ReadInt(const wchar_t*& p, int& out) {
    while (*p == L' ') ++p;
    if (*p < L'0' || *p > L'9') return false;
    int v = 0, digits = 0;
    while (*p >= L'0' && *p <= L'9' && digits < 6) {
        v = v * 10 + (*p - L'0');
        ++p;
        ++digits;
    }
    out = v;
    return true;
}

// WEAO writes dates as "11/09/2026 at 11:41 PM UTC". Returns seconds since
// 1601 (UTC), or 0 when the text cannot be read.
static ULONGLONG ParseWeaoDate(const std::wstring& s) {
    const wchar_t* p = s.c_str();
    int mo = 0, d = 0, y = 0, h = 0, mi = 0;
    if (!ReadInt(p, mo) || *p != L'/') return 0;
    ++p;
    if (!ReadInt(p, d) || *p != L'/') return 0;
    ++p;
    if (!ReadInt(p, y)) return 0;
    while (*p == L' ') ++p;
    if (p[0] == L'a' && p[1] == L't') p += 2;
    if (!ReadInt(p, h) || *p != L':') return 0;
    ++p;
    if (!ReadInt(p, mi)) return 0;
    while (*p == L' ') ++p;
    if ((*p == L'P' || *p == L'p') && h < 12) h += 12;
    if ((*p == L'A' || *p == L'a') && h == 12) h = 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || y < 2000 || y > 2200 || h > 23 || mi > 59) return 0;
    SYSTEMTIME st;
    ZeroMemory(&st, sizeof st);
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000000ULL;
}

// "3h ago" for a moment in the past; empty when unknown.
static std::wstring Ago(ULONGLONG then) {
    const ULONGLONG now = NowSecs();
    if (then == 0 || then > now + 600) return std::wstring();
    const ULONGLONG d = now > then ? now - then : 0;
    if (d < 90) return L"just now";
    if (d < 3600) return std::to_wstring(d / 60) + L"m ago";
    if (d < 86400) return std::to_wstring(d / 3600) + L"h ago";
    if (d < 86400ULL * 60) return std::to_wstring(d / 86400) + L"d ago";
    if (d < 86400ULL * 365) return std::to_wstring(d / (86400ULL * 30)) + L"mo ago";
    return std::to_wstring(d / (86400ULL * 365)) + L"y ago";
}

// WEAO's "extype" is a platform letter plus the kind, e.g. "wexecutor".
static std::wstring KindOf(const std::wstring& extype) {
    const std::wstring t = Lower(extype);
    if (t.find(L"external") != std::wstring::npos) return L"External";
    if (t.find(L"executor") != std::wstring::npos) return L"Executor";
    return std::wstring();
}

static int IntervalIndex() {
    for (int i = 0; i < SEG_COUNT; ++i)
        if (INTERVALS[i] == g_set.intervalMin) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Settings (INI file in %APPDATA%)
// ---------------------------------------------------------------------------
static void InitIniPath() {
    wchar_t base[MAX_PATH] = L"";
    std::wstring dir;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, base))) {
        dir = std::wstring(base) + L"\\RoCheatsMonitor";
    } else {  // unusual: fall back to the folder the exe lives in
        wchar_t exe[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        dir = exe;
        size_t slash = dir.find_last_of(L"\\/");
        dir = (slash == std::wstring::npos) ? L"." : dir.substr(0, slash);
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    g_iniPath = dir + L"\\settings.ini";
    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Start the file with a UTF-16 BOM so the profile API stores Unicode.
        HANDLE h = CreateFileW(g_iniPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const BYTE bom[2] = { 0xFF, 0xFE };
            DWORD written = 0;
            WriteFile(h, bom, 2, &written, nullptr);
            CloseHandle(h);
        }
    }
}

static std::wstring IniGet(const std::wstring& sec, const wchar_t* key, const wchar_t* def = L"") {
    wchar_t buf[2048];
    GetPrivateProfileStringW(sec.c_str(), key, def, buf, 2048, g_iniPath.c_str());
    return buf;
}

static int IniGetInt(const std::wstring& sec, const wchar_t* key, int def) {
    std::wstring v = Trim(IniGet(sec, key));
    if (v.empty()) return def;
    wchar_t* end = nullptr;
    long n = wcstol(v.c_str(), &end, 10);
    return (end && end != v.c_str()) ? (int)n : def;
}

static void IniSet(const std::wstring& sec, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(sec.c_str(), key, value.c_str(), g_iniPath.c_str());
}

static void IniSetInt(const std::wstring& sec, const wchar_t* key, int value) {
    IniSet(sec, key, std::to_wstring(value));
}

static std::wstring ProductSection(const Product& p) { return L"Product." + p.key; }

static void SaveCustomisation() {
    IniSet(L"Settings", L"Density", g_set.density);
    IniSetInt(L"Settings", L"AlertUpdates", g_set.alertUpdates ? 1 : 0);
    IniSetInt(L"Settings", L"AlertVersions", g_set.alertVersions ? 1 : 0);
    IniSetInt(L"Settings", L"AlertDetection", g_set.alertDetection ? 1 : 0);
    IniSetInt(L"Settings", L"AlertRoblox", g_set.alertRoblox ? 1 : 0);
}

static void SaveProduct(const Product& p) {
    const std::wstring sec = ProductSection(p);
    IniSetInt(sec, L"Notify", p.notify ? 1 : 0);
    IniSetInt(sec, L"Updated", p.updated);
    IniSetInt(sec, L"Detected", p.detected);
    IniSet(sec, L"Version", p.version);
    IniSet(sec, L"Kind", p.kind);
    IniSet(sec, L"Rbx", p.rbx);
    IniSet(sec, L"UpdatedAt", std::to_wstring(p.updatedAt));
}

static void LoadSettings() {
    const std::wstring sec = L"Settings";
    g_set.intervalMin = Clamp(IniGetInt(sec, L"IntervalMinutes", 5), 1, 1440);
    g_set.notifications = IniGetInt(sec, L"Notifications", 1) != 0;
    g_set.density = Trim(IniGet(sec, L"Density", L"comfortable"));
    g_set.alertUpdates = IniGetInt(sec, L"AlertUpdates", 1) != 0;
    g_set.alertVersions = IniGetInt(sec, L"AlertVersions", 1) != 0;
    g_set.alertDetection = IniGetInt(sec, L"AlertDetection", 1) != 0;
    g_set.alertRoblox = IniGetInt(sec, L"AlertRoblox", 1) != 0;
    g_set.sound = IniGetInt(sec, L"Sound", 1) != 0;
    g_set.trayHintShown = IniGetInt(sec, L"TrayHintShown", 0) != 0;
    g_set.background = Trim(IniGet(sec, L"Background", L"aurora"));
    g_set.theme = Trim(IniGet(sec, L"Theme", L"glass"));
    g_set.imagePath = Trim(IniGet(sec, L"BackgroundImage"));
    g_set.bgColor = Trim(IniGet(sec, L"BackgroundColor", L"5b9dff"));
    g_set.accent = Trim(IniGet(sec, L"Accent", L"7deeff"));
    g_set.apiUrl = Trim(IniGet(sec, L"ApiUrl"));
    g_savedX = IniGetInt(L"Window", L"X", INT_MIN);
    g_savedY = IniGetInt(L"Window", L"Y", INT_MIN);
    g_rbxVersion = Clean(IniGet(sec, L"RobloxVersion"), 40);
    g_rbxUpload = Clean(IniGet(sec, L"RobloxUpload"), 48);
    g_lastChange = Clean(IniGet(sec, L"LastChange"), 60);
    g_lastChangeAt = wcstoull(Trim(IniGet(sec, L"LastChangeAt")).c_str(), nullptr, 10);

    std::wstring list = Trim(IniGet(sec, L"Products"));
    for (int pass = 0; pass < 2 && g_products.empty(); ++pass) {
        if (pass == 1 || list.empty()) {
            list = DEFAULT_PRODUCTS;
            IniSet(sec, L"Products", list);
        }
        size_t start = 0;
        while (g_products.size() < (size_t)MAX_PRODUCTS) {
            size_t comma = list.find(L',', start);
            std::wstring name = CleanName(
                list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start));
            if (!name.empty() && Lower(name) != L"seliware") {
                std::wstring key = Lower(name);
                bool dup = false;
                for (const Product& q : g_products) dup = dup || q.key == key;
                if (!dup) {
                    Product p;
                    p.name = Upper(name);
                    p.key = key;
                    g_products.push_back(p);
                }
            }
            if (comma == std::wstring::npos) break;
            start = comma + 1;
        }
    }

    for (Product& p : g_products) {
        const std::wstring ps = ProductSection(p);
        p.notify = IniGetInt(ps, L"Notify", 1) != 0;
        p.updated = Clamp(IniGetInt(ps, L"Updated", -1), -1, 1);
        p.detected = Clamp(IniGetInt(ps, L"Detected", -1), -1, 1);
        p.version = Clean(IniGet(ps, L"Version"), 40);
        p.kind = KindOf(IniGet(ps, L"Kind"));
        p.updatedAt = wcstoull(Trim(IniGet(ps, L"UpdatedAt")).c_str(), nullptr, 10);
        p.rbx = Clean(IniGet(ps, L"Rbx"), 48);
        g_lastRbxByProduct.push_back({ p.key, p.rbx });
    }
}

// ---------------------------------------------------------------------------
// Start with Windows (HKCU Run key)
// ---------------------------------------------------------------------------
static std::wstring AutostartCommand() {
    wchar_t exe[1024] = L"";
    GetModuleFileNameW(nullptr, exe, 1024);
    return L"\"" + std::wstring(exe) + L"\" --tray";
}

static bool AutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    LONG r = RegQueryValueExW(key, RUN_VALUE, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

static bool SetAutostart(bool on) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return false;
    LONG r;
    if (on) {
        const std::wstring cmd = AutostartCommand();
        r = RegSetValueExW(key, RUN_VALUE, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                           (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, RUN_VALUE);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Networking (runs on a worker thread)
// ---------------------------------------------------------------------------
static bool HttpGet(const std::wstring& url, DWORD& status, std::string& body) {
    status = 0;
    body.clear();

    wchar_t host[256] = L"", path[2048] = L"";
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
    if (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP) return false;
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 8000, 8000, 10000, 15000);

    bool ok = false;
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", path[0] ? path : L"/", nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           secure ? WINHTTP_FLAG_SECURE : 0);
        if (req) {
            if (WinHttpSendRequest(req, L"Accept: application/json\r\n", (DWORD)-1L,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD size = sizeof status;
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                    WINHTTP_NO_HEADER_INDEX);
                ok = true;
                char chunk[16384];
                for (;;) {  // a read of 0 bytes marks the end of the response
                    DWORD got = 0;
                    if (!WinHttpReadData(req, chunk, sizeof chunk, &got)) { ok = false; break; }
                    if (got == 0) break;
                    if (body.size() + got > MAX_BODY) { ok = false; break; }
                    body.append(chunk, got);
                }
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

static std::wstring JsonText(const json::Value& obj, const char* key, size_t maxLen) {
    const json::Value* v = obj.find(key);
    return (v && v->isString()) ? Clean(Utf8ToWide(v->str), maxLen) : std::wstring();
}

static DWORD WINAPI FetchThread(LPVOID param) {
    FetchParams* p = (FetchParams*)param;
    FetchResult* res = new FetchResult();

    for (const std::wstring& url : p->urls) {
        DWORD status = 0;
        std::string body;
        if (!HttpGet(url, status, body)) continue;  // network trouble: try the next mirror

        if (status == 429) {  // rate limited: back off, do not hammer the mirrors
            res->rateLimited = true;
            res->retryAfterSec = 120;
            json::Value v;
            if (json::Parse(body, v)) {
                const json::Value* info = v.find("rateLimitInfo");
                const json::Value* t = info ? info->find("remainingTime") : nullptr;
                if (t && t->isNumber() && t->num > 0 && t->num < 86400)
                    res->retryAfterSec = Clamp((int)t->num, 15, 3600);
            }
            break;
        }
        if (status != 200) continue;

        json::Value v;
        if (!json::Parse(body, v) || !v.isArray()) continue;
        std::vector<FetchItem> items;
        for (const json::Value& e : v.items) {
            if (!e.isObject()) continue;
            const json::Value* us = e.find("updateStatus");
            if (!us || !us->isBool()) continue;
            FetchItem it;
            it.title = JsonText(e, "title", 64);
            if (it.title.empty()) continue;
            it.updated = us->b;
            const json::Value* det = e.find("detected");
            it.hasDetected = det && det->isBool();
            it.detected = it.hasDetected && det->b;
            it.version = JsonText(e, "version", 40);
            it.platform = JsonText(e, "platform", 24);
            it.kind = KindOf(JsonText(e, "extype", 24));
            it.updatedAt = ParseWeaoDate(JsonText(e, "updatedDate", 40));
            it.rbx = JsonText(e, "rbxversion", 48);
            items.push_back(it);
        }
        if (items.empty()) continue;
        res->items.swap(items);
        res->ok = true;
        break;
    }

    const HWND hwnd = p->hwnd;
    delete p;
    if (!PostMessageW(hwnd, WM_APP_FETCHED, 0, (LPARAM)res)) delete res;
    return 0;
}

static void StartAnim();
static void LayoutWindow();
static void InvalidateAll();
static void SnapshotRows();

// Schedules the next check and remembers when it is due (for the countdown).
static void ArmPoll(UINT ms) {
    SetTimer(g_hwnd, TIMER_POLL, ms, nullptr);
    g_pollStart = GetTickCount64();
    g_pollDue = g_pollStart + ms;
}

static void StartFetch() {
    if (g_fetching || !g_hwnd) return;
    FetchParams* p = new FetchParams();
    p->hwnd = g_hwnd;
    if (!g_set.apiUrl.empty()) p->urls.push_back(g_set.apiUrl);
    for (const wchar_t* u : API_URLS) p->urls.push_back(u);

    KillTimer(g_hwnd, TIMER_POLL);
    HANDLE thread = CreateThread(nullptr, 0, FetchThread, p, 0, nullptr);
    if (!thread) {
        delete p;
        ArmPoll(RETRY_SECONDS * 1000);
        return;
    }
    CloseHandle(thread);
    g_fetching = true;
    g_pollDue = 0;
    StartAnim();
    InvalidateAll();
}

static void ManualCheck() {
    if (g_fetching) return;
    const DWORD now = GetTickCount();
    if (g_hasManualTick && now - g_lastManualTick < 3000) return;  // be polite to the API
    g_hasManualTick = true;
    g_lastManualTick = now;
    StartFetch();
}

// ---------------------------------------------------------------------------
// What the interface says
// ---------------------------------------------------------------------------
static bool HasStatus(const Product& p) { return p.updated >= 0 && p.tracked; }

struct Totals {
    int tracked = 0, up = 0, down = 0, detected = 0;
};

static Totals Count() {
    Totals t;
    for (const Product& p : g_products) {
        if (!HasStatus(p)) continue;
        ++t.tracked;
        if (p.updated == 1) ++t.up;
        else ++t.down;
        if (p.detected == 1) ++t.detected;
    }
    return t;
}

static std::wstring Headline(const Totals& t) {
    if (t.tracked == 0) {
        if (g_everSucceeded) return L"No products tracked";
        return g_lastFailed ? L"No data yet" : L"Checking\u2026";
    }
    return std::to_wstring(t.up) + L" of " + std::to_wstring(t.tracked) + L" updated";
}

static std::wstring MetaLine(uint32_t& color) {
    color = pal::Text3;
    if (g_lastFailed) {
        color = pal::Amber;
        return g_failText;
    }
    if (!g_lastChecked.empty()) return L"Checked " + g_lastChecked;
    return std::wstring();
}

// Seconds until the next check, or -1 when none is scheduled.
static int SecondsToPoll() {
    if (g_pollDue == 0) return -1;
    const ULONGLONG now = GetTickCount64();
    return now >= g_pollDue ? 0 : (int)((g_pollDue - now + 999) / 1000);
}

static std::wstring NextLine() {
    if (g_fetching) return L"Checking\u2026";
    const int s = SecondsToPoll();
    if (s < 0) return std::wstring();
    wchar_t buf[48];
    swprintf(buf, 48, L"%ls %d:%02d", g_lastFailed ? L"Retrying in" : L"Next check in", s / 60, s % 60);
    return buf;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static bool Wide();
static RECT SR(double x, double y, double w, double h);
static int ProductCount();
static double LogicalH();
static bool RowMatches(int i);
static int RowShown(int i);
static RECT BrandRect();
static RECT TitleButton(int n);
static RECT RailRect();
static RECT NavRect(int i);
static RECT HeroRect();
static int BehindCount();
static RECT CheckRect();
static void RefreshRbxMap();

static bool Wide() { return LogicalH() >= kRailMin; }  // the rail needs a tall window

static RECT SR(double x, double y, double w, double h) {
    RECT r = { S(x), S(y), S(x + w), S(y + h) };
    return r;
}

static int ProductCount() { return (int)g_products.size(); }
static int RowShown(int i);
static bool RowMatches(int i);
static double MainX() { return Wide() ? kRailW + 22 : 0; }
static double MainW() { return kW - MainX(); }

// The window grows with the number of products; short lists get some breathing room.
static double LogicalH() {
    const double h = kTitleH + DensHero() + 18 + DensToolbar() + ProductCount() * (DensRow() + DensGap()) + kPad;
    return h < kMinH ? kMinH : h;
}

// The panel occupies its own column, widening the window, unless the screen is
// too narrow for that; then it slides over the dashboard instead.
static bool g_panelInline = false;
static int WinW() { return S(kW) + (g_panelInline ? S(kPanelW) : 0); }
static int WinH() { return S(LogicalH()); }
static double ListTop() { return kTitleH + DensHero() + 18 + DensToolbar(); }

static bool RowMatches(int i) {
    if (g_filter == 0) return true;
    const Product& p = g_products[(size_t)i];
    if (!HasStatus(p)) return false;
    return g_filter == 1 ? p.updated == 1 : p.updated != 1;
}

// Visible rows have consecutive slots, so filtering closes the gaps.
static int RowShown(int i) {
    int slot = 0;
    for (int k = 0; k < i; ++k)
        if (RowMatches(k)) ++slot;
    return RowMatches(i) ? slot : -1;
}

static int VisibleRows() {
    int n = 0;
    for (int i = 0; i < ProductCount(); ++i) n += RowMatches(i) ? 1 : 0;
    return n;
}

static RECT BrandRect() { return SR(9, 9, 34, 34); }
// 0 close, 1 minimise, 2 settings (the gear is only needed while the rail is hidden)
static RECT TitleButton(int n) { return SR(kW - 8 - 40 - n * 44, (kTitleH - 30) / 2, 40, 30); }
static RECT RailRect() { return SR(0, 0, kRailW, LogicalH()); }
static RECT NavRect(int i) { return SR(12, 60 + i * 44, 32, 32); }
static RECT HeroRect() {
    return RECT{ S(MainX() + kPad), S(kTitleH), S(MainX() + MainW() - kPad), S(kTitleH + DensHero()) };
}
static RECT RbxStripRect() {
    const RECT h = HeroRect();
    return RECT{ h.left + S(18), h.top + S(112), h.right - S(18), h.top + S(138) };
}
static RECT CheckRect() {
    const RECT h = HeroRect();
    return RECT{ h.right - S(18 + 128), h.top + S(18), h.right - S(18), h.top + S(54) };
}
static RECT FilterRect(int k);
static RECT RowRect(int i);
static RECT BellRect(int i);
static RECT PanelRect();
static RECT PanelCloseRect();
static RECT SegBox();
static RECT SegRect(int k);
static double PanelRowY(int n);
static RECT PanelRowRect(int n);
static RECT TestRect();
static bool In(const RECT& r, POINT pt);
static int HitTest(POINT pt);
static bool Clickable(int id);
static bool RectOf(int id, RECT& r);


static RECT FilterRect(int k) { return SR(MainX() + kPad + k * 92, kTitleH + DensHero() + 18, 84, 30); }
static RECT RowRect(int i) {
    const int y = S(ListTop() + RowShown(i) * (DensRow() + DensGap()));
    return RECT{ S(MainX() + kPad), y, S(MainX() + MainW() - kPad), y + S(DensRow()) };
}
// The detection badge sits on the artwork's top-left corner. Clicking it still
// toggles this product's alerts, exactly as the old chip did.
static RECT DetRect(int i) {
    const RECT r = RowRect(i);
    const int cx = r.left + S(12), cy = r.top + S(8);  // on the tile's top-left corner
    return RECT{ cx - S(9), cy - S(9), cx + S(9), cy + S(9) };
}
static RECT BellRect(int i) { return DetRect(i); }
static void PanelMotion(int& dx, int& dy);
static double PanelLeft() { return (double)(WinW() - S(kPanelW)) / g_scale; }
static RECT PanelRect() { return RECT{ S(PanelLeft()), 0, WinW(), S(LogicalH()) }; }
static RECT PanelCloseRect() {
    int dx = 0, dy = 0;
    PanelMotion(dx, dy);
    return RECT{ WinW() - S(44) - dx, S(16) + dy, WinW() - S(16) - dx, S(44) + dy };
}
// How far the panel can scroll: its content height, less the window it sits in.
static double PanelMaxScroll() {
    const double room = LogicalH() - 16.0;
    return kPanelContentH > room ? kPanelContentH - room : 0.0;
}
static void ClampScroll() { g_scrollTo = std::max(0.0, std::min(g_scrollTo, PanelMaxScroll())); }
static RECT PanelBodyClip() {
    // the scrolled area: below the title, above the pinned footer
    const int top = S(10) + S(38);
    const int bottom = WinH() - S(10) - S(30);
    return RECT{ S(PanelLeft()), top, WinW(), bottom };
}
// Panel X/Y helpers. Both take the sheet's motion into account, so every
// control travels with the panel instead of sitting still behind it.
static int PX(double logical) {
    int dx = 0, dy = 0;
    PanelMotion(dx, dy);
    return (int)std::lround(S(PanelLeft() + logical)) - dx;
}
// Panel Y helper: logical row position -> client pixel, less the scroll offset.
static int PY(double logical) { return (int)std::lround(S(logical) - g_scrollY); }
static int PYL(double logical) { return S(logical) - (int)std::lround(g_scrollY); }

static bool In(const RECT& r, POINT pt);
static int HitTest(POINT pt);
static bool Clickable(int id);
static bool RectOf(int id, RECT& r);


static RECT SegBox() { return RECT{ PX(20), PY(76), PX(258), PY(108) }; }
static RECT DensBox() { return RECT{ PX(20), PY(138), PX(258), PY(172) }; }
static double PanelRowY(int n) { return 188.0 + n * 46.0; }
static RECT DensRect(int k) {
    const RECT b = DensBox();
    const int w = (b.right - b.left - S(4)) / 2;
    return RECT{ b.left + S(2) + w * k, b.top + S(2), b.left + S(2) + w * (k + 1), b.bottom - S(2) };
}
static const wchar_t* const kAlertTitle[4] = { L"Status changes", L"New versions",
                                               L"Detection changes", L"Roblox updates" };
static const wchar_t* const kAlertHelp[4] = { L"updated / not updated", L"a new version string",
                                              L"flagged or cleared", L"a new client build" };
static const int ACCENTS = 6;
static uint32_t AccentAt(int k) {
    static const uint32_t list[ACCENTS] = { 0x7DEEFF, 0x5B9DFF, 0x8A6BFF, 0x6FE7C4, 0xF5C46C, 0xFF8FA3 };
    return list[Clamp(k, 0, ACCENTS - 1)];
}
static int AccentIndex() {
    for (int k = 0; k < ACCENTS; ++k) {
        wchar_t buf[8];
        swprintf(buf, 8, L"%06x", AccentAt(k));
        if (g_set.accent == buf) return k;
    }
    return 0;
}
static RECT SegRect(int k) {
    const RECT b = SegBox();
    const int w = (b.right - b.left - S(4)) / SEG_COUNT;
    return RECT{ b.left + S(2) + w * k, b.top + S(2), b.left + S(2) + w * (k + 1), b.bottom - S(2) };
}
static RECT PanelRowRect(int n) {
    return RECT{ PX(10), PY(PanelRowY(n)), PX(kPanelW - 10), PY(PanelRowY(n) + 48) };
}
// the four notification switches, indented under the master one
static double AlertRowY(int n) { return 356.0 + n * 38.0; }
static RECT AlertRowRect(int n) {
    return RECT{ PX(18), PY(AlertRowY(n)), PX(kPanelW - 12), PY(AlertRowY(n) + 40) };
}
static RECT AlertSwitchRect(int n) {
    const RECT r = AlertRowRect(n);
    const int cy = (r.top + r.bottom) / 2;
    return RECT{ r.right - S(16 + 34), cy - S(9), r.right - S(16), cy + S(9) };
}
static RECT TestRect() { return RECT{ PX(20), PY(526), PX(kPanelW - 20), PY(558) }; }
static RECT SwatchRect(int k) {
    const int size = 42, gap = 10;
    const int x = PX(20) + (k % 4) * S(size + gap);
    const int y = PY(600) + (k / 4) * S(size + gap);
    return RECT{ x, y, x + S(size), y + S(size) };
}
static RECT PicButtonRect() { return RECT{ PX(20), PY(710), PX(20 + 238), PY(710 + 36) }; }
static RECT ThemeBoxRect() { return RECT{ PX(20), PY(762), PX(kPanelW - 20), PY(798) }; }
static RECT ThemeSegRect(int k) {
    const RECT b = ThemeBoxRect();
    const int w = (b.right - b.left - S(4)) / 2;
    return RECT{ b.left + S(2) + w * k, b.top + S(2), b.left + S(2) + w * (k + 1), b.bottom - S(2) };
}
static RECT AccentRect(int k) {
    const int size = 26, gap = 12;
    const int x = PX(20) + k * S(size + gap);
    const int y = PY(822);
    return RECT{ x, y, x + S(size), y + S(size) };
}
static bool In(const RECT& r, POINT pt) { return PtInRect(&r, pt) != FALSE; }

static int HitTest(POINT pt) {
    if (In(BrandRect(), pt)) return HIT_BRAND;
    if (In(TitleButton(0), pt)) return HIT_TB_CLOSE;
    if (In(TitleButton(1), pt)) return HIT_TB_MIN;
    if (Wide()) {
        if (In(NavRect(0), pt)) return HIT_NAV0;
        if (In(NavRect(1), pt)) return HIT_NAV1;
    } else if (In(TitleButton(2), pt)) {
        return HIT_TB_SETTINGS;
    }

    if (g_sheetOpen) {
        if (!In(PanelRect(), pt)) return HIT_SCRIM;
        if (In(PanelCloseRect(), pt)) return HIT_SHEET_CLOSE;
        // Rects below the header are already scrolled; anything that has slid
        // under the pinned header or footer is not clickable where it cannot be seen.
        const RECT body = PanelBodyClip();
        if (!In(body, pt)) return HIT_SHEET;
        for (int k = 0; k < SEG_COUNT; ++k)
            if (In(SegRect(k), pt)) return HIT_SEG0 + k;
        for (int n = 0; n < 3; ++n)
            if (In(PanelRowRect(n), pt)) return HIT_TGL0 + n;
        }
        for (int a = 0; a < 4; ++a) {
            if (In(AlertRowRect(a), pt)) return HIT_ALERT0 + a;
        }
        for (int d = 0; d < 2; ++d) {
            if (In(DensRect(d), pt)) return HIT_DENS0 + d;
        if (In(TestRect(), pt)) return HIT_TEST;
        for (int k = 0; k < SWATCHES; ++k)
            if (In(SwatchRect(k), pt)) return HIT_SWATCH0 + k;
        for (int k = 0; k < 2; ++k)
            if (In(ThemeSegRect(k), pt)) return HIT_THEME0 + k;
        for (int k = 0; k < ACCENTS; ++k)
            if (In(AccentRect(k), pt)) return HIT_ACCENT0 + k;
        if (In(PicButtonRect(), pt)) return HIT_PICPICK;
        return HIT_SHEET;
    }

    if (In(CheckRect(), pt)) return HIT_CHECK;
    for (int k = 0; k < 3; ++k)
        if (In(FilterRect(k), pt)) return HIT_FILTER0 + k;
    for (int i = 0; i < ProductCount(); ++i) {
        if (!RowMatches(i)) continue;
        if (In(BellRect(i), pt)) return HIT_BELL0 + i;
        if (In(RowRect(i), pt)) return HIT_ROW0 + i;
    }
    return HIT_NONE;
}

static bool Clickable(int id) {
    if (id == HIT_NONE || id == HIT_SCRIM || id == HIT_SHEET) return false;
    return !(id >= HIT_ROW0 && id < HIT_ROW0 + MAX_PRODUCTS);
}

static bool RectOf(int id, RECT& r) {
    const int n = ProductCount();
    switch (id) {
    case HIT_BRAND: r = BrandRect(); return true;
    case HIT_TB_CLOSE: r = TitleButton(0); return true;
    case HIT_TB_MIN: r = TitleButton(1); return true;
    case HIT_TB_SETTINGS: r = TitleButton(2); return true;
    case HIT_NAV0: r = NavRect(0); return true;
    case HIT_NAV1: r = NavRect(1); return true;
    case HIT_CHECK: r = CheckRect(); return true;
    case HIT_SHEET_CLOSE: r = PanelCloseRect(); return true;
    case HIT_TEST: r = TestRect(); return true;
    }
    if (id >= HIT_TGL0 && id < HIT_TGL0 + 3) { r = PanelRowRect(id - HIT_TGL0); return true; }
    if (id >= HIT_ALERT0 && id < HIT_ALERT0 + 4) { r = AlertRowRect(id - HIT_ALERT0); return true; }
    if (id >= HIT_DENS0 && id < HIT_DENS0 + 2) { r = DensRect(id - HIT_DENS0); return true; }
    if (id >= HIT_SEG0 && id < HIT_SEG0 + SEG_COUNT) { r = SegBox(); return true; }
    if (id >= HIT_FILTER0 && id < HIT_FILTER0 + 3) { r = FilterRect(id - HIT_FILTER0); return true; }
    if (id >= HIT_SWATCH0 && id < HIT_SWATCH0 + SWATCHES) { r = SwatchRect(id - HIT_SWATCH0); return true; }
    if (id >= HIT_THEME0 && id < HIT_THEME0 + 2) { r = ThemeSegRect(id - HIT_THEME0); return true; }
    if (id >= HIT_ACCENT0 && id < HIT_ACCENT0 + ACCENTS) { r = AccentRect(id - HIT_ACCENT0); return true; }
    if (id == HIT_PICPICK) { r = PicButtonRect(); return true; }
    if (id >= HIT_ROW0 && id < HIT_ROW0 + n) {
        if (!RowMatches(id - HIT_ROW0)) return false;
        r = RowRect(id - HIT_ROW0);
        return true;
    }
    if (id >= HIT_BELL0 && id < HIT_BELL0 + n) {
        if (!RowMatches(id - HIT_BELL0)) return false;
        r = RowRect(id - HIT_BELL0);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Repaint bookkeeping
// ---------------------------------------------------------------------------
static void InvalidateFront(const RECT* r) {
    if (g_hwnd) InvalidateRect(g_hwnd, r, FALSE);
}

// Marks part of the dashboard as stale; it is redrawn on the next paint.
// Separate areas stay separate, so two cards animating at opposite ends of
// the window do not force everything between them to be redrawn.
static void InvalidateDash(const RECT& r) {
    const RECT bounds = { 0, 0, WinW(), WinH() };
    RECT c;
    if (!IntersectRect(&c, &r, &bounds)) return;
    bool merged = false;
    for (RECT& d : g_dirty) {
        RECT t;
        if (IntersectRect(&t, &d, &c)) {
            UnionRect(&t, &d, &c);
            d = t;
            merged = true;
            break;
        }
    }
    if (!merged) g_dirty.push_back(c);
    if (g_dirty.size() > 12) {  // too fragmented: fall back to one bounding box
        RECT all = g_dirty[0];
        for (const RECT& d : g_dirty) {
            RECT t;
            UnionRect(&t, &all, &d);
            all = t;
        }
        g_dirty.assign(1, all);
    }
    InvalidateFront(&c);
}

static void InvalidateAll() {
    const RECT all = { 0, 0, WinW(), WinH() };
    g_dirty.assign(1, all);
    InvalidateFront(nullptr);
}

static void InvalidateId(int id) {
    RECT r;
    if (!RectOf(id, r)) return;
    InflateRect(&r, S(16), S(16));  // room for the glow around a row
    InvalidateFront(&r);
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------
static bool HotTarget(int id) {
    if (id >= HIT_ROW0 && id < HIT_ROW0 + MAX_PRODUCTS)
        return g_hot == id || g_hot == HIT_BELL0 + (id - HIT_ROW0);
    return g_hot == id;
}

static void StartAnim() {
    if (g_animOn || !g_hwnd) return;
    g_animOn = true;
    QueryPerformanceCounter(&g_animLast);
    SetTimer(g_hwnd, TIMER_ANIM, 15, nullptr);
}

// If anything below ever throws, the window must not die with it: report and
// keep running (an unhandled exception here terminates the whole app).
static void OnAnimTick() {
  try {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = g_qpf.QuadPart > 0 ? (float)((double)(now.QuadPart - g_animLast.QuadPart) / (double)g_qpf.QuadPart)
                                  : 0.016f;
    g_animLast = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    bool busy = false;

    for (int id = 1; id < HIT_COUNT; ++id) {
        g_hov[id].to = HotTarget(id) ? 1.0f : 0.0f;
        if (g_hov[id].step(dt, 18.0f)) {
            InvalidateId(id);
            busy = true;
        }
    }

    const bool on[3] = { g_set.notifications, g_set.sound, g_autostart };
    for (int a = 0; a < 4; ++a) {
        g_alertTgl[a].to = AlertOn(a) ? 1.0f : 0.0f;
        if (g_alertTgl[a].step(dt, 16.0f)) {
            const RECT r = AlertRowRect(a);
            InvalidateFront(&r);
            busy = true;
        }
    }
    for (int k = 0; k < 3; ++k) {
        g_tgl[k].to = on[k] ? 1.0f : 0.0f;
        if (g_tgl[k].step(dt, 16.0f)) {
            const RECT r = PanelRowRect(k);
            InvalidateFront(&r);
            busy = true;
        }
    }

    const int seg = IntervalIndex();
    if (seg >= 0) g_seg.to = (float)seg;
    if (g_seg.step(dt, 16.0f)) {
        const RECT r = SegBox();
        InvalidateFront(&r);
        busy = true;
    }

    g_sheet.to = g_sheetOpen ? 1.0f : 0.0f;
    if (g_sheet.step(dt, 11.0f)) {  // a touch slower than the rest: this is a big surface
        TickDirty(PanelRect());
        busy = true;
    }
    ClampScroll();
    if (std::fabs(g_scrollTo - g_scrollY) > 0.05) {
        // Eased chase, so a flick of the wheel settles instead of stopping dead.
        g_scrollY += (g_scrollTo - g_scrollY) * std::min(1.0, (double)dt * 16.0);
        if (std::fabs(g_scrollTo - g_scrollY) <= 0.05) g_scrollY = g_scrollTo;
        TickDirty(PanelRect());
        busy = true;
    }

    if (g_splashOn) {  // the wordmark runs on a clock, then hands over to the dashboard
        g_splashElapsed += (double)dt;
        const double hold = 1.05;   // it sits still for this long before it lifts
        const double lift = 0.55;   // then this long to dissolve into the checker
        if (g_splashElapsed > hold) g_splash.jump((float)cv::Clamp01((g_splashElapsed - hold) / lift));
        // The dashboard comes up underneath as the veil thins, so the handover is
        // one continuous move rather than a cut.
        g_fade.jump(g_splashElapsed > hold ? Ease(cv::Clamp01((g_splash.v - 0.20f) / 0.80f)) : 0.0f);
        if (g_splash.v >= 0.999f) {
            g_splashOn = false;
            g_splash.jump(0.0f);
            g_fade.jump(1.0f);
            InvalidateAll();
        }
        InvalidateAll();
        busy = true;
    } else if (g_fade.step(dt, 14.0f)) {  // the whole dashboard fades, like a page transition
        InvalidateFront(nullptr);
        busy = true;
    }
    if (g_bgFade < 1.0f) {
        g_bgFade = std::min(1.0f, g_bgFade + dt * 3.0f);
        InvalidateFront(nullptr);
        busy = true;
    }
    if (g_layoutNext) {  // rows settled: now the window may resize
        bool moving = false;
        for (int i = 0; i < ProductCount(); ++i) moving = moving || g_rowFrom[i] >= 0.0f;
        if (!moving) {
            g_layoutNext = false;
            LayoutWindow();
        } else {
            busy = true;
        }
    }

    if (g_fetching || g_spin != 0.0f) {  // the refresh icon always finishes its turn
        float next = g_spin + dt * 1.3f;
        if (!g_fetching && std::floor(next) > std::floor(g_spin)) next = 0.0f;
        if (next > 1000.0f) next -= 1000.0f;
        g_spin = next;
        InvalidateDash(CheckRect());
        busy = true;
    }

    for (int i = 0; i < ProductCount(); ++i) {
        if (!g_rowPos[i].step(dt, 19.0f)) continue;
        InvalidateDash(RECT{ 0, S(ListTop()), WinW(), WinH() });
        busy = true;
        if (g_rowPos[i].v >= 1.0f) g_rowFrom[i] = -1.0f;  // settled
    }
    for (int i = 0; i < ProductCount(); ++i) {
        if (g_flash[i] <= 0.0f) continue;
        g_flash[i] -= dt / 2.2f;
        if (g_flash[i] < 0.0f) g_flash[i] = 0.0f;
        InvalidateId(HIT_ROW0 + i);
        busy = true;
    }

    TickDirtyClear();
    if (!busy) {
        KillTimer(g_hwnd, TIMER_ANIM);
        g_animOn = false;
    }
  } catch (...) {
    // Nothing here is worth closing the app for; the next tick will try again.
  }
}

// ---------------------------------------------------------------------------
// Fonts and DPI
// ---------------------------------------------------------------------------
static int CALLBACK FontFoundProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *(bool*)found = true;
    return 0;  // stop enumerating
}

static bool FontInstalled(const wchar_t* face) {
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FontFoundProc, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}

// GDI exposes the semibold weight as its own family ("Segoe UI Semibold");
// fall back to regular Segoe UI in bold where that face is missing.
static HFONT MakeFont(double px, bool strong) {
    static const bool hasSemibold = FontInstalled(L"Segoe UI Semibold");
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof lf);
    lf.lfHeight = -Max(1, S(px));
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    if (strong && hasSemibold) {
        lf.lfWeight = FW_SEMIBOLD;
        lstrcpynW(lf.lfFaceName, L"Segoe UI Semibold", LF_FACESIZE);
    } else {
        lf.lfWeight = strong ? FW_BOLD : FW_NORMAL;
        lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
    }
    return CreateFontIndirectW(&lf);
}

struct FontEntry {
    int key;
    HFONT font;
};
static std::vector<FontEntry> g_fonts;

// Fonts are created on first use and kept until the scale changes.
static HFONT Font(double px, bool strong) {
    const int key = (int)(px * 4.0 + 0.5) * 2 + (strong ? 1 : 0);
    for (const FontEntry& e : g_fonts)
        if (e.key == key) return e.font;
    const FontEntry e = { key, MakeFont(px, strong) };
    g_fonts.push_back(e);
    return e.font;
}

static void ClearFonts() {
    for (const FontEntry& e : g_fonts)
        if (e.font) DeleteObject(e.font);
    g_fonts.clear();
}

static UINT DpiForWindow(HWND hwnd) {
    typedef UINT(WINAPI * Fn)(HWND);
    static const Fn fn = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn) {
        const UINT d = fn(hwnd);
        if (d) return d;
    }
    HDC dc = GetDC(hwnd);
    const int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    return d > 0 ? (UINT)d : 96;
}

// Recomputes the scale for `dpi`, then sizes the window to fit its content.
// If that would not fit the screen's work area, the interface is scaled down.
static void ApplyDpi(HWND hwnd, UINT dpi, const POINT* moveTo) {
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const int workW = mi.rcWork.right - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;

    const double base = dpi / 96.0;
    g_scale = base;
    if (workW >= 200 && workH >= 200 && (WinW() > workW || WinH() > workH)) {
        double fit = (double)workW / WinW();
        if ((double)workH / WinH() < fit) fit = (double)workH / WinH();
        if (fit < 0.5) fit = 0.5;
        g_scale = base * fit * 0.995;
    }
    ClearFonts();
    SetWindowPos(hwnd, nullptr, moveTo ? moveTo->x : 0, moveTo ? moveTo->y : 0, WinW(), WinH(),
                 SWP_NOZORDER | SWP_NOACTIVATE | (moveTo ? 0 : SWP_NOMOVE));
    InvalidateAll();
}

// Centers the window (center == true) or nudges it back inside the work area.
static void PlaceWindow(HWND hwnd, bool center) {
    RECT wr;
    GetWindowRect(hwnd, &wr);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& wa = mi.rcWork;
    const int w = wr.right - wr.left, h = wr.bottom - wr.top;
    int x = wr.left, y = wr.top;
    if (center) {
        x = wa.left + ((wa.right - wa.left) - w) / 2;
        y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    }
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}


// Applies the current scale to the window size (rows, filters and the panel
// change how tall the window needs to be).
static void LayoutWindow() {
    if (!g_hwnd) return;
    RECT r;
    GetWindowRect(g_hwnd, &r);
    int w = WinW(), h = WinH();
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& wa = mi.rcWork;
    const int workW = wa.right - wa.left;
    if (w > workW) {  // too wide for this screen: scale the whole interface down
        const double fit = (double)workW / (double)w;
        g_scale *= fit * 0.995;
        ClearFonts();
        w = WinW();
        h = WinH();
    }
    int x = r.left;
    if (x + w > wa.right) x = wa.right - w;
    if (x < wa.left) x = wa.left;
    SetWindowPos(g_hwnd, nullptr, x, r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateAll();
}

static int MonitorWidth() {
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcWork.right - mi.rcWork.left;
}

static void SaveWindowPos() {
    if (!g_hwnd || !IsWindowVisible(g_hwnd) || IsIconic(g_hwnd)) return;
    RECT r;
    GetWindowRect(g_hwnd, &r);
    if (r.left == g_savedX && r.top == g_savedY) return;
    g_savedX = r.left;
    g_savedY = r.top;
    IniSetInt(L"Window", L"X", g_savedX);
    IniSetInt(L"Window", L"Y", g_savedY);
}

// ---------------------------------------------------------------------------
// Drawing: surfaces and text
// ---------------------------------------------------------------------------
static const float kPi = 3.14159265f;

static void FreeLayer(Layer& l) {
    if (l.dc) {
        if (l.old) SelectObject(l.dc, l.old);
        DeleteDC(l.dc);
    }
    if (l.bmp) DeleteObject(l.bmp);
    l = Layer();
}

static bool CreateLayer(Layer& l, HDC ref, int w, int h) {
    FreeLayer(l);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(ref);
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    l.dc = dc;
    l.bmp = bmp;
    l.old = SelectObject(dc, bmp);
    l.s.attach((uint32_t*)bits, w, h);
    SetBkMode(dc, TRANSPARENT);
    return true;
}

// The RC monogram, packed as a PNG resource by tools/make_icon.py. It is the
// same mark the .exe, the taskbar and the tray use.
static std::vector<uint32_t> g_mark;
static int g_markW = 0, g_markH = 0;

static IStream* StreamFromResource(int id) {
    HRSRC res = FindResourceW(g_inst, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) return nullptr;
    HGLOBAL handle = LoadResource(g_inst, res);
    const DWORD size = SizeofResource(g_inst, res);
    void* data = handle ? LockResource(handle) : nullptr;
    if (!data || !size) return nullptr;
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy) return nullptr;
    void* dst = GlobalLock(copy);
    if (!dst) {
        GlobalFree(copy);
        return nullptr;
    }
    memcpy(dst, data, size);
    GlobalUnlock(copy);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(copy, TRUE, &stream))) {
        GlobalFree(copy);
        return nullptr;
    }
    return stream;
}

static bool LoadMark() {
    if (!g_mark.empty()) return true;
    bool comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    bool ok = false;
    IStream* stream = StreamFromResource(102);
    if (stream) {
        IWICImagingFactory* factory = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IWICImagingFactory, (void**)&factory))) {
            IWICBitmapDecoder* decoder = nullptr;
            if (SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                           &decoder))) {
                IWICBitmapFrameDecode* frame = nullptr;
                if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                    IWICFormatConverter* conv = nullptr;
                    if (SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
                        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                                   nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                        UINT w = 0, h = 0;
                        if (SUCCEEDED(conv->GetSize(&w, &h)) && w && h && w <= 2048 && h <= 2048) {
                            g_mark.assign((size_t)w * h, 0);
                            if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)g_mark.size() * 4,
                                                           (BYTE*)g_mark.data()))) {
                                g_markW = (int)w;
                                g_markH = (int)h;
                                ok = true;
                            }
                        }
                    }
                    if (conv) conv->Release();
                    frame->Release();
                }
                decoder->Release();
            }
            factory->Release();
        }
        stream->Release();
    }
    if (comReady) CoUninitialize();
    if (!ok) g_mark.clear();
    return ok;
}

// A small, pre-blurred copy of the monogram for the splash backdrop. It is built
// once at load: blurring 512 px per frame would cost, and blurring on the fly
// leaves visible blocks where the downsample grid falls.
static std::vector<uint32_t> g_markBlur;
static int g_blurW = 0, g_blurH = 0;

static void BuildMarkBlur() {
    g_markBlur.clear();
    if (g_mark.empty()) return;
    const int W = 88, H = 88;  // square: the tile is square
    std::vector<uint32_t> small((size_t)W * H, 0);
    for (int y = 0; y < H; ++y) {
        const int y0 = y * g_markH / H, y1 = Max(y0 + 1, (y + 1) * g_markH / H);
        for (int x = 0; x < W; ++x) {
            const int x0 = x * g_markW / W, x1 = Max(x0 + 1, (x + 1) * g_markW / W);
            uint32_t ar = 0, ag = 0, ab = 0, aa = 0, n = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    const uint32_t v = g_mark[(size_t)yy * (size_t)g_markW + (size_t)xx];
                    const uint32_t a = (v >> 24) & 0xFF;
                    ar += ((v >> 16) & 0xFF) * a; ag += ((v >> 8) & 0xFF) * a; ab += (v & 0xFF) * a; aa += a;
                    ++n;
                }
            if (n && aa)
                small[(size_t)y * W + x] = ((aa / n) << 24) | ((ar / aa) << 16) | ((ag / aa) << 8) | (ab / aa);
        }
    }
    // Three box-blur passes, horizontally then vertically, on premultiplied colour.
    const int R = 5;
    std::vector<float> a((size_t)W * H), r((size_t)W * H), g((size_t)W * H), b((size_t)W * H);
    for (size_t k = 0; k < a.size(); ++k) {
        const float al = (float)((small[k] >> 24) & 0xFF) / 255.0f;
        a[k] = al;
        r[k] = (float)((small[k] >> 16) & 0xFF) * al;
        g[k] = (float)((small[k] >> 8) & 0xFF) * al;
        b[k] = (float)(small[k] & 0xFF) * al;
    }
    auto pass = [&](bool horizontal) {
        std::vector<float> na = a, nr = r, ng = g, nb = b;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float sa = 0, sr = 0, sg = 0, sb = 0;
                int n = 0;
                for (int d = -R; d <= R; ++d) {
                    const int sx = horizontal ? Clamp(x + d, 0, W - 1) : x;
                    const int sy = horizontal ? y : Clamp(y + d, 0, H - 1);
                    const size_t k = (size_t)sy * W + sx;
                    sa += a[k]; sr += r[k]; sg += g[k]; sb += b[k]; ++n;
                }
                const size_t k = (size_t)y * W + x;
                na[k] = sa / n; nr[k] = sr / n; ng[k] = sg / n; nb[k] = sb / n;
            }
        a.swap(na); r.swap(nr); g.swap(ng); b.swap(nb);
    };
    for (int p = 0; p < 3; ++p) { pass(true); pass(false); }
    // Fade the tile's own square edge out, so it reads as haze and not a panel.
    g_markBlur.assign((size_t)W * H, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t k = (size_t)y * W + x;
            const float dx = ((float)x / (W - 1) - 0.5f) * 2.0f, dy = ((float)y / (H - 1) - 0.5f) * 2.0f;
            const float rr = std::sqrt(dx * dx + dy * dy);
            const float fall = cv::Clamp01((1.05f - rr) / 0.60f);
            const float al = a[k] * fall * fall;
            if (al <= 0.002f) continue;
            const float inv = al > 0.0001f ? 1.0f / al : 0.0f;
            const uint32_t A = (uint32_t)(cv::Clamp01(al) * 255.0f);
            const uint32_t R8 = (uint32_t)std::min(255.0f, r[k] * inv);
            const uint32_t G8 = (uint32_t)std::min(255.0f, g[k] * inv);
            const uint32_t B8 = (uint32_t)std::min(255.0f, b[k] * inv);
            g_markBlur[k] = (A << 24) | (R8 << 16) | (G8 << 8) | B8;
        }
    g_blurW = W;
    g_blurH = H;
}

// The pre-blurred monogram, scaled up with a bilinear filter: soft, never blocky.
static void DrawMarkBlurred(cv::Surface& s, float cx, float cy, float size, float alpha) {
    if (g_markBlur.empty() || size < 8.0f) return;
    const int w = Max(2, (int)size), h = Max(2, (int)size);
    const int x0 = (int)std::lround(cx - w / 2.0f), y0 = (int)std::lround(cy - h / 2.0f);
    for (int y = Max(0, y0); y < std::min(y0 + h, s.h); ++y) {
        const float fy = (float)(y - y0) / (float)h * (float)(g_blurH - 1);
        const int iy = Clamp((int)fy, 0, g_blurH - 2);
        const float ty = cv::Clamp01(fy - (float)iy);
        uint32_t* row = s.row(y);
        for (int x = Max(0, x0); x < std::min(x0 + w, s.w); ++x) {
            const float fx = (float)(x - x0) / (float)w * (float)(g_blurW - 1);
            const int ix = Clamp((int)fx, 0, g_blurW - 2);
            const float tx = cv::Clamp01(fx - (float)ix);
            const uint32_t a11 = g_markBlur[(size_t)iy * g_blurW + ix], a12 = g_markBlur[(size_t)iy * g_blurW + ix + 1];
            const uint32_t a21 = g_markBlur[(size_t)(iy + 1) * g_blurW + ix], a22 = g_markBlur[(size_t)(iy + 1) * g_blurW + ix + 1];
            auto ch = [&](int shift) {
                const float t = (float)((a11 >> shift) & 0xFF) * (1 - tx) + (float)((a12 >> shift) & 0xFF) * tx;
                const float u = (float)((a21 >> shift) & 0xFF) * (1 - tx) + (float)((a22 >> shift) & 0xFF) * tx;
                return t * (1 - ty) + u * ty;
            };
            const float a = (ch(24) / 255.0f) * alpha;
            if (a <= 0.004f) continue;
            const cv::Color c = { ch(16), ch(8), ch(0), a };
            cv::Blend(row[x], c, 1.0f);
        }
    }
}

// Draws the monogram centred in `box`, keeping its aspect, at `alpha`.
static void DrawMark(cv::Surface& s, float cx, float cy, float size, float alpha) {
    if (g_mark.empty() || size < 2.0f) return;
    const float aspect = (float)g_markW / (float)std::max(1, g_markH);
    const float w = size * aspect, h = size;
    const int x0 = (int)std::lround(cx - w / 2.0f), y0 = (int)std::lround(cy - h / 2.0f);
    const int x1 = (int)std::lround(cx + w / 2.0f), y1 = (int)std::lround(cy + h / 2.0f);
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    const float sx = (float)g_markW / (float)(x1 - x0), sy = (float)g_markH / (float)(y1 - y0);
    for (int y = Max(y0, s.cy0); y < std::min(y1, s.cy1); ++y) {
        uint32_t* row = s.row(y);
        const int srcY = Clamp((int)((y - y0) * sy), 0, g_markH - 1);
        for (int x = Max(x0, s.cx0); x < std::min(x1, s.cx1); ++x) {
            const int srcX = Clamp((int)((x - x0) * sx), 0, g_markW - 1);
            const uint32_t v = g_mark[(size_t)srcY * (size_t)g_markW + (size_t)srcX];
            const float a = (float)(v >> 24) / 255.0f * alpha;
            if (a <= 0.002f) continue;
            const cv::Color c = { (float)((v >> 16) & 0xFF), (float)((v >> 8) & 0xFF), (float)(v & 0xFF), a };
            cv::Blend(row[x], c, 1.0f);
        }
    }
}

// A picture of the user's own, decoded with WIC and cropped to fill the window.
static std::wstring g_imageLoaded;
static std::vector<uint32_t> g_image;
static int g_imageW = 0, g_imageH = 0;

static void ReleaseImage() {
    g_image.clear();
    g_imageW = g_imageH = 0;
    g_imageLoaded.clear();
}

static bool LoadImageFrame(IStream* stream, UINT width, UINT height, float scale) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&factory)))
        return false;
    bool ok = false;
    IWICBitmapDecoder* decoder = nullptr;
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
            IWICBitmapScaler* scaler = nullptr;
            const UINT sw = Max(1, (UINT)((float)width * scale));
            const UINT sh = Max(1, (UINT)((float)height * scale));
            if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant))) {
                IWICFormatConverter* converter = nullptr;
                if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA,
                                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                                    WICBitmapPaletteTypeCustom))) {
                    g_image.assign((size_t)sw * sh, 0);
                    if (SUCCEEDED(converter->CopyPixels(nullptr, sw * 4, (UINT)g_image.size() * 4,
                                                        (BYTE*)g_image.data()))) {
                        for (uint32_t& v : g_image) v |= 0xFF000000u;  // the picture is always opaque
                        g_imageW = (int)sw;
                        g_imageH = (int)sh;
                        ok = true;
                    }
                }
                if (converter) converter->Release();
            }
            if (scaler) scaler->Release();
            frame->Release();
        }
        decoder->Release();
    }
    factory->Release();
    return ok;
}

static bool LoadImageFile(const std::wstring& path, UINT width, UINT height) {
    if (path.empty() || path == g_imageLoaded) return !g_image.empty();
    ReleaseImage();
    // WIC on Windows 10/11 is an inbox component; without it the app keeps its
    // built-in backgrounds.
    bool comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    IWICImagingFactory* probe = nullptr;
    const bool wic = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_IWICImagingFactory, (void**)&probe));
    if (probe) probe->Release();
    if (!wic) {
        if (comReady) CoUninitialize();
        return false;
    }
    IStream* stream = nullptr;
    if (SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr,
                                         &stream))) {
        // Shrink big pictures until the decode fits comfortably in memory.
        float scale = 1.0f;
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (LoadImageFrame(stream, width, height, scale)) break;
            scale *= 0.5f;
        }
        stream->Release();
    }
    if (comReady) CoUninitialize();
    if (!g_image.empty()) g_imageLoaded = path;
    return !g_image.empty();
}

// The built-in scenes, in swatch order.
static const wchar_t* const kScenes[7] = { L"aurora", L"midnight", L"ember", L"forest", L"carbon",
                                           L"ocean",   L"ice" };
static std::wstring SceneId(int k) { return (k >= 0 && k < 7) ? kScenes[k] : L"aurora"; }

// "#rrggbb" or "rrggbb" -> 0xRRGGBB, falling back to the accent.
static uint32_t ColourFromSetting() {
    std::wstring t = g_set.bgColor;
    if (!t.empty() && t[0] == L'#') t = t.substr(1);
    if (t.size() != 6) return pal::Accent;
    wchar_t* end = nullptr;
    const unsigned long v = wcstoul(t.c_str(), &end, 16);
    return (end && *end == 0) ? (uint32_t)v : pal::Accent;
}

static std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Six built-in scenes, each two colours and a mood. They are cheap to draw.
static void PaintScene(cv::Surface& s, int w, int h, const std::wstring& id) {
    struct Scene {
        const wchar_t* id;
        uint32_t bg, a, b, c;
        float ka, kb, kc;
    };
    static const Scene scenes[] = {
        { L"aurora",   0x08090C, 0x4C8DFF, 0x7C5CFF, 0x27D3C0, 0.30f, 0.26f, 0.16f },
        { L"midnight", 0x05060A, 0x2B4C9E, 0x6C3BD6, 0x1B7FA8, 0.26f, 0.24f, 0.14f },
        { L"ember",    0x0B0808, 0xFF6B4A, 0xB23A6B, 0xF0B243, 0.28f, 0.24f, 0.14f },
        { L"forest",   0x060A09, 0x2FBF8F, 0x1F6FA8, 0x8FD44A, 0.26f, 0.22f, 0.14f },
        { L"carbon",   0x0A0A0C, 0x3A3F4A, 0x2A2E38, 0x505866, 0.22f, 0.20f, 0.12f },
        { L"ocean",    0x05090D, 0x1E6BD6, 0x2AA7C4, 0x4C8DFF, 0.28f, 0.24f, 0.15f },
        { L"ice",      0x07090B, 0x7DEEFF, 0x4A8FD6, 0xD8F6FF, 0.30f, 0.24f, 0.16f },
    };
    const Scene* sc = &scenes[0];
    for (const Scene& q : scenes)
        if (id == q.id) sc = &q;
    cv::FillRect(s, 0, 0, w, h, cv::Hex(sc->bg));
    cv::Glow(s, U(120), U(-40), U(470), cv::Hex(sc->a, sc->ka));
    cv::Glow(s, (float)w - U(30), U(-60), U(380), cv::Hex(sc->b, sc->kb));
    cv::Glow(s, U(40), (float)h * 0.92f, U(380), cv::Hex(sc->c, sc->kc));
    // A long diagonal ribbon of light behind the list, so the panels have something to bend
    cv::Glow(s, (float)w * 0.70f, (float)h * 0.52f, U(340), cv::Hex(sc->a, sc->ka * 0.70f));
    cv::Glow(s, (float)w * 0.16f, (float)h * 0.60f, U(300), cv::Hex(sc->b, sc->kb * 0.60f));
    cv::Glow(s, (float)w * 0.45f, (float)h * 0.86f, U(300), cv::Hex(sc->c, sc->kc * 0.55f));
    const float cx = U(kW - kPad - 80), cy = U(kTitleH + DensHero() / 2);
    for (int k = 1; k <= 3; ++k)
        cv::StrokeArc(s, cx, cy, U(70.0 + k * 32.0), (float)Max(1, S(1)), 0.0f, 6.28318531f,
                      cv::Hex(pal::White, 0.02f));
}

// A picture background: cover-fit, then dimmed and tinted so text stays legible.
static bool GlassActive();  // the glass theme is on (defined with the glass engine below)

static void PaintImage(cv::Surface& s, int w, int h) {
    if (!g_imageW || !g_imageH) return;
    const float sc = std::max((float)w / (float)g_imageW, (float)h / (float)g_imageH);
    const float ox = ((float)w - (float)g_imageW * sc) / 2.0f;
    const float oy = ((float)h - (float)g_imageH * sc) / 2.0f;
    for (int y = 0; y < h; ++y) {
        const int sy = Clamp((int)(((float)y + 0.5f - oy) / sc), 0, g_imageH - 1);
        uint32_t* row = s.row(y);
        for (int x = 0; x < w; ++x) {
            const int sx = Clamp((int)(((float)x + 0.5f - ox) / sc), 0, g_imageW - 1);
            row[x] = g_image[(size_t)sy * g_imageW + sx];
        }
    }
    // A gentle vignette and scrim: enough to seat the interface, light enough that
    // the glass panels still read as glass over the picture.
    for (int y = 0; y < h; ++y) {
        uint32_t* row = s.row(y);
        const float fy = (float)y / std::max(1.0f, (float)h - 1.0f);
        for (int x = 0; x < w; ++x) {
            const float fx = (float)x / std::max(1.0f, (float)w - 1.0f);
            const float dx = (fx - 0.5f) * 1.35f, dy = (fy - 0.5f) * 1.15f;
            const float vig = cv::Clamp01((dx * dx + dy * dy) * 1.5f);
            // Flat panels have no blur to calm the picture down, so it gets a heavier scrim there.
            const float base = GlassActive() ? 0.30f : 0.62f;
            cv::Blend(row[x], cv::Hex(0x05070B, base + (1.0f - base * 0.6f) * 0.42f * vig), 1.0f);
        }
    }
    cv::Glow(s, U(150), U(-20), U(430), cv::Hex(pal::Accent, 0.07f));
}

// The window background never changes at a given size, so it is drawn once
// (and again whenever the background setting changes).
static void BuildBackground(int w, int h) {
    g_bgPrev = g_bg;  // kept for the cross-fade
    if (g_bgPrev.size() != (size_t)w * (size_t)h) g_bgPrev.clear();
    g_bg.assign((size_t)w * (size_t)h, 0);
    g_bgW = w;
    g_bgH = h;
    ++g_bgGen;
    cv::Surface s;
    s.attach(g_bg.data(), w, h);
    if (g_set.background == L"image") {
        LoadImageFile(g_set.imagePath, (UINT)w, (UINT)h);
        if (!g_image.empty()) {
            PaintImage(s, w, h);
            return;
        }
    }
    if (g_set.background == L"colour") {
        const uint32_t c = ColourFromSetting();
        cv::FillRect(s, 0, 0, w, h, cv::Hex(pal::Bg));
        cv::Glow(s, U(160), U(-30), U(460), cv::Hex(c, 0.30f));
        cv::Glow(s, (float)w - U(80), (float)h * 0.8f, U(380), cv::Hex(c, 0.16f));
        cv::Glow(s, U(80), (float)h * 0.95f, U(300), cv::Hex(0xFFFFFF, 0.04f));
    } else {
        PaintScene(s, w, h, g_set.background == L"image" ? std::wstring(L"aurora") : g_set.background);
    }
}

static bool EnsureLayers(HDC ref, int w, int h) {
    if (g_dash.dc && g_front.dc && g_dash.s.w == w && g_dash.s.h == h) return true;
    if (!CreateLayer(g_dash, ref, w, h) || !CreateLayer(g_front, ref, w, h)) {
        FreeLayer(g_dash);
        FreeLayer(g_front);
        return false;
    }
    BuildBackground(w, h);
    const RECT all = { 0, 0, w, h };
    g_dirty.assign(1, all);
    InvalidateFront(nullptr);
    return true;
}

static COLORREF Ref(uint32_t hex) { return RGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF); }

// GDI text cannot be alpha-blended, so fades are colour mixes: `amount` of
// `fg` over `bg`.
static COLORREF RefMix(uint32_t fg, uint32_t bg, float amount) {
    const cv::Color c = cv::Mix(cv::Hex(bg), cv::Hex(fg), cv::Clamp01(amount));
    return RGB((int)(c.r + 0.5f), (int)(c.g + 0.5f), (int)(c.b + 0.5f));
}

static void Text(HDC dc, HFONT font, COLORREF color, const RECT& box, const std::wstring& text,
                 UINT format = DT_LEFT) {
    if (text.empty() || box.right <= box.left) return;
    RECT rc = box;
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), (int)text.size(), &rc, format | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, old);
}

static void TextWrapped(HDC dc, HFONT font, COLORREF color, const RECT& box, const std::wstring& text) {
    RECT rc = box;
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), (int)text.size(), &rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
}

// Wide-tracked small caps for eyebrow labels (the reference's letter-spacing).
static void TextSpaced(HDC dc, HFONT font, COLORREF color, int x, int baselineCenterY,
                       const std::wstring& text, int track) {
    if (text.empty()) return;
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    int cx = x;
    for (wchar_t ch : text) {
        std::wstring one(1, ch);
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32W(dc, one.c_str(), 1, &sz);
        TextOutW(dc, cx, baselineCenterY - sz.cy / 2, one.c_str(), 1);
        cx += sz.cx + track;
    }
    SelectObject(dc, old);
}

static int TextSpacedW(HDC dc, HFONT font, const std::wstring& text, int track) {
    HGDIOBJ old = SelectObject(dc, font);
    int total = 0;
    for (wchar_t ch : text) {
        std::wstring one(1, ch);
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32W(dc, one.c_str(), 1, &sz);
        total += sz.cx + track;
    }
    SelectObject(dc, old);
    return total - (text.empty() ? 0 : track);
}

static int TextW(HDC dc, HFONT font, const std::wstring& text) {
    if (text.empty()) return 0;
    SIZE sz = { 0, 0 };
    HGDIOBJ old = SelectObject(dc, font);
    GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

static void SetTextClip(HDC dc, const RECT& clip) {
    SelectClipRgn(dc, nullptr);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
}

static float Mid(int a, int b) { return (float)(a + b) / 2.0f; }
static float FW(const RECT& r) { return (float)(r.right - r.left); }
static float FH(const RECT& r) { return (float)(r.bottom - r.top); }

static void RoundBox(cv::Surface& s, const RECT& r, float radius, const cv::Color& c) {
    cv::FillRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), radius, c);
}

static void RoundOutline(cv::Surface& s, const RECT& r, float radius, float width, const cv::Color& c) {
    cv::StrokeRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), radius, width, c);
}

static float Hairline() { return (float)Max(1, S(1)); }

// ---------------------------------------------------------------------------
// Drawing: icons (line icons on a small grid, scaled with the interface)
// ---------------------------------------------------------------------------

// --- Icon set -------------------------------------------------------------
// Every glyph is drawn in a 13-unit box with the same stroke weight, so they
// read as one family at 16 px on a 100 % display.

static void IconGear(cv::Surface& s, float cx, float cy, const cv::Color& c) {
    // A cog with a hollow hub: eight trapezoid teeth around a ring.
    std::vector<cv::Pt> p;
    const float ro = U(6.4), ri = U(4.6), tooth = 13.0f;
    for (int k = 0; k < 8; ++k) {
        const float base = (float)k * 45.0f;
        const float deg[4] = { base - tooth, base - tooth * 0.62f, base + tooth * 0.62f, base + tooth };
        const float rad[4] = { ri, ro, ro, ri };
        for (int j = 0; j < 4; ++j) {
            const float a = deg[j] * kPi / 180.0f;
            p.push_back({ cx + rad[j] * std::cos(a), cy + rad[j] * std::sin(a) });
        }
    }
    cv::StrokePath(s, p.data(), (int)p.size(), true, U(1.35), c);
    cv::StrokeArc(s, cx, cy, U(2.4), U(1.35), 0.0f, 2.0f * kPi, c);
}

static void IconMinimize(cv::Surface& s, float cx, float cy, const cv::Color& c) {
    cv::StrokeLine(s, cx - U(5), cy, cx + U(5), cy, U(1.25), c);
}

static void IconClose(cv::Surface& s, float cx, float cy, const cv::Color& c) {
    const float d = U(4.5);
    cv::StrokeLine(s, cx - d, cy - d, cx + d, cy + d, U(1.25), c);
    cv::StrokeLine(s, cx + d, cy - d, cx - d, cy + d, U(1.25), c);
}

// A clean two-arrow refresh: two arcs with their own heads.
static void IconRefresh(cv::Surface& s, float cx, float cy, float turns, const cv::Color& c) {
    const float rad = U(5.2), w = U(1.4);
    const float base = turns * 2.0f * kPi;
    for (int k = 0; k < 2; ++k) {
        const float mid = base + (float)k * kPi;
        const float start = mid - 1.42f, sweep = 2.42f;
        cv::StrokeArc(s, cx, cy, rad, w, start, sweep, c);
        const float a = start + sweep;
        const float nx = std::cos(a), ny = std::sin(a);
        const float tx = -ny, ty = nx;
        const float ex = cx + rad * nx, ey = cy + rad * ny;
        const cv::Pt tri[3] = {
            { ex + tx * U(2.9), ey + ty * U(2.9) },
            { ex + nx * U(2.6) - tx * U(0.8), ey + ny * U(2.6) - ty * U(0.8) },
            { ex - nx * U(2.6) - tx * U(0.8), ey - ny * U(2.6) - ty * U(0.8) },
        };
        cv::FillConvex(s, tri, 3, c);
    }
}

static void IconPicture(cv::Surface& s, float cx, float cy, const cv::Color& c) {
    const float w = U(6.8), h = U(5.2), st = U(1.3);
    cv::StrokeRoundRect(s, cx - w, cy - h, w * 2, h * 2, U(2.2), st, c);
    cv::FillCircle(s, cx - w * 0.38f, cy - h * 0.36f, U(1.15f), c);
    cv::StrokeLine(s, cx - w * 0.72f, cy + h * 0.55f, cx - w * 0.05f, cy - h * 0.12f, st, c);
    cv::StrokeLine(s, cx - w * 0.05f, cy - h * 0.12f, cx + w * 0.30f, cy + h * 0.26f, st, c);
    cv::StrokeLine(s, cx + w * 0.30f, cy + h * 0.26f, cx + w * 0.72f, cy - h * 0.22f, st, c);
}

// Status view: a grid of rounded tiles, drawn as filled tiles with a gap.
static void IconGrid(cv::Surface& s, float cx, float cy, const cv::Color& c) {
    const float d = U(4.1f), r = U(1.7f);
    for (int k = 0; k < 4; ++k) {
        const float x = cx + (k % 2 ? d : -d), y = cy + (k / 2 ? d : -d);
        cv::FillRoundRect(s, x - r, y - r, r * 2, r * 2, U(1.7f), c);
    }
}

// Settings: three sliders with knobs at different points.
static void IconSliders(cv::Surface& s, float cx, float cy, const cv::Color& c) {
    const float w = U(6.0f), st = U(1.25);
    const float lanes[3] = { U(-4.3f), 0.0f, U(4.3f) };
    const float knobs[3] = { U(2.2f), U(-2.8f), U(3.0f) };
    for (int k = 0; k < 3; ++k) {
        cv::StrokeLine(s, cx - w, cy + lanes[k], cx + w, cy + lanes[k], st, c);
        cv::FillCircle(s, cx + knobs[k], cy + lanes[k], U(1.75f), c);
        cv::FillCircle(s, cx + knobs[k], cy + lanes[k], U(1.75f), c);
    }
}


static void DrawSwitch(cv::Surface& s, const RECT& r, float t, float alpha) {
    const float w = FW(r), h = FH(r);
    cv::FillRoundRect(s, (float)r.left, (float)r.top, w, h, h / 2.0f,
                      cv::Fade(cv::Mix(cv::Hex(pal::ToggleOff), cv::Hex(pal::Accent), t), alpha));
    const float inset = U(3), kr = h / 2.0f - inset;
    const float kx = (float)r.left + inset + kr + (w - 2.0f * inset - 2.0f * kr) * t;
    cv::FillCircle(s, kx, (float)r.top + h / 2.0f, kr,
                   cv::Fade(cv::Mix(cv::Hex(0xB9BEC8), cv::Hex(pal::White), t), alpha));
}

// ---------------------------------------------------------------------------
// Drawing: product artwork
// ---------------------------------------------------------------------------
struct Logo {
    int size = 0;
    std::vector<uint32_t> px;  // size * size, 0xAARRGGBB
};

static Logo g_logos[LOGO_COUNT];

// Reads res/logos.bin: "RCL1", tile count, tile size, then RGBA tiles.
static bool LoadLogos() {
    HRSRC res = FindResourceW(g_inst, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!res) return false;
    const DWORD size = SizeofResource(g_inst, res);
    HGLOBAL mem = LoadResource(g_inst, res);
    const BYTE* data = mem ? (const BYTE*)LockResource(mem) : nullptr;
    if (!data || size < 12 || memcmp(data, "RCL1", 4) != 0) return false;
    DWORD count = 0, tile = 0;
    memcpy(&count, data + 4, 4);
    memcpy(&tile, data + 8, 4);
    if (tile < 8 || tile > 512) return false;
    const size_t each = (size_t)tile * tile * 4;
    if (12 + each * count > size) return false;
    for (DWORD k = 0; k < count && k < (DWORD)LOGO_COUNT; ++k) {
        Logo& l = g_logos[k];
        l.size = (int)tile;
        l.px.resize((size_t)tile * tile);
        const BYTE* src = data + 12 + each * k;
        for (size_t i = 0; i < l.px.size(); ++i) {
            const BYTE* p = src + i * 4;
            l.px[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
    return true;
}

// Bilinear sample of the artwork into `dst`. Rounded corners are cut out of the
// alpha channel, so the tile sits on any background.
static void DrawLogo(cv::Surface& s, const RECT& dst, const Logo& logo, float alpha, bool round) {
    const int w = dst.right - dst.left, h = dst.bottom - dst.top;
    if (!logo.size || w <= 0 || h <= 0 || alpha <= 0.0f) return;
    if (!s.touches((float)dst.left, (float)dst.top, (float)w, (float)h)) return;
    const float n = (float)logo.size, radius = std::min((float)w, (float)h) * 0.22f;
    const cv::RRect rr((float)dst.left, (float)dst.top, (float)w, (float)h, round ? radius : 0.0f);
    (void)0;
    for (int y = std::max(s.cy0, (int)dst.top); y < std::min(s.cy1, (int)dst.bottom); ++y) {
        const float srcY = ((float)(y - dst.top) + 0.5f) * n / (float)h - 0.5f;
        const int y0 = Clamp((int)std::floor(srcY), 0, logo.size - 1), y1 = Clamp(y0 + 1, 0, logo.size - 1);
        const float fy = cv::Clamp01(srcY - std::floor(srcY));
        uint32_t* row = s.row(y);
        for (int x = std::max(s.cx0, (int)dst.left); x < std::min(s.cx1, (int)dst.right); ++x) {
            const float srcX = ((float)(x - dst.left) + 0.5f) * n / (float)w - 0.5f;
            const int x0 = Clamp((int)std::floor(srcX), 0, logo.size - 1), x1 = Clamp(x0 + 1, 0, logo.size - 1);
            const float fx = cv::Clamp01(srcX - std::floor(srcX));
            const uint32_t p00 = logo.px[(size_t)y0 * logo.size + x0], p10 = logo.px[(size_t)y0 * logo.size + x1];
            const uint32_t p01 = logo.px[(size_t)y1 * logo.size + x0], p11 = logo.px[(size_t)y1 * logo.size + x1];
            float ch[4];
            for (int c = 0; c < 4; ++c) {
                const int shift = c == 3 ? 24 : 16 - c * 8;
                const float a = (float)((p00 >> shift) & 0xFF), b = (float)((p10 >> shift) & 0xFF);
                const float d = (float)((p01 >> shift) & 0xFF), e = (float)((p11 >> shift) & 0xFF);
                ch[c] = (a + (b - a) * fx) * (1.0f - fy) + (d + (e - d) * fx) * fy;
            }
            float a = ch[3] / 255.0f;
            if (round) a *= cv::Clamp01(0.5f - rr.dist((float)x + 0.5f, (float)y + 0.5f));
            a *= alpha;
            if (a <= 0.004f) continue;
            cv::Color c;
            c.r = ch[0];
            c.g = ch[1];
            c.b = ch[2];
            c.a = std::min(1.0f, a);
            cv::Blend(row[x], c, 1.0f);
        }
    }
}

// The artwork that belongs to a product (matched by name), or nullptr.
static const Logo* LogoFor(const Product& p) {
    // Seliware's artwork stays in the atlas so the indices do not shift, but the
    // product is no longer tracked.
    static const char* const names[LOGO_COUNT] = { "volt", "wave", "potassium", "isaeva", "matcha",
                                                   "serotonin", "", "lucid", "axis", "codex" };
    for (int k = 0; k < LOGO_COUNT; ++k) {
        const std::wstring want = Utf8ToWide(names[k]);
        if (p.key == want && g_logos[k].size) return &g_logos[k];
    }
    return nullptr;
}

// Hue used for monograms and decorative tints, keyed by the product name.
static float TileHue(const Product& p) {
    static const struct {
        const wchar_t* key;
        float hue;
    } known[] = { { L"volt", 250 },   { L"wave", 206 },      { L"potassium", 258 }, { L"isaeva", 230 },
                  { L"matcha", 214 }, { L"serotonin", 244 }, { L"codex", 254 },
                  { L"lucid", 238 },  { L"axis", 210 } };
    for (const auto& k : known)
        if (p.key == k.key) return k.hue;
    unsigned h = 2166136261u;
    for (wchar_t c : p.key) {
        h ^= (unsigned)c;
        h *= 16777619u;
    }
    return 206.0f + (float)(h % 9u) * 6.5f;
}

// Monogram used when a product has no artwork (its name is not one we ship).
static void Monogram(cv::Surface& s, const RECT& dst, const Product& p, bool live) {
    const float hue = TileHue(p);
    const cv::Color a = live ? cv::Hsl(hue, 0.85f, 0.66f) : cv::Hsl(hue, 0.12f, 0.35f);
    const cv::Color b = live ? cv::Hsl(hue + 14.0f, 0.70f, 0.46f) : cv::Hsl(hue + 14.0f, 0.12f, 0.24f);
    const float w = FW(dst), h = FH(dst);
    cv::FillRoundRectFn(s, (float)dst.left, (float)dst.top, w, h, U(10), [&](float px, float py) {
        return cv::Mix(a, b, cv::Clamp01(((px - (float)dst.left) + (py - (float)dst.top)) / (w + h)));
    });
}

// ---------------------------------------------------------------------------
// Glass surfaces
// ---------------------------------------------------------------------------
// "Liquid glass" is drawn in two parts. The blur and the light are painted into
// the dashboard layer; the text that sits on top is mixed down towards the
// blurred backdrop, because GDI text cannot be alpha-blended after the fact.
static float g_textFade = 1.0f;  // how much the current text is faded into the glass

struct GlassStyle {
    float blur = 1.0f;       // 0 = clear, 1 = fully frosted
    float tint = 0.46f;      // weight of the panel colour over the blur
    float topLight = 0.075f;  // white sheen down from the top edge
    float edge = 0.13f;       // border strength
    float rim = 0.30f;        // refraction: the backdrop shows through near the edge
};

// Blurs a box and leaves it in `out`; the source is untouched so both can be
// combined into a refractive edge.
static void BlurCopy(const cv::Surface& s, int x0, int y0, int x1, int y1, int radius, int passes,
                     std::vector<uint32_t>& out, RECT* box = nullptr) {
    x0 = Max(x0, s.cx0);
    y0 = Max(y0, s.cy0);
    x1 = std::min(x1, s.cx1);
    y1 = std::min(y1, s.cy1);
    const int w = x1 - x0, h = y1 - y0;
    out.clear();
    if (box) *box = RECT{ x0, y0, x1, y1 };
    if (w < 3 || h < 3 || radius < 1 || passes < 1) return;
    out.resize((size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y)
        memcpy(out.data() + (size_t)y * w, s.row(y0 + y) + x0, (size_t)w * sizeof(uint32_t));

    auto channel = [](uint32_t v, int c) { return (int)((v >> (16 - c * 8)) & 0xFF); };
    std::vector<int> line((size_t)std::max(w, h), 0);
    const int window = radius * 2 + 1;
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < h; ++y) {  // horizontal
            uint32_t* row = out.data() + (size_t)y * w;
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int x = -radius; x <= radius; ++x) sum += channel(row[Clamp(x, 0, w - 1)], c);
                for (int x = 0; x < w; ++x) {
                    line[(size_t)x] = sum / window;
                    sum -= channel(row[Clamp(x - radius, 0, w - 1)], c);
                    sum += channel(row[Clamp(x + radius + 1, 0, w - 1)], c);
                }
                for (int x = 0; x < w; ++x) {
                    const int v = line[(size_t)x];
                    row[x] = (row[x] & ~(0xFFu << (16 - c * 8))) | ((uint32_t)Clamp(v, 0, 255) << (16 - c * 8));
                }
            }
        }
        for (int x = 0; x < w; ++x) {  // vertical
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int y = -radius; y <= radius; ++y) sum += channel(out[(size_t)Clamp(y, 0, h - 1) * w + x], c);
                for (int y = 0; y < h; ++y) {
                    line[(size_t)y] = sum / window;
                    sum -= channel(out[(size_t)Clamp(y - radius, 0, h - 1) * w + x], c);
                    sum += channel(out[(size_t)Clamp(y + radius + 1, 0, h - 1) * w + x], c);
                }
                for (int y = 0; y < h; ++y) {
                    uint32_t& v = out[(size_t)y * w + x];
                    v = (v & ~(0xFFu << (16 - c * 8))) |
                        ((uint32_t)Clamp(line[(size_t)y], 0, 255) << (16 - c * 8));
                }
            }
        }
    }
}

static GlassStyle g_style;
static bool GlassActive() { return g_style.blur > 0.0f; }

static uint32_t TintColor(uint32_t bg, float tint) {
    return cv::Pack(cv::Mix(cv::Hex(bg), cv::Hex(pal::Card), tint));
}

// Frosted panel: blur what is behind it, tint it, then add the edges that make
// glass read as glass (a bright top rim, a soft bottom rim, a light sweep).
static void GlassPanel(cv::Surface& s, const RECT& r, float radius, uint32_t tint, const GlassStyle& st,
                       bool heavy) {
    const int x0 = r.left, y0 = r.top, x1 = r.right, y1 = r.bottom;
    if (x1 - x0 < 4 || y1 - y0 < 4) return;
    if (!s.touches((float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0))) return;
    const cv::RRect rr((float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0), radius);

    std::vector<uint32_t> blurred;
    RECT blurBox = { 0, 0, 0, 0 };
    float scrim = 0.0f;
    if (st.blur > 0.0f) {
        BlurCopy(s, x0, y0, x1, y1, heavy ? Max(2, S(3)) : Max(1, S(2)), heavy ? 2 : 1, blurred, &blurBox);
        if (!blurred.empty()) {  // a bright backdrop gets a little more tint, so text stays legible
            double sum = 0;
            for (uint32_t v : blurred)
                sum += 0.2126 * (double)((v >> 16) & 0xFF) + 0.7152 * (double)((v >> 8) & 0xFF) +
                       0.0722 * (double)(v & 0xFF);
            const float lum = (float)(sum / (double)blurred.size());
            scrim = cv::Clamp01((lum - 80.0f) / 150.0f) * 0.30f;
        }
    }

    const cv::Color t(tint ? cv::Hex(tint) : cv::Hex(pal::Card));
    const float bw = (float)(x1 - x0), bh = (float)(y1 - y0);
    const float rimW = std::min(U(14), std::min(bw, bh) * 0.32f);  // width of the refracting band
    for (int y = std::max(s.cy0, y0); y < std::min(s.cy1, y1); ++y) {
        uint32_t* row = s.row(y);
        for (int x = std::max(s.cx0, x0); x < std::min(s.cx1, x1); ++x) {
            const float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            const float cov = cv::Clamp01(0.5f - rr.dist(fx, fy));
            if (cov <= 0.0f) continue;
            const cv::Color orig = cv::Hex(row[x]);
            cv::Color inside;
            // The blur is clipped to the surface it was taken from, so index it by
            // that box -- never by the panel rect, which can reach past it.
            const bool haveBlur = st.blur > 0.0f && !blurred.empty() && x >= blurBox.left &&
                                  x < blurBox.right && y >= blurBox.top && y < blurBox.bottom;
            if (haveBlur) {
                const size_t bw2 = (size_t)(blurBox.right - blurBox.left);
                const uint32_t bv = blurred[(size_t)(y - blurBox.top) * bw2 + (size_t)(x - blurBox.left)];
                // Liquid glass: frosted and tinted in the middle, clearer right at the edge
                // so the backdrop seems to bend through the rim.
                const float edge = cv::Clamp01(rr.dist(fx, fy) / std::max(1.0f, rimW));
                const float weight = std::min(0.96f, st.tint * (0.55f + 0.45f * st.blur) + scrim) -
                                     st.rim * (1.0f - edge) * (1.0f - edge);
                inside = cv::Mix(cv::Hex(bv), t, cv::Clamp01(weight));
            } else {
                inside = t;
            }
            const cv::Color outc =
                st.blur > 0.0f
                    ? cv::Mix(inside, orig, st.rim * 0.55f * cv::Clamp01(rr.dist(fx, fy) / std::max(1.0f, rimW)))
                    : inside;
            cv::Color c = outc;
            c.a = cov;
            cv::Blend(row[x], c, cov);
        }
    }

    // A diagonal sweep of light across the top-left, the way real glass catches a room.
    cv::FillRoundRectFn(s, (float)x0, (float)y0, bw, bh, radius, [&](float px, float py) {
        const float u = ((px - (float)x0) / std::max(1.0f, bw)) * 0.75f + ((py - (float)y0) / std::max(1.0f, bh)) * 0.25f;
        const float band = cv::Clamp01(1.0f - std::fabs(u - 0.18f) / 0.30f);
        const float top = cv::Clamp01(1.0f - (py - (float)y0) / std::max(1.0f, bh * 0.42f));
        const cv::Color c = cv::Hex(pal::White, st.topLight * (0.55f * band * band + 1.15f * top * top));
        return c;
    });

    // Rim: bright at the top edge, a hairline elsewhere, plus a soft inner shadow at the bottom.
    cv::StrokeRoundRect(s, (float)x0 + 0.5f, (float)y0 + 0.5f, bw - 1.0f, bh - 1.0f, radius, Hairline(),
                        cv::Hex(pal::White, st.edge));
    // A sliver of shade along the inner bottom edge gives the pane thickness.
    cv::FillRoundRectFn(s, (float)x0, (float)y0, bw, bh, radius, [&](float px, float py) {
        const float dx = std::fabs(px - (x0 + bw * 0.5f)) / (bw * 0.5f);
        const float bot = cv::Clamp01(1.0f - std::fabs(py - (float)y1) / U(2.5));
        const cv::Color c = cv::Hex(pal::Black, 0.26f * bot * (1.0f - dx * dx * 0.8f));
        return c;
    });
}

// Colour to use for text drawn on a glass surface: faded towards the backdrop.
static COLORREF GlassText(HDC dc, uint32_t fg, uint32_t bg) {
    (void)dc;
    return RefMix(fg, TintColor(bg, g_style.tint * 0.85f), g_textFade);
}

// Applies the theme's fade to a whole frame in the front buffer.
static void FadeFrame(cv::Surface& s, float amount) {
    if (amount >= 0.999f) return;
    s.resetClip();
    const cv::Color black = cv::Hex(pal::Bg);
    for (int y = 0; y < s.h; ++y) {
        uint32_t* row = s.row(y);
        for (int x = 0; x < s.w; ++x) cv::Blend(row[x], black, 1.0f - cv::Clamp01(amount));
    }
}

// ---------------------------------------------------------------------------
// Drawing: title bar, icon rail, summary, toolbar, rows, settings panel
// ---------------------------------------------------------------------------
static void DrawTitleShapes(cv::Surface& s) {
    if (Wide()) {
        const RECT rail = RailRect();
        cv::FillRect(s, rail.left, rail.top, rail.right - rail.left, rail.bottom - rail.top, cv::Hex(pal::Rail));
        cv::FillRect(s, rail.right - Max(1, S(1)), rail.top, Max(1, S(1)), rail.bottom - rail.top, cv::Hex(pal::Line));
        for (int k = 0; k < 2; ++k) {
            const RECT r = NavRect(k);
            const bool on = (k == 0 && !g_sheetOpen) || (k == 1 && g_sheetOpen);
            const float hov = g_hov[HIT_NAV0 + k].v;
            if (on) {
                cv::FillRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), U(10), cv::Hex(pal::Accent, 0.10f));
                cv::StrokeRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), U(10), Hairline(),
                                    cv::Hex(pal::Accent, 0.30f));
            } else if (hov > 0.0f) {
                cv::FillRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), U(10), cv::Hex(pal::White, 0.05f * hov));
            }
            const cv::Color ink = on ? cv::Hex(pal::Accent) : cv::Mix(cv::Hex(pal::Text3), cv::Hex(pal::Text), hov);
            const float cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
            if (k == 0) IconGrid(s, cx, cy, ink);
            else IconSliders(s, cx, cy, ink);
            if (on)  // the tiny live dot from the sidebar
                cv::FillCircle(s, (float)r.right - U(4.5f), (float)r.bottom - U(4.5f), U(2.1f), cv::Hex(pal::Accent));
        }
        // Brand mark at the top of the rail
        const RECT b = BrandRect();
        const float hov = g_hov[HIT_BRAND].v;
        if (hov > 0.01f)
            cv::Glow(s, Mid(b.left, b.right), Mid(b.top, b.bottom), U(26), cv::Hex(pal::Accent, 0.22f * hov));
        DrawMark(s, Mid(b.left, b.right), Mid(b.top, b.bottom), FH(b) * (0.94f + 0.04f * hov), 1.0f);
        cv::FillCircle(s, (float)b.right - U(1), (float)b.top + U(3), U(3.2f), cv::Hex(pal::Green));
        cv::FillCircle(s, (float)b.right - U(1), (float)b.top + U(3), U(4.8f), cv::Hex(pal::Green, 0.22f));
    } else {
        const RECT b = BrandRect();
        DrawMark(s, Mid(b.left, b.right), Mid(b.top, b.bottom), FH(b) * 0.94f, 1.0f);
    }
    static const int ids[3] = { HIT_TB_CLOSE, HIT_TB_MIN, HIT_TB_SETTINGS };
    for (int n = 0; n < 3; ++n) {
        if (n == 2 && Wide()) continue;  // settings lives on the rail when it is there
        const RECT r = TitleButton(n);
        const float hov = g_hov[ids[n]].v;
        const bool pressed = g_pressed == ids[n];
        const float cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
        cv::Color icon = cv::Mix(cv::Hex(pal::Text2), cv::Hex(pal::Text), hov);
        if (n == 0) {
            RoundBox(s, r, U(7), cv::Hex(pal::CloseHot, hov * (pressed ? 0.8f : 1.0f)));
            icon = cv::Mix(cv::Hex(pal::Text2), cv::Hex(pal::White), hov);
            IconClose(s, cx, cy, icon);
        } else {
            float bg = 0.08f * hov + (pressed ? 0.05f : 0.0f);
            if (n == 2 && g_sheetOpen) {
                RoundBox(s, r, U(7), cv::Hex(pal::Accent, 0.18f));
                icon = cv::Hex(pal::AccentHot);
                bg = 0.0f;
            }
            RoundBox(s, r, U(7), cv::Hex(pal::White, bg));
            if (n == 1) IconMinimize(s, cx, cy, icon);
            else IconGear(s, cx, cy, icon);
        }
    }
}

// Small "LIVE" / "RETRYING" badge after the window title.
struct Badge {
    const wchar_t* text = nullptr;
    uint32_t color = pal::Green;
    int x = 0, w = 0;
};

static Badge BuildBadge(HDC dc) {
    Badge b;
    if (g_lastFailed) {
        b.text = L"RETRYING";
        b.color = pal::Amber;
    } else if (g_everSucceeded) {
        b.text = L"LIVE";
    } else {
        return b;
    }
    const int titleX = S(52);
    b.x = titleX + TextW(dc, Font(13, true), APP_NAME) + S(12);
    b.w = S(20) + TextSpacedW(dc, Font(9, true), b.text, S(1)) + S(11);
    return b;
}

static RECT BadgeRect(const Badge& b) {
    return RECT{ b.x, S(kTitleH / 2 - 9), b.x + b.w, S(kTitleH / 2 + 9) };
}

static void DrawBadgeShape(cv::Surface& s, const Badge& b) {
    if (!b.text) return;
    const RECT r = BadgeRect(b);
    RoundBox(s, r, FH(r) / 2.0f, cv::Hex(b.color, 0.13f));
    RoundOutline(s, r, FH(r) / 2.0f, Hairline(), cv::Hex(b.color, 0.30f));
    cv::FillCircle(s, (float)r.left + U(10), Mid(r.top, r.bottom), U(3), cv::Hex(b.color));
}

static void DrawTitleText(HDC dc, const Badge& b) {
    Text(dc, Font(13, true), Ref(pal::Text), SR(52, 0, 320, kTitleH), APP_NAME);
    if (b.text) {
        const RECT r = BadgeRect(b);
        TextSpaced(dc, Font(9, true), Ref(b.color), r.left + S(20), Mid(r.top, r.bottom), b.text, S(1));
    }
}

// ---------------------------------------------------------------------------
// Summary card
// ---------------------------------------------------------------------------
struct HeroView {
    Totals t;
    std::wstring headline, meta, next;
    uint32_t metaColor = pal::Text3;
    std::wstring num[4];
    const wchar_t* labels[4] = { L"", L"", L"", L"" };
    int numX[4] = { 0, 0, 0, 0 }, labelX[4] = { 0, 0, 0, 0 }, endX[4] = { 0, 0, 0, 0 };
    int stats = 0;
};

static const wchar_t* const kStatLabels[4] = { L"updated", L"stale", L"detected", L"behind Roblox" };
static int BehindCount();
static bool OnLatestRoblox(const Product& p);
static const uint32_t kStatColors[3] = { pal::Green, pal::Red, pal::Amber };
static const wchar_t* const kStatAlt[3] = { L"updated", L"stale", L"detected" };

static HeroView BuildHero(HDC dc) {
    HeroView v;
    v.t = Count();
    v.headline = Headline(v.t);
    v.meta = MetaLine(v.metaColor);
    v.next = NextLine();
    const int counts[4] = { v.t.up, v.t.down, v.t.detected, BehindCount() };
    const wchar_t* labels[4] = { kStatAlt[0], kStatAlt[1], kStatAlt[2], kStatLabels[3] };
    const RECT h = HeroRect();
    int x = h.left + S(24);
    int n = 3;
    // The fourth number only earns its space when we know the live Roblox build.
    if (!g_rbxUpload.empty()) n = 4;
    for (int k = 0; k < n; ++k) {
        v.num[k] = std::to_wstring(counts[k]);
        v.numX[k] = x + S(12);
        v.labelX[k] = v.numX[k] + TextW(dc, Font(13, true), v.num[k]) + S(6);
        v.endX[k] = v.labelX[k] + TextW(dc, Font(12.5, false), labels[k]);
        x = v.endX[k] + S(20);
    }
    v.stats = n;
    for (int k = 0; k < n; ++k) v.labels[k] = labels[k];
    return v;
}

static void DrawHeroShapes(cv::Surface& s) {
    const RECT h = HeroRect();
    if (!s.touches((float)h.left - U(24), (float)h.top - U(24), FW(h) + U(48), FH(h) + U(48))) return;

    cv::Shadow(s, (float)h.left, (float)h.top + U(10), FW(h), FH(h) + U(10), U(18), U(30), cv::Hex(pal::Black, 0.42f));
    if (g_style.blur > 0.0f) {
        GlassPanel(s, h, U(18), pal::Card, g_style, true);
        // Soft accent wash in the upper right, behind the action.
        const float hw = FW(h), hh = FH(h);
        cv::FillRoundRectFn(s, (float)h.left, (float)h.top, hw, hh, U(18), [&](float px, float py) {
            const float dx = (px - (float)h.left) / std::max(1.0f, hw);
            const float dy = (py - (float)h.top) / std::max(1.0f, hh);
            const float d = std::sqrt((dx - 0.82f) * (dx - 0.82f) * 1.3f + (dy - 0.12f) * (dy - 0.12f));
            return cv::Hex(pal::Accent, 0.13f * cv::Clamp01(1.0f - d * 1.6f));
        });
    } else {
        const cv::Color base = cv::Hex(0x0E1017), tint = cv::Hex(pal::Accent);
        const float hw = FW(h);
        cv::FillRoundRectFn(s, (float)h.left, (float)h.top, hw, FH(h), U(18), [&](float px, float) {
            const float t = cv::Clamp01(1.0f - (px - (float)h.left) / std::max(1.0f, hw));
            return cv::Mix(base, tint, 0.10f * t * t);
        });
        cv::StrokeRoundRect(s, (float)h.left, (float)h.top, FW(h), FH(h), U(18), Hairline(), cv::Hex(0x242A38));
    }

    // Roblox build strip: the pill that turns amber the moment something lags behind.
    if (!g_rbxUpload.empty()) {
        const RECT sr = RbxStripRect();
        cv::FillRoundRect(s, (float)sr.left, (float)sr.top, FW(sr), FH(sr), FH(sr) / 2.0f,
                          cv::Hex(pal::White, 0.045f));
        cv::StrokeRoundRect(s, (float)sr.left, (float)sr.top, FW(sr), FH(sr), FH(sr) / 2.0f, Hairline(),
                            cv::Hex(pal::White, 0.07f));
        const int behind = BehindCount();
        const bool current = behind == 0;
        cv::Color dot = current ? cv::Hex(pal::Green) : cv::Hex(pal::Amber);
        if (g_rbxPulse > 0.01f) {
            dot = cv::Mix(dot, cv::Hex(pal::Accent), g_rbxPulse);
            cv::Glow(s, (float)sr.left + U(14), Mid(sr.top, sr.bottom), U(7) * (0.8f + 0.4f * g_rbxPulse),
                     cv::Hex(pal::Accent, 0.30f * g_rbxPulse));
        }
        cv::FillCircle(s, (float)sr.left + U(14), Mid(sr.top, sr.bottom), U(3.0f), dot);
    }

    // The button glows when pointed at
    const RECT b = CheckRect();
    const float bh = g_hov[HIT_CHECK].v;
    if (bh > 0.01f)
        cv::Shadow(s, (float)b.left, (float)b.top, FW(b), FH(b), FH(b) / 2.0f, U(12), cv::Hex(pal::White, 0.10f * bh));
}

static void DrawHeroText(HDC dc, const HeroView& v) {
    const RECT h = HeroRect();
    if (!RectVisible(dc, &h)) return;
    const int left = h.left + S(24), right = h.right - S(196);

    TextSpaced(dc, Font(8.5, true), Ref(pal::Accent), left, h.top + S(24), L"WEAO STATUS", S(1.6));
    Text(dc, Font(25.5, true), Ref(pal::Text), RECT{ left, h.top + S(34), right, h.top + S(68) }, v.headline,
         DT_LEFT | DT_END_ELLIPSIS);

    for (int k = 0; k < v.stats; ++k) {
        Text(dc, Font(13, true), Ref(pal::Text), RECT{ v.numX[k], h.top + S(64), v.labelX[k], h.top + S(84) },
             v.num[k]);
        Text(dc, Font(12.5, false), Ref(pal::Text2),
             RECT{ v.labelX[k], h.top + S(64), v.endX[k] + S(4), h.top + S(84) }, v.labels[k]);
    }
    if (!v.meta.empty())
        Text(dc, Font(12, false), Ref(v.metaColor), RECT{ left, h.top + S(90), right, h.top + S(110) }, v.meta,
             DT_LEFT | DT_END_ELLIPSIS);
    if (!g_rbxUpload.empty()) {
        const RECT sr = RbxStripRect();
        const int mid = Mid(sr.top, sr.bottom);
        TextSpaced(dc, Font(8.0, true), Ref(pal::Text3), sr.left + S(24), mid + S(3), L"ROBLOX", S(1.4));
        const int vx = sr.left + S(24) + TextW(dc, Font(8.0, true), L"ROBLOX  ") + S(6);
        Text(dc, Font(11.0, true), Ref(pal::Text), RECT{ vx, mid - S(8), vx + S(140), mid + S(9) },
             g_rbxVersion);
        const int behind = BehindCount();
        int live = 0;
        for (const Product& p : g_products) live += HasStatus(p) ? 1 : 0;
        const std::wstring tail = std::to_wstring(live - behind) + L" of " + std::to_wstring(live) + L" current";
        Text(dc, Font(10.5, false), Ref(behind == 0 ? pal::Text3 : pal::Amber),
             RECT{ sr.left, mid - S(8), sr.right - S(14), mid + S(9) }, tail, DT_RIGHT);
    }
    if (!v.next.empty() && g_rbxUpload.empty())
        Text(dc, Font(11.5, false), Ref(pal::Text3), RECT{ left, h.top + S(112), right, h.top + S(130) }, v.next);


}

// ---------------------------------------------------------------------------
// Toolbar: filters, the count and the check button
// ---------------------------------------------------------------------------
static void DrawToolbarShapes(cv::Surface& s) {
    for (int k = 0; k < 3; ++k) {
        const RECT r = FilterRect(k);
        const bool on = g_filter == k;
        const float hov = g_hov[HIT_FILTER0 + k].v;
        cv::FillRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), FH(r) / 2.0f,
                          on ? cv::Hex(pal::Accent, 0.12f) : cv::Hex(pal::White, 0.035f + 0.045f * hov));
        cv::StrokeRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), FH(r) / 2.0f, Hairline(),
                            on ? cv::Hex(pal::Accent, 0.34f) : cv::Hex(pal::White, 0.10f + 0.06f * hov));
    }
    const RECT b = CheckRect();
    const bool busy = g_fetching;
    const float hov = busy ? 0.0f : g_hov[HIT_CHECK].v;
    const bool pressed = g_pressed == HIT_CHECK && !busy;
    cv::FillRoundRect(s, (float)b.left, (float)b.top, FW(b), FH(b), FH(b) / 2.0f,
                      cv::Hex(pal::White, (busy ? 0.06f : 0.10f) + 0.06f * hov + (pressed ? 0.04f : 0.0f)));
    cv::StrokeRoundRect(s, (float)b.left, (float)b.top, FW(b), FH(b), FH(b) / 2.0f, Hairline(),
                        cv::Hex(pal::White, (busy ? 0.08f : 0.14f) + 0.16f * hov));
    IconRefresh(s, (float)b.left + U(21), Mid(b.top, b.bottom), g_spin,
                busy ? cv::Hex(pal::Text3) : cv::Mix(cv::Hex(pal::Text), cv::Hex(pal::White), hov));
}

static void DrawToolbarText(HDC dc) {
    const RECT h = HeroRect();
    (void)h;
    const std::wstring count = std::to_wstring(VisibleRows()) + L" of " + std::to_wstring(ProductCount()) +
                               L" products";
    Text(dc, Font(12, false), Ref(pal::Text3),
         RECT{ CheckRect().left - S(230), h.bottom + S(18), CheckRect().left - S(18), h.bottom + S(48) }, count,
         DT_RIGHT);
    static const wchar_t* const labels[3] = { L"All", L"Updated", L"Not updated" };
    for (int k = 0; k < 3; ++k)
        Text(dc, Font(12, true), Ref(g_filter == k ? pal::Text : pal::Text2), FilterRect(k), labels[k], DT_CENTER);
    const RECT b = CheckRect();
    Text(dc, Font(12.5, true), Ref(pal::Text), RECT{ b.left + S(30), b.top, b.right - S(6), b.bottom },
         g_fetching ? L"Checking\u2026" : L"Check now");
}

// ---------------------------------------------------------------------------
// Product rows
// ---------------------------------------------------------------------------
struct RowView {
    RECT r = { 0, 0, 0, 0 };  // where the row is this frame
    RECT art = { 0, 0, 0, 0 }; // the product tile, for the detection badge
    int index = 0;
    float shift = 0.0f;       // how far it still has to travel (fades while moving)
    float hov = 0.0f;
    bool live = false;
    bool detAlert = false;
    bool detIconOnly = false;
    uint32_t statusColor = pal::Text3;
    std::wstring status, detect, version, when, kind;
    int statusRight = 0, detLeft = 0, detW = 0, nameW = 0;
};

static std::vector<RowView> BuildRows(HDC dc) {
    std::vector<RowView> out;
    for (int i = 0; i < ProductCount(); ++i) {
        if (!RowMatches(i)) continue;
        const Product& p = g_products[(size_t)i];
        RowView v;
        v.index = i;
        v.r = RowRect(i);
        if (g_rowFrom[i] >= 0.0f) {  // sliding from where the row used to sit
            const float t = Ease(g_rowPos[i].v);
            const float from = S(ListTop() + (double)g_rowFrom[i] * (DensRow() + DensGap()));
            const float to = (float)v.r.top;
            v.shift = (from - to) * (1.0f - t);
            v.r.top = to + (int)std::lround(v.shift);
            v.r.bottom = v.r.top + S(DensRow());
        }
        v.hov = g_hov[HIT_ROW0 + i].v;
        v.live = HasStatus(p);
        if (v.live) {
            v.statusColor = p.updated == 1 ? pal::Green : pal::Red;
            v.status = p.updated == 1 ? L"Updated" : L"Not updated";
            if (p.detected != -1) {
                v.detAlert = p.detected == 1;
                v.detect = v.detAlert ? L"Detected" : L"Undetected";
            }
            v.version = p.version;
            v.when = Ago(p.updatedAt);
        } else if (g_everSucceeded) {
            v.status = L"No live source";
        } else {
            v.status = g_lastFailed ? L"No data" : L"Checking\u2026";
        }
        v.nameW = TextW(dc, Font(14, true), p.name);
        {
            const int pad = Compact() ? 4 : 5;
            const int side = (int)S(DensRow()) - pad * 2;
            v.art = RECT{ v.r.left + S(9), v.r.top + S(pad), v.r.left + S(9) + side,
                           v.r.top + S(pad) + side };
        }
        v.detLeft = v.r.right - S(64);
        v.statusRight = v.r.right - S(18);
        out.push_back(v);
    }
    return out;
}

// A row's face is identical every frame unless it is hovered, flashing, or the
// scale/theme changed. Building that face means a backdrop blur over the whole
// card, which is the single most expensive thing the app does; so it is built
// once and blitted until one of those inputs actually changes.
struct RowFaceKey {
    int left = -1, top = -1, w = -1, h = -1;
    int hovered = 0, flashing = 0, scaleQ = 0, style = 0, gen = 0;
    bool operator==(const RowFaceKey& o) const {
        return left == o.left && top == o.top && w == o.w && h == o.h && hovered == o.hovered &&
               flashing == o.flashing && scaleQ == o.scaleQ && style == o.style && gen == o.gen;
    }
};
struct RowFace {
    RowFaceKey key;
    int w = 0, h = 0;
    std::vector<uint32_t> px;  // RGBA straight, blitted into the surface
};
static std::vector<RowFace> g_rowFace;  // grown on demand: a closed panel costs nothing
static int g_rowFaceScale = 0, g_rowFaceStyle = 0;

static void BlitFace(cv::Surface& s, const RowFace& f, int left, int top) {
    for (int y = std::max(s.cy0, top); y < std::min(s.cy1, top + f.h); ++y) {
        const uint32_t* src = f.px.data() + (size_t)(y - top) * f.w;
        uint32_t* dst = s.row(y);
        for (int x = std::max(s.cx0, left); x < std::min(s.cx1, left + f.w); ++x) {
            const uint32_t v = src[x - left];
            const int a = (int)(v >> 24);
            if (a == 0) continue;
            if (a == 255) {
                dst[x] = v;
            } else {
                const uint32_t d = dst[x];
                const int ia = 255 - a;
                const int r = (int)((v >> 16) & 0xFF) * a / 255 + (int)((d >> 16) & 0xFF) * ia / 255;
                const int g = (int)((v >> 8) & 0xFF) * a / 255 + (int)((d >> 8) & 0xFF) * ia / 255;
                const int b = (int)(v & 0xFF) * a / 255 + (int)(d & 0xFF) * ia / 255;
                dst[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }
}

// Draws just the face (card + artwork + badges) of a row into `s`, whose origin
// is the row's top-left corner.
static void DrawRowFaceBody(cv::Surface& s, const RowView& v, const RECT& r);

static void DrawRowShapes(cv::Surface& s, const RowView& v) {
    const RECT r = v.r;
    if (!s.touches((float)r.left - U(16), (float)r.top - U(16), FW(r) + U(32), FH(r) + U(32))) return;
    const float rad = U(14), hov = v.hov;
    const cv::Color status = cv::Hex(v.statusColor);

    if (g_flash[(size_t)v.index] > 0.0f)
        cv::Shadow(s, (float)r.left, (float)r.top, FW(r), FH(r), rad, U(16),
                   cv::Fade(status, 0.40f * g_flash[(size_t)v.index]));

    // The face is cached; a hover, a flash or a scale change rebuilds it.
    const int scaleQ = (int)std::lround(g_scale * 100.0);
    const int styleQ = (int)std::lround((g_style.blur + g_style.topLight + g_style.edge) * 100.0f);
    if (g_rowFaceScale != scaleQ || g_rowFaceStyle != styleQ) {
        g_rowFaceScale = scaleQ;
        g_rowFaceStyle = styleQ;
        for (RowFace& f : g_rowFace) f.key.left = -1;  // everything is stale
    }
    if (g_rowFace.size() < g_products.size()) g_rowFace.resize(g_products.size());
    RowFace& face = g_rowFace[(size_t)v.index];
    RowFaceKey want;
    want.left = 0;  // the face is built in local space, so position does not matter
    want.top = 0;
    want.w = FW(r);
    want.h = FH(r);
    want.hovered = hov > 0.002f ? 1 : 0;
    want.flashing = g_flash[(size_t)v.index] > 0.0f ? 1 : 0;
    want.scaleQ = scaleQ;
    want.style = styleQ;
    want.gen = g_bgGen;
    if (!(face.key == want)) {
        if (face.w != want.w || face.h != want.h) {
            face.w = want.w;
            face.h = want.h;
            face.px.assign((size_t)face.w * (size_t)face.h, 0u);
        } else {
            std::fill(face.px.begin(), face.px.end(), 0u);
        }
        cv::Surface scratch;
        scratch.attach(face.px.data(), face.w, face.h);
        // Seed the scratch with the backdrop the glass will sample, otherwise the
        // frosted card has nothing to bend and comes out flat.
        for (int y = 0; y < face.h; ++y) {
            const int sy = r.top + y;
            if (sy < 0 || sy >= (int)g_bgH || face.w <= 0) break;
            const int sx = std::max(0, (int)r.left);
            const size_t rowOff = (size_t)sy * (size_t)g_bgW + (size_t)sx;
            if (rowOff + (size_t)face.w > g_bg.size() || sx + face.w > g_bgW) break;
            memcpy(face.px.data() + (size_t)y * (size_t)face.w, g_bg.data() + rowOff,
                   (size_t)face.w * sizeof(uint32_t));
        }
        // The face is drawn in local space, so artwork and badges move with it.
        RowView local = v;
        local.r = RECT{ 0, 0, face.w, face.h };
        local.art = RECT{ v.art.left - r.left, v.art.top - r.top, v.art.right - r.left, v.art.bottom - r.top };
        DrawRowFaceBody(scratch, local, local.r);
        for (uint32_t& px : face.px) px |= 0xFF000000u;  // finished composite
        face.key = want;
    }
    BlitFace(s, face, r.left, r.top);
    return;
}

static void DrawRowFaceBody(cv::Surface& s, const RowView& v, const RECT& r) {
    const Product& p = g_products[(size_t)v.index];
    const float rad = U(14), hov = v.hov;
    const cv::Color status = cv::Hex(v.statusColor);

    if (g_style.blur > 0.0f) {
        GlassStyle st = g_style;
        st.topLight *= 0.55f + 0.45f * hov;
        st.edge += 0.06f * hov;
        GlassPanel(s, r, rad, pal::Row, st, false);
        cv::FillRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), rad, cv::Hex(pal::White, 0.035f * hov));
    } else {
        cv::FillRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), rad,
                          cv::Mix(cv::Hex(pal::Row), cv::Hex(pal::RowHot), hov));
        cv::StrokeRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), rad, Hairline(),
                            cv::Mix(cv::Hex(pal::Line), cv::Hex(pal::LineHot), hov));
    }
    if (g_flash[(size_t)v.index] > 0.0f)
        cv::StrokeRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), rad, Hairline(),
                            cv::Fade(status, 0.7f * g_flash[(size_t)v.index]));

    // Artwork
    const RECT art = v.art;
    const Logo* logo = LogoFor(p);
    if (logo) {
        DrawLogo(s, art, *logo, 1.0f, true);
    } else {
        Monogram(s, art, p, v.live);
        cv::StrokeRoundRect(s, (float)art.left, (float)art.top, FW(art), FH(art), U(10), Hairline(),
                            cv::Hex(pal::White, 0.12f));
    }
    // A status ring on the corner of the artwork keeps the row readable at a glance.
    const cv::Color dotBack = g_style.blur > 0.0f ? cv::Hex(pal::Bg) : cv::Mix(cv::Hex(pal::Row), cv::Hex(pal::RowHot), hov);
    if (!v.detAlert) {  // the badge owns the top-left corner when it is shown
        cv::FillCircle(s, (float)art.right - U(1), (float)art.bottom - U(1), U(6.5f), dotBack);
        cv::FillCircle(s, (float)art.right - U(1), (float)art.bottom - U(1), U(4.2f), cv::Fade(status, 0.95f));
    }

    // Detection: a small exclamation mark on the corner of the artwork. It only
    // appears when WEAO reports the product as detected; hover explains it.
    if (v.detAlert) {
        const float bx = (float)v.art.left + U(3), by = (float)v.art.top + U(3);
        const float k = 1.0f + 0.10f * g_hov[HIT_BELL0 + v.index].v;
        cv::FillCircle(s, bx, by, U(11) * k, cv::Hex(pal::Row));
        cv::FillCircle(s, bx, by, U(9.5f) * k, cv::Hex(pal::Amber));
        cv::FillRoundRect(s, bx - U(0.9f), by - U(5), U(1.8f), U(6), U(0.9f), cv::Hex(0x241A05));
        cv::FillCircle(s, bx, by + U(4), U(1.1f), cv::Hex(0x241A05));
    }

}

static void DrawRowText(HDC dc, const RowView& v) {
    if (!RectVisible(dc, &v.r)) return;
    const Product& p = g_products[(size_t)v.index];
    const RECT& r = v.r;
    const uint32_t card = v.hov > 0.5f ? pal::RowHot : pal::Row;

    // Name
    const int left = r.left + S(62);
    const int nameRight = r.left + S(300);
    Text(dc, Font(14, true), GlassText(dc, v.live ? pal::Text : pal::Text2, pal::Row),
         RECT{ left, r.top, nameRight, r.bottom }, p.name, DT_LEFT | DT_END_ELLIPSIS);

    // Version column; an amber caret means it lags the Roblox build we last saw.
    if (v.live) {
        const std::wstring shown = v.version.empty() ? L"\u2014" : v.version;
        const int vw = TextW(dc, Font(13, false), shown);
        Text(dc, Font(13, false), Ref(pal::Text2),
             RECT{ r.left + S(300), r.top, v.statusRight - S(92), r.bottom }, shown,
             DT_LEFT | DT_END_ELLIPSIS);
        if (!g_rbxUpload.empty() && !p.rbx.empty() && !OnLatestRoblox(p)) {
            const int room = v.statusRight - S(92) - r.left - S(300) - S(14);
            const int cx = r.left + S(300) + std::min(vw, room < 0 ? 0 : room);
            Text(dc, Font(11, true), Ref(pal::Amber),
                 RECT{ cx + S(5), r.top, cx + S(20), r.bottom }, L"\u25B2");
        }
    }

    // Status, right aligned, with its dot
    const std::wstring& word = v.status;
    const int w = TextW(dc, Font(12.5, true), word);
    Text(dc, Font(12.5, true), Ref(v.live ? v.statusColor : pal::Text2),
         RECT{ v.statusRight - w, r.top, v.statusRight, r.bottom }, word);
    // Detection is a corner badge now; no word in the row.
    (void)card;
}

// ---------------------------------------------------------------------------
// Settings panel (a column on the right of the window)
// ---------------------------------------------------------------------------
static const wchar_t* const kRowTitle[3] = { L"Desktop notifications", L"Sound", L"Start with Windows" };
static const wchar_t* const kRowHelp[3] = { L"When a status changes", L"Play a sound",
                                            L"Open hidden in the tray" };

static RECT SwitchRect(int n) {
    const RECT r = PanelRowRect(n);
    return RECT{ r.right - S(16 + 36), r.top + S(14), r.right - S(16), r.top + S(34) };
}

// The sheet itself, in screen space: it lifts, slides in from the right and
// settles with a small overshoot, so opening Settings reads as one gesture.
static RECT rc_all(const cv::Surface& s) { return RECT{ 0, 0, s.w, s.h }; }
// A packed colour mixed towards another, for text that has to fade with the panel.
static uint32_t MixPacked(uint32_t fg, uint32_t bg, float amount) {
    const cv::Color c = cv::Mix(cv::Hex(bg), cv::Hex(fg), cv::Clamp01(amount));
    return ((uint32_t)(c.r + 0.5f) << 16) | ((uint32_t)(c.g + 0.5f) << 8) | (uint32_t)(c.b + 0.5f);
}
// Where the sheet sits this frame: it comes in from the right, eases down a
// few pixels and overshoots by a hair, which is what gives it the spring.
static void PanelMotion(int& dx, int& dy) {
    const float t = Ease(g_sheet.v);
    const float over = std::sin(g_sheet.v * 3.14159265f);
    dx = (int)std::lround(S(34) * (1.0f - t) - S(2) * over);
    dy = (int)std::lround(S(8) * (1.0f - t));
}
static RECT SheetRect() {
    const RECT p = PanelRect();
    int dx = 0, dy = 0;
    PanelMotion(dx, dy);
    return RECT{ p.left + S(10) - dx, S(10) + dy, p.right - S(10) - dx, p.bottom - S(10) + dy };
}
static float SheetAlpha() { return cv::Clamp01(Ease(g_sheet.v) * 1.35f); }

static void DrawPanelShapes(cv::Surface& s) {
    // While the sheet is still coming in, clear the column first: the backdrop
    // and the dashboard are both showing through it. Once it is fully open the
    // glass styles paint their own face and this is skipped.
    if (g_sheet.v < 0.995f) {
        const int keepFull[4] = { s.cx0, s.cy0, s.cx1, s.cy1 };
        const RECT sheetNow = SheetRect();
        RECT cl;
        const RECT full = rc_all(s);
        if (IntersectRect(&cl, &full, &sheetNow)) {
            s.setClip(cl.left, cl.top, cl.right, cl.bottom);
            cv::FillRect(s, cl.left, cl.top, cl.right - cl.left, cl.bottom - cl.top, cv::Hex(pal::Panel));
        }
        s.setClip(keepFull[0], keepFull[1], keepFull[2], keepFull[3]);
    }
    if (g_style.blur > 0.0f) {
        RECT sheet = SheetRect();
        cv::Shadow(s, (float)sheet.left, (float)sheet.top + U(6), FW(sheet), FH(sheet), U(22), U(26),
                   cv::Hex(pal::Black, 0.45f));
        GlassStyle st = g_style;
        st.topLight *= 1.2f;
        GlassPanel(s, sheet, U(22), pal::Panel, st, true);
    } else {
        const RECT sheet = SheetRect();
        cv::Shadow(s, (float)sheet.left, (float)sheet.top + U(6), FW(sheet), FH(sheet), U(18), U(22),
                   cv::Hex(pal::Black, 0.42f * SheetAlpha()));
        cv::FillRoundRect(s, (float)sheet.left, (float)sheet.top, FW(sheet), FH(sheet), U(18),
                          cv::Hex(pal::Panel, SheetAlpha()));
        cv::FillRect(s, sheet.left, sheet.top, Max(1, S(1)), sheet.bottom - sheet.top,
                     cv::Hex(pal::Line, SheetAlpha()));
    }

    // Everything below the title scrolls inside the sheet; the header and footer
    // stay put, so the panel never feels like it is sliding under itself.
    const RECT body = PanelBodyClip();
    const int keepBody[4] = { s.cx0, s.cy0, s.cx1, s.cy1 };
    RECT bc;
    const RECT all = rc_all(s);
    if (IntersectRect(&bc, &all, &body)) s.setClip(bc.left, bc.top, bc.right, bc.bottom);

    {  // close button
        const RECT r = PanelCloseRect();
        const float hov = g_hov[HIT_SHEET_CLOSE].v;
        RoundBox(s, r, U(8), cv::Hex(pal::White, 0.08f * hov + (g_pressed == HIT_SHEET_CLOSE ? 0.05f : 0.0f)));
        const float d = U(4.2), cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
        const cv::Color c = cv::Mix(cv::Hex(pal::Text2), cv::Hex(pal::Text), hov);
        cv::StrokeLine(s, cx - d, cy - d, cx + d, cy + d, U(1.3), c);
        cv::StrokeLine(s, cx + d, cy - d, cx - d, cy + d, U(1.3), c);
    }
    {  // "choose a picture": a proper button under the scenes
        const RECT r = PicButtonRect();
        const float hov = g_hov[HIT_PICPICK].v;
        const bool on = g_set.background == L"image";
        RoundBox(s, r, U(9), cv::Hex(pal::White, 0.035f + 0.05f * hov));
        RoundOutline(s, r, U(9), Hairline(),
                     on ? cv::Hex(pal::Accent, 0.34f) : cv::Hex(pal::White, 0.10f + 0.08f * hov));
        IconPicture(s, (float)r.left + U(20), Mid(r.top, r.bottom), cv::Hex(on ? pal::Accent : pal::Text2));
    }
    {  // accent presets: six dots, a ring around the live one
        const int cur = AccentIndex();
        for (int k = 0; k < ACCENTS; ++k) {
            const RECT r = AccentRect(k);
            const float hov = g_hov[HIT_ACCENT0 + k].v;
            if (k == cur)
                cv::StrokeRoundRect(s, (float)r.left - U(3), (float)r.top - U(3), FW(r) + U(6), FH(r) + U(6),
                                    (FH(r) + U(6)) / 2.0f, U(1.4f), cv::Hex(AccentAt(k), 0.85f));
            cv::FillCircle(s, Mid(r.left, r.right), Mid(r.top, r.bottom), FH(r) / 2.0f + U(0.6f) * hov,
                           cv::Hex(AccentAt(k), 0.92f + 0.08f * hov));
        }
    }
    {  // interval picker
        const RECT box = SegBox();
        RoundBox(s, box, U(10), cv::Hex(pal::White, 0.045f));
        RoundOutline(s, box, U(10), Hairline(), cv::Hex(pal::Line));
        const int cur = IntervalIndex();
        for (int k = 0; k < SEG_COUNT; ++k) {
            if (k == cur) continue;
            const float hov = g_hov[HIT_SEG0 + k].v;
            if (hov > 0.0f) RoundBox(s, SegRect(k), U(8), cv::Hex(pal::White, 0.07f * hov));
        }
        if (cur >= 0) {
            const RECT a = SegRect(0), b = SegRect(SEG_COUNT - 1);
            const float span = (float)(b.left - a.left) / (float)(SEG_COUNT - 1);
            cv::FillRoundRect(s, (float)a.left + span * g_seg.v, (float)a.top, FW(a), FH(a), U(8),
                              cv::Hex(pal::Accent));
        }
    }
    for (int n = 0; n < 3; ++n) {
        const float hov = g_hov[HIT_TGL0 + n].v;
        if (hov > 0.0f) RoundBox(s, PanelRowRect(n), U(10), cv::Hex(pal::White, 0.04f * hov));
        DrawSwitch(s, SwitchRect(n), g_tgl[n].v, (n == 1 && !g_set.notifications) ? 0.4f : 1.0f);
    }
    {  // density picker
        const RECT box = DensBox();
        RoundBox(s, box, U(10), cv::Hex(pal::White, 0.045f));
        RoundOutline(s, box, U(10), Hairline(), cv::Hex(pal::Line));
        const int cur = Compact() ? 1 : 0;
        cv::FillRoundRect(s, (float)DensRect(cur).left, (float)DensRect(cur).top,
                          FW(DensRect(cur)), FH(DensRect(cur)), U(8), cv::Hex(pal::Accent));
        for (int k = 0; k < 2; ++k) {
            const float hov = g_hov[HIT_DENS0 + k].v;
            if (k != cur && hov > 0.0f)
                RoundBox(s, DensRect(k), U(8), cv::Hex(pal::White, 0.07f * hov));
        }
    }
    for (int a = 0; a < 4; ++a) {  // per-event notification switches
        const float hov = g_hov[HIT_ALERT0 + a].v;
        if (hov > 0.0f) RoundBox(s, AlertRowRect(a), U(8), cv::Hex(pal::White, 0.035f * hov));
        const float alpha = g_set.notifications ? 1.0f : 0.42f;
        DrawSwitch(s, AlertSwitchRect(a), g_alertTgl[a].v, alpha);
        if (a < 3) {  // a short hairline between them keeps the group readable
            const RECT rr = AlertRowRect(a);
            cv::FillRect(s, rr.left + S(30), rr.bottom - Max(1, S(1)),
                         rr.right - rr.left - S(96), Max(1, S(1)), cv::Hex(pal::White, 0.05f));
        }
    }
    {  // background swatches
        static const uint32_t swatch[SWATCHES] = { 0x5B9DFF, 0x8A6BFF, 0xFF7A5C, 0x6FCF97,
                                                   0x59616B, 0x4FC9D8, 0xEDEAE4, 0xF0B243 };
        for (int k = 0; k < SWATCHES; ++k) {
            const RECT r = SwatchRect(k);
            const bool on = k < 7 ? g_set.background == SceneId(k) : g_set.background == L"colour";
            const float hov = g_hov[HIT_SWATCH0 + k].v;
            if (k == 6) {  // Ice: the cool scene that matches the accent family
                const uint32_t col = 0x7DEEFF;
                cv::FillRoundRectFn(s, (float)r.left, (float)r.top, FW(r), FH(r), U(10), [&](float px, float py) {
                    const float t = cv::Clamp01(((px - r.left) + (py - r.top)) / (FW(r) + FH(r)));
                    return cv::Mix(cv::Mix(cv::Hex(col), cv::Hex(pal::White), 0.35f), cv::Hex(0x4A8FD6), t);
                });
            } else if (k == 7) {  // a colour of your own, cycled
                const uint32_t col = ColourFromSetting();
                cv::FillRoundRectFn(s, (float)r.left, (float)r.top, FW(r), FH(r), U(10), [&](float px, float py) {
                    const float t = cv::Clamp01(((px - r.left) + (py - r.top)) / (FW(r) + FH(r)));
                    return cv::Mix(cv::Mix(cv::Hex(col), cv::Hex(pal::White), 0.25f), cv::Hex(col), t);
                });
            } else {
                const uint32_t col = swatch[k];
                cv::FillRoundRectFn(s, (float)r.left, (float)r.top, FW(r), FH(r), U(10), [&](float px, float py) {
                    const float t = cv::Clamp01(((px - r.left) + (py - r.top)) / (FW(r) + FH(r)));
                    return cv::Mix(cv::Mix(cv::Hex(col), cv::Hex(pal::White), 0.25f), cv::Hex(col), t);
                });
            }
            cv::StrokeRoundRect(s, (float)r.left - U(1), (float)r.top - U(1), FW(r) + U(2), FH(r) + U(2), U(11),
                                Max(2.0f, U(2)), cv::Hex(pal::Text, on ? 1.0f : 0.0f));
            cv::StrokeRoundRect(s, (float)r.left, (float)r.top, FW(r), FH(r), U(10), Hairline(),
                                cv::Hex(pal::White, on ? 0.35f : 0.10f + 0.15f * hov));
            if (on) {  // a tick, dark on light swatches so it always reads
                const float cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
                const cv::Pt tick[3] = { { cx - U(5), cy }, { cx - U(1.5f), cy + U(4) }, { cx + U(6), cy - U(5) } };
                const uint32_t face = k == 7 ? ColourFromSetting() : swatch[k];
                const cv::Color ink = cv::Hex(face);
                const bool light = (ink.r * 299 + ink.g * 587 + ink.b * 114) > 150000;
                cv::StrokePath(s, tick, 3, false, U(2), light ? cv::Hex(0x1A1D1E, 0.92f) : cv::Hex(pal::White, 0.95f));
            }
        }
    }
    {  // theme picker
        const RECT box = ThemeBoxRect();
        RoundBox(s, box, U(10), cv::Hex(pal::White, 0.05f));
        RoundOutline(s, box, U(10), Hairline(), cv::Hex(pal::Line));
        const bool glass = g_set.theme != L"flat";
        const RECT on = ThemeSegRect(glass ? 0 : 1);
        cv::FillRoundRect(s, (float)on.left, (float)on.top, FW(on), FH(on), U(8), cv::Hex(pal::Accent));
    }
    {  // test button
        const RECT r = TestRect();
        const float hov = g_hov[HIT_TEST].v;
        RoundBox(s, r, U(10), cv::Hex(pal::White, 0.05f + 0.05f * hov + (g_pressed == HIT_TEST ? 0.04f : 0.0f)));
        RoundOutline(s, r, U(10), Hairline(), cv::Mix(cv::Hex(pal::Line), cv::Hex(pal::LineHot), hov));
    }
    s.setClip(keepBody[0], keepBody[1], keepBody[2], keepBody[3]);
}

static void DrawPanelText(HDC dc) {
    const RECT p = PanelRect();
    if (!RectVisible(dc, &p)) return;
    int dx = 0, dy = 0;
    PanelMotion(dx, dy);
    const int x = p.left + S(20) - dx;
    // Fade the whole column with the sheet: GDI text cannot blend, so it is a
    // colour mix against the panel face.
    const float a = SheetAlpha();
    const uint32_t cText = MixPacked(pal::Text, pal::Panel, a);
        const uint32_t cText3 = MixPacked(pal::Text3, pal::Panel, a);
    const uint32_t cText4 = MixPacked(pal::Text4, pal::Panel, a);

    // Pinned header, outside the scroll area.
    Text(dc, Font(16, true), Ref(cText), RECT{ x, S(14) + dy, p.right - S(54) - dx, S(42) + dy }, L"Settings");
    // Scrolled body: every Y below is measured from the scrolled position, so a
    // single offset moves the entire column.
    const RECT body = PanelBodyClip();
    HRGN rgn = CreateRectRgn(body.left, body.top + dy, body.right - dx, body.bottom);
    SelectClipRgn(dc, rgn);
    DeleteObject(rgn);

    TextSpaced(dc, Font(9.5, true), Ref(cText3), x, PYL(58), L"CHECK EVERY", S(1.5));
    TextSpaced(dc, Font(9.5, true), Ref(cText3), x, PYL(124), L"DENSITY", S(1.5));
    Text(dc, Font(12, true), RefMix(!Compact() ? pal::Bg : pal::Text3, pal::Panel, a), DensRect(0), L"Comfortable", DT_CENTER);
    Text(dc, Font(12, true), RefMix(Compact() ? pal::Bg : pal::Text3, pal::Panel, a), DensRect(1), L"Compact", DT_CENTER);
    TextSpaced(dc, Font(9.5, true), Ref(cText3), x, PYL(336), L"TELL ME ABOUT", S(1.5));

    const int cur = IntervalIndex();
    for (int k = 0; k < SEG_COUNT; ++k) {
        const float under = cur >= 0 ? cv::Clamp01(1.0f - std::fabs(g_seg.v - (float)k)) : 0.0f;
        Text(dc, Font(12, true), RefMix(pal::Bg, pal::Text2, under), SegRect(k), IntervalShort(INTERVALS[k]), DT_CENTER);
    }
    for (int n = 0; n < 3; ++n) {
        const bool dim = n == 1 && !g_set.notifications;
        const int y = PY(PanelRowY(n));
        Text(dc, Font(13, false), Ref(dim ? cText3 : cText), RECT{ x, y + S(9), p.right - S(70) - dx, y + S(30) },
             kRowTitle[n]);
        Text(dc, Font(11.5, false), Ref(dim ? cText4 : cText3),
             RECT{ x, y + S(29), p.right - S(64) - dx, y + S(47) }, kRowHelp[n], DT_LEFT | DT_END_ELLIPSIS);
    }
    for (int a2 = 0; a2 < 4; ++a2) {
        const int y = PY(AlertRowY(a2));
        const bool dim = !g_set.notifications;
        Text(dc, Font(12, false), Ref(dim ? cText3 : cText),
             RECT{ x + S(8), y + S(7), p.right - S(62) - dx, y + S(23) }, kAlertTitle[a2]);
        Text(dc, Font(10.5, false), Ref(dim ? cText4 : cText3),
             RECT{ x + S(8), y + S(23), p.right - S(56) - dx, y + S(37) }, kAlertHelp[a2], DT_LEFT | DT_END_ELLIPSIS);
    }
    Text(dc, Font(12.5, true), Ref(cText), TestRect(), L"Send test notification", DT_CENTER);

    TextSpaced(dc, Font(9.5, true), Ref(cText3), x, PYL(578), L"BACKGROUND", S(1.5));
    TextSpaced(dc, Font(9.5, true), Ref(cText3), x, PYL(804), L"ACCENT", S(1.5));
    {
        const RECT r = PicButtonRect();
        Text(dc, Font(11.5, false), RefMix(g_set.background == L"image" ? pal::Text : pal::Text2, pal::Panel, a),
             RECT{ r.left + S(34), r.top, r.right - S(12), r.bottom },
             g_set.background == L"image" ? Clip(FileNameOf(g_set.imagePath), 28) : L"Choose a picture\u2026",
             DT_LEFT | DT_END_ELLIPSIS);
    }
    Text(dc, Font(12, true), RefMix(g_set.theme != L"flat" ? pal::Bg : pal::Text3, pal::Panel, a), ThemeSegRect(0),
         L"Liquid glass", DT_CENTER);
    Text(dc, Font(12, true), RefMix(g_set.theme == L"flat" ? pal::Bg : pal::Text3, pal::Panel, a), ThemeSegRect(1),
         L"Flat", DT_CENTER);

    SelectClipRgn(dc, nullptr);

    // Pinned footer: version, and the honesty note when there is room for it.
    Text(dc, Font(11, false), Ref(cText4), RECT{ x, p.bottom - S(36), p.right - S(20) - dx, p.bottom - S(18) },
         std::wstring(APP_NAME) + L" " + APP_VERSION + L"");
    if (g_scrollTo <= 0.5)  // only pinned at rest, so it never rides over the swatches
        TextWrapped(dc, Font(11.5, false), Ref(cText3),
                    RECT{ x, p.bottom - S(92), p.right - S(20) - dx, p.bottom - S(44) },
                    L"\u201CUpdated\u201D means a tool supports the current Roblox build. It is not a safety rating.");
}

// ---------------------------------------------------------------------------
// Composing a frame
// ---------------------------------------------------------------------------
static int g_tipW = 0;  // measured in BuildTip so the rect can be positioned there
struct Tip { std::wstring text; RECT anchor = { 0, 0, 0, 0 }; float a = 0.0f; };

static RECT TipRect(const Tip& t) {
    const int w = std::max(S(84), g_tipW + S(22)), h = S(26);
    int left = (t.anchor.left + t.anchor.right) / 2 - w / 2;
    int top = t.anchor.top - h - S(9);
    const int cw = g_dash.s.w, ch = g_dash.s.h;
    left = std::max(S(6), std::min(left, cw - w - S(6)));
    if (top < S(6)) top = t.anchor.bottom + S(9);
    (void)ch;
    return RECT{ left, top, left + w, top + h };
}

// ---------------------------------------------------------------------------
// Tooltip: what the corner badge means, in words
// ---------------------------------------------------------------------------
static Tip BuildTip(HDC dc) {
    Tip t;

    for (int i = 0; i < ProductCount(); ++i) {
        const Product& p = g_products[(size_t)i];
        if (p.detected != 1 || !RowMatches(i)) continue;
        // g_hot already tracks the pointer for every row's badge
        if (g_hot != HIT_BELL0 + i) continue;
        t.text = p.name + L" \u00B7 detected";
        t.anchor = DetRect(i);
        t.a = g_hov[HIT_BELL0 + i].v;
        g_tipW = TextW(dc, Font(10.5, true), t.text);
        break;
    }
    return t;
}

static void DrawTipShapes(cv::Surface& s, const Tip& t) {
    if (t.a <= 0.02f || t.text.empty()) return;
    const int w = (int)s.w;
    (void)w;
    const RECT box = TipRect(t);
    cv::Shadow(s, (float)box.left, (float)box.top + U(3), FW(box), FH(box), U(7), U(14),
               cv::Hex(pal::Black, 0.45f * t.a));
    cv::FillRoundRect(s, (float)box.left, (float)box.top, FW(box), FH(box), U(7),
                      cv::Hex(pal::Card, 0.97f * t.a));
    cv::StrokeRoundRect(s, (float)box.left, (float)box.top, FW(box), FH(box), U(7), Hairline(),
                        cv::Hex(pal::White, 0.14f * t.a));
}

static void DrawTipText(HDC dc, const Tip& t) {
    if (t.a <= 0.02f || t.text.empty()) return;
    const RECT box = TipRect(t);
    if (!RectVisible(dc, &box)) return;
    Text(dc, Font(10.5, true), Ref(cv::Pack(cv::Fade(cv::Hex(pal::Text), t.a))), box, t.text,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void RenderDashboard(const RECT& clip) {
    cv::Surface& s = g_dash.s;
    HDC dc = g_dash.dc;
    GdiFlush();
    s.setClip(clip.left, clip.top, clip.right, clip.bottom);
    if (!s.clipEmpty()) {
        if (!g_bgPrev.empty() && g_bgFade < 1.0f) {  // cross-fade from the old backdrop
            const float t = Ease(g_bgFade);
            for (int y = s.cy0; y < s.cy1; ++y) {
                const uint32_t* a = g_bgPrev.data() + (size_t)y * (size_t)s.w + (size_t)s.cx0;
                const uint32_t* b = g_bg.data() + (size_t)y * (size_t)s.w + (size_t)s.cx0;
                uint32_t* row = s.row(y) + s.cx0;
                for (int x = 0; x < s.cx1 - s.cx0; ++x)
                    row[x] = cv::Pack(cv::Mix(cv::Hex(a[x]), cv::Hex(b[x]), t));
            }
        } else {
            for (int y = s.cy0; y < s.cy1; ++y)
                memcpy(s.row(y) + s.cx0, g_bg.data() + (size_t)y * (size_t)s.w + (size_t)s.cx0,
                       (size_t)(s.cx1 - s.cx0) * sizeof(uint32_t));
        }

        const Badge badge = BuildBadge(dc);
        const HeroView hero = BuildHero(dc);
        const std::vector<RowView> rows = BuildRows(dc);
        const Tip tip = BuildTip(dc);

        DrawTitleShapes(s);
        DrawBadgeShape(s, badge);
        DrawHeroShapes(s);
        DrawToolbarShapes(s);
        for (const RowView& v : rows) DrawRowShapes(s, v);
        if (!g_sheetOpen) DrawTipShapes(s, tip);  // never over the settings panel

        SetTextClip(dc, clip);
        DrawTitleText(dc, badge);
        DrawHeroText(dc, hero);
        DrawToolbarText(dc);
        for (const RowView& v : rows) DrawRowText(dc, v);
        if (!g_sheetOpen) DrawTipText(dc, tip);
        SelectClipRgn(dc, nullptr);
        GdiFlush();
    }
    s.resetClip();
}

// The opening wordmark: the monogram, the name, and a thin progress rule. It
// sits over everything, then lifts to reveal the checker underneath.


// The opening wordmark. It is drawn over the finished frame, so the checker is
// already there underneath for it to dissolve into.
static void DrawSplashShapes(cv::Surface& s) {
    if (!g_splashOn) return;
    const float t = cv::Clamp01(g_splash.v);
    const float veil = t < 0.5f ? 1.0f : 1.0f - Ease((t - 0.5f) / 0.5f);  // lifts at the end
    if (veil <= 0.004f) return;
    s.resetClip();
    // The veil is opaque while it holds: a translucent one lets the card behind
    // bleed through the GDI text and makes the wordmark look stained.
    cv::FillRect(s, 0, 0, s.w, s.h, cv::Hex(pal::Bg, veil));
    const float cx = (float)s.w / 2.0f;
    const float drift = Ease(cv::Clamp01(t / 0.7f));  // the backdrop mark breathes outwards
    const float cy = (float)s.h * 0.46f - U(18) * Ease(cv::Clamp01((t - 0.40f) / 0.60f));
    // The RC, blown up and out of focus behind the wordmark.
    cv::Glow(s, cx, cy, U(200) + U(40) * drift, cv::Hex(pal::Accent, 0.10f * veil));
    DrawMarkBlurred(s, cx, cy, U(470) + U(90) * drift, 0.30f * veil);
    DrawMarkBlurred(s, cx, cy, U(300) + U(50) * drift, 0.22f * veil);
    DrawMark(s, cx, cy, U(60), veil);
    // A hairline under the wordmark that fills up, so a first launch reads as work.
    const float ruleW = U(170), ruleY = (float)s.h * 0.46f + U(96);
    const float fill = cv::Clamp01(g_splashElapsed / 0.85f);
    cv::FillRoundRect(s, cx - ruleW / 2, ruleY, ruleW, U(2), U(1), cv::Hex(pal::White, 0.10f * veil));
    cv::FillRoundRect(s, cx - ruleW / 2, ruleY, ruleW * fill, U(2), U(1), cv::Hex(pal::Accent, 0.85f * veil));
}

static void DrawSplashText(HDC dc) {
    if (!g_splashOn) return;
    const float t = cv::Clamp01(g_splash.v);
    const float veil = t < 0.5f ? 1.0f : 1.0f - Ease((t - 0.5f) / 0.5f);
    if (veil <= 0.02f) return;
    const int w = S(kW);
    const int cy = (int)((float)S(LogicalH()) * 0.46f + (float)S(46) -
                         U(18) * Ease(cv::Clamp01((t - 0.40f) / 0.60f)));
    // One line: ROCHEATS STATUS CHECKER, wide-tracked, with the second half in the accent.
    GdiFlush();  // the veil was drawn with the rasteriser; GDI cannot see it yet
    const std::wstring a = L"ROCHEATS ", b = L"STATUS CHECKER";
    const int gap = S(10);
    const int room = w - S(2 * kPad + 8);
    double size = 27.0;
    int wa = 0, wb = 0;
    for (; size > 12.0; size -= 1.0) {  // shrink until the line fits the frame
        wa = TextSpacedW(dc, Font(size, true), a, S(2));
        wb = TextSpacedW(dc, Font(size, true), b, S(2));
        if (wa + gap + wb <= room) break;
    }
    const int x0 = w / 2 - (wa + gap + wb) / 2;
    TextSpaced(dc, Font(size, true), RefMix(pal::Bg, pal::Text, veil), x0, cy, a, S(2));
    TextSpaced(dc, Font(size, true), RefMix(pal::Bg, pal::Accent, veil), x0 + wa + gap, cy, b, S(2));
    // A tracked caption underneath, the way the checker labels its own sections.
    const std::wstring sub = L"WEAO  \u00B7  LIVE STATUS";
    const int ws = TextSpacedW(dc, Font(9, true), sub, S(3));
    TextSpaced(dc, Font(9, true), RefMix(pal::Bg, pal::Text3, veil), w / 2 - ws / 2, cy + S(30), sub, S(3));
}

// Copies the dashboard to the front buffer, then adds the settings panel.
static void Compose(const RECT& rc) {
    cv::Surface& s = g_front.s;
    GdiFlush();
    s.setClip(rc.left, rc.top, rc.right, rc.bottom);
    if (s.clipEmpty()) {
        s.resetClip();
        return;
    }
    for (int y = s.cy0; y < s.cy1; ++y)
        memcpy(s.row(y) + s.cx0, g_dash.s.row(y) + s.cx0, (size_t)(s.cx1 - s.cx0) * sizeof(uint32_t));

    if (g_sheetOpen) {
        const RECT p = PanelRect();
        if (!g_panelInline) {  // no room for a column: dim the dashboard behind the panel
            const int keep[4] = { s.cx0, s.cy0, s.cx1, s.cy1 };
            s.setClip(s.cx0, s.cy0, std::min(s.cx1, (int)p.left), s.cy1);
            cv::Dim(s, 0, 0, (int)p.left, s.h, 0.55f);
            s.setClip(keep[0], keep[1], keep[2], keep[3]);
        }
        RECT clip;
        if (IntersectRect(&clip, &rc, &p)) {
            s.setClip(clip.left, clip.top, clip.right, clip.bottom);
            DrawPanelShapes(s);
            SetTextClip(g_front.dc, clip);
            DrawPanelText(g_front.dc);
            SelectClipRgn(g_front.dc, nullptr);
        }
    }
    s.resetClip();
    if (g_fade.v < 0.999f) FadeFrame(s, Ease(g_fade.v) * 0.98f + 0.02f);
    if (g_splashOn) {  // the wordmark sits on top of everything, including the panel
        RECT all;
        GetClientRect(g_hwnd, &all);
        s.setClip(all.left, all.top, all.right, all.bottom);
        DrawSplashShapes(s);
        SetTextClip(g_front.dc, all);
        DrawSplashText(g_front.dc);
        SelectClipRgn(g_front.dc, nullptr);
        s.resetClip();
        GdiFlush();
    }
    if (!g_systemBorder) {  // thin border, unless Windows 11 draws one for us
        const int keep[4] = { s.cx0, s.cy0, s.cx1, s.cy1 };
        s.resetClip();
        cv::StrokeRoundRect(s, 0.0f, 0.0f, (float)s.w, (float)s.h, g_roundCorners ? U(8) : 0.0f, Hairline(),
                            cv::Hex(0x242835));
        s.setClip(keep[0], keep[1], keep[2], keep[3]);
    }
    GdiFlush();
    s.resetClip();
}

static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right > 0 && client.bottom > 0 && EnsureLayers(hdc, client.right, client.bottom)) {
        RenderDashboard(client);
        const RECT rc = ps.rcPaint;
        Compose(rc);
        BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, g_front.dc, rc.left, rc.top, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Tray icon + notifications
// ---------------------------------------------------------------------------
static void InitNid(NOTIFYICONDATAW& nid) {
    ZeroMemory(&nid, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
}

static std::wstring TrayTip() { return std::wstring(APP_NAME) + L"\n" + Headline(Count()); }

static bool AddTrayIcon() {
    NOTIFYICONDATAW nid;
    InitNid(nid);
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g_iconSmall;
    lstrcpynW(nid.szTip, TrayTip().c_str(), ARRAYSIZE(nid.szTip));
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        Shell_NotifyIconW(NIM_DELETE, &nid);  // a stale icon may be left from a timed-out add
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            g_trayAdded = false;
            return false;
        }
    }
    nid.uVersion = NOTIFYICON_VERSION_4;
    g_trayV4 = Shell_NotifyIconW(NIM_SETVERSION, &nid) != FALSE;
    g_trayAdded = true;
    return true;
}

static void EnsureTrayIcon() {
    if (AddTrayIcon()) {
        KillTimer(g_hwnd, TIMER_TRAY);
        return;
    }
    // The taskbar may not exist yet right after sign-in; keep trying for a while.
    if (++g_trayRetries <= 60) SetTimer(g_hwnd, TIMER_TRAY, 2000, nullptr);
    else KillTimer(g_hwnd, TIMER_TRAY);
}

static void RemoveTrayIcon() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid;
    InitNid(nid);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

static void UpdateTrayTip() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid;
    InitNid(nid);
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    lstrcpynW(nid.szTip, TrayTip().c_str(), ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// Native Windows notification (shown as a toast on Windows 10/11).
static void ShowNotification(const std::wstring& title, const std::wstring& text) {
    if (!g_trayAdded && !AddTrayIcon()) return;
    NOTIFYICONDATAW nid;
    InitNid(nid);
    nid.uFlags = NIF_INFO;
    lstrcpynW(nid.szInfoTitle, Clip(title, ARRAYSIZE(nid.szInfoTitle) - 1).c_str(), ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, Clip(text, ARRAYSIZE(nid.szInfo) - 1).c_str(), ARRAYSIZE(nid.szInfo));
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | (g_set.sound ? 0 : NIIF_NOSOUND);
    nid.hBalloonIcon = g_iconLarge;
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid) && AddTrayIcon()) Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static const wchar_t* const TXT_UP   = L"\U0001F7E2 Updated";       // green circle
static const wchar_t* const TXT_DOWN = L"\U0001F534 Not updated";   // red circle

static void SendTestNotification() {
    ShowNotification(L"VOLT is updated", std::wstring(TXT_DOWN) + L" \u2192 " + TXT_UP +
                                             L"\nThis is a test. Real alerts look like this.");
}

static void NotifyChanges(const std::vector<Change>& changes) {
    if (changes.empty()) return;
    std::vector<int> prods;
    for (const Change& c : changes) {
        bool seen = false;
        for (int i : prods) seen = seen || i == c.index;
        if (!seen) prods.push_back(c.index);
    }

    std::wstring title, body;
    if (prods.size() == 1) {
        const Product& p = g_products[prods[0]];
        if (changes.size() == 1) {
            const Change& c = changes[0];
            if (c.kind == 2) {
                title = p.name + L" " + p.version;
                body = L"New version.";
            } else if (c.kind == 0) {
                title = p.name + (c.now ? L" is updated" : L" is no longer updated");
                body = c.now ? std::wstring(TXT_DOWN) + L" \u2192 " + TXT_UP
                             : std::wstring(TXT_UP) + L" \u2192 " + TXT_DOWN;
                if (c.now && !p.version.empty()) body += L"\nVersion " + p.version;
            } else {
                title = p.name + (c.now ? L" is now detected" : L" is now undetected");
                body = c.now ? L"Undetected \u2192 Detected" : L"Detected \u2192 Undetected";
            }
        } else {
            title = p.name + L" status changed";
            for (const Change& c : changes) {
                if (!body.empty()) body += L"\n";
                if (c.kind == 0)
                    body += c.now ? std::wstring(TXT_DOWN) + L" \u2192 " + TXT_UP
                                  : std::wstring(TXT_UP) + L" \u2192 " + TXT_DOWN;
                else
                    body += c.now ? L"Undetected \u2192 Detected" : L"Detected \u2192 Undetected";
            }
        }
    } else {
        title = std::to_wstring(prods.size()) + L" products changed";
        const std::wstring heads[4] = { std::wstring(TXT_UP) + L": ", std::wstring(TXT_DOWN) + L": ",
                                        L"Detected: ", L"Undetected: " };
        for (int group = 0; group < 4; ++group) {
            std::wstring names;
            for (const Change& c : changes) {
                const int gidx = c.kind == 0 ? (c.now ? 0 : 1) : (c.now ? 2 : 3);
                if (gidx != group) continue;
                if (!names.empty()) names += L", ";
                names += g_products[c.index].name;
            }
            if (names.empty()) continue;
            if (!body.empty()) body += L"\n";
            body += heads[group] + names;
        }
    }
    ShowNotification(title, body);
}

// ---------------------------------------------------------------------------
// Applying a poll result
// ---------------------------------------------------------------------------
static const FetchItem* FindItem(const FetchResult& r, const Product& p) {
    const FetchItem* best = nullptr;
    for (const FetchItem& it : r.items) {
        if (Lower(it.title) != p.key) continue;
        // Same name on several platforms: prefer the Windows entry.
        if (!best || (lstrcmpiW(it.platform.c_str(), L"Windows") == 0 &&
                      lstrcmpiW(best->platform.c_str(), L"Windows") != 0))
            best = &it;
    }
    return best;
}

static void ApplyResult(const FetchResult& r) {
    std::vector<Change> changes;
    for (size_t i = 0; i < g_products.size(); ++i) {
        Product& p = g_products[i];
        const FetchItem* it = FindItem(r, p);
        if (!it) {
            p.tracked = false;
            continue;
        }
        p.tracked = true;
        bool dirty = false;
        const int nu = it->updated ? 1 : 0;
        if (p.updated != nu) {
            if (p.updated != -1) changes.push_back({ (int)i, 0, nu == 1 });  // -1 = first sighting
            p.updated = nu;
            dirty = true;
        }
        if (it->hasDetected) {
            const int nd = it->detected ? 1 : 0;
            if (p.detected != nd) {
                if (p.detected != -1) changes.push_back({ (int)i, 1, nd == 1 });
                p.detected = nd;
                dirty = true;
            }
        }
        if (p.version != it->version) {
            // a new version string while the status stayed put is its own event
            if (!p.version.empty() && !it->version.empty()) changes.push_back({ (int)i, 2, true });
            p.version = it->version;
            dirty = true;
        }
        if (p.kind != it->kind) {
            p.kind = it->kind;
            dirty = true;
        }
        if (p.updatedAt != it->updatedAt) {
            p.updatedAt = it->updatedAt;
            dirty = true;
        }
        if (p.rbx != it->rbx) {
            p.rbx = it->rbx;
            dirty = true;
        }
        if (dirty) SaveProduct(p);
    }

    RefreshRbxMap();

    for (const Change& c : changes) g_flash[c.index] = 1.0f;  // shown in the window even when alerts are off
    if (!changes.empty()) {
        bool several = false;
        for (const Change& c : changes) several = several || c.index != changes[0].index;
        if (several) {
            int count = 0;
            std::vector<int> seen;
            for (const Change& c : changes) {
                bool dup = false;
                for (int k : seen) dup = dup || k == c.index;
                if (!dup) {
                    seen.push_back(c.index);
                    ++count;
                }
            }
            g_lastChange = std::to_wstring(count) + L" products changed";
        } else {
            const Change& c = changes[0];
            g_lastChange = g_products[c.index].name + (c.kind == 0 ? (c.now ? L" updated" : L" no longer updated")
                                                                   : (c.now ? L" detected" : L" undetected"));
        }
        g_lastChangeAt = NowSecs();
        IniSet(L"Settings", L"LastChange", g_lastChange);
        IniSet(L"Settings", L"LastChangeAt", std::to_wstring(g_lastChangeAt));
    }

    if (!g_set.notifications) return;
    std::vector<Change> wanted;
    for (const Change& c : changes) {
        if (!g_products[c.index].notify) continue;
        // one switch per event, so "new versions" can be off while statuses stay on
        const bool allowed = c.kind == 2 ? g_set.alertVersions
                            : c.kind == 1 ? g_set.alertDetection
                                          : g_set.alertUpdates;
        if (allowed) wanted.push_back(c);
    }
    NotifyChanges(wanted);
}

// ---------------------------------------------------------------------------
// Roblox client build
// ---------------------------------------------------------------------------
static const wchar_t* const RBX_URL =
    L"https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer";

// Pulls "version" and "clientVersionUpload" out of the client-version response.
static bool ParseRobloxVersion(const std::string& body, std::wstring& ver, std::wstring& upload) {
    auto field = [&](const char* key) -> std::wstring {
        std::string needle = std::string("\"") + key + "\"";
        size_t at = body.find(needle);
        if (at == std::string::npos) return std::wstring();
        at = body.find(':', at + needle.size());
        if (at == std::string::npos) return std::wstring();
        at = body.find('"', at);
        if (at == std::string::npos) return std::wstring();
        const size_t end = body.find('"', at + 1);
        if (end == std::string::npos) return std::wstring();
        return std::wstring(body.begin() + (long)at + 1, body.begin() + (long)end);
    };
    ver = field("version");
    upload = field("clientVersionUpload");
    return !upload.empty();
}

struct RbxResult {
    bool ok = false;
    std::wstring version, upload;
};

static DWORD WINAPI RbxThread(LPVOID param) {
    const HWND hwnd = (HWND)param;
    RbxResult* out = new RbxResult();
    DWORD status = 0;
    std::string body;
    if (HttpGet(RBX_URL, status, body) && status == 200) {
        out->ok = ParseRobloxVersion(body, out->version, out->upload);
    }
    if (!PostMessageW(hwnd, WM_APP_RBX, 0, (LPARAM)out)) delete out;
    return 0;
}

static void PollRoblox() {
    if (g_rbxFetching) return;
    g_rbxFetching = true;
    HANDLE h = CreateThread(nullptr, 0, RbxThread, g_hwnd, 0, nullptr);
    if (h) CloseHandle(h);
    else g_rbxFetching = false;
}

// True when the product's build matches the one Roblox ships right now.
static bool OnLatestRoblox(const Product& p) {
    for (const auto& pair : g_lastRbxByProduct) {
        if (pair.first == p.key) return !g_rbxUpload.empty() && pair.second == g_rbxUpload;
    }
    return false;
}

// How many tracked products are behind the live build.
static int BehindCount() {
    int n = 0;
    for (const auto& pair : g_lastRbxByProduct)
        if (!g_rbxUpload.empty() && !pair.second.empty() && pair.second != g_rbxUpload) ++n;
    return n;
}

static void OnRobloxFetched(RbxResult* r) {
    g_rbxFetching = false;
    if (r->ok && !r->upload.empty()) {
        const bool changed = !g_rbxUpload.empty() && r->upload != g_rbxUpload;
        const bool first = g_rbxUpload.empty();
        g_rbxVersion = r->version;
        g_rbxUpload = r->upload;
        IniSet(L"Settings", L"RobloxVersion", g_rbxVersion);
        IniSet(L"Settings", L"RobloxUpload", g_rbxUpload);
        if (changed) {
            g_rbxPulse = 1.0f;
            if (g_set.notifications && g_set.alertRoblox) {
                ShowNotification(L"Roblox updated",
                                 (g_rbxVersion.empty() ? std::wstring(L"A new client build is out.")
                                                       : L"Now on " + g_rbxVersion) +
                                     L"\nExecutors may need to catch up.");
            }
            // The executors' currency flags just all changed: refresh the feed too.
            if (!g_fetching) StartFetch();
        }
        if (changed || first) {
            StartAnim();
            InvalidateAll();
            UpdateTrayTip();
        }
    }
    delete r;
}

static void RefreshRbxMap() {
    g_lastRbxByProduct.clear();
    for (const Product& p : g_products) g_lastRbxByProduct.push_back({ p.key, p.rbx });
}

static void StartFetch();
static void UpdateTrayTip();
static void InvalidateAll();
static void StartAnim();

static void OnFetched(FetchResult* r) {
    g_fetching = false;

    if (r->ok) {
        // A response that lists none of our products while we had data before is
        // more likely a hiccup than nine products vanishing at once: keep state.
        bool anyMatch = false, hadData = false;
        for (const Product& p : g_products) {
            anyMatch = anyMatch || FindItem(*r, p) != nullptr;
            hadData = hadData || p.updated != -1;
        }
        if (!anyMatch && hadData) r->ok = false;
    }

    if (r->ok) {
        ApplyResult(*r);
        g_everSucceeded = true;
        g_lastFailed = false;
        g_failText.clear();
        g_lastChecked = NowTime();
        ArmPoll((UINT)g_set.intervalMin * 60000u);
    } else {
        g_lastFailed = true;
        g_failText = r->rateLimited ? L"Rate limited by WEAO" : L"Can\u2019t reach WEAO";
        ArmPoll((UINT)(r->rateLimited ? r->retryAfterSec + 5 : RETRY_SECONDS) * 1000u);
    }
    delete r;

    UpdateTrayTip();
    StartAnim();
    InvalidateAll();
}

// ---------------------------------------------------------------------------
// Window behaviour
// ---------------------------------------------------------------------------
static bool g_ignoreUp = false;  // swallow a stale button-up after showing from the tray
static void RefreshHot();

static void ShowMainWindow() {
    const bool hidden = !IsWindowVisible(g_hwnd);
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    // A window shown from the tray does not receive the click that asked for it,
    // so ignore the matching button-up when it arrives late.
    if (hidden) {
        g_pressed = HIT_NONE;
        g_ignoreUp = true;
        RefreshHot();
    }
    SetForegroundWindow(g_hwnd);
}

static void HideToTray() {
    if (!g_trayAdded) {  // no tray to hide in: just minimise
        ShowWindow(g_hwnd, SW_MINIMIZE);
        return;
    }
    SaveWindowPos();
    // A hidden window cannot take focus, so it must not be asked to wait for it.
    if (GetCapture() == g_hwnd) ReleaseCapture();
    g_pressed = HIT_NONE;
    ShowWindow(g_hwnd, SW_HIDE);
    if (!g_set.trayHintShown) {
        g_set.trayHintShown = true;
        IniSetInt(L"Settings", L"TrayHintShown", 1);
        ShowNotification(L"Still running in the tray",
                         L"RoCheats Monitor keeps checking in the background. Right-click the tray icon to exit.");
    }
}

static void ExitApp() {
    SaveWindowPos();
    DestroyWindow(g_hwnd);
}

static void SetSheet(bool open) {
    if (g_sheetOpen == open) return;
    SnapshotRows();  // the list may have to slide aside for the panel
    g_sheetOpen = open;
    g_scrollTo = g_scrollY = 0.0;  // always opens at the top
    // A column of its own when the screen allows it, an overlay when it does not.
    const int wider = (int)std::lround(kPanelW * g_scale);
    const bool old = g_panelInline;
    g_panelInline = open && MonitorWidth() >= WinW() + wider;
    if (old != g_panelInline) LayoutWindow();
    StartAnim();
    InvalidateAll();
}

// Called before anything that changes which rows exist or where they sit: each
// row remembers its current top so it can slide to the new one.
// Remembers where every row is sitting, in slots, so a filter change can carry
// each row from its old place to its new one instead of snapping.
static void SnapshotRows() {
    for (int i = 0; i < ProductCount(); ++i) {
        g_rowFrom[i] = RowMatches(i) ? (float)RowShown(i) : -1.0f;
        g_rowPos[i].jump(0.0f);
        g_rowPos[i].target(1.0f);
    }
}

static void SetHot(int hit) {
    if (hit == g_hot) return;
    const int old = g_hot;
    g_hot = hit;
    // The bell explains itself in the card's second line while hovered.
    if (old >= HIT_BELL0 && old < HIT_BELL0 + MAX_PRODUCTS) InvalidateId(old);
    if (hit >= HIT_BELL0 && hit < HIT_BELL0 + MAX_PRODUCTS) InvalidateId(hit);
    StartAnim();
}

static void RefreshHot() {
    POINT pt;
    GetCursorPos(&pt);
    if (WindowFromPoint(pt) != g_hwnd) {
        SetHot(HIT_NONE);
        return;
    }
    ScreenToClient(g_hwnd, &pt);
    SetHot(HitTest(pt));
}

static void SetInterval(int minutes) {
    if (g_set.intervalMin == minutes) return;
    g_set.intervalMin = minutes;
    IniSetInt(L"Settings", L"IntervalMinutes", minutes);
    if (!g_fetching && !g_lastFailed) ArmPoll((UINT)minutes * 60000u);
    InvalidateDash(HeroRect());
}

static void HandleCommand(int cmd) {
    switch (cmd) {
    case ID_OPEN: ShowMainWindow(); break;
    case ID_CHECK: ManualCheck(); break;
    case ID_TEST: SendTestNotification(); break;
    case ID_EXIT: ExitApp(); break;
    }
}

static void ShowTrayMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ID_OPEN, L"Open");
    AppendMenuW(menu, MF_STRING | (g_fetching ? MF_GRAYED : 0), ID_CHECK, L"Check now");
    AppendMenuW(menu, MF_STRING, ID_TEST, L"Send test notification");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
    SetMenuDefaultItem(menu, ID_OPEN, FALSE);

    SetForegroundWindow(g_hwnd);  // required so the menu closes when clicking elsewhere
    const int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                                        x, y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    HandleCommand(cmd);
}

static void OnTrayMessage(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(lParam)) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
    case NIN_BALLOONUSERCLICK:
    case WM_LBUTTONDBLCLK:
        ShowMainWindow();
        break;
    case WM_LBUTTONUP:
        if (!g_trayV4) ShowMainWindow();  // version 4 sends NIN_SELECT instead
        break;
    case WM_CONTEXTMENU:
        if (g_trayV4) {
            ShowTrayMenu(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
            break;
        }
        // fall through
    case WM_RBUTTONUP:
        if (!g_trayV4 || LOWORD(lParam) == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            ShowTrayMenu(pt.x, pt.y);
        }
        break;
    }
}

static void Activate(int hit) {
    const int n = ProductCount();
    if (hit == HIT_BRAND) {
        ManualCheck();
    } else if (hit == HIT_NAV0) {
        SetSheet(false);
    } else if (hit == HIT_NAV1 || hit == HIT_TB_SETTINGS) {
        SetSheet(!g_sheetOpen);
    } else if (hit == HIT_TB_MIN) {
        ShowWindow(g_hwnd, SW_MINIMIZE);
    } else if (hit == HIT_TB_CLOSE) {
        HideToTray();
    } else if (hit == HIT_CHECK) {
        ManualCheck();
    } else if (hit == HIT_SHEET_CLOSE) {
        SetSheet(false);  // the only way out is the cross or Esc; a stray click stays put
    } else if (hit == HIT_SCRIM) {
        // deliberately nothing: clicking the dashboard behind the panel used to
        // dismiss it by accident, which lost whatever you were in the middle of.
    } else if (hit == HIT_TEST) {
        SendTestNotification();
    } else if (hit >= HIT_FILTER0 && hit < HIT_FILTER0 + 3) {
        const int want = hit - HIT_FILTER0;
        if (g_filter != want) {
            SnapshotRows();
            g_filter = want;
            g_layoutNext = true;  // resize once the rows have settled, not mid-flight
            InvalidateAll();      // the whole list and the chips change
            StartAnim();
        }
    } else if (hit == HIT_TGL0) {
        g_set.notifications = !g_set.notifications;
        IniSetInt(L"Settings", L"Notifications", g_set.notifications ? 1 : 0);
        InvalidateAll();  // the bells on the rows follow this switch
    } else if (hit == HIT_TGL0 + 1) {
        g_set.sound = !g_set.sound;
        IniSetInt(L"Settings", L"Sound", g_set.sound ? 1 : 0);
    } else if (hit == HIT_TGL0 + 2) {
        const bool want = !g_autostart;
        if (SetAutostart(want)) g_autostart = want;
    } else if (hit >= HIT_DENS0 && hit < HIT_DENS0 + 2) {
        g_set.density = (hit == HIT_DENS0 + 1) ? L"compact" : L"comfortable";
        SaveCustomisation();
        g_layoutNext = true;  // rows changed height, so re-measure
        InvalidateAll();
        StartAnim();
    } else if (hit >= HIT_ALERT0 && hit < HIT_ALERT0 + 4) {
        const int a = hit - HIT_ALERT0;
        AlertSet(a, !AlertOn(a));
        SaveCustomisation();
        InvalidateFront(nullptr);
    } else if (hit >= HIT_SWATCH0 && hit < HIT_SWATCH0 + SWATCHES) {
        const int k = hit - HIT_SWATCH0;
        const std::wstring bg = k < 7 ? SceneId(k) : L"colour";
        if (k == 7) {  // a colour of your own, cycled from a small palette
            static const wchar_t* const colours[] = { L"5b9dff", L"8a6bff", L"6fcf97", L"f0b243", L"ff7a5c",
                                                     L"4fc9d8" };
            int at = 0;
            for (int q = 0; q < 6; ++q)
                if (g_set.bgColor == colours[q]) at = q;
            g_set.bgColor = colours[(at + 1) % 6];
            IniSet(L"Settings", L"BackgroundColor", g_set.bgColor);
        }
        g_set.background = bg;
        IniSet(L"Settings", L"Background", bg);
        ReleaseImage();
        g_bgFade = 0.0f;  // cross-fade to the new background
        BuildBackground(g_dash.s.w, g_dash.s.h);
        InvalidateAll();
        StartAnim();
    } else if (hit == HIT_PICPICK) {
        // Pick a picture; keep the old one if the dialog is cancelled.
        wchar_t file[MAX_PATH] = L"";
        wmemcpy(file, g_set.imagePath.c_str(), std::min<size_t>(g_set.imagePath.size(), MAX_PATH - 1));
        OPENFILENAMEW ofn;
        ZeroMemory(&ofn, sizeof ofn);
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = g_hwnd;
        ofn.lpstrFilter = L"Pictures\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            g_set.imagePath = file;
            IniSet(L"Settings", L"BackgroundImage", file);
            g_set.background = L"image";
            IniSet(L"Settings", L"Background", g_set.background);
            ReleaseImage();
            g_bgFade = 0.0f;
            BuildBackground(g_dash.s.w, g_dash.s.h);
            InvalidateAll();
        }
        StartAnim();
    } else if (hit >= HIT_ACCENT0 && hit < HIT_ACCENT0 + ACCENTS) {
        wchar_t buf[8];
        swprintf(buf, 8, L"%06x", AccentAt(hit - HIT_ACCENT0));
        g_set.accent = buf;
        IniSet(L"Settings", L"Accent", g_set.accent);
        pal::Accent = AccentAt(hit - HIT_ACCENT0);
        g_bgFade = 0.0f;  // the backdrop picks up the new tint with a cross-fade
        BuildBackground(g_dash.s.w, g_dash.s.h);
        InvalidateAll();
        StartAnim();
    } else if (hit == HIT_THEME0 || hit == HIT_THEME0 + 1) {
        const std::wstring want = hit == HIT_THEME0 ? L"glass" : L"flat";
        if (g_set.theme != want) {
            g_set.theme = want;
            IniSet(L"Settings", L"Theme", want);
            g_style = want == L"flat" ? GlassStyle{ 0.0f, 1.0f, 0.0f, 0.14f } : GlassStyle();
            g_bgFade = 0.0f;
            InvalidateAll();
            StartAnim();
        }
    } else if (hit >= HIT_SEG0 && hit < HIT_SEG0 + SEG_COUNT) {
        SetInterval(INTERVALS[hit - HIT_SEG0]);
    } else if (hit >= HIT_BELL0 && hit < HIT_BELL0 + n) {
        Product& p = g_products[hit - HIT_BELL0];
        p.notify = !p.notify;
        SaveProduct(p);
    } else if (hit >= HIT_ROW0 && hit < HIT_ROW0 + n) {
        // The row body is inert on purpose: clicking a product must not open
        // anything. Only its detection chip toggles that product's alerts.
    }
    InvalidateId(hit);
    StartAnim();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {  // Explorer restarted
        g_trayAdded = false;
        g_trayRetries = 0;
        EnsureTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_NCCREATE:
        g_hwnd = hwnd;
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_NCHITTEST: {  // the strip at the top moves the window, like a title bar
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y >= 0 && pt.y < S(kTitleH) && HitTest(pt) == HIT_NONE) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        SetHot(HitTest(pt));
        if (!g_mouseTracking) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof tme;
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            g_mouseTracking = TrackMouseEvent(&tme) != FALSE;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_mouseTracking = false;
        SetHot(HIT_NONE);
        return 0;

    case WM_LBUTTONDOWN: {
        g_ignoreUp = false;
        const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        g_pressed = HitTest(pt);
        if (g_pressed != HIT_NONE) {
            SetCapture(hwnd);
            InvalidateId(g_pressed);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_ignoreUp) {  // the click that reopened the window, delivered late
            g_ignoreUp = false;
            g_pressed = HIT_NONE;
            return 0;
        }
        const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const int pressed = g_pressed;
        g_pressed = HIT_NONE;
        if (GetCapture() == hwnd) ReleaseCapture();
        InvalidateId(pressed);
        const int hit = HitTest(pt);
        if (hit != HIT_NONE && hit == pressed) Activate(hit);
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_pressed != HIT_NONE) {
            InvalidateId(g_pressed);
            g_pressed = HIT_NONE;
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursorW(nullptr, Clickable(HitTest(pt)) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_MOUSEWHEEL:
        if (g_sheetOpen) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_scrollTo -= (double)delta / (double)WHEEL_DELTA * 64.0;
            ClampScroll();
            StartAnim();
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_F5) {
            ManualCheck();
        } else if (wParam == VK_ESCAPE) {
            if (g_sheetOpen) SetSheet(false);
            else HideToTray();
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_POLL) {
            KillTimer(hwnd, TIMER_POLL);
            StartFetch();
        } else if (wParam == TIMER_TRAY) {
            EnsureTrayIcon();
        } else if (wParam == TIMER_ANIM) {
            OnAnimTick();
        } else if (wParam == TIMER_RBX) {
            // Watch Roblox on its own clock: a client build can ship at any moment.
            PollRoblox();
        } else if (wParam == TIMER_CLOCK) {
            if (!IsIconic(hwnd)) InvalidateDash(HeroRect());  // countdown to the next check
        }
        return 0;

    case WM_SHOWWINDOW:
        if (wParam) {
            g_fade.jump(0.0f);  // the window eases in instead of appearing
            g_fade.target(1.0f);
            SetTimer(hwnd, TIMER_CLOCK, 1000, nullptr);
            SetTimer(hwnd, TIMER_RBX, 20000, nullptr);
            PollRoblox();  // and do not wait 20 s for the first answer
            StartAnim();
            InvalidateAll();
        } else {
            KillTimer(hwnd, TIMER_CLOCK);
        KillTimer(hwnd, TIMER_RBX);
            g_pressed = HIT_NONE;  // nothing stays highlighted while the window is away
            SetHot(HIT_NONE);
        }
        break;

    case WM_APP_FETCHED:
        OnFetched((FetchResult*)lParam);
        return 0;

    case WM_APP_RBX:
        OnRobloxFetched((RbxResult*)lParam);
        return 0;

    case WM_APP_SHOW:
        ShowMainWindow();
        return 0;

    case WM_APP_TRAY:
        OnTrayMessage(wParam, lParam);
        return 0;

    case WM_DPICHANGED: {
        const RECT* suggested = (const RECT*)lParam;
        const POINT pt = { suggested->left, suggested->top };
        ApplyDpi(hwnd, HIWORD(wParam), &pt);
        return 0;
    }

    case WM_EXITSIZEMOVE:
        SaveWindowPos();
        RefreshHot();
        return 0;

    case WM_POWERBROADCAST:
        // Timers do not account for sleep: check shortly after waking up.
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            if (!g_fetching) {
                ArmPoll(10000);
                InvalidateDash(HeroRect());
            }
        }
        return TRUE;

    case WM_CLOSE:  // closing hides to the tray; use the tray menu to exit
        HideToTray();
        return 0;

    case WM_ENDSESSION:
        if (wParam) {
            SaveWindowPos();
            RemoveTrayIcon();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_POLL);
        KillTimer(hwnd, TIMER_TRAY);
        KillTimer(hwnd, TIMER_ANIM);
        KillTimer(hwnd, TIMER_CLOCK);
        KillTimer(hwnd, TIMER_RBX);
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Window chrome
// ---------------------------------------------------------------------------
// Dark context menus. Uses the uxtheme ordinals that Windows' own apps use
// (available since Windows 10 1903); silently skipped anywhere else.
static void EnableDarkMenus() {
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn rtlGetVersion = ntdll ? (RtlGetVersionFn)(void*)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    OSVERSIONINFOW vi;
    ZeroMemory(&vi, sizeof vi);
    vi.dwOSVersionInfoSize = sizeof vi;
    if (!rtlGetVersion || rtlGetVersion(&vi) != 0) return;
    if (vi.dwMajorVersion < 10 || vi.dwBuildNumber < 18362) return;

    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    typedef int(WINAPI * SetPreferredAppModeFn)(int);
    typedef void(WINAPI * FlushMenuThemesFn)(void);
    SetPreferredAppModeFn setMode = (SetPreferredAppModeFn)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    FlushMenuThemesFn flush = (FlushMenuThemesFn)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (setMode) setMode(2);  // ForceDark
    if (flush) flush();
}

// The window has no system frame; the app draws its own title bar and border.
// On Windows 11 the compositor is asked to round the corners.
static void StyleWindow(HWND hwnd) {
    HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dwm) return;
    typedef HRESULT(WINAPI * SetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
    SetAttrFn setAttr = (SetAttrFn)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!setAttr) return;
    const BOOL dark = TRUE;
    if (FAILED(setAttr(hwnd, 20, &dark, sizeof dark))) setAttr(hwnd, 19, &dark, sizeof dark);
    const DWORD round = 2;  // DWMWCP_ROUND
    if (SUCCEEDED(setAttr(hwnd, 33, &round, sizeof round))) {  // DWMWA_WINDOW_CORNER_PREFERENCE
        g_roundCorners = true;
        const COLORREF border = Ref(0x2A2E3A);
        g_systemBorder = SUCCEEDED(setAttr(hwnd, 34, &border, sizeof border));  // DWMWA_BORDER_COLOR
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int) {
    g_inst = hInst;
    const bool startHidden = cmdLine && (wcsstr(cmdLine, L"--tray") || wcsstr(cmdLine, L"/tray"));

    // Single instance: a second launch just brings the first one to the front.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!startHidden) {
            HWND other = FindWindowW(WND_CLASS, nullptr);
            if (other) {
                DWORD pid = 0;
                GetWindowThreadProcessId(other, &pid);
                AllowSetForegroundWindow(pid);
                PostMessageW(other, WM_APP_SHOW, 0, 0);
            }
        }
        CloseHandle(mutex);
        return 0;
    }

    QueryPerformanceFrequency(&g_qpf);
    EnableDarkMenus();

    InitIniPath();
    LoadSettings();
    g_autostart = AutostartEnabled();
    if (g_autostart) SetAutostart(true);  // keep the path current if the exe was moved

    pal::Accent = AccentAt(AccentIndex());
    g_style = g_set.theme == L"flat" ? GlassStyle{ 0.0f, 1.0f, 0.0f, 0.14f } : GlassStyle();
    LoadMark();
    BuildMarkBlur();
    for (int i = 0; i < MAX_PRODUCTS; ++i) g_rowFrom[i] = -1.0f;
    g_fade.jump(0.0f);
    g_splashOn = true;  // the wordmark plays once, over a dashboard that is already built
    g_splash.jump(0.0f);
    g_splashElapsed = 0.0;
    for (int a = 0; a < 4; ++a) g_alertTgl[a].set(AlertOn(a) ? 1.0f : 0.0f);
    g_tgl[0].set(g_set.notifications ? 1.0f : 0.0f);
    g_tgl[1].set(g_set.sound ? 1.0f : 0.0f);
    g_tgl[2].set(g_autostart ? 1.0f : 0.0f);
    g_seg.set((float)Max(0, IntervalIndex()));

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_iconLarge = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                    GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    g_iconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = g_iconLarge;
    wc.hIconSm = g_iconSmall;
    wc.lpszClassName = WND_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    // Restore the last position if it is still on a connected screen.
    bool havePos = g_savedX != INT_MIN && g_savedY != INT_MIN;
    if (havePos) {
        const RECT probe = { g_savedX, g_savedY, g_savedX + 200, g_savedY + 100 };
        havePos = MonitorFromRect(&probe, MONITOR_DEFAULTTONULL) != nullptr;
    }
    HWND hwnd = CreateWindowExW(WND_EXSTYLE, WND_CLASS, APP_NAME, WND_STYLE, havePos ? g_savedX : 100,
                                havePos ? g_savedY : 100, 400, 400, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    g_hwnd = hwnd;

    StyleWindow(hwnd);
    LoadLogos();
    ApplyDpi(hwnd, DpiForWindow(hwnd), nullptr);
    PlaceWindow(hwnd, !havePos);

    EnsureTrayIcon();
    StartFetch();

    if (!startHidden) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ClearFonts();
    FreeLayer(g_dash);
    FreeLayer(g_front);
    if (g_iconLarge) DestroyIcon(g_iconLarge);
    if (g_iconSmall) DestroyIcon(g_iconSmall);
    if (mutex) CloseHandle(mutex);
    return 0;
}
