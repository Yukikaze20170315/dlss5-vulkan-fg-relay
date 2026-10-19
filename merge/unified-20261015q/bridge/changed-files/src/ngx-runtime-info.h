#pragma once

#include <cstddef>

// Layout of NVSDK_NGX_FeatureCommonInfo from the NGX SDK. The bridge uses
// private Init_Ext/Init_ProjectID exports and keeps its own NGX declarations.
// test-ngx-info.cpp checks this layout against the vendored public SDK.
struct BridgeNgxRuntimeInfo {
    struct Paths {
        const wchar_t* const* path;
        unsigned length;
    } paths;
    void* internal;
    struct Logging {
        void (*callback)(const char*, int, int);
        int minimum_level;
        bool disable_other_sinks;
    } logging;
};

static_assert(sizeof(BridgeNgxRuntimeInfo) == 40);
static_assert(offsetof(BridgeNgxRuntimeInfo, internal) == 16);
static_assert(offsetof(BridgeNgxRuntimeInfo, logging) == 24);

static const BridgeNgxRuntimeInfo* BridgeNgxFeatureInfo();
