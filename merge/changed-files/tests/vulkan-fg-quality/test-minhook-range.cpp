#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <initializer_list>
#include <MinHook.h>
extern "C" {
#include "trampoline.h"
}

static unsigned checks, failures;
static void Check(bool ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", why); }
}
using Entry = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
static Entry original;
static uint64_t Detour(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    return original(a, b, c, d) + 100;
}
static const BYTE code[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08,
    0x48, 0x89, 0xC8, 0x48, 0x01, 0xD0,
    0x4C, 0x01, 0xC0, 0x4C, 0x01, 0xC8, 0xC3
};

static void CheckDefaultAndRetirement()
{
    auto *target = static_cast<BYTE *>(VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(target != nullptr, "ordinary target allocated");
    if (target == nullptr) return;
    memcpy(target, code, sizeof(code));
    const auto call = reinterpret_cast<Entry>(target);
    Check(call(1, 2, 3, 4) == 10, "unhooked four-argument baseline");
    Check(MH_CreateHookPrologueExtended(target, reinterpret_cast<void *>(&Detour), reinterpret_cast<void **>(&original)) == MH_OK,
          "opt-in keeps ordinary allocation when available");
    const auto a = reinterpret_cast<uintptr_t>(target), b = reinterpret_cast<uintptr_t>(original);
    Check((a > b ? a - b : b - a) <= 0x40000000ULL, "ordinary nearby allocation remains first choice");
    Check(MH_EnableHook(target) == MH_OK && call(1, 2, 3, 4) == 110, "ordinary detour and original execute");
    Check(MH_DisableHook(target) == MH_OK && call(1, 2, 3, 4) == 10, "ordinary disable restores bytes");
    Check(MH_EnableHook(target) == MH_OK && call(2, 3, 4, 5) == 114, "ordinary re-enable works");
    const void *saved = reinterpret_cast<void *>(original);
    Check(VirtualFree(target, 0x10000, MEM_DECOMMIT) != FALSE, "retirement target decommitted");
    Check(MH_RetireHook(target) == MH_OK, "retirement avoids the decommitted target");
    MEMORY_BASIC_INFORMATION memory = {};
    Check(VirtualQuery(saved, &memory, sizeof(memory)) != 0 && memory.State == MEM_COMMIT,
          "retirement keeps the published executable buffer mapped");
    Check(VirtualAlloc(target, 0x10000, MEM_COMMIT, PAGE_EXECUTE_READWRITE) == target, "target address recommitted");
    memcpy(target, code, sizeof(code));
    Check(MH_CreateHookPrologueExtended(target, reinterpret_cast<void *>(&Detour), reinterpret_cast<void **>(&original)) == MH_OK,
          "retired target address can be hooked again");
    Check(saved != reinterpret_cast<void *>(original), "retired trampoline slot is not reused");
    Check(MH_EnableHook(target) == MH_OK && call(2, 3, 4, 5) == 114, "reloaded target forwards correctly");
    Check(MH_RemoveHook(target) == MH_OK, "reloaded target removed");
    VirtualFree(target, 0, MEM_RELEASE);
}

static void CheckExhaustedRange()
{
    const SIZE_T size = 0x110000000ULL;
    auto *reservation = static_cast<BYTE *>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS));
    Check(reservation != nullptr, "entire rel32 window reserved without committing it");
    if (reservation == nullptr) return;
    auto *target = reservation + 0x88000000ULL;
    Check(VirtualAlloc(target, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE) == target, "only the target page is committed");
    memcpy(target, code, sizeof(code));
    void *forward = nullptr;
    Check(MH_CreateHook(target, reinterpret_cast<void *>(&Detour), &forward) == MH_ERROR_MEMORY_ALLOC,
          "ordinary allocator refuses a fully occupied window");
    Check(MH_CreateHookPrologueExtended(target, reinterpret_cast<void *>(&Detour), &forward) == MH_ERROR_MEMORY_ALLOC,
          "opt-in allocator also refuses when no reachable block exists");
    Check(forward == nullptr && memcmp(target, code, sizeof(code)) == 0, "allocation failure publishes nothing and changes no target bytes");
    Check(reinterpret_cast<Entry>(target)(1, 2, 3, 4) == 10, "failed installation preserves original behavior");
    VirtualFree(reservation, 0, MEM_RELEASE);
}

static void CheckRipDisplacements()
{
    auto *reservation = static_cast<BYTE *>(VirtualAlloc(nullptr, 0xE0010000ULL, MEM_RESERVE, PAGE_NOACCESS));
    Check(reservation != nullptr, "RIP boundary fixture reserved");
    if (reservation == nullptr) return;
    BYTE *target = reservation + 0x70000000ULL;
    BYTE *below = reservation;
    BYTE *above = reservation + 0xE0000000ULL;
    bool committed = true;
    for (BYTE *p : {target, below, above})
        committed = committed && VirtualAlloc(p, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE) == p;
    Check(committed, "RIP boundary pages committed");
    if (!committed) { VirtualFree(reservation, 0, MEM_RELEASE); return; }
    struct Case { BYTE *trampoline; INT32 displacement; bool accepted; INT32 expected; };
    const Case cases[] = {
        {below, 0x0FFFFFFF, true, INT_MAX},
        {below, 0x10000000, false, 0},
        {above, -0x10000000, true, INT_MIN},
        {above, -0x10000001, false, 0}
    };
    for (const auto &test : cases) {
        const BYTE mov[] = {0x8B, 0x05, 0, 0, 0, 0, 0xC3};
        memcpy(target, mov, sizeof(mov));
        memcpy(target + 2, &test.displacement, 4);
        BYTE before[sizeof(mov)]; memcpy(before, target, sizeof(before));
        TRAMPOLINE ct = {};
        ct.pTarget = target; ct.pTrampoline = test.trampoline; ct.trampolineSize = 64;
        const bool accepted = CreateTrampolineFunction(&ct) != FALSE;
        Check(accepted == test.accepted, "RIP relocation accepts INT32 boundary and rejects overflow");
        Check(memcmp(before, target, sizeof(before)) == 0, "RIP builder never patches the target");
        if (accepted) {
            INT32 relocated = 0; memcpy(&relocated, test.trampoline + 2, 4);
            Check(relocated == test.expected, "RIP boundary displacement is exact");
        }
    }
    VirtualFree(reservation, 0, MEM_RELEASE);
}

int main()
{
    Check(MH_Initialize() == MH_OK, "MinHook initialized");
    CheckDefaultAndRetirement();
    CheckExhaustedRange();
    CheckRipDisplacements();
    std::printf("%s MinHook range safety: checks=%u failures=%u\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
