// Core of the Endfield Streamline / frame-generation switcher: embedded file sets,
// game-folder state, file replacement and the two Endfield driver-profile settings.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../../upstream/NVIDIA-nvapi-87dca62/nvapi.h"
#include "../../upstream/NVIDIA-nvapi-87dca62/NvApiDriverSettings.h"
#include "resource.h"

enum Set { kStock = 0, kUpgraded = 1 };
struct Embedded { const wchar_t* name; int res[2]; const char* sha[2]; };

// Set 0 = the game's own Streamline 2.10.3 + DLSS-G 310.5.2; set 1 = Streamline 2.14.1 + DLSS-G 310.9.1.
static const Embedded kFiles[] = {
    {L"sl.common.dll",   {IDR_S0_COMMON, IDR_S1_COMMON},   {"eca8c0f1d4af654103a2f8b315e91fcd54ee4437570ac20c90ab78131c791274", "82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d"}},
    {L"sl.deepdvc.dll",  {IDR_S0_DEEPDVC, IDR_S1_DEEPDVC}, {"6faa17387f6aa826a76b9e398530599ffe553fc23668c6d6afea3c9078863548", "e4b7e83c9e67eab7023222af13de239241e8a09bdb63faf26f803e9fd0f3e9c9"}},
    {L"sl.dlss.dll",     {IDR_S0_DLSS, IDR_S1_DLSS},       {"164750168cb36a79e7552e7fc74bf5723347f97f69e5d466167aa09623615c03", "73bf52c0cfaa5900a8f3f4a91306e4625e7cca696dfb305aae44c9f97b582e1f"}},
    {L"sl.dlss_d.dll",   {IDR_S0_DLSSD, IDR_S1_DLSSD},     {"7a36e3452a28cf56d5656890a5dbeb089d452ed18fbe502c7d5e23209f7dcfce", "026b9f4f9ea848f2f5aa0e2024fc6b1c885f57eea5d41078124dba14f873b86b"}},
    {L"sl.dlss_g.dll",   {IDR_S0_DLSSG, IDR_S1_DLSSG},     {"29b1e42a5f5447b0e39687017f7fd70dff09c3b1c9a4e7a592f3faed4a053ae0", "f4a6b2b14dcc0b1485989e430d3b4e3a44ac1800b92ba1ad74f476e64fb2b09c"}},
    {L"sl.pcl.dll",      {IDR_S0_PCL, IDR_S1_PCL},         {"14941cd6bbfdfe339574a148998b0c75ce083be6327a3e1725f5d7d1db851dc6", "f13d51cfa05f4cd514df2026049e2db8adf359221713170ad386fd499915b582"}},
    {L"sl.reflex.dll",   {IDR_S0_REFLEX, IDR_S1_REFLEX},   {"2fb1d61aa0b693a2294929ffc91799b70e231b1f22f610bf8b3ff17b911d06a7", "0ce9725e3e03ea9e7f81d008b57f33ee365973d2e349131c8b1c3e3378fe2db0"}},
    {L"nvngx_dlssg.dll", {IDR_S0_NVDLSSG, IDR_S1_NVDLSSG}, {"f66510b59cc2407226d2b38eb5c023abf8e43a40f28cb4b4eee0ed3af8645816", "ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82"}},
};
constexpr int kFileCount = sizeof(kFiles) / sizeof(kFiles[0]);

inline std::string Sha256(const void* data, size_t size) {
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE h{}; unsigned char d[32]{}; std::string out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return out;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        while (size) { ULONG n = static_cast<ULONG>(size > 0x40000000 ? 0x40000000 : size); BCryptHashData(h, const_cast<unsigned char*>(p), n, 0); p += n; size -= n; }
        if (BCryptFinishHash(h, d, 32, 0) == 0) { char hex[65]{}; for (int i = 0; i < 32; ++i) sprintf_s(hex + i * 2, 3, "%02x", d[i]); out = hex; }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}
inline bool ReadAll(const std::wstring& path, std::vector<unsigned char>& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1LL << 30);
    if (ok) { out.resize(static_cast<size_t>(size.QuadPart)); DWORD got = 0; ok = ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size(); }
    CloseHandle(f); return ok;
}
inline std::string FileSha(const std::wstring& path) {
    std::vector<unsigned char> b; return ReadAll(path, b) ? Sha256(b.data(), b.size()) : std::string();
}
inline bool Resource(int id, const void*& data, DWORD& size) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA); if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r); if (!g) return false;
    data = LockResource(g); size = SizeofResource(nullptr, r); return data && size;
}
inline bool FileExists(const std::wstring& p) { const DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
inline bool IsGameFolder(const std::wstring& dir) { return FileExists(dir + L"\\Endfield.exe") && FileExists(dir + L"\\sl.interposer.dll"); }

inline bool GameRunning() {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W p{}; p.dwSize = sizeof(p); bool found = false;
    for (BOOL ok = Process32FirstW(s, &p); ok; ok = Process32NextW(s, &p)) if (_wcsicmp(p.szExeFile, L"Endfield.exe") == 0) { found = true; break; }
    CloseHandle(s); return found;
}

// Per file: 0 = stock, 1 = upgraded, -1 = missing, -2 = some other version.
struct FolderState { int file[kFileCount]{}; bool allKnown = true; int whole = -2; };
inline FolderState ReadFolder(const std::wstring& dir) {
    FolderState s; int stock = 0, up = 0;
    for (int i = 0; i < kFileCount; ++i) {
        const std::string h = FileSha(dir + L"\\" + kFiles[i].name);
        s.file[i] = h.empty() ? -1 : h == kFiles[i].sha[0] ? 0 : h == kFiles[i].sha[1] ? 1 : -2;
        if (s.file[i] == 0) ++stock; else if (s.file[i] == 1) ++up; else s.allKnown = false;
    }
    s.whole = stock == kFileCount ? kStock : up == kFileCount ? kUpgraded : s.allKnown ? -1 : -2;
    return s;
}

inline std::wstring AppDataDir() {
    PWSTR base = nullptr; std::wstring dir;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base) == S_OK) dir = std::wstring(base) + L"\\EndfieldFGSwitch";
    CoTaskMemFree(base); CreateDirectoryW(dir.c_str(), nullptr); return dir;
}
inline std::wstring Stamp() {
    SYSTEMTIME t{}; GetLocalTime(&t); wchar_t b[32]{};
    swprintf_s(b, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); return b;
}
inline void Log(const std::wstring& line) {
    const std::wstring path = AppDataDir() + L"\\fg-switch.log";
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    const std::wstring text = Stamp() + L"  " + line + L"\r\n";
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u(static_cast<size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, u.data(), n, nullptr, nullptr);
    DWORD w = 0; WriteFile(f, u.data(), static_cast<DWORD>(u.size() - 1), &w, nullptr); CloseHandle(f);
}

// Replaces the eight files with the chosen set. Every current file must already be one of
// the two embedded versions, so nothing unknown (for example after a game update) is overwritten.
inline bool SwitchFiles(const std::wstring& dir, Set target, std::wstring& error) {
    if (!IsGameFolder(dir)) { error = L"所选文件夹里没有 Endfield.exe 和 sl.interposer.dll。/ The folder does not contain Endfield.exe and sl.interposer.dll."; return false; }
    if (GameRunning()) { error = L"游戏正在运行，请先完全退出游戏。/ The game is running; close it first."; return false; }
    const FolderState before = ReadFolder(dir);
    if (!before.allKnown) { error = L"游戏目录中的 Streamline 文件不是本工具认识的版本（游戏可能已更新），为安全起见不做替换。/ The game's Streamline files are a version this tool does not know (the game may have been updated); nothing was replaced."; return false; }
    for (int i = 0; i < kFileCount; ++i) {
        const void* d; DWORD n;
        if (!Resource(kFiles[i].res[target], d, n) || Sha256(d, n) != kFiles[i].sha[target]) { error = L"本工具内部文件已损坏，请重新下载。/ This tool's embedded files are damaged; download it again."; return false; }
    }
    if (before.whole == target) { Log(L"files already set " + std::to_wstring(target)); return true; }
    const std::wstring backup = AppDataDir() + L"\\backup-" + Stamp();
    CreateDirectoryW(backup.c_str(), nullptr);
    for (int i = 0; i < kFileCount; ++i) CopyFileW((dir + L"\\" + kFiles[i].name).c_str(), (backup + L"\\" + kFiles[i].name).c_str(), FALSE);
    Log(L"switch to set " + std::to_wstring(target) + L" in " + dir + L"; backup " + backup);
    for (int i = 0; i < kFileCount; ++i) {
        if (before.file[i] == target) continue;
        const void* d; DWORD n; Resource(kFiles[i].res[target], d, n);
        const std::wstring final = dir + L"\\" + kFiles[i].name, temp = final + L".fgswitch-new";
        HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD w = 0; const bool wrote = f != INVALID_HANDLE_VALUE && WriteFile(f, d, n, &w, nullptr) && w == n && FlushFileBuffers(f);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        if (!wrote || FileSha(temp) != kFiles[i].sha[target] || !MoveFileExW(temp.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            error = std::wstring(L"无法写入 ") + kFiles[i].name + L"。请确认游戏已退出、本工具以管理员身份运行，然后重试。/ Could not write " + kFiles[i].name + L". Make sure the game is closed and the tool runs as administrator, then try again.";
            Log(L"write failed: " + std::wstring(kFiles[i].name) + L" error " + std::to_wstring(GetLastError()));
            return false;
        }
    }
    if (ReadFolder(dir).whole != target) { error = L"替换后校验未通过，请重试。/ Verification after replacing failed; try again."; return false; }
    Log(L"files verified as set " + std::to_wstring(target));
    return true;
}

// GPU tier: 50 = RTX 50 (Blackwell, up to 6x), 40 = RTX 40 (2x only), 0 = no DLSS Frame Generation.
struct GpuInfo { bool nvapi = false; int tier = 0; NvU32 driver = 0; std::wstring name; };
inline GpuInfo ReadGpu() {
    GpuInfo g; if (NvAPI_Initialize() != NVAPI_OK) return g;
    g.nvapi = true; NvAPI_ShortString branch{}; NvAPI_SYS_GetDriverAndBranchVersion(&g.driver, branch);
    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS]{}; NvU32 count = 0; NvAPI_EnumPhysicalGPUs(gpus, &count);
    for (NvU32 i = 0; i < count; ++i) {
        NV_GPU_ARCH_INFO arch{}; arch.version = NV_GPU_ARCH_INFO_VER;
        if (NvAPI_GPU_GetArchInfo(gpus[i], &arch) != NVAPI_OK) continue;
        const int tier = arch.architecture_id >= NV_GPU_ARCHITECTURE_GB200 ? 50 : arch.architecture_id >= NV_GPU_ARCHITECTURE_AD100 ? 40 : 0;
        if (tier >= g.tier) { g.tier = tier; NvAPI_ShortString n{}; NvAPI_GPU_GetFullName(gpus[i], n); g.name = std::wstring(n, n + strlen(n)); }
    }
    return g;
}

// The game's own driver profile; the tool never creates profiles or touches the global profile.
struct Profile {
    NvDRSSessionHandle h{}; NvDRSProfileHandle p{}; bool ok = false;
    Profile() {
        if (NvAPI_DRS_CreateSession(&h) != NVAPI_OK) return;
        if (NvAPI_DRS_LoadSettings(h) != NVAPI_OK) return;
        NvAPI_UnicodeString name{}; const wchar_t* exe = L"Endfield.exe";
        for (int i = 0; exe[i]; ++i) name[i] = exe[i];
        NVDRS_APPLICATION app{}; app.version = NVDRS_APPLICATION_VER;
        ok = NvAPI_DRS_FindApplicationByName(h, name, &p, &app) == NVAPI_OK;
    }
    ~Profile() { if (h) NvAPI_DRS_DestroySession(h); }
    // Returns the fixed multiplier the driver forces for the game, or 0 when the game decides.
    int Forced() {
        NVDRS_SETTING m{}, c{}; m.version = c.version = NVDRS_SETTING_VER;
        if (NvAPI_DRS_GetSetting(h, p, NGX_DLSSG_MODE_ID, &m) != NVAPI_OK || m.u32CurrentValue != NGX_DLSSG_MODE_ON) return 0;
        if (NvAPI_DRS_GetSetting(h, p, NGX_DLSSG_MULTI_FRAME_COUNT_ID, &c) != NVAPI_OK || c.u32CurrentValue == 0) return 0;
        return static_cast<int>(c.u32CurrentValue) + 1;
    }
    bool Put(NvU32 id, NvU32 v) {
        NVDRS_SETTING s{}; s.version = NVDRS_SETTING_VER; s.settingId = id; s.settingType = NVDRS_DWORD_TYPE; s.u32CurrentValue = v;
        return NvAPI_DRS_SetSetting(h, p, &s) == NVAPI_OK;
    }
    bool Remove(NvU32 id) { const NvAPI_Status r = NvAPI_DRS_DeleteProfileSetting(h, p, id); return r == NVAPI_OK || r == NVAPI_SETTING_NOT_FOUND; }
};
// multiplier 0 = remove the overrides so the game's own menu decides again.
inline bool SetMultiplier(int multiplier, std::wstring& error) {
    Profile pr;
    if (!pr.ok) { error = L"在 NVIDIA 驱动中找不到《终末地》的配置，请更新显卡驱动。/ The NVIDIA driver has no profile for Endfield; update the graphics driver."; return false; }
    const bool staged = multiplier ? pr.Put(NGX_DLSSG_MODE_ID, NGX_DLSSG_MODE_ON) && pr.Put(NGX_DLSSG_MULTI_FRAME_COUNT_ID, static_cast<NvU32>(multiplier - 1))
                                   : pr.Remove(NGX_DLSSG_MODE_ID) && pr.Remove(NGX_DLSSG_MULTI_FRAME_COUNT_ID);
    if (!staged || NvAPI_DRS_SaveSettings(pr.h) != NVAPI_OK) { error = L"写入 NVIDIA 驱动设置失败（需要管理员权限）。/ Writing the NVIDIA driver setting failed (administrator rights are needed)."; return false; }
    Profile check;
    if (!check.ok || check.Forced() != multiplier) { error = L"驱动设置写入后读回不一致。/ The driver setting did not read back as written."; return false; }
    Log(L"driver multiplier " + (multiplier ? std::to_wstring(multiplier) + L"x fixed" : std::wstring(L"returned to the game")));
    return true;
}
inline int CurrentMultiplier() { Profile pr; return pr.ok ? pr.Forced() : -1; }
