#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Contract discovery for Streamline's internal DLSS-G focus gate. This finds a
// specific call site, not a version/hash or a process-wide focus API. A changed
// implementation is left untouched unless the same unique contract is found.
namespace focus_fg {
struct Site { size_t object_slot = 0, return_address = 0; };

inline bool Within(size_t at, size_t length, size_t size)
{
    return at <= size && length <= size - at;
}

inline bool Relative(size_t next, const uint8_t *displacement, size_t size, size_t *result)
{
    int32_t delta = 0;
    memcpy(&delta, displacement, sizeof(delta));
    const int64_t address = static_cast<int64_t>(next) + delta;
    if (address < 0 || static_cast<uint64_t>(address) >= size) return false;
    *result = static_cast<size_t>(address);
    return true;
}

inline bool Find(const uint8_t *image, size_t size, size_t code_begin, size_t code_size, Site *out)
{
    if (image == nullptr || out == nullptr || !Within(code_begin, code_size, size)) return false;
    static const char message[] = "DLSS-G disabled: window not focused";
    size_t matches = 0;
    Site found;
    for (size_t i = code_begin; Within(i, 74, code_begin + code_size); ++i) {
        // mov rcx,[rip+keyboard]; mov rax,[rcx]; call [rax+18h]; test al,al;
        // jne enabled; cmp byte ptr [rbx+override],sil; jne enabled; ...
        // lea rdx,[rip+"DLSS-G disabled: window not focused"]
        if (memcmp(image + i, "\x48\x8b\x0d", 3) != 0 ||
            memcmp(image + i + 7, "\x48\x8b\x01\xff\x50\x18\x84\xc0\x0f\x85", 10) != 0 ||
            memcmp(image + i + 21, "\x40\x38\xb3", 3) != 0 ||
            memcmp(image + i + 28, "\x0f\x85", 2) != 0 ||
            memcmp(image + i + 67, "\x48\x8d\x15", 3) != 0) continue;
        size_t slot = 0, text = 0, enabled_a = 0, enabled_b = 0;
        if (!Relative(i + 7, image + i + 3, size, &slot) || !Within(slot, sizeof(void *), size) ||
            !Relative(i + 74, image + i + 70, size, &text) || !Within(text, sizeof(message), size) ||
            memcmp(image + text, message, sizeof(message)) != 0 ||
            !Relative(i + 21, image + i + 17, size, &enabled_a) ||
            !Relative(i + 34, image + i + 30, size, &enabled_b) || enabled_a != enabled_b ||
            enabled_a <= i + 74 || enabled_a >= code_begin + code_size) continue;
        ++matches;
        found = { slot, i + 13 };
    }
    if (matches != 1) return false;
    *out = found;
    return true;
}

// An explicit game Off, invalid tags, resolution changes and resource release
// remain Streamline's responsibility. Only this internal focus result changes.
inline bool Override(bool original, bool exact_caller, bool enabled, bool runtime_present,
                     bool current_process, bool visible, bool minimized, bool drawable,
                     bool same_client_size)
{
    return original || (exact_caller && enabled && runtime_present && current_process &&
        visible && !minimized && drawable && same_client_size);
}
} // namespace focus_fg
