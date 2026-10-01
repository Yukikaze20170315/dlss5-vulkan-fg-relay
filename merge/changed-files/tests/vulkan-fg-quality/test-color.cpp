// Exercises production FG viewport decoding/copy commands without a game/GPU.
#include "dlss5-bridge.cpp"

static unsigned checks, failures;
static void Check(bool ok, const char *why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
class RegionParams final : public SynthParams {
public:
    using SynthParams::Get;
    MVkResource resources[2]{};
    std::unordered_map<std::string, unsigned> values;
    RegionParams(unsigned w, unsigned h, unsigned x, unsigned y) {
        for (unsigned i = 0; i < 2; ++i) {
            auto &r = resources[i].iv;
            r.Image = 0x10000 + i; r.ImageView = 0x20000 + i;
            r.aspectMask = r.levelCount = r.layerCount = 1;
            r.Format = 37; r.Width = i == 0 ? w : 5120; r.Height = i == 0 ? h : 2160;
        }
        values["DLSSG.HUDLessSubrectWidth"] = values["DLSSG.BackbufferSubrectWidth"] = w;
        values["DLSSG.HUDLessSubrectHeight"] = values["DLSSG.BackbufferSubrectHeight"] = h;
        values["DLSSG.BackbufferSubrectBaseX"] = x; values["DLSSG.BackbufferSubrectBaseY"] = y;
    }
    NVSDK_NGX_Result Get(const char *key, void **out) const override {
        if (strcmp(key, "DLSSG.HUDLess") == 0) { *out = const_cast<MVkResource *>(&resources[0]); return NGX_SUCCESS; }
        if (strcmp(key, "DLSSG.Backbuffer") == 0) { *out = const_cast<MVkResource *>(&resources[1]); return NGX_SUCCESS; }
        return SynthParams::Get(key, out);
    }
    NVSDK_NGX_Result Get(const char *key, unsigned *out) const override {
        const auto i = values.find(key); if (i == values.end()) return SynthParams::Get(key, out);
        *out = i->second; return NGX_SUCCESS;
    }
};
static SVkImageCopy recorded;
static unsigned copies;
static void Record(SVkCommandBuffer, SVkHandle, uint32_t, SVkHandle, uint32_t, uint32_t count, const SVkImageCopy *copy)
{ Check(count == 1, "one explicit colour region"); recorded = *copy; ++copies; }
int main()
{
    InitializeCriticalSection(&g_log_cs); strcpy_s(g_log_path, "colour.log");
    g_svk.CmdCopyImage = Record;
    const unsigned cases[][4] = {{3840,2160,640,0}, {1920,1080,1600,540}, {3440,1440,840,360}, {5120,2160,0,0}};
    for (const auto &c : cases) {
        RegionParams p(c[0],c[1],c[2],c[3]); VkmRes hud{}, back{}; const char *why = nullptr;
        Check(FgInputReadColor(&p,&hud,&back,&why), "cold-start viewport accepted independently of previous resolution");
        Check(hud.w == c[0] && back.w == c[0] && back.h == c[1], "carrier dimensions equal active viewport");
        Check(back.fg_x == c[2] && back.fg_y == c[3] && hud.fg_x == 0, "distinct source origins preserved");
        FgColorCopy(nullptr,back.image,6,0x30000,1,back.fg_x,back.fg_y,0,0,back.w,back.h);
        Check(recorded.srcOffset.x == static_cast<int>(c[2]) && recorded.srcOffset.y == static_cast<int>(c[3]) && recorded.dstOffset.x == 0,
              "read only active viewport into local carrier origin");
        FgColorCopy(nullptr,0x30000,6,back.image,7,0,0,back.fg_x,back.fg_y,back.w,back.h);
        Check(recorded.dstOffset.x == static_cast<int>(c[2]) && recorded.dstOffset.y == static_cast<int>(c[3]) && recorded.extent.width == c[0] && recorded.extent.height == c[1],
              "writeback targets original viewport only");
        FgGuideContract guides{}; guides.inverted=1;
        g_fg_input.vk_fmt=37;g_fg_input.w=5120;g_fg_input.h=2160;g_fg_input.depth_inverted=1;
        Check(FgInputShapeChanged(back,guides) == (c[0]!=5120 || c[1]!=2160), "effective extent change rebuilds even when physical canvas is unchanged");
    }
    RegionParams p(3840,2160,640,0); VkmRes hud{},back{}; const char *why = nullptr;
    p.values["DLSSG.BackbufferSubrectBaseX"]=UINT_MAX;
    Check(!FgInputReadColor(&p,&hud,&back,&why),"overflowing origin rejected");
    p.values["DLSSG.BackbufferSubrectBaseX"]=1281;
    Check(!FgInputReadColor(&p,&hud,&back,&why),"viewport extending beyond allocation rejected");
    p.values["DLSSG.BackbufferSubrectBaseX"]=640;p.values["DLSSG.BackbufferSubrectWidth"]=0;
    Check(!FgInputReadColor(&p,&hud,&back,&why),"zero extent remains transient invalid input");
    p.values["DLSSG.BackbufferSubrectWidth"]=3840;p.resources[0].iv.Format=44;
    Check(!FgInputReadColor(&p,&hud,&back,&why),"incompatible colour formats rejected");
    p.resources[0].iv.Format=37;p.values["DLSSG.HUDLessSubrectWidth"]=1920;
    Check(!FgInputReadColor(&p,&hud,&back,&why),"different effective viewport sizes not silently stretched");
    p.values["DLSSG.HUDLessSubrectWidth"]=3840;
    Check(FgInputReadColor(&p,&hud,&back,&why),"valid viewport resumes after invalid transitional input");
    Check(!g_fg_input.refused,"invalid viewport did not retire NR session");
    std::printf("colour regions: %u checks, %u failures, %u recorded production copies\n",checks,failures,copies);
    DeleteCriticalSection(&g_log_cs);
    return failures ? 1 : 0;
}
