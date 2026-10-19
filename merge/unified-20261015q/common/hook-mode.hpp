#pragma once
#include <cstdint>

namespace dlss5::hook_mode {
constexpr uint32_t kDefault = 2;
constexpr uint32_t kUnknown = 0xffffffffu;
enum Operation : uint32_t { Query = 0, Select = 1, Current = 2 };
struct Snapshot {
    uint32_t size = sizeof(Snapshot), version = 1;
    uint32_t current = kUnknown, selected = kUnknown;
    uint32_t stored = 0;
};
inline bool Valid(uint32_t mode) { return mode <= 2; }
inline uint32_t Effective(bool hooks, bool streamline) { return hooks ? (streamline ? 1u : 2u) : 0u; }
// The owner writes through ReShade's configuration cache. Runtime hook flags
// are deliberately not changed; installing/removing hooks requires a restart.
template<class Read, class Write>
bool Control(uint32_t operation, uint32_t value, uint32_t current,
             Snapshot* result, Read read, Write write) {
    if (!result || result->size != sizeof(Snapshot) || result->version != 1 ||
        !Valid(current) || operation > Current || (operation == Select && !Valid(value))) return false;
    if (operation == Select) write(value);
    const uint32_t selected = operation == Current ? current : read();
    result->current = current;
    result->selected = selected;
    return operation != Select || selected == value;
}
}
