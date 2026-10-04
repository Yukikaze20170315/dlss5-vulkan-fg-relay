#include <windows.h>
#include <cstdint>
static volatile LONG plan = 0;
static volatile LONG version = 2;
extern "C" __declspec(dllexport) uint32_t __cdecl DLSS5StageProtocolVersion() {
    return static_cast<uint32_t>(InterlockedCompareExchange(&version, 0, 0));
}
extern "C" __declspec(dllexport) uint32_t __cdecl DLSS5GetStagePlan() {
    return static_cast<uint32_t>(InterlockedCompareExchange(&plan, 0, 0));
}
extern "C" __declspec(dllexport) void __cdecl SetPlan(uint32_t value) {
    InterlockedExchange(&plan, static_cast<LONG>(value));
}
extern "C" __declspec(dllexport) void __cdecl SetVersion(uint32_t value) {
    InterlockedExchange(&version, static_cast<LONG>(value));
}
