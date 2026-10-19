#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace dlss5::durable {
namespace fs = std::filesystem;
inline uint64_t Checksum(const void* data, size_t size) {
  uint64_t hash = 14695981039346656037ull;
  const auto* bytes = static_cast<const unsigned char*>(data);
  while (size--) { hash ^= *bytes++; hash *= 1099511628211ull; }
  return hash;
}
inline bool Plain(const fs::path& path) {
  auto p = fs::absolute(path).lexically_normal();
  for (;;) {
    const DWORD attr = GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    const auto parent = p.parent_path();
    if (parent == p || parent.empty()) break;
    p = parent;
  }
  return true;
}
inline fs::path GameDirectory() {
  wchar_t path[32768] = {};
  const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
  return n && n < 32768 ? fs::path(path).parent_path() : fs::path{};
}
inline bool SamePath(const fs::path& left, const fs::path& right) {
  // Windows module paths use backslashes even when LoadLibrary received '/'.
  const auto a=fs::absolute(left).lexically_normal().make_preferred().wstring();
  const auto b=fs::absolute(right).lexically_normal().make_preferred().wstring();
  return _wcsicmp(a.c_str(),b.c_str())==0;
}
inline bool Write(const fs::path& path, const void* data, DWORD size) {
  if (!Plain(path)) return false;
  static std::atomic_uint32_t serial{0};
  const auto temp = fs::path(path.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId())
                            + L"-" + std::to_wstring(++serial));
  HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(h, data, size, &written, nullptr) && written == size && FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok) return false; // Keep interrupted data for diagnosis.
  return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
inline fs::path StateDirectory(const fs::path& game) {
  if (game.empty()) return {};
  wchar_t local[32768] = {};
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
  auto key = game.lexically_normal().wstring();
  for (auto& ch : key) ch = static_cast<wchar_t>(towlower(ch));
  const fs::path fallback = n && n < 32768 ? fs::path(local) / L"dlss5-bridge" /
      std::to_wstring(Checksum(key.data(), key.size() * sizeof(wchar_t))) : fs::path{};
  // A locator is not needed: an existing fallback stays authoritative even if
  // a later elevated process can write to the game directory.
  std::array<fs::path, 2> choices{game / L"dlss5/state", fallback};
  std::error_code ec;
  if (!fallback.empty() && fs::exists(fallback / L"location", ec)) std::swap(choices[0], choices[1]);
  for (const auto& path : choices) {
    if (path.empty() || !Plain(path)) continue;
    fs::create_directories(path, ec);
    if (ec) { ec.clear(); continue; }
    const auto probe = path / (L"write-probe-" + std::to_wstring(GetCurrentProcessId()));
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) continue;
    CloseHandle(h);
    if (path == fallback) { const char marker[] = "DLSS5 state location v1\n"; if (!Write(path / L"location", marker, sizeof(marker))) continue; }
    return path;
  }
  return {};
}
inline fs::path RecoveryMirror(const fs::path& game) {
  wchar_t local[32768]{};
  const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
  if(!n || n>=32768) return {};
  auto key=game.lexically_normal().wstring();
  for(auto& ch:key) ch=static_cast<wchar_t>(towlower(ch));
  const auto path=fs::path(local)/L"dlss5-bridge"/L"drs-recovery"/
      std::to_wstring(Checksum(key.data(),key.size()*sizeof(wchar_t)));
  if(!Plain(path)) return {};
  std::error_code ec; fs::create_directories(path,ec);
  return ec ? fs::path{} : path;
}
enum class ReadResult { Missing, Valid, Invalid };
// Preserve an unusable record before allowing the next launch to initialize it.
// MoveFileEx without REPLACE_EXISTING cannot destroy an earlier recovery copy.
inline bool ArchiveRecord(const fs::path& path) {
  if (!Plain(path)) return false;
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    return GetLastError() == ERROR_FILE_NOT_FOUND;
  const auto backup=fs::path(path.wstring()+L".recovered-"+std::to_wstring(GetCurrentProcessId())+
      L"-"+std::to_wstring(GetTickCount64()));
  return MoveFileExW(path.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
}
template<class T> inline ReadResult Read(const fs::path& path, T& value, uint32_t magic) {
  if (!Plain(path)) return ReadResult::Invalid;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? ReadResult::Missing : ReadResult::Invalid;
  LARGE_INTEGER size{}; T copy{}; DWORD got = 0;
  const bool ok = GetFileSizeEx(h, &size) && size.QuadPart == sizeof(T) &&
      ReadFile(h, &copy, sizeof(T), &got, nullptr) && got == sizeof(T);
  CloseHandle(h);
  const uint64_t checksum = copy.checksum; copy.checksum = 0;
  if (!ok || copy.magic != magic || copy.version != 1 || checksum != Checksum(&copy, sizeof(T))) return ReadResult::Invalid;
  copy.checksum = checksum; value = copy;
  return ReadResult::Valid;
}
template<class T> inline bool Save(const fs::path& path, T value) {
  value.checksum = 0;
  value.checksum = Checksum(&value, sizeof(T));
  return Write(path, &value, sizeof(T));
}
inline std::string Sha256(const fs::path& path) {
  if (!Plain(path)) return {};
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
  if (ok) ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
  std::vector<unsigned char> buffer(1024 * 1024);
  DWORD got = 0;
  while (ok) {
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) { ok = false; break; }
    if (!got) break;
    ok = BCryptHashData(hash, buffer.data(), got, 0) >= 0;
  }
  unsigned char digest[32]{};
  if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(file);
  if (!ok) return {};
  std::string text;
  for (unsigned char b : digest) { text += "0123456789abcdef"[b >> 4]; text += "0123456789abcdef"[b & 15]; }
  return text;
}
} // namespace dlss5::durable
