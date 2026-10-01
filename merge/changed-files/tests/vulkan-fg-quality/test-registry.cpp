#include "fg-handle-registry.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace bridge_lifecycle;
static std::atomic<unsigned> checks{0}, failures{0};
static void Check(bool ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", why); }
}
static const void *Pointer(std::uintptr_t value) { return reinterpret_cast<const void *>(value); }

int main()
{
    Registry registry;
    const void *owner = Pointer(0x10000);
    // More than either of the old fixed capacities, then repeated lifetime reuse.
    for (unsigned i = 0; i != 257; ++i)
        Check(registry.Created(Api::vulkan, Pointer(0x20000 + i), 11, owner), "register >8 simultaneous FG handles");
    Check(registry.Size(Api::vulkan) == 257, "no 8/32 entry limit");
    for (unsigned i = 0; i != 257; ++i) {
        const auto h = Pointer(0x20000 + i);
        const auto f = registry.Find(Api::vulkan, h);
        Check(f && f.id == 11, "every active FG handle recognised");
        Check(registry.Released(Api::vulkan, h, f, true), "successful release removes entry");
    }
    Check(registry.Size(Api::vulkan) == 0, "released handles do not accumulate");
    const auto reused = Pointer(0x30000);
    for (unsigned i = 0; i != 10000; ++i) {
        Check(registry.Created(Api::vulkan, reused, 11, owner), "create repeated lifetime");
        const auto before = registry.Find(Api::vulkan, reused);
        Check(!registry.Released(Api::vulkan, reused, before, false), "failed release retains entry");
        Check(registry.Find(Api::vulkan, reused).generation == before.generation, "failed release preserves generation");
        Check(registry.Created(Api::vulkan, reused, 1, owner), "SR reuses FG address");
        const auto sr = registry.Find(Api::vulkan, reused);
        Check(sr.id == 1 && sr.generation != before.generation, "new feature replaces old classification");
        Check(!registry.Released(Api::vulkan, reused, before, true), "late old release cannot erase reused handle");
        Check(registry.Released(Api::vulkan, reused, sr, true), "new lifetime can be released");
        Check(!registry.Released(Api::vulkan, reused, sr, true), "duplicate nested release is idempotent");
    }
    Check(registry.Created(Api::d3d11, reused, 1, owner), "D3D11 registration");
    Check(registry.Created(Api::vulkan, reused, 11, Pointer(0x40000)), "Vulkan same address registration");
    Check(registry.Find(Api::d3d11, reused).id == 1 && registry.Find(Api::vulkan, reused).id == 11,
          "backend address namespaces remain separate");
    registry.ForgetOwner(owner);
    Check(!registry.Find(Api::d3d11, reused) && registry.Find(Api::vulkan, reused),
          "module retirement removes only owned features");
    registry.ForgetOwner(Pointer(0x40000));
    Check(!registry.Created(Api::vulkan, nullptr, 11, owner), "null is never a live feature");
    Check(!registry.Released(Api::vulkan, reused, {}, true), "unknown release cannot erase entries");

    // Concurrent create/read/release on independent game lifetimes. Shared
    // snapshots are also queried while other threads mutate the same table.
    std::vector<std::thread> workers;
    for (unsigned thread = 0; thread != 12; ++thread)
        workers.emplace_back([&, thread] {
            const auto h = Pointer(0x50000 + thread);
            for (unsigned i = 0; i != 3000; ++i) {
                Check(registry.Created(Api::vulkan, h, 11, owner), "concurrent create");
                const auto f = registry.Find(Api::vulkan, h);
                Check(f && f.id == 11, "concurrent own lifetime lookup");
                (void)registry.Find(Api::vulkan, Pointer(0x50000 + (thread + 1) % 12));
                Check(registry.Released(Api::vulkan, h, f, true), "concurrent release");
            }
        });
    for (auto &thread : workers) thread.join();
    Check(registry.Size(Api::vulkan) == 0, "all concurrent lifetimes drained");
    std::printf("registry: %u checks, %u failures\n", checks.load(), failures.load());
    return failures == 0 ? 0 : 1;
}
