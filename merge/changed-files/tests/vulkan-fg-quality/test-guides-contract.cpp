// Contract and emitted-command tests for the actual production guide helper.
// This executable does not load a game, NR runtime, or graphics driver.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

using SVkHandle = uint64_t;
using SVkCommandBuffer = void *;
struct SVkExtent3D { uint32_t width, height, depth; };
struct SVkOffset3D { int32_t x, y, z; };
struct SVkLayers { uint32_t aspect, mip, layer, count; };
struct SVkImageCopy { SVkLayers srcSubresource; SVkOffset3D srcOffset; SVkLayers dstSubresource; SVkOffset3D dstOffset; SVkExtent3D extent; };
static constexpr uint32_t kSVkAspectColor=1, kSVkLayoutTransferSrc=6, kSVkLayoutGeneral=1;
struct VkmRes { uint64_t image, view; uint32_t fmt, aspect, mip, layer, w, h; };
static constexpr unsigned NGX_SUCCESS=1, NGX_FAIL=0;
struct NVSDK_NGX_Parameter {
    std::unordered_map<std::string,unsigned> u;
    std::unordered_map<std::string,float> f;
    std::unordered_map<std::string,VkmRes> resources;
    unsigned Get(const char *key,unsigned *out) const { auto i=u.find(key); if(i==u.end()) return NGX_FAIL; *out=i->second; return NGX_SUCCESS; }
    unsigned Get(const char *key,int *out) const { unsigned n; if(Get(key,&n)!=NGX_SUCCESS) return NGX_FAIL; *out=static_cast<int>(n); return NGX_SUCCESS; }
    unsigned Get(const char *key,float *out) const { auto i=f.find(key); if(i==f.end()) return NGX_FAIL; *out=i->second; return NGX_SUCCESS; }
};
struct SynthParams { std::unordered_map<std::string,double> values; template<class T> void Set(const char *key,T value) { values[key]=value; } };
static bool VkmGetRes(const NVSDK_NGX_Parameter *p,const char *key,VkmRes *out,const char **why) {
    auto i=p->resources.find(key); if(i==p->resources.end()) { *why="missing resource"; return false; } *out=i->second; return true;
}
static bool FgInputReadU(const NVSDK_NGX_Parameter *p,const char *key,unsigned *out) { return p->Get(key,out)==NGX_SUCCESS; }
static void Log(const char *,...) {}
struct Recorded { unsigned copies=0,computes=0,x=0,y=0,w=0,h=0,dst_w=0,dst_h=0; SVkHandle src=0,dst=0; SVkImageCopy copy={}; } recorded;
static void Copy(SVkCommandBuffer,SVkHandle src,uint32_t,SVkHandle dst,uint32_t,uint32_t n,const SVkImageCopy *r) {
    if(n!=1) std::abort(); ++recorded.copies; recorded.src=src; recorded.dst=dst; recorded.copy=*r;
}
static struct { void *dev=reinterpret_cast<void*>(1); decltype(&Copy) CmdCopyImage=Copy; } g_svk;
#include "fg-guides.inc"
// CPU tests check routing into the backend. Actual dispatch, raw bits and
// synchronization are exercised separately by test-gpu.cpp on real queues.
static uint64_t prepared_bytes=0;
static bool FgGuideComputePrepare(uint64_t bytes,uint32_t local_types,uint64_t families) {
    prepared_bytes=bytes; return local_types!=0 && families!=0;
}
static void FgGuideComputeRecord(SVkCommandBuffer,const FgGuideImage &g,SVkHandle dst,unsigned w,unsigned h) {
    ++recorded.computes; recorded.src=g.resource.image; recorded.dst=dst;
    recorded.x=g.x; recorded.y=g.y; recorded.w=g.w; recorded.h=g.h; recorded.dst_w=w; recorded.dst_h=h;
}
static unsigned capability_queries=0;
static void TestFormats(void *physical,uint32_t,FgGuideFormatProperties *out) {
    if(physical!=reinterpret_cast<void*>(0x1234)) std::abort();
    ++capability_queries; out->optimal=0x400u|0x800u;
}
static void TestQueues(void *physical,uint32_t *count,FgGuideQueueProperties *out) {
    if(physical!=reinterpret_cast<void*>(0x1234) || *count<3) std::abort();
    ++capability_queries; *count=3; out[0].flags=3; out[1].flags=4; out[2].flags=6;
}
static void TestMemory(void *physical,FgGuideMemoryProperties *out) {
    if(physical!=reinterpret_cast<void*>(0x1234)) std::abort();
    ++capability_queries; out->type_count=2; out->types[0].flags=1; out->types[1].flags=6;
}
static void *TestLayerGipa(void *instance,const char *name) {
    if(instance!=reinterpret_cast<void*>(0x5678)) std::abort();
    if(std::strcmp(name,"vkGetPhysicalDeviceFormatProperties")==0) return reinterpret_cast<void*>(&TestFormats);
    if(std::strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties")==0) return reinterpret_cast<void*>(&TestQueues);
    if(std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties")==0) return reinterpret_cast<void*>(&TestMemory);
    return nullptr;
}
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)

int main()
{
    CHECK(FgGuideRectValid(5120,2160,0,0,5120,2160));
    CHECK(FgGuideRectValid(5200,2200,19,23,3413,1440));
    CHECK(!FgGuideRectValid(10,10,0,0,0,10));
    CHECK(!FgGuideRectValid(10,10,9,0,2,10));
    CHECK(!FgGuideRectValid(10,10,UINT_MAX,0,1,10));
    CHECK(!FgGuideRectValid(UINT_MAX,10,0,0,10,10));
    float scale=0;
    CHECK(FgGuideScale(3413,3413,5120,&scale) && scale==5120);
    CHECK(FgGuideScale(-1440,1440,2160,&scale) && scale==-2160);
    CHECK(FgGuideScale(0,1440,2160,&scale) && scale==0);
    CHECK(FgGuideScale(1,5120,5120,&scale) && scale==1);
    CHECK(!FgGuideScale(1,0,5120,&scale));
    CHECK(!FgGuideScale(std::numeric_limits<float>::quiet_NaN(),1,1,&scale));
    CHECK(!FgGuideScale(std::numeric_limits<float>::infinity(),1,1,&scale));
    CHECK(!FgGuideScale(std::numeric_limits<float>::max(),1,UINT_MAX,&scale));
    uint64_t bytes=0;
    CHECK(FgGuideBufferBytes(5120,2160,&bytes) && bytes==44236800);
    CHECK(FgGuideBufferBytes(32768,1024,&bytes) && bytes==134217728);
    CHECK(!FgGuideBufferBytes(32768,1025,&bytes));
    CHECK(!FgGuideBufferBytes(32769,1,&bytes));
    CHECK(!FgGuideBufferBytes(0,2160,&bytes));

    NVSDK_NGX_Parameter p;
    p.resources["DLSSG.Depth"]={10,0,100,1,0,0,3450,1480};
    p.resources["DLSSG.MVecs"]={11,0,83,1,0,0,3450,1480};
    for(const char *name:{"Depth","MVecs"}) {
        p.u[std::string("DLSSG.")+name+"SubrectBaseX"]=19;
        p.u[std::string("DLSSG.")+name+"SubrectBaseY"]=23;
        p.u[std::string("DLSSG.")+name+"SubrectWidth"]=3413;
        p.u[std::string("DLSSG.")+name+"SubrectHeight"]=1440;
    }
    p.f["DLSSG.MvecScaleX"]=3413; p.f["DLSSG.MvecScaleY"]=1440;
    p.u["DLSSG.DepthInverted"]=1;
    VkmRes output={20,0,37,1,0,0,5120,2160};
    FgGuideContract g={}; const char *why=nullptr;
    CHECK(FgGuideRead(&p,output,&g,&why));
    CHECK(g.mv.x==19 && g.mv.y==23 && g.mv.w==3413 && g.mv.h==1440);
    CHECK(g.carrier_scale_x==5120 && g.carrier_scale_y==2160);
    CHECK(!FgGuideCanTransport(g,0));
    FgGuideNoteDevice(nullptr,reinterpret_cast<void*>(0x1234),g_svk.dev,TestLayerGipa);
    CHECK(capability_queries==0 && !FgGuideCanTransport(g,0));
    FgGuideNoteDevice(reinterpret_cast<void*>(0x5678),reinterpret_cast<void*>(0x1234),g_svk.dev,TestLayerGipa);
    CHECK(capability_queries==4);
    CHECK(FgGuideCanTransport(g,0));
    CHECK(!FgGuideCanTransport(g,1));
    CHECK(FgGuideCanTransport(g,2));
    CHECK(FgGuidePrepareTransport(g,2) && prepared_bytes==44236800);
    uint32_t families[4]={};
    CHECK(FgGuideSharingFamilies(g_svk.dev,families,4)==2 && families[0]==0 && families[1]==2);
    CHECK(FgGuideSharingFamilies(g_svk.dev,families,1)==0);
    FgGuideRecord(nullptr,g.mv,99,5120,2160);
    CHECK(recorded.computes==1 && recorded.copies==0);
    CHECK(recorded.src==11 && recorded.dst==99);
    CHECK(recorded.x==19 && recorded.y==23);
    CHECK(recorded.w==3413 && recorded.h==1440);
    CHECK(recorded.dst_w==5120 && recorded.dst_h==2160);
    FgGuideImage same=g.mv; same.w=5; same.h=7;
    FgGuideRecord(nullptr,same,98,5,7);
    CHECK(recorded.copies==1 && recorded.copy.srcOffset.x==19 && recorded.copy.srcOffset.y==23);
    CHECK(recorded.copy.extent.width==5 && recorded.copy.extent.height==7);
    FgGuideContract unscaled=g; unscaled.out_w=3413; unscaled.out_h=1440;
    CHECK(FgGuideCanTransport(unscaled,UINT_MAX));

    SynthParams sp;
    FgGuideApply(&sp,g); CHECK(sp.values["Reset"]==1);
    CHECK(sp.values["MV.Scale.X"]==5120 && sp.values["MV.Scale.Y"]==2160);
    FgGuideApply(&sp,g); CHECK(sp.values["Reset"]==0);
    FgGuidePause("transition"); FgGuideApply(&sp,g); CHECK(sp.values["Reset"]==1);
    FgGuideApply(&sp,g); CHECK(sp.values["Reset"]==0);
    FgGuideContract changed=g; ++changed.mv.x;
    FgGuideApply(&sp,changed); CHECK(sp.values["Reset"]==1);
    FgGuideApply(&sp,changed); CHECK(sp.values["Reset"]==0);
    changed.reset=1; FgGuideApply(&sp,changed); CHECK(sp.values["Reset"]==1);
    changed.reset=0; FgGuideApply(&sp,changed); CHECK(sp.values["Reset"]==0);
    p.u["DLSSG.MvecJittered"]=1;
    CHECK(!FgGuideRead(&p,output,&g,&why) && std::strstr(why,"jittered")!=nullptr);
    p.u["DLSSG.MvecJittered"]=0;
    p.u["DLSSG.MVecsSubrectWidth"]=0;
    CHECK(!FgGuideRead(&p,output,&g,&why));
    p.u["DLSSG.MVecsSubrectWidth"]=3413;
    p.resources["DLSSG.Depth"].fmt=126;
    CHECK(!FgGuideRead(&p,output,&g,&why));
    p.resources["DLSSG.Depth"].fmt=100;
    CHECK(FgGuideRead(&p,output,&g,&why));
    p.f["DLSSG.JitterOffsetX"]=std::numeric_limits<float>::infinity();
    CHECK(!FgGuideRead(&p,output,&g,&why));
    p.f["DLSSG.JitterOffsetX"]=0;
    const VkmRes saved=p.resources["DLSSG.MVecs"];
    p.resources.erase("DLSSG.MVecs"); CHECK(!FgGuideRead(&p,output,&g,&why));
    p.resources["DLSSG.MVecs"]=saved; CHECK(FgGuideRead(&p,output,&g,&why));
    for(uintptr_t i=2;i<130;++i) g_fg_guide_devices[reinterpret_cast<void*>(i)]={reinterpret_cast<void*>(i),true,1,5,1};
    g_svk.dev=reinterpret_cast<void*>(129); CHECK(FgGuideCanTransport(g,0));
    FgGuideForgetDevice(g_svk.dev); CHECK(!FgGuideCanTransport(g,0));
    std::printf("PASS: %u checks; production rectangle/scale/read/record/reset/capability helpers.\n",checks);
    return 0;
}
