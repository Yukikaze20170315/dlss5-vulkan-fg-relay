#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cassert>
#include <vector>
#include <fstream>
#include <iterator>
#include <string>
#include <cstring>
#include <intrin.h>

namespace reshade { namespace api {
struct effect_runtime { void *window; void *get_hwnd() const { return window; } };
} }
struct Hook { void *target = nullptr, *original = nullptr; bool active = false; };
static void TriggerHookScan() {}
static bool ForegroundIsThisProcess() { return false; }
static void FgtLog(const char *, ...) {}
static void Log(const char *, ...) {}
static HANDLE StartModuleWorker(LPTHREAD_START_ROUTINE) { return nullptr; }
static bool PresentAdapterFocusKeepFG() { return false; }
static void PresentAdapterPath(wchar_t *path, size_t, const wchar_t *) { path[0] = 0; }
// No stage-protocol Generic in this test: the gate keeps its NRHookPoint reading.
static bool PresentAdapterStagePlan(uint32_t *) { return false; }
static bool PinHookModule(const void *) { return true; }
static bool HookInstall(Hook &, void *, void *) { return false; }
#include "focus-fg.inc"

static unsigned options_calls, free_calls, original_calls;
static const void *expected_options, *expected_viewport;
static bool original_result;
static bool Original(void *) { ++original_calls; return original_result; }
static bool Capture(void *) { g_focus_fg.caller = _ReturnAddress(); return false; }
static PFN_FocusFgHasFocus volatile invoke_target;
__declspec(noinline) static bool CallerA()
{
    volatile bool result = invoke_target(nullptr);
    _ReadWriteBarrier();
    return result;
}
__declspec(noinline) static bool CallerB()
{
    volatile bool result = invoke_target(reinterpret_cast<void *>(1));
    _ReadWriteBarrier();
    return result;
}
static uint32_t Options(const void *viewport, const void *options)
{
    assert(viewport == expected_viewport && options == expected_options);
    ++options_calls;
    return 123u;
}
static uint32_t Free(uint32_t feature, const void *viewport)
{
    assert(feature == 1000 && viewport == expected_viewport);
    ++free_calls;
    return 456u;
}

static void TestPolicy()
{
    // Exhaust every guard independently and in combination. An original true
    // is always forwarded; false changes only when all eight guards are met.
    for (unsigned bits = 0; bits < 512; ++bits) {
        bool v[9];
        for (unsigned i = 0; i < 9; ++i) v[i] = (bits & (1u << i)) != 0;
        const bool expected = v[0] || (v[1] && v[2] && v[3] && v[4] && v[5] && !v[6] && v[7] && v[8]);
        assert(focus_fg::Override(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]) == expected);
    }
    g_focus_fg.focus.original = reinterpret_cast<void *>(&Original);
    invoke_target = Capture;
    assert(!CallerA());
    assert(g_focus_fg.caller != nullptr);
    invoke_target = FocusFgHasFocusDetour;
    original_result = false;
    assert(!CallerA());
    assert(g_focus_fg_calls == 1 && g_focus_fg_original_false == 1);
    assert(!CallerB());
    assert(g_focus_fg_calls == 1 && g_focus_fg_original_false == 1);
    original_result = true;
    assert(CallerA());
    assert(CallerB());
    assert(g_focus_fg_calls == 2 && original_calls == 4);

    reshade::api::effect_runtime first{ nullptr }, other{ nullptr };
    FocusFgSetRuntime(&first);
    assert(FocusFgWindowSnapshot().runtime == &first);
    FgtOnDestroyRuntime(&other);
    assert(FocusFgWindowSnapshot().runtime == &first);
    FgtOnDestroyRuntime(&first);
    assert(FocusFgWindowSnapshot().runtime == nullptr && FocusFgWindowSnapshot().window == nullptr);

    FocusFgOptions options = {};
    const uint8_t guid[] = { 0xcb, 0xf1, 0xc5, 0xfa, 0xfd, 0x2d, 0x36, 0x4f, 0xa1, 0xe6, 0x3a, 0x9e, 0x86, 0x52, 0x56, 0xc5 };
    memcpy(options.type, guid, sizeof(guid));
    options.version = 5;
    options.frames = 3;
    options.mode = 0; // explicit Off must remain Off and exactly one original call
    FocusFgOptions read = {};
    assert(FocusFgReadOptions(&options, &read) && read.frames == 3 && read.mode == 0);
    assert(!FocusFgReadOptions(nullptr, &read));
    expected_options = &options;
    expected_viewport = &first;
    g_focus_fg.options.original = reinterpret_cast<void *>(&Options);
    assert(FocusFgOptionsDetour(expected_viewport, &options) == 123);
    assert(options_calls == 1 && options.mode == 0);
    g_focus_fg.free_resources.original = reinterpret_cast<void *>(&Free);
    assert(FocusFgFreeDetour(1000, expected_viewport) == 456);
    assert(free_calls == 1);
    FocusFgOnPresent();
    FocusFgMaintenance();
    puts("policy: 512 guards; exact return-address scope; runtime retirement; explicit Off and free passthrough PASS");
}

static std::vector<uint8_t> MapDisk(const wchar_t *path)
{
    FILE *file = nullptr;
    _wfopen_s(&file, path, L"rb");
    assert(file != nullptr);
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    assert(length > 4096);
    fseek(file, 0, SEEK_SET);
    std::vector<uint8_t> disk(static_cast<size_t>(length));
    assert(fread(disk.data(), 1, disk.size(), file) == disk.size());
    fclose(file);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(disk.data());
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(disk.data() + dos->e_lfanew);
    assert(nt->Signature == IMAGE_NT_SIGNATURE);
    std::vector<uint8_t> image(nt->OptionalHeader.SizeOfImage);
    memcpy(image.data(), disk.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto *sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        assert(focus_fg::Within(sections[i].VirtualAddress, sections[i].SizeOfRawData, image.size()));
        assert(focus_fg::Within(sections[i].PointerToRawData, sections[i].SizeOfRawData, disk.size()));
        memcpy(image.data() + sections[i].VirtualAddress, disk.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
    }
    return image;
}

static void TestRuntimeContract(const wchar_t *path, bool report_only)
{
    auto image = MapDisk(path);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image.data());
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(image.data() + dos->e_lfanew);
    const auto *sections = IMAGE_FIRST_SECTION(nt);
    focus_fg::Site site;
    unsigned found = 0;
    size_t start = 0, size = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sections[i].Characteristics & IMAGE_SCN_CNT_CODE)) continue;
        if (focus_fg::Find(image.data(), image.size(), sections[i].VirtualAddress, sections[i].Misc.VirtualSize, &site)) {
            ++found;
            start = sections[i].VirtualAddress;
            size = sections[i].Misc.VirtualSize;
        }
    }
    printf("disk contract search: matches=%u return RVA=0x%zx object slot RVA=0x%zx\n", found,
           found ? site.return_address : static_cast<size_t>(0), found ? site.object_slot : static_cast<size_t>(0));
    if (report_only) return;
    assert(found == 1 && site.return_address == 0x4c0a7 && site.object_slot == 0x94450);
    printf("disk contract: unique return RVA=0x%zx object slot RVA=0x%zx PASS (no DLL load)\n", site.return_address, site.object_slot);
    const size_t call = site.return_address - 13;
    // Unknown call implementation, malformed jump and truncated region refuse.
    image[call + 12] = 0x20;
    assert(!focus_fg::Find(image.data(), image.size(), start, size, &site));
    image[call + 12] = 0x18;
    image[call + 30] ^= 1;
    assert(!focus_fg::Find(image.data(), image.size(), start, size, &site));
    image[call + 30] ^= 1;
    assert(!focus_fg::Find(image.data(), image.size(), call, 73, &site));
    assert(!focus_fg::Find(image.data(), image.size(), image.size() - 1, 4, &site));
    // A second match is ambiguous; leave the runtime unchanged.
    std::vector<uint8_t> duplicate(image);
    memcpy(duplicate.data() + call - 0x200, image.data() + call, 74);
    const size_t copy = call - 0x200;
    for (size_t displacement : { 3u, 17u, 30u, 70u }) {
        int32_t value = 0;
        memcpy(&value, duplicate.data() + copy + displacement, sizeof(value));
        value += 0x200;
        memcpy(duplicate.data() + copy + displacement, &value, sizeof(value));
    }
    assert(!focus_fg::Find(duplicate.data(), duplicate.size(), start, size, &site));
    puts("contract rejection: changed ABI, different branches, truncation, out of bounds, ambiguous sites PASS");
}

int wmain(int argc, wchar_t **argv)
{
    // --report-only prints the contract search for another sl.dlss_g.dll without
    // asserting the RVAs of the Streamline 2.14.1 build this test was written for.
    const bool report_only = argc == 3 && wcscmp(argv[2], L"--report-only") == 0;
    assert(argc == 2 || report_only);
    TestPolicy();
    TestRuntimeContract(argv[1], report_only);
    if (report_only) { puts("report only"); return 0; }
    puts("focus FG tests PASS");
}
