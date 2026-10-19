#pragma once
// Anomaly event log shared by the three add-ons.
//
// The Bridge owns the sink (bridge/src/event-sink.h) and exports
// DLSS5LogEventV1. Generic and MFG Unlock forward their own log messages to it
// through Forward() below, so every anomaly of the suite lands in one file under
// dlss5/dlss5_log. A missing Bridge, or one without the export, makes Forward a
// no-op: the add-ons' own logging is unchanged either way.
//
// Everything here is header-only, allocation-free and safe to call from hooks:
// no loader-lock work beyond GetModuleHandle/GetProcAddress, and the caller's
// last-error value is preserved.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace dlss5::eventlog {

enum Source : uint32_t { kBridge = 1, kGeneric = 2, kMfg = 3, kSystem = 4 };
// Same numbering as reshade::log::level.
enum Level : uint32_t { kError = 1, kWarning = 2, kInfo = 3, kDebug = 4 };

inline const char* SourceName(uint32_t source) {
  switch (source) {
    case kBridge: return "bridge";
    case kGeneric: return "generic";
    case kMfg: return "mfg";
    case kSystem: return "system";
    default: return "other";
  }
}
inline const char* LevelName(uint32_t level) {
  switch (level) {
    case kError: return "error";
    case kWarning: return "warn";
    case kInfo: return "info";
    default: return "debug";
  }
}

inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Case-insensitive search for an ASCII needle; returns the offset or -1.
inline int FindNoCase(const char* text, const char* needle) {
  if (text == nullptr || needle == nullptr || *needle == 0) return -1;
  for (int i = 0; text[i] != 0; ++i) {
    int k = 0;
    while (needle[k] != 0 && text[i + k] != 0 && Lower(text[i + k]) == needle[k]) ++k;
    if (needle[k] == 0) return i;
  }
  return -1;
}

// A counter that reads zero ("timeouts=0", "errors: 0", "failures=0") is a
// status report, not an anomaly. True when the keyword at `at` is followed by
// an identifier tail and then "=0" or ": 0" with nothing numeric after.
inline bool ZeroCounterAt(const char* text, int at, int keyword_length) {
  const char* p = text + at + keyword_length;
  while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') ++p;
  if (*p == '=') ++p;
  else if (*p == ':' && p[1] == ' ') p += 2;
  else return false;
  if (*p != '0') return false;
  ++p;
  return !(*p >= '0' && *p <= '9') && *p != '.' && *p != 'x';
}

// Periodic status lines that carry counter names. They are recorded in the
// owning add-on's own log; the event log keeps anomalies only.
inline bool StatusLine(const char* text) {
  static const char* const kStatus[] = {
      "telemetry t=", "nr-verdict", "nr cost:", "frame contract", "nr-warn summary",
      "[fg-census]", "[fg-relay] rate", "[focus-fg] state:", "[fg-transition]",
      "[bridge] config:", "[wait-cause] summary",
  };
  for (const char* s : kStatus) if (FindNoCase(text, s) >= 0) return true;
  return false;
}

// Keyword classifier for free-form log lines. Lines at warning or error level
// are always anomalies; this decides only for info/debug lines.
inline bool LooksAnomalous(const char* text) {
  if (text == nullptr || *text == 0 || StatusLine(text)) return false;
  static const char* const kWords[] = {
      "fail", "error", "timed out", "timeout", "time out", "not confirmed", "refus",
      "reject", "unavailable", "mismatch", "could not", "cannot", "can't", "invalid",
      "abort", "exception", "crash", "fault", "device_removed", "device removed",
      "device_hung", "lost", "stale", "over budget", "not found", "missing", "denied",
      "unexpected", "violation", "overflow", "truncat", "recovered", "fallback",
      "fell back", "giving up", "gave up", "stall", "hung", "unchanged:", "blocked",
      "nr-warn enter", "not hooked", "not loaded", "unsupported", "no longer",
  };
  for (const char* word : kWords) {
    const int length = static_cast<int>(strlen(word));
    int from = 0;
    for (;;) {
      const int at = FindNoCase(text + from, word);
      if (at < 0) break;
      const int index = from + at;
      // Word start only: "install" is not "stall", "default" is not "fault",
      // and "UnityCrashHandler64" is a module name, not a crash.
      const char before = index > 0 ? text[index - 1] : ' ';
      const bool word_start = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z'));
      if (word_start && !ZeroCounterAt(text, index, length)) return true;
      from = index + length;
    }
  }
  return false;
}

// Deduplication key: the source plus the text with every run of digits (and
// hex after 0x) collapsed to '#', so "waited 905 ms" and "waited 907 ms" are
// one anomaly while different sites and different words stay different.
inline uint64_t KeyOf(uint32_t source, const char* text, size_t limit = 200) {
  uint64_t hash = 14695981039346656037ull ^ source;
  hash *= 1099511628211ull;
  size_t used = 0;
  bool in_number = false;
  for (const char* p = text; p != nullptr && *p != 0 && used < limit; ++p) {
    const char c = *p;
    const bool hex_tail = in_number && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == 'x');
    if ((c >= '0' && c <= '9') || hex_tail) {
      if (!in_number) { hash ^= '#'; hash *= 1099511628211ull; ++used; }
      in_number = true;
      continue;
    }
    in_number = false;
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ull;
    ++used;
  }
  return hash;
}

// --- client side: Generic and MFG Unlock ------------------------------------
using LogEventFn = void(__cdecl*)(uint32_t source, uint32_t level, const char* text);

inline LogEventFn ResolveSink() {
  static std::atomic<LogEventFn> cached{nullptr};
  static std::atomic<ULONGLONG> next_try{0};
  LogEventFn fn = cached.load(std::memory_order_acquire);
  if (fn != nullptr) return fn;
  const ULONGLONG now = GetTickCount64();
  if (now < next_try.load(std::memory_order_relaxed)) return nullptr;
  next_try.store(now + 2000, std::memory_order_relaxed);
  const HMODULE bridge = GetModuleHandleW(L"dlss5-bridge.addon64");
  if (bridge == nullptr) return nullptr;
  fn = reinterpret_cast<LogEventFn>(GetProcAddress(bridge, "DLSS5LogEventV1"));
  if (fn == nullptr) return nullptr;
  // The Bridge never unloads before the process ends once it is pinned here,
  // so a cached pointer cannot outlive its image.
  HMODULE pinned = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(fn), &pinned)) return nullptr;
  cached.store(fn, std::memory_order_release);
  return fn;
}

inline void Forward(uint32_t source, int level, const char* text) {
  if (text == nullptr || *text == 0) return;
  const DWORD last_error = GetLastError();
  if (const LogEventFn fn = ResolveSink()) fn(source, static_cast<uint32_t>(level), text);
  SetLastError(last_error);
}

// --- shared location ---------------------------------------------------------
// All log files of the suite live in <game>\dlss5\dlss5_log. Returns false when
// the folder cannot be created; the caller then keeps its previous location.
inline bool EnsureLogDirectory(const wchar_t* game_directory, wchar_t* out, size_t out_count) {
  if (game_directory == nullptr || out == nullptr || out_count == 0) return false;
  wchar_t dlss5[MAX_PATH] = {};
  if (_snwprintf_s(dlss5, _TRUNCATE, L"%ls\\dlss5", game_directory) < 0) return false;
  if (!CreateDirectoryW(dlss5, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
  if (_snwprintf_s(out, out_count, _TRUNCATE, L"%ls\\dlss5_log", dlss5) < 0) return false;
  if (!CreateDirectoryW(out, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
  const DWORD attributes = GetFileAttributesW(out);
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
         !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

}  // namespace dlss5::eventlog
