#pragma once
#include "../../../../common/durable.hpp"
#include <mutex>

namespace renodx::addons::dlss5::stages {
namespace trial {
constexpr uint32_t kMagic = 0x3552594c;
struct Record {
  uint32_t magic = kMagic, version = 1;
  uint64_t checksum = 0, generation = 0;
  uint32_t confirmed = 8, requested = 8, pending = 0, reserved = 0;
};
inline Record record;
inline std::filesystem::path file;
inline std::mutex mutex;
inline std::atomic_uint64_t deadline{0};
inline std::atomic_uint32_t fallback{8};
inline std::atomic_bool changed{false}, safe{false};
inline bool writable = false;
using SafeQuery = uint32_t (__cdecl*)();
inline SafeQuery bridge_safe_query=nullptr;
inline std::string status;
inline bool Persist(Record next) {
  next.generation = record.generation + 1;
  if (!writable || !::dlss5::durable::Save(file, next)) {
    status = "状态无法保存，拒绝高层数试用 / Cannot save trial state.";
    return false;
  }
  record = next;
  return true;
}
inline void Initialize(Plan requested, std::filesystem::path directory = {}) {
  std::lock_guard<std::mutex> lock(mutex);
  deadline=0; safe=false; changed=false; record={};
  auto dir = directory.empty() ? ::dlss5::durable::StateDirectory(::dlss5::durable::GameDirectory()) : directory;
  file = dir / L"generic-layers.state";
  writable = !dir.empty();
  auto result = writable ? ::dlss5::durable::Read(file, record, kMagic) : ::dlss5::durable::ReadResult::Invalid;
  if (result == ::dlss5::durable::ReadResult::Invalid || !Plan{record.confirmed}.Valid() ||
      !Plan{record.requested}.Valid() || record.pending > 1) {
    if(writable && std::filesystem::exists(file)) {
      std::error_code ec;
      std::filesystem::copy_file(file,std::filesystem::path(file.wstring()+L".bad-"+std::to_wstring(GetTickCount64())),std::filesystem::copy_options::none,ec);
      if(ec) writable=false;
    }
    record = {}; safe = true;
    status = "层数状态损坏或不可写，本次暂停 NR。请运行故障恢复。 / Invalid state; NR paused.";
  }
  const auto bridge = GetModuleHandleW(L"dlss5-bridge.addon64");
  using Safe = uint32_t (__cdecl*)();
  auto get_safe = bridge ? reinterpret_cast<Safe>(GetProcAddress(bridge, "DLSS5DeploymentSafeMode")) : nullptr;
  bridge_safe_query=get_safe;
  if (get_safe && get_safe()) safe = true;
  if (record.pending) {
    requested = Plan{record.confirmed};
    Record next = record; next.pending = 0; next.requested = next.confirmed;
    Persist(next);
    status = "上次层数未确认，已回退 / Unconfirmed layer plan restored.";
  } else if (requested.Total() < 5) {
    Record next = record; next.confirmed = next.requested = requested.packed;
    Persist(next);
  } else if (requested.packed != record.confirmed || result != ::dlss5::durable::ReadResult::Valid) {
    Record next = record; next.pending = 1; next.requested = requested.packed;
    if (Persist(next)) deadline = GetTickCount64() + 15000;
    else requested = Plan{record.confirmed};
  }
  fallback = record.confirmed;
  configuration.store(requested.packed, std::memory_order_release);
}
inline bool Request(Plan plan) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!plan.Valid() || safe.load() || (bridge_safe_query && bridge_safe_query())) return false;
  Record next = record;
  next.requested = plan.packed;
  next.pending = plan.Total() >= 5 && plan.packed != record.confirmed;
  if (!next.pending) next.confirmed = plan.packed;
  // No GPU consumer can see a high-layer plan before its rollback record is durable.
  if (!Persist(next)) return false;
  fallback.store(record.confirmed, std::memory_order_release);
  deadline.store(next.pending ? GetTickCount64() + 15000 : 0, std::memory_order_release);
  configuration.store(plan.packed, std::memory_order_release);
  return true;
}
inline Plan Snapshot() {
  if (safe.load(std::memory_order_acquire) || (bridge_safe_query && bridge_safe_query())) return {0};
  const uint64_t due = deadline.load(std::memory_order_acquire);
  if (due && GetTickCount64() >= due) {
    // Every NR dispatch takes a snapshot, including with the overlay closed.
    // A stalled rendering thread cannot dispatch the expired plan on resumption.
    return {fallback.load(std::memory_order_acquire)};
  }
  return {configuration.load(std::memory_order_acquire)};
}
inline bool Resolve(bool keep, uint64_t now = GetTickCount64()) {
  std::lock_guard<std::mutex> lock(mutex);
  const auto due = deadline.load();
  if (!due) return false;
  if (keep && (now >= due || now + 13000 < due)) return false;
  Record next = record;
  if (keep) next.confirmed = next.requested;
  else next.requested = next.confirmed;
  next.pending = 0;
  if (!Persist(next)) {
    if (keep) return false;
    // Keep the pending disk record for recovery on the next launch.
  }
  configuration.store(next.requested, std::memory_order_release);
  fallback = next.confirmed;
  deadline = 0;
  changed = true;
  status = keep ? "已保留层数 / Layer plan confirmed." : "已自动回退层数 / Layer plan restored.";
  return true;
}
inline bool Poll() {
  const auto due = deadline.load();
  if (due && GetTickCount64() >= due) Resolve(false);
  return changed.exchange(false);
}
} // namespace trial
} // namespace renodx::addons::dlss5::stages
