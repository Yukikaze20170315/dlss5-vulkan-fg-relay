/*
 * Thread-safe Detours installation.
 * SPDX-License-Identifier: MIT
 *
 * Detours rewrites the first bytes of the target function. If another thread's
 * instruction pointer is inside that function at the moment of the rewrite, it
 * resumes mid-instruction -- a hang or a crash, not an error code.
 *
 * DetourUpdateThread is the documented answer: every thread that might be
 * executing the patched code must be registered with the transaction so Detours
 * can suspend it and fix up its instruction pointer. Registering only
 * GetCurrentThread() is sufficient only when no other thread can execute the
 * target. Late module discovery does not provide that guarantee.
 */

#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <detours.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <sstream>
#include <tuple>
#include <vector>

#include "./reshade_compat.hpp"

namespace mfgunlock::hook {

// Function name, storage for the trampoline, replacement.
using HookItem = std::tuple<const char*, void**, void*>;

namespace internal {

struct ThreadFailure {
  const char* stage = "none";
  DWORD error = NO_ERROR;
  DWORD thread_id = 0;
  uint32_t exited_skipped = 0;
};

inline void SetThreadFailure(ThreadFailure* failure, const char* stage,
                             DWORD error, DWORD thread_id = 0) {
  if (failure) *failure = {stage, error, thread_id, failure->exited_skipped};
}

inline void CloseThreads(const std::vector<HANDLE>& threads) {
  for (HANDLE thread : threads) CloseHandle(thread);
}

// Outcome of asking whether a thread which failed to register still matters.
enum class ThreadLifetime { kExited, kAlive, kUnknown };

// A terminated thread keeps its handle valid, so SuspendThread answers
// ERROR_ACCESS_DENIED for a thread which has already left. That condition is
// benign: an exited thread cannot be parked inside the bytes about to be
// rewritten. Liveness is proven by the exit signal, never assumed from an
// error code.
inline ThreadLifetime VerifyThreadExited(DWORD id) {
  if (id == 0) return ThreadLifetime::kUnknown;
  HANDLE query = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION |
                                SYNCHRONIZE,
                            FALSE, id);
  if (!query) {
    query = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, id);
    if (!query) return GetLastError() == ERROR_INVALID_PARAMETER ? ThreadLifetime::kExited
                                                                : ThreadLifetime::kUnknown;
  }
  const DWORD wait = WaitForSingleObject(query, 0);
  DWORD exit_code = 0;
  const BOOL exit_ok = GetExitCodeThread(query, &exit_code);
  CloseHandle(query);
  if (wait == WAIT_OBJECT_0 && (!exit_ok || exit_code != STILL_ACTIVE))
    return ThreadLifetime::kExited;
  if (wait == WAIT_TIMEOUT || (exit_ok && exit_code == STILL_ACTIVE)) return ThreadLifetime::kAlive;
  return ThreadLifetime::kUnknown;
}

// A failed snapshot/access check is not an empty process. Only a thread which
// exited between enumeration and OpenThread may be omitted.
inline bool OpenOtherThreads(std::vector<HANDLE>& threads, ThreadFailure* failure = nullptr,
                             std::vector<DWORD>* known_ids = nullptr) {
  const DWORD pid = GetCurrentProcessId();
  const DWORD self = GetCurrentThreadId();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    SetThreadFailure(failure, "snapshot", GetLastError());
    return false;
  }
  THREADENTRY32 entry = {};
  entry.dwSize = sizeof(entry);
  BOOL present = Thread32First(snapshot, &entry);
  bool ok = present || GetLastError() == ERROR_NO_MORE_FILES;
  if (!ok) SetThreadFailure(failure, "thread-first", GetLastError());
  while (present && ok) {
    if (entry.dwSize < FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) +
                           sizeof(entry.th32OwnerProcessID)) {
      ok = false;
      SetThreadFailure(failure, "thread-entry-size", ERROR_BAD_LENGTH);
      break;
    }
    if (entry.th32OwnerProcessID == pid && entry.th32ThreadID != self) {
      HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                    THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
      if (thread != nullptr) {
        threads.push_back(thread);
        if (known_ids != nullptr) known_ids->push_back(entry.th32ThreadID);
      }
      else if (const DWORD error = GetLastError(); error != ERROR_INVALID_PARAMETER) {
        SetThreadFailure(failure, "open-thread", error, entry.th32ThreadID);
        ok = false;
      }
    }
    entry.dwSize = sizeof(entry);
    present = Thread32Next(snapshot, &entry);
    if (!present && GetLastError() != ERROR_NO_MORE_FILES) {
      if (ok) SetThreadFailure(failure, "thread-next", GetLastError());
      ok = false;
    }
  }
  CloseHandle(snapshot);
  if (!ok) {
    CloseThreads(threads);
    threads.clear();
  }
  return ok;
}

// Registering a thread which has already exited is not a reason to refuse the
// patch: it cannot be parked inside the bytes being rewritten. Any other
// registration failure -- including a live thread the caller cannot suspend --
// still stops the whole transaction, so the original guarantee is unchanged.
//
// `known_ids` carries the snapshot ids because these handles were opened with
// THREAD_SUSPEND_RESUME only; GetThreadId needs query rights and would answer
// zero for exactly the threads being diagnosed.
inline bool EnlistThreads(const std::vector<HANDLE>& threads, ThreadFailure* failure = nullptr,
                          const std::vector<DWORD>* known_ids = nullptr) {
  const LONG current = DetourUpdateThread(GetCurrentThread());
  bool ok = current == NO_ERROR;
  if (!ok) SetThreadFailure(failure, "enlist-current", current, GetCurrentThreadId());
  size_t index = 0;
  for (HANDLE thread : threads) {
    const DWORD id = (known_ids != nullptr && index < known_ids->size()) ? (*known_ids)[index] : 0;
    const LONG result = DetourUpdateThread(thread);
    if (result != NO_ERROR) {
      // Re-check the lifetime now: the thread may have ended between the
      // snapshot and this registration attempt.
      if (VerifyThreadExited(id) == ThreadLifetime::kExited) {
        if (failure) ++failure->exited_skipped;
        ++index;
        continue;
      }
      if (ok) SetThreadFailure(failure, "enlist-thread", result, id);
      ok = false;
    }
    ++index;
  }
  return ok;
}

}  // namespace internal

// Installs `hooks` into `module`. Returns true only if EVERY hook attached and
// the transaction committed -- a partial install leaves some entry points
// patched and others not, which is worse than none.
inline bool Install(HMODULE module, const std::vector<HookItem>& hooks,
                    const char* module_label) {
  if (module == nullptr) return false;

  std::vector<std::tuple<void**, void*, void*>> resolved;
  resolved.reserve(hooks.size());
  for (const auto& [name, real, replacement] : hooks) {
    FARPROC proc = GetProcAddress(module, name);
    if (proc == nullptr) {
      std::stringstream s;
      s << "mfgunlock::hook: " << module_label << " has no export " << name
        << " -- not hooking anything in this module.";
      mfgunlock::eventlog::message(reshade::log::level::error, s.str().c_str());
      return false;
    }
    if (*real != nullptr) return false;  // already installed
    resolved.emplace_back(real, replacement, reinterpret_cast<void*>(proc));
  }

  std::vector<HANDLE> threads;
  if (!internal::OpenOtherThreads(threads)) return false;
  if (DetourTransactionBegin() != NO_ERROR) {
    internal::CloseThreads(threads);
    return false;
  }
  // Publish only after every export has been resolved. A missing optional
  // export must not leave an unhooked address looking like an installed hook.
  for (auto& [real, replacement, address] : resolved) *real = address;

  const bool threads_ok = internal::EnlistThreads(threads);
  if (!threads_ok) {
    DetourTransactionAbort();
    for (HANDLE h : threads) CloseHandle(h);
    for (auto& [real, replacement, address] : resolved) *real = nullptr;
    mfgunlock::eventlog::message(
        reshade::log::level::error,
        "mfgunlock::hook: could not register every thread with the Detours "
        "transaction -- refusing to patch, because patching code another thread "
        "is executing hangs the process.");
    return false;
  }

  bool ok = true;
  for (auto& [real, replacement, address] : resolved) {
    if (DetourAttach(real, replacement) != NO_ERROR) {
      ok = false;
      break;
    }
  }

  if (!ok || DetourTransactionCommit() != NO_ERROR) {
    if (!ok) DetourTransactionAbort();
    for (HANDLE h : threads) CloseHandle(h);
    for (auto& [real, replacement, address] : resolved) *real = nullptr;
    std::stringstream s;
    s << "mfgunlock::hook: failed to install hooks in " << module_label << ".";
    mfgunlock::eventlog::message(reshade::log::level::error, s.str().c_str());
    return false;
  }

  for (HANDLE h : threads) CloseHandle(h);
  std::stringstream s;
  s << "mfgunlock::hook: installed " << resolved.size() << " hook(s) in "
    << module_label << ", with " << threads.size()
    << " other thread(s) registered with the transaction.";
  mfgunlock::eventlog::message(reshade::log::level::info, s.str().c_str());
  return true;
}

// Explicit teardown only. Process termination leaves cleanup to the OS.
// The checked form is used by owners which also hold a module reference: the
// reference may be released only after every detour has been detached.
inline bool TryUninstall(const std::vector<HookItem>& hooks) {
  std::vector<HANDLE> threads;
  if (!internal::OpenOtherThreads(threads)) return false;
  if (DetourTransactionBegin() != NO_ERROR) {
    internal::CloseThreads(threads);
    return false;
  }
  bool ok = internal::EnlistThreads(threads);
  for (const auto& [name, real, replacement] : hooks) {
    (void)name;
    if (ok && *real != nullptr && DetourDetach(real, replacement) != NO_ERROR) ok = false;
  }
  if (ok) ok = DetourTransactionCommit() == NO_ERROR;
  else DetourTransactionAbort();
  internal::CloseThreads(threads);
  return ok;
}

inline void Uninstall(const std::vector<HookItem>& hooks) {
  (void)TryUninstall(hooks);
}

// Hook native entries so callers keep the original function address.
struct ModuleIdentity {
  HMODULE module = nullptr;
  DWORD timestamp = 0;
  DWORD image_bytes = 0;

  bool operator==(const ModuleIdentity& other) const {
    return module == other.module && timestamp == other.timestamp &&
           image_bytes == other.image_bytes;
  }
};

namespace internal {
inline bool Readable(const void* address, size_t bytes, HMODULE allocation) {
  if (address == nullptr || bytes == 0 || allocation == nullptr) return false;
  auto cursor = reinterpret_cast<uintptr_t>(address);
  if (cursor > UINTPTR_MAX - bytes) return false;
  const uintptr_t end = cursor + bytes;
  while (cursor < end) {
    MEMORY_BASIC_INFORMATION memory = {};
    if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) == 0) return false;
    if (memory.State != MEM_COMMIT || memory.AllocationBase != allocation ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
      return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(memory.BaseAddress);
    if (base > UINTPTR_MAX - memory.RegionSize) return false;
    const uintptr_t next = base + memory.RegionSize;
    if (next <= cursor) return false;
    cursor = next < end ? next : end;
  }
  return true;
}
}  // namespace internal

inline bool CaptureModule(HMODULE module, ModuleIdentity& identity) {
  identity = {};
  if (!module || !internal::Readable(module, sizeof(IMAGE_DOS_HEADER), module)) return false;

  IMAGE_DOS_HEADER dos = {};
  std::memcpy(&dos, module, sizeof(dos));
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 16 * 1024 * 1024) {
    return false;
  }

  const auto base = reinterpret_cast<uintptr_t>(module);
  const auto offset = static_cast<uintptr_t>(dos.e_lfanew);
  if (base > UINTPTR_MAX - offset) return false;
  const auto* nt_address = reinterpret_cast<const void*>(base + offset);
  if (!internal::Readable(nt_address, sizeof(IMAGE_NT_HEADERS64), module)) return false;

  IMAGE_NT_HEADERS64 nt = {};
  std::memcpy(&nt, nt_address, sizeof(nt));
  if (nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      nt.OptionalHeader.SizeOfImage == 0) {
    return false;
  }

  identity = {module, nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage};
  return true;
}

inline bool IsCurrent(const ModuleIdentity& identity) {
  if (!identity.module) return false;
  ModuleIdentity current;
  return CaptureModule(identity.module, current) && current == identity;
}

struct AddressHook {
  std::atomic<void*> target{nullptr};
  std::atomic<void*> replacement{nullptr};
  std::atomic<void*> trampoline{nullptr};
  ModuleIdentity identity{};
  std::atomic_bool installed{false};
  SRWLOCK lock = SRWLOCK_INIT;

  template <class Function>
  Function Original() const {
    return reinterpret_cast<Function>(trampoline.load(std::memory_order_acquire));
  }

  bool Targets(const void* address) const {
    return installed.load(std::memory_order_acquire) &&
           target.load(std::memory_order_acquire) == address;
  }
};

inline void ClearAddressState(AddressHook& state) {
  state.installed.store(false, std::memory_order_release);
  state.trampoline.store(nullptr, std::memory_order_release);
  state.target.store(nullptr, std::memory_order_release);
  state.replacement.store(nullptr, std::memory_order_release);
  state.identity = {};
}

// Call only before a transaction or after commit/abort has resumed every thread.
// ReShade logging and the CRT may take locks owned by suspended threads.
inline bool LogAddressFailure(const AddressHook& state, void* target,
                              const char* label, const char* stage, DWORD error,
                              DWORD thread_id = 0) {
  const DWORD saved_error = GetLastError();
  MEMORY_BASIC_INFORMATION memory = {};
  const SIZE_T queried = VirtualQuery(target, &memory, sizeof(memory));
  std::stringstream message;
  message << "mfgunlock::hook: address failure label=" << label
          << " stage=" << stage << " error=" << error << " thread=" << thread_id
          << " target=" << target << " query=" << queried
          << " allocation=" << memory.AllocationBase
          << " state=0x" << std::hex << memory.State
          << " type=0x" << memory.Type << " protect=0x" << memory.Protect
          << " installed=" << state.installed.load(std::memory_order_acquire)
          << " stored_target=" << state.target.load(std::memory_order_acquire)
          << " stored_module=" << state.identity.module << ".";
  mfgunlock::eventlog::message(reshade::log::level::warning, message.str().c_str());
  SetLastError(saved_error);
  return false;
}

inline bool InstallAddress(AddressHook& state, void* target, void* replacement,
                           const char* label) {
  if (!target || !replacement)
    return LogAddressFailure(state, target, label, "arguments", ERROR_INVALID_PARAMETER);
  AcquireSRWLockExclusive(&state.lock);
  struct Unlock {
    SRWLOCK* lock;
    ~Unlock() { ReleaseSRWLockExclusive(lock); }
  } unlock{&state.lock};

  if (state.installed.load(std::memory_order_acquire)) {
    if (IsCurrent(state.identity)) {
      const bool same = state.target.load(std::memory_order_acquire) == target &&
                        state.replacement.load(std::memory_order_acquire) == replacement;
      return same || LogAddressFailure(state, target, label, "existing-target", ERROR_ALREADY_EXISTS);
    }
    // The old image disappeared. Its detour disappeared with the mapping too,
    // so this record can be reused without writing through stale pointers.
    ClearAddressState(state);
  }

  MEMORY_BASIC_INFORMATION memory = {};
  if (VirtualQuery(target, &memory, sizeof(memory)) == 0)
    return LogAddressFailure(state, target, label, "query-target", GetLastError());
  if (memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || !memory.AllocationBase)
    return LogAddressFailure(state, target, label, "target-not-image", ERROR_INVALID_ADDRESS);
  ModuleIdentity identity;
  if (!CaptureModule(reinterpret_cast<HMODULE>(memory.AllocationBase), identity))
    return LogAddressFailure(state, target, label, "module-identity", ERROR_BAD_EXE_FORMAT);

  void* trampoline = target;
  std::vector<HANDLE> threads;
  std::vector<DWORD> known_ids;
  internal::ThreadFailure failure;
  if (!internal::OpenOtherThreads(threads, &failure, &known_ids))
    return LogAddressFailure(state, target, label, failure.stage, failure.error, failure.thread_id);
  const LONG begin = DetourTransactionBegin();
  if (begin != NO_ERROR) {
    internal::CloseThreads(threads);
    return LogAddressFailure(state, target, label, "transaction-begin", begin);
  }
  if (!internal::EnlistThreads(threads, &failure, &known_ids)) {
    DetourTransactionAbort();
    internal::CloseThreads(threads);
    return LogAddressFailure(state, target, label, failure.stage, failure.error, failure.thread_id);
  }
  if (failure.exited_skipped != 0) {
    std::stringstream s;
    s << "mfgunlock::hook: " << failure.exited_skipped
      << " thread(s) exited between enumeration and registration in " << label
      << " -- skipped, because an exited thread cannot be parked in the patched bytes.";
    mfgunlock::eventlog::message(reshade::log::level::info, s.str().c_str());
  }

  PDETOUR_TRAMPOLINE prepared = nullptr;
  const LONG attach = DetourAttachEx(&trampoline, replacement, &prepared, nullptr, nullptr);
  const bool attached = attach == NO_ERROR;
  if (attached) {
    state.identity = identity;
    state.target.store(target, std::memory_order_release);
    state.replacement.store(replacement, std::memory_order_release);
    // Commit resumes target threads before returning. Publish the prepared
    // trampoline first so a newly entered callback never sees a null original.
    state.trampoline.store(reinterpret_cast<void*>(prepared), std::memory_order_release);
  }
  const LONG commit = attached ? DetourTransactionCommit() : ERROR_INVALID_OPERATION;
  if (!attached) DetourTransactionAbort();
  internal::CloseThreads(threads);
  if (!attached || commit != NO_ERROR) {
    ClearAddressState(state);
    return LogAddressFailure(state, target, label, attached ? "transaction-commit" : "attach",
                             attached ? commit : attach);
  }

  state.installed.store(true, std::memory_order_release);
  return true;
}

inline void UninstallAddress(AddressHook& state) {
  AcquireSRWLockExclusive(&state.lock);
  struct Unlock {
    SRWLOCK* lock;
    ~Unlock() { ReleaseSRWLockExclusive(lock); }
  } unlock{&state.lock};

  if (!state.installed.load(std::memory_order_acquire)) {
    ClearAddressState(state);
    return;
  }
  if (!IsCurrent(state.identity)) {
    // The patched image has already been unmapped, so there is no mapped code
    // left to restore and no stale address should be touched.
    ClearAddressState(state);
    return;
  }

  void* trampoline = state.trampoline.load(std::memory_order_acquire);
  void* replacement = state.replacement.load(std::memory_order_acquire);
  if (!trampoline || !replacement) return;
  std::vector<HANDLE> threads;
  if (!internal::OpenOtherThreads(threads)) return;
  if (DetourTransactionBegin() != NO_ERROR) {
    internal::CloseThreads(threads);
    return;
  }
  if (!internal::EnlistThreads(threads)) {
    DetourTransactionAbort();
    internal::CloseThreads(threads);
    return;
  }
  const bool detached = DetourDetach(&trampoline, replacement) == NO_ERROR;
  const LONG commit = detached ? DetourTransactionCommit() : ERROR_INVALID_OPERATION;
  if (!detached) DetourTransactionAbort();
  for (HANDLE thread : threads) CloseHandle(thread);
  if (!detached || commit != NO_ERROR) {
    mfgunlock::eventlog::message(reshade::log::level::error,
                         "mfgunlock::hook: failed to remove address hook.");
    return;
  }
  ClearAddressState(state);
}

}  // namespace mfgunlock::hook
