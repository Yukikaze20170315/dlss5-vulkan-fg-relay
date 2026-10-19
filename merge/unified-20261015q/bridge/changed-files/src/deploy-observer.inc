// Deployment observer: records, changes nothing -- except the opt-in V2
// experiment sl_plugins_dir= further down, which is off unless that key is set.
//
// A unified deployment (one file set for RTX 20-50, a Streamline version chosen
// in game, the NR runtime chosen per GPU, an early cuInit on Turing/Ampere) has
// to be built on facts about load order that differ per machine and cannot be
// read from source alone:
//   - when, from where and by whom the Streamline plugins, nvngx_dlssg.dll and
//     nvngx_dlssnr.dll are loaded (the caller decides whether a redirect can
//     reach them);
//   - whether this add-on attaches, and ReShade calls AddonInit, before the
//     driver creates its first VkInstance (an early cuInit must come first);
//   - how the low virtual address space looks at those moments (RTX 30: the
//     driver leaks a ~255 GB reservation per VkInstance and cuInit fails once
//     the space below 1 TB is gone);
//   - whether the game process is elevated and its folder writable.
//
// Everything goes to dlss5-observe.log in dlss5\dlss5_log (the previous run is
// kept as dlss5-observe.prev.log). observe=0 in dlss5-bridge.cfg turns it off.
// observe_sl_log=1 (default) also sets SL_LOG_LEVEL=1, SL_LOG_PATH and
// SL_LOG_NAME=dlss5-sl.log in this process's environment, unless they are
// already set, so Streamline 2.10.3 writes its own INFO log (plugin search
// path, every plugin it loads and its version, OTA decisions). slInit reads
// these after the game's preferences; nothing on disk or in the registry is
// changed, and rendering arguments are not touched.
//
// The loader callback runs under the loader lock on the loading thread: it only
// copies into a fixed ring and, for the modules that matter, captures return
// addresses. Names, versions, hashes and file I/O happen on the hook worker.

#include <locale.h>

struct ObsUnicodeString { USHORT Length; USHORT MaximumLength; PWSTR Buffer; };

struct ObsLoadRecord
{
    volatile LONG ready;      // 1 once the writer has finished the record
    ULONG         reason;     // 1 loaded, 2 unloaded
    LARGE_INTEGER qpc;
    DWORD         tid;
    void         *base;
    ULONG         size;
    USHORT        frames;
    bool          interesting;
    void         *stack[20];
    wchar_t       path[260];
};

static const LONG     kObsRing = 1024;
static ObsLoadRecord  g_obs_ring[kObsRing];
static volatile LONG  g_obs_write = -1;   // last claimed slot (monotonic)
static volatile LONG  g_obs_read;         // next slot to flush (flusher only writes it)
static volatile LONG  g_obs_dropped;
static bool           g_obs_on;
static LARGE_INTEGER  g_obs_t0, g_obs_freq;
static ULONGLONG      g_obs_attach_tick;
static char           g_obs_path[MAX_PATH];
static CRITICAL_SECTION g_obs_cs;
static bool           g_obs_cs_ready;
static volatile LONG  g_obs_addon_init_calls;
static bool           g_obs_late_va_done;
static wchar_t        g_obs_sl_signature[2048];

static double ObsMs(LARGE_INTEGER t)
{
    return g_obs_freq.QuadPart != 0
        ? double(t.QuadPart - g_obs_t0.QuadPart) * 1000.0 / double(g_obs_freq.QuadPart) : 0.0;
}

static void ObsLog(_Printf_format_string_ const char *fmt, ...)
{
    if (!g_obs_on || !g_obs_cs_ready) return;
    char line[2048];
    // UTF-8 for %ls: under the CRT's default "C" locale a path with any
    // non-ASCII character fails the conversion and the whole line comes out
    // empty. A private locale object leaves the module's global locale alone.
    static const _locale_t utf8 = _create_locale(LC_CTYPE, ".UTF8");
    va_list ap;
    va_start(ap, fmt);
    if (utf8 != nullptr) _vsnprintf_s_l(line, sizeof(line), _TRUNCATE, fmt, utf8, ap);
    else _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    SYSTEMTIME st;
    GetLocalTime(&st);
    static long written;
    EnterCriticalSection(&g_obs_cs);
    if (written < 4 * 1024 * 1024)
    {
        FILE *f = nullptr;
        if (fopen_s(&f, g_obs_path, "a") == 0 && f != nullptr)
        {
            written += fprintf(f, "%02u:%02u:%02u.%03u +%9.1fms  %s\n", st.wHour, st.wMinute,
                               st.wSecond, st.wMilliseconds, ObsMs(now), line);
            fclose(f);
        }
    }
    LeaveCriticalSection(&g_obs_cs);
    if (dlss5::eventlog::LooksAnomalous(line))
        g_events.Add(dlss5::eventlog::kBridge, dlss5::eventlog::kWarning, line);
}

static const wchar_t *ObsLeaf(const wchar_t *path)
{
    const wchar_t *s = wcsrchr(path, L'\\');
    return s != nullptr ? s + 1 : path;
}

// Resolved the way the rest of this file does, without psapi.h.
static DWORD ObsEnumModules(HMODULE *mods, DWORD cap)
{
    static const auto enum_modules = reinterpret_cast<PFN_EnumProcessModules>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"));
    DWORD need = 0;
    if (enum_modules == nullptr || !enum_modules(GetCurrentProcess(), mods, cap * sizeof(HMODULE), &need)) return 0;
    const DWORD n = need / sizeof(HMODULE);
    return n < cap ? n : cap;
}

// Modules whose load order, path and caller the deployment plan depends on.
static bool ObsInteresting(const wchar_t *leaf)
{
    if (_wcsnicmp(leaf, L"sl.", 3) == 0) return true;
    if (wcsstr(leaf, L"nvngx") != nullptr || wcsstr(leaf, L"NVNGX") != nullptr) return true;
    static const wchar_t *const names[] = {
        L"nvcuda.dll", L"nvoglv64.dll", L"vulkan-1.dll", L"nvapi64.dll", L"NvLowLatencyVk.dll",
        L"renodx-dlss5.addon64", L"renodx-mfgunlock.addon64", L"ReShade64.dll", L"d3d12.dll",
        L"D3D12Core.dll", L"nvwgf2umx.dll", L"nvcuda64.dll", L"nvptxJitCompiler64.dll",
    };
    for (const wchar_t *n : names)
        if (_wcsicmp(leaf, n) == 0) return true;
    return false;
}


static void ObsOnLoaderEvent(ULONG reason, const void *data)
{
    if (!g_obs_on || data == nullptr || (reason != 1 && reason != 2)) return;
    const auto *d = static_cast<const LdrDllNotificationData *>(data);
    // Claim a slot only while one is free, so a full ring never leaves a
    // claimed slot unwritten for the flusher to wait on.
    LONG seq;
    for (;;)
    {
        const LONG cur = InterlockedCompareExchange(&g_obs_write, 0, 0);
        if (cur + 1 - InterlockedCompareExchange(&g_obs_read, 0, 0) >= kObsRing)
        { InterlockedIncrement(&g_obs_dropped); return; }
        if (InterlockedCompareExchange(&g_obs_write, cur + 1, cur) == cur) { seq = cur + 1; break; }
    }
    ObsLoadRecord &r = g_obs_ring[seq % kObsRing];
    r.ready = 0;
    r.reason = reason;
    QueryPerformanceCounter(&r.qpc);
    r.tid = GetCurrentThreadId();
    r.base = d->DllBase;
    r.size = d->SizeOfImage;
    r.path[0] = 0;
    r.frames = 0;
    if (const auto *full = static_cast<const ObsUnicodeString *>(d->FullDllName))
    {
        const size_t want = full->Length / sizeof(wchar_t);
        const size_t n = want < _countof(r.path) - 1 ? want : _countof(r.path) - 1;
        if (full->Buffer != nullptr) memcpy(r.path, full->Buffer, n * sizeof(wchar_t));
        r.path[n] = 0;
    }
    r.interesting = ObsInteresting(ObsLeaf(r.path));
    // The loader notification only publishes bounded records. Hooking, stack
    // walking, hashing and file access belong to callers outside the loader lock.
    InterlockedExchange(&r.ready, 1);
}

static bool ObsSystemModule(const wchar_t *leaf)
{
    static const wchar_t *const names[] = { L"ntdll.dll", L"KERNELBASE.dll", L"KERNEL32.DLL" };
    for (const wchar_t *n : names)
        if (_wcsicmp(leaf, n) == 0) return true;
    return false;
}

// "module+0xRVA" for the frames outside the loader and this add-on: the first
// one is who asked for the library.
static void ObsDescribeStack(const ObsLoadRecord &r, char *out, size_t cch)
{
    out[0] = 0;
    size_t used = 0;
    int shown = 0;
    for (USHORT i = 0; i < r.frames && shown < 6; ++i)
    {
        HMODULE m = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                static_cast<LPCWSTR>(r.stack[i]), &m) || m == nullptr)
            continue;
        if (m == g_self) continue;
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t *leaf = ObsLeaf(path);
        if (ObsSystemModule(leaf)) continue;
        const int n = _snprintf_s(out + used, cch - used, _TRUNCATE, "%s%ls+0x%llx",
                                  shown == 0 ? "" : " < ", leaf,
                                  static_cast<unsigned long long>(
                                      reinterpret_cast<uintptr_t>(r.stack[i]) - reinterpret_cast<uintptr_t>(m)));
        if (n < 0) break;
        used += static_cast<size_t>(n);
        ++shown;
    }
    if (shown == 0) strcpy_s(out, cch, "(no caller outside the loader)");
}

static void ObsFileVersion(const wchar_t *path, char *out, size_t cch)
{
    strcpy_s(out, cch, "no version");
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (size == 0) return;
    void *buf = malloc(size);
    if (buf == nullptr) return;
    VS_FIXEDFILEINFO *fi = nullptr;
    UINT len = 0;
    if (GetFileVersionInfoW(path, 0, size, buf) &&
        VerQueryValueW(buf, L"\\", reinterpret_cast<void **>(&fi), &len) && fi != nullptr)
        _snprintf_s(out, cch, _TRUNCATE, "%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS),
                    LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    free(buf);
}

static void ObsFileFacts(const wchar_t *path, char *out, size_t cch)
{
    char ver[48];
    ObsFileVersion(path, ver, sizeof(ver));
    WIN32_FILE_ATTRIBUTE_DATA a = {};
    unsigned long long bytes = 0;
    SYSTEMTIME st = {};
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &a))
    {
        bytes = (static_cast<unsigned long long>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
        FILETIME local = {};
        FileTimeToLocalFileTime(&a.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &st);
    }
    char hash[80] = "not hashed (over 64 MB or unreadable)";
    Sha256File(path, hash, sizeof(hash));
    _snprintf_s(out, cch, _TRUNCATE, "version %s, %llu bytes, %04u-%02u-%02u %02u:%02u, sha256 %s",
                ver, bytes, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, hash);
}

// Allocations of 16 GB or more below 4 TB, and the free space below 1 TB.
static void ObsVaReport(const char *label)
{
    if (!g_obs_on) return;
    const uintptr_t kLimit = uintptr_t(4) << 40, kTiB = uintptr_t(1) << 40, kBig = uintptr_t(16) << 30;
    uintptr_t at = 0x10000, alloc_base = 0, alloc_size = 0, largest_free_1t = 0, free_1t = 0;
    DWORD alloc_state = 0;
    unsigned big = 0, regions = 0;
    char list[1024] = {};
    size_t used = 0;
    auto finish = [&]() {
        if (alloc_base != 0 && alloc_size >= kBig)
        {
            ++big;
            if (used < sizeof(list) - 64)
            {
                const int n = _snprintf_s(list + used, sizeof(list) - used, _TRUNCATE, " [%llxh %lluGB %s]",
                    static_cast<unsigned long long>(alloc_base >> 20),
                    static_cast<unsigned long long>(alloc_size >> 30),
                    alloc_state == MEM_RESERVE ? "res" : "mix");
                if (n > 0) used += static_cast<size_t>(n);
            }
        }
        alloc_base = 0; alloc_size = 0; alloc_state = 0;
    };
    while (at < kLimit)
    {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<void *>(at), &mbi, sizeof(mbi)) == 0) break;
        ++regions;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t end = base + mbi.RegionSize;
        if (mbi.State == MEM_FREE)
        {
            finish();
            if (base < kTiB)
            {
                const uintptr_t span = (end < kTiB ? end : kTiB) - base;
                free_1t += span;
                if (span > largest_free_1t) largest_free_1t = span;
            }
        }
        else
        {
            const uintptr_t ab = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
            if (ab != alloc_base) { finish(); alloc_base = ab; alloc_state = mbi.State; }
            else if (mbi.State != alloc_state) alloc_state = 0;
            alloc_size += mbi.RegionSize;
        }
        if (end <= at) break;
        at = end;
    }
    finish();
    ObsLog("[va] %s: free below 1 TB %llu GB (largest %llu GB); %u allocation(s) >= 16 GB below 4 TB "
           "(base in MB, size):%s  regions=%u nvoglv64=%s nvcuda=%s vulkan-1=%s",
           label, static_cast<unsigned long long>(free_1t >> 30),
           static_cast<unsigned long long>(largest_free_1t >> 30), big, used ? list : " none", regions,
           GetModuleHandleW(L"nvoglv64.dll") ? "loaded" : "no", GetModuleHandleW(L"nvcuda.dll") ? "loaded" : "no",
           GetModuleHandleW(L"vulkan-1.dll") ? "loaded" : "no");
}

static void ObsElevation()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { ObsLog("[process] token unreadable (%lu)", GetLastError()); return; }
    TOKEN_ELEVATION elev = {};
    DWORD got = 0;
    const bool e_ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &got) != FALSE;
    BYTE il_buf[64] = {};
    DWORD rid = 0;
    if (GetTokenInformation(token, TokenIntegrityLevel, il_buf, sizeof(il_buf), &got))
    {
        const auto *il = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(il_buf);
        rid = *GetSidSubAuthority(il->Label.Sid, *GetSidSubAuthorityCount(il->Label.Sid) - 1);
    }
    CloseHandle(token);
    ObsLog("[process] pid=%lu elevated=%s integrity=0x%lx (%s)", GetCurrentProcessId(),
           e_ok ? (elev.TokenIsElevated ? "yes" : "no") : "unknown", rid,
           rid >= 0x4000 ? "system" : rid >= 0x3000 ? "high" : rid >= 0x2000 ? "medium" : "low");
}

static void ObsProbeWritable(const wchar_t *dir, const char *label)
{
    wchar_t probe[MAX_PATH];
    _snwprintf_s(probe, _TRUNCATE, L"%ls\\dlss5-observe-%lu.probe", dir, GetCurrentProcessId());
    HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    const DWORD err = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    ObsLog("[process] %s writable=%s%s  (%ls)", label, h != INVALID_HANDLE_VALUE ? "yes" : "no",
           h != INVALID_HANDLE_VALUE ? "" : (err == ERROR_ACCESS_DENIED ? " access denied" : " error"), dir);
}

static void ObsGpus()
{
    for (DWORD i = 0; i < 8; ++i)
    {
        DISPLAY_DEVICEW dd = {};
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        if ((dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0) continue;
        ObsLog("[gpu] %lu: %ls  id=%ls%s", i, dd.DeviceString, dd.DeviceID,
               (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? " primary" : "");
    }
    // Driver version as the display class key records it (the same 32.0.x the
    // DLSS logs print), for every NVIDIA adapter instance.
    for (int i = 0; i < 16; ++i)
    {
        wchar_t key[128], ver[64] = {}, desc[128] = {}, match[128] = {};
        _snwprintf_s(key, _TRUNCATE,
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\%04d", i);
        DWORD cb = sizeof(ver);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"DriverVersion", RRF_RT_REG_SZ, nullptr, ver, &cb) != ERROR_SUCCESS) continue;
        cb = sizeof(desc);
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"DriverDesc", RRF_RT_REG_SZ, nullptr, desc, &cb);
        cb = sizeof(match);
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"MatchingDeviceId", RRF_RT_REG_SZ, nullptr, match, &cb);
        if (_wcsnicmp(match, L"pci\\ven_10de", 12) != 0) continue;
        ObsLog("[gpu] driver %ls  %ls  %ls", ver, desc, match);
    }
}

static void ObsFingerprint()
{
    wchar_t exe[MAX_PATH] = {}, dir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wcscpy_s(dir, exe);
    if (wchar_t *s = wcsrchr(dir, L'\\')) *s = 0;
    static const wchar_t *const files[] = {
        L"Endfield.exe", L"GameAssembly.dll", L"UnityPlayer.dll", L"sl.interposer.dll", L"sl.common.dll",
        L"sl.dlss.dll", L"sl.dlss_g.dll", L"sl.dlss_d.dll", L"sl.reflex.dll", L"sl.pcl.dll", L"sl.deepdvc.dll",
        L"nvngx_dlssg.dll", L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_deepdvc.dll", L"nvngx_dlssnr.dll",
    };
    ObsLog("[fingerprint] game folder %ls", dir);
    for (const wchar_t *f : files)
    {
        wchar_t p[MAX_PATH];
        _snwprintf_s(p, _TRUNCATE, L"%ls\\%ls", dir, f);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) { ObsLog("[fingerprint] %-20ls absent", f); continue; }
        char facts[256];
        ObsFileFacts(p, facts, sizeof(facts));
        ObsLog("[fingerprint] %-20ls %s", f, facts);
    }
}

// The set of Streamline/DLSS-G modules in the process, said only when it
// changes: one folder for all of them is the "all or nothing" a version switch
// must be able to verify.
static void ObsSlSnapshot(const char *trigger)
{
    HMODULE mods[1024];
    const DWORD count = ObsEnumModules(mods, 1024);
    if (count == 0) return;
    wchar_t sig[2048] = {};
    struct Item { wchar_t path[MAX_PATH]; } items[24];
    int n = 0;
    for (DWORD i = 0; i < count && n < 24; ++i)
    {
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
        const wchar_t *leaf = ObsLeaf(path);
        if (_wcsnicmp(leaf, L"sl.", 3) != 0 && _wcsicmp(leaf, L"nvngx_dlssg.dll") != 0) continue;
        wcscpy_s(items[n++].path, path);
        wcscat_s(sig, path);
        wcscat_s(sig, L"|");
    }
    if (wcscmp(sig, g_obs_sl_signature) == 0) return;
    wcscpy_s(g_obs_sl_signature, sig);
    ObsLog("[sl-set] after %s: %d Streamline/DLSS-G module(s) loaded", trigger, n);
    wchar_t first_dir[MAX_PATH] = {};
    bool one_dir = true;
    for (int i = 0; i < n; ++i)
    {
        char ver[48];
        ObsFileVersion(items[i].path, ver, sizeof(ver));
        wchar_t d[MAX_PATH];
        wcscpy_s(d, items[i].path);
        if (wchar_t *s = wcsrchr(d, L'\\')) *s = 0;
        if (_wcsicmp(ObsLeaf(items[i].path), L"sl.interposer.dll") != 0)
        {
            if (first_dir[0] == 0) wcscpy_s(first_dir, d);
            else if (_wcsicmp(first_dir, d) != 0) one_dir = false;
        }
        ObsLog("[sl-set]   %-20ls %-14s %ls", ObsLeaf(items[i].path), ver, d);
    }
    ObsLog("[sl-set]   plugins and DLSS-G from %s", one_dir ? "one folder" : "MORE THAN ONE folder");
}

// ---------------------------------------------------------------------------
// V2 experiment: Streamline plugin folder (sl_plugins_dir=<folder>)
//
// Off unless dlss5-bridge.cfg has sl_plugins_dir=, and only with observe on.
// The value is a folder relative to this add-on (dlss5\sl-2.14.1) or absolute.
// slInit is hooked the moment the loader maps sl.interposer.dll: that is before
// its DllMain and before LoadLibrary returns to the game, so the game cannot
// reach slInit first however slow the machine is. The detour logs the game's
// Preferences and, when the folder holds the full plugin set, calls the
// original with a copy whose pathsToPlugins names only that folder. The game's
// own structure is not written. Streamline 2.10.3 copies the paths
// (PluginManager::setPreferences), and findPlugins uses the first path that
// has any plugin, so a complete folder cannot be mixed with the game folder.
// The checks before redirecting are the struct's GUID (the exact one, or an
// entirely absent one, as this game sends) and the plausibility of the two
// lists it points at; structVersion is filled in on the copy rather than
// required, because the game's binding leaves it 0 and Streamline 2.10.3 reads
// neither field. Anything unexpected passes the game's Preferences through
// unchanged and says why.
// ---------------------------------------------------------------------------

struct SldPreferencesV1   // sl::Preferences, Streamline 2.10.3 sl_core_types.h, x64
{
    void            *next;
    unsigned char    structType[16];
    size_t           structVersion;
    bool             showConsole;
    uint32_t         logLevel;
    const wchar_t  **pathsToPlugins;
    uint32_t         numPathsToPlugins;
    const wchar_t   *pathToLogsAndData;
    void            *allocateCallback;
    void            *releaseCallback;
    void            *logMessageCallback;
    uint64_t         flags;
    const uint32_t  *featuresToLoad;
    uint32_t         numFeaturesToLoad;
    uint32_t         applicationId;
    uint32_t         engine;
    const char      *engineVersion;
    const char      *projectId;
    uint32_t         renderAPI;
};
static_assert(offsetof(SldPreferencesV1, showConsole) == 32, "BaseStructure is 32 bytes");
static_assert(offsetof(SldPreferencesV1, flags) == 88, "layout of sl::Preferences v1");
static_assert(sizeof(SldPreferencesV1) == 144, "sizeof sl::Preferences v1");

// {1CA10965-BF8E-432B-8DA1-6716D879FB14}
static const unsigned char kSldPreferencesType[16] = {
    0x65, 0x09, 0xa1, 0x1c, 0x8e, 0xbf, 0x2b, 0x43, 0x8d, 0xa1, 0x67, 0x16, 0xd8, 0x79, 0xfb, 0x14 };

static const wchar_t *const kSldRequired[] = {
    L"sl.common.dll", L"sl.deepdvc.dll", L"sl.dlss_d.dll", L"sl.dlss.dll", L"sl.dlss_g.dll",
    L"sl.reflex.dll", L"sl.pcl.dll", L"nvngx_dlssg.dll", L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_deepdvc.dll" };

typedef int (*PFN_SldSlInit)(const SldPreferencesV1 *pref, uint64_t sdk_version);
static wchar_t        g_sld_dir[MAX_PATH];     // empty: experiment off
static const wchar_t *g_sld_paths[1];
static bool           g_sld_complete;          // every required file is in g_sld_dir
static volatile LONG  g_sld_runtime_active;
static PVOID volatile g_sld_original;
static void          *g_sld_target;
static volatile LONG  g_sld_hook_state;        // 0 not tried, 1 hooked, -1 failed
static int            g_sld_hook_status;       // MinHook status, or -100 export not found
static wchar_t        g_sld_missing[64];

static const BridgeNgxRuntimeInfo* BridgeNgxFeatureInfo()
{
    // Startup owns g_sld_paths for the lifetime of the process. A panel choice
    // only changes next-launch state; NR must use the initialized SL stack.
    // No accepted redirect (including original/safe mode) keeps NGX defaults.
    if (!InterlockedCompareExchange(&g_sld_runtime_active, 0, 0)) return nullptr;
    static const BridgeNgxRuntimeInfo info = {{g_sld_paths, 1}, nullptr, {nullptr, 0, false}};
    return &info;
}

// Called from DllMain (ObsAttach). Reads the key the way CfgKeyInt does: start
// of a line, last match wins.
static void SldReadConfig(const wchar_t *addon_dir)
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_self, path, MAX_PATH);
    char *sl = strrchr(path, (char)92);
    if (sl == nullptr) return;
    strcpy_s(sl + 1, MAX_PATH - (sl + 1 - path), "dlss5-bridge.cfg");
    FILE *f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || f == nullptr) return;
    char line[512], value[MAX_PATH] = {};
    static const char key[] = "sl_plugins_dir=";
    while (fgets(line, sizeof(line), f) != nullptr)
    {
        if (_strnicmp(line, key, sizeof(key) - 1) != 0) continue;
        strncpy_s(value, line + sizeof(key) - 1, _TRUNCATE);
        for (size_t n = strlen(value); n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r' ||
             value[n - 1] == ' ' || value[n - 1] == '\\' || value[n - 1] == '/'); --n)
            value[n - 1] = 0;
    }
    fclose(f);
    if (value[0] == 0) return;
    wchar_t wide[MAX_PATH] = {};
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, wide, MAX_PATH) == 0 &&
        MultiByteToWideChar(CP_ACP, 0, value, -1, wide, MAX_PATH) == 0)
        return;
    for (wchar_t *c = wide; *c; ++c) if (*c == L'/') *c = L'\\';
    const bool absolute = (wide[0] != 0 && wide[1] == L':') || (wide[0] == L'\\' && wide[1] == L'\\');
    wchar_t joined[MAX_PATH] = {};
    if (absolute) wcscpy_s(joined, wide);
    else _snwprintf_s(joined, _TRUNCATE, L"%ls\\%ls", addon_dir, wide);
    if (GetFullPathNameW(joined, MAX_PATH, g_sld_dir, nullptr) == 0) wcscpy_s(g_sld_dir, joined);
    g_sld_paths[0] = g_sld_dir;
    g_sld_complete = true;
    for (const wchar_t *req : kSldRequired)
    {
        wchar_t p[MAX_PATH];
        _snwprintf_s(p, _TRUNCATE, L"%ls\\%ls", g_sld_dir, req);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES)
        {
            g_sld_complete = false;
            wcscpy_s(g_sld_missing, req);
            break;
        }
    }
}

#include "deployment.inc"

static void SldDescribePreferences(const SldPreferencesV1 *p, const char *label) {
    // Never follow strings or lists merely to print diagnostic data.
    ObsLog("[sl-dir] %s: version=%zu flags=0x%llx renderAPI=%u paths=%u features=%u",
           label,p->structVersion,static_cast<unsigned long long>(p->flags),p->renderAPI,
           p->numPathsToPlugins,p->numFeaturesToLoad);
}
static bool SldCopyPreferences(const SldPreferencesV1* source,SldPreferencesV1* copy) {
    __try { if(!source) return false; memcpy(copy,source,sizeof(*copy)); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool SldReadable(const void* p,size_t size) {
    if(!p || reinterpret_cast<uintptr_t>(p)+size<reinterpret_cast<uintptr_t>(p)) return false;
    auto current=reinterpret_cast<uintptr_t>(p);
    const auto end=current+size;
    while(current<end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(reinterpret_cast<void*>(current),&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT ||
           (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const auto next=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        if(next<=current) return false;
        current=next;
    }
    return true;
}
static bool SldFieldsPlausible(const SldPreferencesV1 *p)
{
    if (p->structVersion > 1 || p->next != nullptr || p->renderAPI != 2 ||
        p->numPathsToPlugins > 64 || p->numFeaturesToLoad > 256) return false;
    // A list named with a count of zero may be null; anything else must point at
    // readable memory, because Streamline copies every entry it is told about.
    if (p->numPathsToPlugins != 0 && !SldReadable(p->pathsToPlugins,p->numPathsToPlugins*sizeof(void*))) return false;
    if (p->numFeaturesToLoad != 0 && !SldReadable(p->featuresToLoad,p->numFeaturesToLoad*sizeof(uint32_t))) return false;
    return true;
}

static int SldSlInit(const SldPreferencesV1 *pref, uint64_t sdk_version)
{

    ObsLog("[sl-dir] slInit called on thread %lu, sdkVersion=0x%llx", GetCurrentThreadId(),
           static_cast<unsigned long long>(sdk_version));
    const SldPreferencesV1 *use = pref;
    SldPreferencesV1 copy{};
    SldPreferencesV1 snapshot{};
    const SldPreferencesV1* const original_pref=pref;
    if (!SldCopyPreferences(pref,&snapshot))
        ObsLog("[sl-dir] Preferences is null: passed through");
    else
    {
        pref=&snapshot;
        SldDescribePreferences(pref, "game");
        // What the game sends (measured twice): structType all zero and
        // structVersion 0, with every other field of the v1 layout consistent
        // (flags 0x85, featuresToLoad 0,1000,3, renderAPI 2, numPathsToPlugins 0).
        // The IL2CPP binding zero-initialises the struct and fills only the fields
        // it needs. Streamline 2.10.3 reads neither field -- its slInit calls
        // PluginManager::setPreferences, which copies the paths and stores the
        // flags -- which is why the game works as it is. Two shapes are accepted,
        // and nothing else: the exact sl::Preferences GUID, or a struct whose
        // GUID is entirely absent. The copy handed to Streamline then has both
        // fields filled in, so a Streamline that does check them would still see a
        // well-formed struct; the game's own memory is not written.
        const bool guid_matches = memcmp(pref->structType, kSldPreferencesType, sizeof(kSldPreferencesType)) == 0;
        bool guid_absent = true;
        for (unsigned char c : pref->structType) if (c != 0) { guid_absent = false; break; }
        const bool field_ok = SldFieldsPlausible(pref);
        if (!guid_matches && !guid_absent)
            ObsLog("[sl-dir] NOT redirected: Preferences carries neither sl::Preferences' GUID nor an empty one, so this "
                   "build does not know the layout in front of it");
        else if (!field_ok)
            ObsLog("[sl-dir] NOT redirected: Preferences' counts and pointers do not read as valid (counts out of range, "
                   "or a list that cannot be read); the game's call goes through unchanged");
        else if (!g_sld_complete)
            ObsLog("[sl-dir] NOT redirected: %ls is missing from %ls", g_sld_missing, g_sld_dir);
        else
        {
            copy = *pref;
            memcpy(copy.structType, kSldPreferencesType, sizeof(kSldPreferencesType));
            copy.structVersion = 1;
            copy.pathsToPlugins = g_sld_paths;
            copy.numPathsToPlugins = 1;
            use = &copy;
            ObsLog("[sl-dir] REDIRECTED: Streamline is given pathsToPlugins[0]=%ls%s (the game's structure is unchanged)",
                   g_sld_dir, guid_absent ? "; the copy's GUID and structVersion, absent from the game's struct, are filled in"
                                          : "");
        }
    }
    const auto original=reinterpret_cast<PFN_SldSlInit>(InterlockedCompareExchangePointer(&g_sld_original,nullptr,nullptr));
    const int result = original(use, sdk_version);
    const LONG runtime_active = result == 0 && use == &copy ? 1 : 0;
    InterlockedExchange(&g_sld_runtime_active, runtime_active);
    if (runtime_active)
        ObsLog("[sl-dir] bridge NGX feature search path follows initialized Streamline: %ls", g_sld_dir);
    ObsLog("[sl-dir] slInit returned %d", result);
    deployment::SlResult(result, use != original_pref);
    return result;
}

// Intercept symbol resolution from AddonInit, outside the loader lock. This
// catches the loading thread synchronously before it can call slInit, without
// patching the newly mapped image or suspending threads in a DLL notification.
using SldGetProc = FARPROC (WINAPI*)(HMODULE,LPCSTR);
static SldGetProc g_sld_get_proc;
static FARPROC WINAPI SldResolve(HMODULE module,LPCSTR name) {
    const FARPROC original=g_sld_get_proc(module,name);
    if(!original || !g_sld_complete || reinterpret_cast<uintptr_t>(name)<=65535 || strcmp(name,"slInit")!=0) return original;
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(module,path,32768) || _wcsicmp(path,(deployment::game/L"sl.interposer.dll").c_str())!=0) return original;
    HMODULE owner=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(original),&owner)) return original;
    const auto prior=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_sld_original),reinterpret_cast<PVOID>(original),nullptr);
    if(prior && prior!=reinterpret_cast<PVOID>(original)) return original;
    return reinterpret_cast<FARPROC>(&SldSlInit);
}
static void SldInstallResolver() {
    if(!g_sld_complete || g_sld_hook_state) return;
    void* target=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcAddress"));
    MH_STATUS result=MH_Initialize();
    if(result==MH_OK || result==MH_ERROR_ALREADY_INITIALIZED)
        result=MH_CreateHook(target,reinterpret_cast<void*>(&SldResolve),reinterpret_cast<void**>(&g_sld_get_proc));
    if(result==MH_OK && PinHookModule(reinterpret_cast<void*>(&SldResolve))) result=MH_EnableHook(target);
    else if(result==MH_OK) result=MH_ERROR_MEMORY_ALLOC;
    g_sld_target=target; g_sld_hook_status=result;
    InterlockedExchange(&g_sld_hook_state,result==MH_OK ? 1:-1);
}

static void SldReport()
{
    static LONG reported;
    const LONG state = InterlockedCompareExchange(&g_sld_hook_state, 0, 0);
    if (state == 0 || InterlockedCompareExchange(&reported, 1, 0) != 0) return;
    if (state == 1)
        ObsLog("[sl-dir] slInit resolver armed at %p from AddonInit, outside DLL notification", g_sld_target);
    else if (g_sld_hook_status == -100)
        ObsLog("[sl-dir] slInit NOT hooked: export not found in sl.interposer.dll; Streamline runs as the game set it");
    else
        ObsLog("[sl-dir] slInit NOT hooked: MinHook status %d at %p; Streamline runs as the game set it",
               g_sld_hook_status, g_sld_target);
}

// From the worker: the experiment folder's contents, for comparing with what loads.
static void SldDescribeFolder()
{
    if (g_sld_dir[0] == 0) return;
    ObsLog("[sl-dir] experiment on: sl_plugins_dir -> %ls (%s)", g_sld_dir,
           g_sld_complete ? "all required files present" : "INCOMPLETE, slInit will not be redirected");
    for (const wchar_t *req : kSldRequired)
    {
        wchar_t p[MAX_PATH];
        _snwprintf_s(p, _TRUNCATE, L"%ls\\%ls", g_sld_dir, req);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) { ObsLog("[sl-dir]   %-18ls absent", req); continue; }
        char facts[256];
        ObsFileFacts(p, facts, sizeof(facts));
        ObsLog("[sl-dir]   %-18ls %s", req, facts);
    }
    if (GetModuleHandleW(L"sl.interposer.dll") != nullptr && InterlockedCompareExchange(&g_sld_hook_state, 0, 0) == 0)
        ObsLog("[sl-dir] sl.interposer.dll was already loaded when this add-on attached: too late to redirect");
}

static void ObsFlush()
{
    if (!g_obs_on) return;
    static SRWLOCK lock = SRWLOCK_INIT;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    const LONG last = InterlockedCompareExchange(&g_obs_write, 0, 0);
    while (g_obs_read <= last)
    {
        ObsLoadRecord &r = g_obs_ring[g_obs_read % kObsRing];
        if (InterlockedCompareExchange(&r.ready, 0, 0) == 0) break;   // still being written
        const wchar_t *leaf = ObsLeaf(r.path);
        if (r.reason == 2)
            ObsLog("[ldr] @%.1fms unload %ls base=%p", ObsMs(r.qpc), r.path, r.base);
        else if (!r.interesting)
            ObsLog("[ldr] @%.1fms load tid=%lu %ls", ObsMs(r.qpc), r.tid, r.path);
        else
        {
            char caller[640], facts[256];
            ObsDescribeStack(r, caller, sizeof(caller));
            ObsFileFacts(r.path, facts, sizeof(facts));
            ObsLog("[ldr] @%.1fms LOAD tid=%lu %ls base=%p size=0x%lx", ObsMs(r.qpc), r.tid, r.path, r.base, r.size);
            ObsLog("[ldr]     caller: %s", caller);
            ObsLog("[ldr]     file: %s", facts);
            if (_wcsicmp(leaf, L"nvoglv64.dll") == 0) ObsVaReport("after nvoglv64.dll loaded");
            if (_wcsicmp(leaf, L"nvcuda.dll") == 0) ObsVaReport("after nvcuda.dll loaded");
            if (_wcsicmp(leaf, L"sl.interposer.dll") == 0) ObsVaReport("after sl.interposer.dll loaded");
            if (_wcsnicmp(leaf, L"sl.", 3) == 0 || _wcsicmp(leaf, L"nvngx_dlssg.dll") == 0)
                ObsSlSnapshot("a Streamline/DLSS-G load");
        }
        InterlockedExchange(&r.ready, 0);
        InterlockedIncrement(&g_obs_read);
    }
    SldReport();
    const LONG dropped = InterlockedExchange(&g_obs_dropped, 0);
    if (dropped != 0) ObsLog("[ldr] %ld loader event(s) dropped: the ring was full", dropped);
    if (!g_obs_late_va_done && GetTickCount64() - g_obs_attach_tick > 90000)
    {
        g_obs_late_va_done = true;
        ObsVaReport("90 s after attach");
        ObsSlSnapshot("90 s");
    }
    ReleaseSRWLockExclusive(&lock);
}

static void ObsListLoadedModules()
{
    HMODULE mods[1024];
    const DWORD count = ObsEnumModules(mods, 1024);
    ObsLog("[ldr] %lu module(s) already loaded when this add-on attached; the relevant ones:", count);
    for (DWORD i = 0; i < count; ++i)
    {
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
        if (ObsInteresting(ObsLeaf(path))) ObsLog("[ldr]   %ls base=%p", path, mods[i]);
    }
}

// SL_LOG_* are read by slInit (Streamline 2.10.3, sl.cpp ConfigureLogOverrides)
// after the game's own preferences. Set only when absent, so a user's or a
// support thread's values win.
static void ObsEnableStreamlineLog(const wchar_t *dir)
{
    if (CfgKeyInt("observe_sl_log", 1) == 0) { ObsLog("[sl-log] observe_sl_log=0: Streamline's own log left as the game set it"); return; }
    if (GetEnvironmentVariableW(L"SL_LOG_LEVEL", nullptr, 0) != 0 ||
        GetEnvironmentVariableW(L"SL_LOG_PATH", nullptr, 0) != 0)
    {
        ObsLog("[sl-log] SL_LOG_LEVEL or SL_LOG_PATH is already set in this process; left unchanged");
        return;
    }
    // Streamline reads SL_LOG_PATH with GetEnvironmentVariableA and decodes the
    // ANSI bytes as UTF-8, so a non-ASCII folder (E:\测试用\...) arrives mangled.
    // Its 8.3 short name is ASCII where the volume keeps short names.
    wchar_t ascii[MAX_PATH] = {};
    wcscpy_s(ascii, dir);
    auto is_ascii = [](const wchar_t *s) { for (; *s; ++s) if (*s > 0x7e) return false; return true; };
    if (!is_ascii(ascii))
    {
        wchar_t shortp[MAX_PATH] = {};
        const DWORD n = GetShortPathNameW(dir, shortp, MAX_PATH);
        if (n == 0 || n >= MAX_PATH || !is_ascii(shortp))
        {
            ObsLog("[sl-log] not set: the folder name is not ASCII and has no ASCII short name, which "
                   "Streamline would decode wrongly (%ls)", dir);
            return;
        }
        wcscpy_s(ascii, shortp);
    }
    const bool ok = SetEnvironmentVariableW(L"SL_LOG_LEVEL", L"1") &&
                    SetEnvironmentVariableW(L"SL_LOG_PATH", ascii) &&
                    SetEnvironmentVariableW(L"SL_LOG_NAME", L"dlss5-sl.log");
    ObsLog("[sl-log] %s: SL_LOG_LEVEL=1 SL_LOG_PATH=%ls SL_LOG_NAME=dlss5-sl.log (process environment only; "
           "Streamline reads it at slInit%s)", ok ? "set" : "FAILED", ascii,
           GetModuleHandleW(L"sl.interposer.dll") ? ", but sl.interposer.dll is ALREADY loaded, so slInit may have run" : "");
}

// DllMain, after g_self and before RegisterWithReShade's callers act on hooks.
static void ObsAttach()
{
    QueryPerformanceFrequency(&g_obs_freq);
    QueryPerformanceCounter(&g_obs_t0);
    g_obs_attach_tick = GetTickCount64();
    g_obs_on = CfgKeyInt("observe", 1) != 0;
    if (!g_obs_on) return;
    InitializeCriticalSection(&g_obs_cs);
    g_obs_cs_ready = true;

    // In the suite's log folder (g_log_dir, set before this runs).
    char prev[MAX_PATH] = {};
    if (LogNarrowPath("dlss5-observe.log", g_obs_path, MAX_PATH) &&
        LogNarrowPath("dlss5-observe.prev.log", prev, MAX_PATH))
        MoveFileExA(g_obs_path, prev, MOVEFILE_REPLACE_EXISTING);
    wchar_t dir[MAX_PATH] = {};
    GetModuleFileNameW(g_self, dir, MAX_PATH);
    if (wchar_t *s = wcsrchr(dir, L'\\')) *s = 0;

    ObsLog("dlss5-bridge %s deployment observer (records only; observe=0 turns it off)", BRIDGE_VERSION);
    ObsLog("[attach] DllMain thread=%lu", GetCurrentThreadId());
    ObsElevation();
    ObsProbeWritable(dir, "add-on folder");
    wchar_t local[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) != 0) ObsProbeWritable(local, "%LOCALAPPDATA%");
    ObsEnableStreamlineLog(g_log_dir);
    SldReadConfig(dir);
    ObsListLoadedModules();
    ObsVaReport("at DllMain attach");
}

// Fingerprint and GPU list read files and the registry: kept off DllMain.
static void ObsAfterAttach()
{
    if (!g_obs_on) return;
    static LONG done;
    if (InterlockedCompareExchange(&done, 1, 0) != 0) return;
    ObsGpus();
    ObsFingerprint();
    SldDescribeFolder();
}

// ReShade calls AddonInit right after LoadLibrary, outside DllMain, and again
// for an add-on it keeps registered across a Vulkan instance teardown. Logged
// only; it must return true or ReShade unloads the add-on.
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
    deployment::Initialize();
    SldInstallResolver();
    deployment::StartMaintenance();
    TriggerHookScan();
    const LONG n = InterlockedIncrement(&g_obs_addon_init_calls);
    if (g_obs_on)
    {
        ObsLog("[attach] AddonInit call %ld thread=%lu addon=%p reshade=%p", n, GetCurrentThreadId(),
               addon_module, reshade_module);
        ObsVaReport(n == 1 ? "at first AddonInit" : "at a later AddonInit");
        ObsFlush();
    }
    return true;
}
