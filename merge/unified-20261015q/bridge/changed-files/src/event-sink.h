#pragma once
// The anomaly event log of the suite: dlss5/dlss5_log/dlss5-events-*.log.
//
// Producers (any thread, including hooks) call Add(); it formats one line and
// queues it under a short lock. The Bridge's maintenance thread calls Flush()
// about four times a second; Flush is the only writer of the file.
//
// Two modes:
//  * bounded (default): the first occurrence of each distinct anomaly is
//    written in full; repeats are counted and written as one "[repeat xN]" line
//    per anomaly per window. One session writes at most two 8 MB parts, the
//    bounded files in the folder are kept to 64 MB and 30 files, oldest first.
//  * full (opt-in, panel confirmation): every occurrence is written in full, in
//    64 MB parts named dlss5-events-full-*, which the bounded retention never
//    deletes. Writing pauses while the log volume has less than 4 GB free and
//    resumes above 6 GB; paused events are counted and the count is written.
//
// Nothing here throws into the caller, and nothing calls back into a logger.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../common/event-log.hpp"

class EventSink {
public:
    struct Limits {
        uint64_t bounded_part = 8ull << 20;
        uint32_t bounded_parts = 2;
        uint64_t bounded_folder_budget = 64ull << 20;
        uint32_t bounded_keep_files = 30;
        uint64_t full_part = 64ull << 20;
        uint64_t full_floor_stop = 4ull << 30;
        uint64_t full_floor_resume = 6ull << 30;
        uint64_t bounded_floor_stop = 512ull << 20;
        uint32_t repeat_window_ms = 10000;
        uint32_t budget_summary_ms = 300000;
        uint32_t space_check_ms = 2000;
        uint32_t paused_check_ms = 30000;
        size_t queue_cap_bytes = 32u << 20;
        size_t key_cap = 2048;
    };
    // Free bytes available on the volume holding `dir`; replaceable for tests.
    using FreeSpaceFn = uint64_t (*)(const wchar_t *dir);

    EventSink() { InitializeCriticalSection(&file_cs_); }
    EventSink(const EventSink &) = delete;
    EventSink &operator=(const EventSink &) = delete;

    void Configure(const wchar_t *dir, const Limits &limits, FreeSpaceFn free_space = nullptr)
    {
        EnterCriticalSection(&file_cs_);
        wcsncpy_s(dir_, dir != nullptr ? dir : L"", _TRUNCATE);
        limits_ = limits;
        free_space_ = free_space != nullptr ? free_space : &VolumeFree;
        SYSTEMTIME st; GetLocalTime(&st);
        _snwprintf_s(session_, _TRUNCATE, L"%04u%02u%02u-%02u%02u%02u-%lu",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
        configured_.store(dir_[0] != 0, std::memory_order_release);
        LeaveCriticalSection(&file_cs_);
    }

    void SetFull(bool full) { full_.store(full, std::memory_order_release); }
    bool Full() const { return full_.load(std::memory_order_acquire); }

    void Add(uint32_t source, uint32_t level, const char *text)
    {
        if (text == nullptr || *text == 0) return;
        seen_.fetch_add(1, std::memory_order_relaxed);
        SYSTEMTIME st; GetLocalTime(&st);
        char head[96];
        const int n = _snprintf_s(head, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s][%s] ",
                                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                                  st.wMilliseconds, dlss5::eventlog::SourceName(source),
                                  dlss5::eventlog::LevelName(level));
        Pending item;
        item.source = source;
        item.level = level;
        item.key = dlss5::eventlog::KeyOf(source, text);
        item.prefix = n > 0 ? static_cast<uint32_t>(n) : 0;
        item.line.reserve(item.prefix + strlen(text) + 2);
        item.line.append(head, item.prefix);
        for (const char *p = text; *p != 0; ++p) item.line.push_back(*p == '\n' || *p == '\r' ? ' ' : *p);
        item.line.push_back('\n');
        AcquireSRWLockExclusive(&queue_lock_);
        if (queue_bytes_ + item.line.size() > limits_.queue_cap_bytes) {
            ReleaseSRWLockExclusive(&queue_lock_);
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        queue_bytes_ += item.line.size();
        queue_.push_back(std::move(item));
        ReleaseSRWLockExclusive(&queue_lock_);
    }

    // Maintenance thread only.
    void Flush(uint64_t now_ms)
    {
        if (!configured_.load(std::memory_order_acquire)) return;
        std::vector<Pending> batch;
        AcquireSRWLockExclusive(&queue_lock_);
        batch.swap(queue_);
        queue_bytes_ = 0;
        ReleaseSRWLockExclusive(&queue_lock_);

        EnterCriticalSection(&file_cs_);
        const bool full = full_.load(std::memory_order_acquire);
        if (full != file_full_ && file_ != INVALID_HANDLE_VALUE) CloseFile();
        if (!started_) { started_ = true; window_start_ = now_ms; budget_summary_at_ = now_ms; Retain(); }
        CheckSpace(now_ms, full);

        const uint64_t dropped = dropped_.exchange(0, std::memory_order_relaxed);
        if (dropped != 0) {
            char text[160];
            _snprintf_s(text, _TRUNCATE, "[event-log] %llu event(s) were discarded because the in-memory queue reached %zu MB",
                        static_cast<unsigned long long>(dropped), limits_.queue_cap_bytes >> 20);
            WriteMeta(text, full);
        }
        for (Pending &item : batch) {
            if (full) { WriteEvent(item.line, true); continue; }
            auto found = keys_.find(item.key);
            if (found == keys_.end()) {
                if (keys_.size() >= limits_.key_cap) {
                    ++other_repeats_; ++suppressed_session_;
                    continue;
                }
                Key key;
                key.total = 1;
                keys_.emplace(item.key, std::move(key));
                WriteEvent(item.line, false);
                continue;
            }
            Key &key = found->second;
            ++key.total;
            ++key.repeats;
            ++suppressed_session_;
            key.last.assign(item.line, item.prefix, std::string::npos);
            key.source = item.source;
            key.level = item.level;
        }
        if (!full && now_ms - window_start_ >= limits_.repeat_window_ms) {
            WriteRepeats(now_ms);
            window_start_ = now_ms;
        }
        if (!full && budget_hit_ && now_ms - budget_summary_at_ >= limits_.budget_summary_ms) {
            budget_summary_at_ = now_ms;
            char text[200];
            _snprintf_s(text, _TRUNCATE, "[event-log] bounded session budget reached; %llu further event(s) counted, not written",
                        static_cast<unsigned long long>(over_budget_));
            WriteRaw(text, true);
        }
        LeaveCriticalSection(&file_cs_);
    }

    // Crash path: no blocking lock, best effort. Writes the queued lines raw and
    // then `text`, so the crash block follows whatever led up to it.
    void CrashWrite(const char *text)
    {
        if (!configured_.load(std::memory_order_acquire)) return;
        if (!TryEnterCriticalSection(&file_cs_)) { CrashFallback(text); return; }
        if (file_ == INVALID_HANDLE_VALUE) OpenFile(full_.load(std::memory_order_acquire));
        if (TryAcquireSRWLockExclusive(&queue_lock_)) {
            for (const Pending &item : queue_) Append(item.line.data(), item.line.size());
            queue_.clear();
            queue_bytes_ = 0;
            ReleaseSRWLockExclusive(&queue_lock_);
        }
        if (text != nullptr) {
            SYSTEMTIME st; GetLocalTime(&st);
            char line[1200];
            const int n = _snprintf_s(line, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u.%03u [bridge][error] %s\n",
                                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                                      st.wMilliseconds, text);
            if (n > 0) Append(line, static_cast<size_t>(n));
        }
        LeaveCriticalSection(&file_cs_);
    }

    struct Stats {
        uint64_t seen, written_session, suppressed_session, folder_bytes;
        bool full, paused, budget_hit;
        wchar_t file[MAX_PATH];
    };
    Stats Snapshot()
    {
        Stats s{};
        s.seen = seen_.load(std::memory_order_relaxed);
        s.full = full_.load(std::memory_order_acquire);
        if (TryEnterCriticalSection(&file_cs_)) {
            s.written_session = written_session_;
            s.suppressed_session = suppressed_session_;
            s.folder_bytes = folder_bytes_;
            s.paused = paused_;
            s.budget_hit = budget_hit_;
            wcsncpy_s(s.file, path_, _TRUNCATE);
            last_stats_ = s;
            LeaveCriticalSection(&file_cs_);
            return s;
        }
        Stats cached = last_stats_;
        cached.seen = s.seen;
        cached.full = s.full;
        return cached;
    }

    // Recomputes the folder size and applies bounded retention. Cheap enough
    // for a few-second cadence; Flush also calls it when a bounded part opens.
    void Rescan()
    {
        EnterCriticalSection(&file_cs_);
        if (configured_.load(std::memory_order_acquire)) Retain();
        LeaveCriticalSection(&file_cs_);
    }

    static uint64_t VolumeFree(const wchar_t *dir)
    {
        ULARGE_INTEGER available{};
        if (!GetDiskFreeSpaceExW(dir, &available, nullptr, nullptr)) return UINT64_MAX;
        return available.QuadPart;
    }

private:
    struct Pending {
        uint32_t source = 0, level = 0, prefix = 0;
        uint64_t key = 0;
        std::string line;
    };
    struct Key {
        uint64_t total = 0;
        uint32_t repeats = 0;
        uint32_t source = 0, level = 0;
        std::string last;   // newest occurrence without its timestamp/source prefix, with newline
    };

    void CheckSpace(uint64_t now_ms, bool full)
    {
        const uint32_t cadence = paused_ ? limits_.paused_check_ms : limits_.space_check_ms;
        if (space_checked_ && now_ms - space_checked_at_ < cadence) return;
        space_checked_ = true;
        space_checked_at_ = now_ms;
        const uint64_t free_bytes = free_space_(dir_);
        const uint64_t stop = full ? limits_.full_floor_stop : limits_.bounded_floor_stop;
        const uint64_t resume = full ? limits_.full_floor_resume : limits_.bounded_floor_stop + (256ull << 20);
        if (!paused_ && free_bytes < stop) {
            char text[220];
            _snprintf_s(text, _TRUNCATE, "[event-log] writing paused: %llu MB free on the log volume, below the %llu MB floor; events are counted until space returns",
                        static_cast<unsigned long long>(free_bytes >> 20), static_cast<unsigned long long>(stop >> 20));
            WriteRaw(text, true);
            paused_ = true;
        } else if (paused_ && free_bytes >= resume) {
            paused_ = false;
            char text[200];
            _snprintf_s(text, _TRUNCATE, "[event-log] writing resumed; %llu event(s) were counted while paused",
                        static_cast<unsigned long long>(paused_events_));
            paused_events_ = 0;
            WriteRaw(text, false);
        }
    }

    void WriteEvent(const std::string &line, bool full)
    {
        if (paused_) { ++paused_events_; return; }
        if (!full && budget_hit_) { ++over_budget_; return; }
        Write(line.data(), line.size(), full);
    }
    void WriteMeta(const char *text, bool full)
    {
        if (paused_) { ++paused_events_; return; }
        if (!full && budget_hit_) return;
        WriteRaw(text, false);
    }
    void WriteRepeats(uint64_t now_ms)
    {
        const double seconds = static_cast<double>(now_ms - window_start_) / 1000.0;
        for (auto &entry : keys_) {
            Key &key = entry.second;
            if (key.repeats == 0) continue;
            char head[160];
            SYSTEMTIME st; GetLocalTime(&st);
            const int n = _snprintf_s(head, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s][%s][repeat x%u in %.1fs, total %llu] ",
                                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                                      dlss5::eventlog::SourceName(key.source), dlss5::eventlog::LevelName(key.level),
                                      key.repeats, seconds, static_cast<unsigned long long>(key.total));
            key.repeats = 0;
            if (n <= 0) continue;
            std::string line(head, static_cast<size_t>(n));
            line += key.last;   // the newest occurrence's text, newline included
            WriteEvent(line, false);
        }
        if (other_repeats_ != 0) {
            char text[200];
            _snprintf_s(text, _TRUNCATE, "[event-log] %llu event(s) of kinds beyond the %zu tracked kinds were counted, not written",
                        static_cast<unsigned long long>(other_repeats_), limits_.key_cap);
            other_repeats_ = 0;
            WriteMeta(text, false);
        }
    }
    void WriteRaw(const char *text, bool force)
    {
        SYSTEMTIME st; GetLocalTime(&st);
        char line[512];
        const int n = _snprintf_s(line, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u.%03u [bridge][info] %s\n",
                                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                                  st.wMilliseconds, text);
        if (n <= 0) return;
        if (force) { if (file_ == INVALID_HANDLE_VALUE) OpenFile(full_.load()); Append(line, static_cast<size_t>(n)); return; }
        Write(line, static_cast<size_t>(n), full_.load());
    }

    void Write(const char *data, size_t size, bool full)
    {
        if (file_ == INVALID_HANDLE_VALUE && !OpenFile(full)) return;
        const uint64_t part = full ? limits_.full_part : limits_.bounded_part;
        if (part_bytes_ + size > part && part_bytes_ != 0) {
            if (!full && part_index_ >= limits_.bounded_parts) {
                if (!budget_hit_) {
                    budget_hit_ = true;
                    char text[200];
                    _snprintf_s(text, _TRUNCATE, "[event-log] bounded session budget of %llu MB reached; further events are counted and summarised every %u minutes",
                                static_cast<unsigned long long>((limits_.bounded_part * limits_.bounded_parts) >> 20),
                                limits_.budget_summary_ms / 60000);
                    WriteRaw(text, true);
                }
                ++over_budget_;
                return;
            }
            CloseFile();
            ++part_index_;
            if (!OpenFile(full)) return;
        }
        Append(data, size);
    }
    void Append(const char *data, size_t size)
    {
        if (file_ == INVALID_HANDLE_VALUE || size == 0) return;
        DWORD done = 0;
        if (WriteFile(file_, data, static_cast<DWORD>(size), &done, nullptr)) {
            part_bytes_ += done;
            written_session_ += done;
            folder_bytes_ += done;
        }
    }
    bool OpenFile(bool full)
    {
        if (dir_[0] == 0) return false;
        if (part_index_ == 0) part_index_ = 1;
        if (part_index_ == 1)
            _snwprintf_s(path_, _TRUNCATE, L"%ls\\dlss5-events-%ls%ls.log", dir_, full ? L"full-" : L"", session_);
        else
            _snwprintf_s(path_, _TRUNCATE, L"%ls\\dlss5-events-%ls%ls-p%u.log", dir_, full ? L"full-" : L"", session_, part_index_);
        file_ = CreateFileW(path_, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) { path_[0] = 0; return false; }
        LARGE_INTEGER size{};
        part_bytes_ = GetFileSizeEx(file_, &size) ? static_cast<uint64_t>(size.QuadPart) : 0;
        file_full_ = full;
        if (part_bytes_ == 0) {
            char header[256];
            const int n = _snprintf_s(header, _TRUNCATE,
                "# DLSS5 anomaly event log, %s mode, process %lu, part %u. One line per event; "
                "bounded mode merges repeats into one 'repeat xN' line per anomaly per window.\n",
                full ? "full" : "bounded", GetCurrentProcessId(), part_index_);
            if (n > 0) Append(header, static_cast<size_t>(n));
        }
        if (!full) Retain();
        return true;
    }
    void CloseFile()
    {
        if (file_ != INVALID_HANDLE_VALUE) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
        // A mode switch starts a new file family; parts restart at 1.
        if (file_full_ != full_.load()) part_index_ = 0;
    }
    void CrashFallback(const char *text)
    {
        if (text == nullptr) return;
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%ls\\dlss5-events-crash-%lu.log", dir_, GetCurrentProcessId());
        HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD done = 0;
        WriteFile(h, text, static_cast<DWORD>(strlen(text)), &done, nullptr);
        WriteFile(h, "\n", 1, &done, nullptr);
        CloseHandle(h);
    }

    // Bounded retention: only dlss5-events-<date>... files that are not full
    // mode, not crash fallbacks and not the file being written now.
    void Retain()
    {
        struct Entry { std::wstring path; uint64_t size; FILETIME time; };
        std::vector<Entry> bounded;
        uint64_t total_all = 0;
        wchar_t pattern[MAX_PATH];
        _snwprintf_s(pattern, _TRUNCATE, L"%ls\\dlss5-events-*.log", dir_);
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) { folder_bytes_ = 0; return; }
        do {
            if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            const uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            total_all += size;
            if (wcsncmp(data.cFileName, L"dlss5-events-full-", 18) == 0 ||
                wcsncmp(data.cFileName, L"dlss5-events-crash-", 19) == 0) continue;
            wchar_t full_path[MAX_PATH];
            _snwprintf_s(full_path, _TRUNCATE, L"%ls\\%ls", dir_, data.cFileName);
            if (_wcsicmp(full_path, path_) == 0) continue;
            bounded.push_back({full_path, size, data.ftLastWriteTime});
        } while (FindNextFileW(find, &data));
        FindClose(find);
        std::sort(bounded.begin(), bounded.end(), [](const Entry &a, const Entry &b) {
            return CompareFileTime(&a.time, &b.time) < 0;
        });
        uint64_t bounded_total = 0;
        for (const Entry &e : bounded) bounded_total += e.size;
        size_t count = bounded.size();
        for (const Entry &e : bounded) {
            if (bounded_total <= limits_.bounded_folder_budget && count < limits_.bounded_keep_files) break;
            if (DeleteFileW(e.path.c_str())) { bounded_total -= e.size; total_all -= e.size; }
            --count;
        }
        folder_bytes_ = total_all;
    }

    Limits limits_{};
    FreeSpaceFn free_space_ = &VolumeFree;
    wchar_t dir_[MAX_PATH] = {};
    wchar_t session_[64] = {};
    wchar_t path_[MAX_PATH] = {};
    std::atomic<bool> configured_{false}, full_{false};
    std::atomic<uint64_t> seen_{0}, dropped_{0};

    SRWLOCK queue_lock_ = SRWLOCK_INIT;
    std::vector<Pending> queue_;
    size_t queue_bytes_ = 0;

    CRITICAL_SECTION file_cs_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    bool file_full_ = false, started_ = false, paused_ = false, budget_hit_ = false, space_checked_ = false;
    uint32_t part_index_ = 0;
    uint64_t part_bytes_ = 0, written_session_ = 0, suppressed_session_ = 0, folder_bytes_ = 0;
    uint64_t paused_events_ = 0, over_budget_ = 0, other_repeats_ = 0;
    uint64_t window_start_ = 0, budget_summary_at_ = 0, space_checked_at_ = 0;
    std::unordered_map<uint64_t, Key> keys_;
    Stats last_stats_{};
};
