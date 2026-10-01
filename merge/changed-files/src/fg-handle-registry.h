#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

// Live NGX handles, not a history of all addresses seen during a process. API
// namespaces are distinct even if two backends return the same pointer value.
// The caller never keeps this mutex across an NGX or GPU call.
namespace bridge_lifecycle {
enum class Api : unsigned { d3d11 = 0, vulkan = 1 };

struct Feature {
    int id = 0;
    std::uint64_t generation = 0;
    const void *owner = nullptr;
    explicit operator bool() const { return generation != 0; }
};

class Registry {
public:
    bool Created(Api api, const void *handle, int feature, const void *owner,
                 Feature *created = nullptr) noexcept
    {
        if (!handle) return false;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            Feature value{feature, ++generation_, owner};
            // A successful Create is a new lifetime, including address reuse.
            entries_[Index(api)].insert_or_assign(handle, value);
            if (created) *created = value;
            return true;
        } catch (...) {
            // Allocation failure must not unwind through somebody else's ABI.
            // The integration logs this and forwards the game's feature intact.
            return false;
        }
    }

    Feature Find(Api api, const void *handle) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto &table = entries_[Index(api)];
        const auto it = table.find(handle);
        return it == table.end() ? Feature{} : it->second;
    }

    bool Released(Api api, const void *handle, Feature expected, bool succeeded)
    {
        if (!succeeded || !expected) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto &table = entries_[Index(api)];
        const auto it = table.find(handle);
        if (it == table.end() || it->second.generation != expected.generation) return false;
        table.erase(it);
        return true;
    }

    void ForgetOwner(const void *owner)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &table : entries_)
            for (auto it = table.begin(); it != table.end();)
                if (it->second.owner == owner) it = table.erase(it);
                else ++it;
    }

    std::size_t Size(Api api) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_[Index(api)].size();
    }

private:
    static unsigned Index(Api api) { return static_cast<unsigned>(api); }
    mutable std::mutex mutex_;
    std::unordered_map<const void *, Feature> entries_[2];
    std::uint64_t generation_ = 0;
};
} // namespace bridge_lifecycle
