#pragma once

#include <cstddef>
#include <cstdint>

// CPU evidence only. The caller serializes access; this type never calls an API,
// allocates, logs, waits, or treats missing ReShade events as an empty recording.
namespace fg_submit_observation {

constexpr unsigned kSamples = 8;
constexpr unsigned kCommandLists = 512;
constexpr unsigned kLocations = 8;
enum class Command : unsigned { dispatch, copy, barrier, draw, secondary, indirect, clear, render_pass, bind, query, count };
enum class Coverage : unsigned { untracked, init_only, begin_observed, partial };
enum class State : unsigned { pending, in_submit, returned, reset, destroyed, reinitialized, device_destroyed };

inline void Add(uint64_t &value, uint64_t count = 1)
{
    const uint64_t max = ~uint64_t(0);
    value = count > max - value ? max : value + count;
}

struct Counts {
    uint64_t kind[static_cast<unsigned>(Command::count)]{};
    uint64_t total = 0;
};
struct Images { uint64_t hud = 0, back = 0, depth = 0, mv = 0; unsigned known = 0; };
struct Sample {
    uint64_t id = 0, cb = 0, generation = 0, eval_queue = 0, qpc = 0;
    Images images{};
    Counts prefix{};
    Coverage coverage = Coverage::untracked;
    State state = State::pending;
    unsigned index = 0;
    bool indexed = false;
};
struct Location { uint32_t batch = 0, cb_index = 0, batch_cbs = 0, waits = 0, signals = 0; };
struct Match {
    Sample sample{};
    Location locations[kLocations]{};
    uint64_t occurrences = 0, omitted_locations = 0, submit_generation = 0;
    unsigned stored = 0;
    bool claimed = false, generation_changed = false;
};
struct Submit {
    uint64_t id = 0, queue = 0, fence = 0;
    uint64_t batches_read = 0, cbs_read = 0, waits = 0, signals = 0, declared_cbs = 0, read_faults = 0;
    Match matches[kSamples]{};
    uint32_t batches = 0, result = 0;
    unsigned candidates = 0;
    bool submit2 = false;
};
struct Stats {
    uint64_t inits = 0, resets = 0, destroys = 0, device_destroys = 0;
    uint64_t events = 0, partial_starts = 0, capacity_misses = 0, generation_exhausted = 0;
    uint64_t firsts = 0, prefixes = 0, untracked_firsts = 0, partial_firsts = 0, invalidated = 0;
    uint64_t submits = 0, submits2 = 0, nested_suppressed = 0, batches = 0, cbs = 0, read_faults = 0;
    uint64_t returned = 0, handle_only = 0, duplicate_occurrences = 0, omitted_locations = 0, failed_results = 0;
};

class Observer {
    struct Entry {
        uint64_t cb = 0, device = 0, generation = 0;
        Counts commands{};
        Coverage coverage = Coverage::untracked;
        bool used = false; // A cleared, previously used slot is a probe-chain tombstone.
    };
    Entry entries_[kCommandLists]{};
    Sample samples_[kSamples]{};
    uint64_t generation_ = 0;
    unsigned sample_count_ = 0;
    Stats stats_{};

    static unsigned Hash(uint64_t cb) { return static_cast<unsigned>((cb >> 4) ^ (cb >> 32)) % kCommandLists; }
    Entry *Find(uint64_t cb)
    {
        if (cb == 0) return nullptr;
        for (unsigned n = 0, i = Hash(cb); n < kCommandLists; ++n, i = (i + 1) % kCommandLists) {
            Entry &e = entries_[i];
            if (e.cb == cb) return &e;
            if (!e.used) break;
        }
        return nullptr;
    }
    Entry *Acquire(uint64_t cb)
    {
        if (cb == 0) return nullptr;
        Entry *free = nullptr;
        for (unsigned n = 0, i = Hash(cb); n < kCommandLists; ++n, i = (i + 1) % kCommandLists) {
            Entry &e = entries_[i];
            if (e.cb == cb) return &e;
            if (e.cb == 0 && free == nullptr) free = &e;
            if (!e.used) break;
        }
        if (free == nullptr) { Add(stats_.capacity_misses); return nullptr; }
        *free = {};
        free->used = true;
        free->cb = cb;
        return free;
    }
    uint64_t NextGeneration()
    {
        // Never wrap into an old recording identity, including after destruction.
        if (generation_ == ~uint64_t(0)) { Add(stats_.generation_exhausted); return 0; }
        return ++generation_;
    }
    void Invalidate(uint64_t cb, State state)
    {
        for (unsigned i = 0; i < sample_count_; ++i) {
            Sample &s = samples_[i];
            if (s.cb == cb && (s.state == State::pending || s.state == State::in_submit)) {
                s.state = state;
                Add(stats_.invalidated);
            }
        }
    }
    void Start(uint64_t cb, uint64_t device, Coverage coverage, State reason)
    {
        if (cb == 0) return;
        Invalidate(cb, reason);
        Entry *e = Acquire(cb);
        if (e == nullptr) return;
        e->device = device;
        e->generation = NextGeneration();
        e->coverage = e->generation != 0 ? coverage : Coverage::untracked;
        e->commands = {};
    }

public:
    void Init(uint64_t cb, uint64_t device)
    {
        Add(stats_.inits);
        Start(cb, device, Coverage::init_only, State::reinitialized);
    }
    void Reset(uint64_t cb, uint64_t device)
    {
        // ReShade's Vulkan reset_command_list is before vkBeginCommandBuffer,
        // not a promise that all native reset/pool/recording paths are visible.
        Add(stats_.resets);
        Start(cb, device, Coverage::begin_observed, State::reset);
    }
    void Destroy(uint64_t cb)
    {
        Add(stats_.destroys);
        if (cb == 0) return;
        Invalidate(cb, State::destroyed);
        if (Entry *e = Find(cb)) { *e = {}; e->used = true; }
    }
    void DestroyDevice(uint64_t device)
    {
        Add(stats_.device_destroys);
        for (Entry &e : entries_) {
            if (e.cb == 0 || e.device != device) continue;
            Invalidate(e.cb, State::device_destroyed);
            e = {}; e.used = true;
        }
    }
    void Activity(uint64_t cb, uint64_t device, Command command)
    {
        if (cb == 0 || command >= Command::count) return;
        Add(stats_.events);
        Entry *e = Find(cb);
        if (e == nullptr) {
            e = Acquire(cb);
            if (e == nullptr) return;
            e->device = device;
            e->generation = NextGeneration();
            e->coverage = e->generation != 0 ? Coverage::partial : Coverage::untracked;
            Add(stats_.partial_starts);
        }
        Add(e->commands.kind[static_cast<unsigned>(command)]);
        Add(e->commands.total);
    }
    Sample First(uint64_t cb, uint64_t queue, const Images &images, uint64_t qpc, unsigned index, bool indexed)
    {
        Add(stats_.firsts);
        Sample s{};
        s.id = stats_.firsts; s.cb = cb; s.eval_queue = queue; s.images = images;
        s.qpc = qpc; s.index = index; s.indexed = indexed;
        if (const Entry *e = Find(cb)) {
            s.generation = e->generation; s.coverage = e->coverage; s.prefix = e->commands;
        }
        if (s.prefix.total != 0) Add(stats_.prefixes);
        if (s.coverage == Coverage::untracked) Add(stats_.untracked_firsts);
        else if (s.coverage != Coverage::begin_observed) Add(stats_.partial_firsts);
        if (sample_count_ < kSamples) samples_[sample_count_++] = s;
        else s.id = 0; // All later firsts contribute only to summary counters.
        return s;
    }
    void BeginSubmit(Submit &out, uint64_t queue, uint32_t batches, uint64_t fence, bool submit2)
    {
        out = {};
        Add(stats_.submits);
        if (submit2) Add(stats_.submits2);
        out.id = stats_.submits; out.queue = queue; out.batches = batches;
        out.fence = fence; out.submit2 = submit2;
        for (unsigned i = 0; i < sample_count_; ++i) {
            const Sample &s = samples_[i];
            if (s.state == State::pending && s.cb != 0) out.matches[out.candidates++].sample = s;
        }
    }
    // Walks the entire caller-owned batch/CB arrays through the guarded adapter.
    // Only matching locations have a storage cap; traversal is never capped at it.
    static void MatchCommand(Submit &out, uint64_t cb, const Location &location)
    {
        Add(out.cbs_read);
        if (cb == 0) return;
        for (unsigned i = 0; i < out.candidates; ++i) {
            Match &m = out.matches[i];
            if (m.sample.cb != cb) continue;
            Add(m.occurrences);
            if (m.stored < kLocations) m.locations[m.stored++] = location;
            else Add(m.omitted_locations);
        }
    }
    void Claim(Submit &out)
    {
        Add(stats_.batches, out.batches_read); Add(stats_.cbs, out.cbs_read); Add(stats_.read_faults, out.read_faults);
        for (unsigned i = 0; i < out.candidates; ++i) {
            Match &m = out.matches[i];
            if (m.occurrences == 0) continue;
            Sample &s = samples_[static_cast<unsigned>(m.sample.id - 1)];
            const Entry *e = Find(s.cb);
            m.submit_generation = e != nullptr ? e->generation : 0;
            if (s.state != State::pending || s.generation != m.submit_generation) continue;
            // generation==0 is deliberately only a handle match, not a recording match.
            s.state = State::in_submit;
            m.claimed = true;
        }
    }
    void Finish(Submit &out, uint32_t result)
    {
        out.result = result; // Preserve VkResult bits; no success/GPU-completion inference.
        if (result != 0) Add(stats_.failed_results);
        for (unsigned i = 0; i < out.candidates; ++i) {
            Match &m = out.matches[i];
            if (!m.claimed) continue;
            Sample &s = samples_[static_cast<unsigned>(m.sample.id - 1)];
            const Entry *e = Find(s.cb);
            m.generation_changed = s.state != State::in_submit || (e != nullptr ? e->generation : 0) != m.submit_generation;
            if (s.state == State::in_submit) s.state = State::returned;
            Add(stats_.returned);
            if (m.sample.generation == 0) Add(stats_.handle_only);
            if (m.occurrences > 1) Add(stats_.duplicate_occurrences, m.occurrences - 1);
            Add(stats_.omitted_locations, m.omitted_locations);
        }
    }
    void NestedSubmitSkipped() { Add(stats_.nested_suppressed); }
    Stats Summary() const { return stats_; }
    unsigned SampleCount() const { return sample_count_; }
    Sample GetSample(unsigned index) const { return index < sample_count_ ? samples_[index] : Sample{}; }
};

inline const char *CoverageName(Coverage coverage)
{
    switch (coverage) {
    case Coverage::begin_observed: return "reshade-begin-observed/native-coverage-incomplete";
    case Coverage::init_only: return "reshade-init-only/recording-boundary-unknown";
    case Coverage::partial: return "partial/late-or-missed-lifecycle";
    default: return "untracked/no-same-CB-event-coverage";
    }
}
} // namespace fg_submit_observation
