// A module that looks like an NGX evaluate module to the carrier search: it
// exports the D3D12 evaluate name and carries a DLSS version resource
// (sr-fixture.rc). Built twice: as SR, and with /DRR_FIXTURE as Ray Reconstruction.
#include <windows.h>

extern "C" __declspec(dllexport) int NVSDK_NGX_D3D12_EvaluateFeature(void *list, void *handle, void *params, void *callback)
{
    volatile int seen = 0;
    seen += list != nullptr;
    seen += handle != nullptr;
    seen += params != nullptr;
    seen += callback != nullptr;
    return seen;
}
