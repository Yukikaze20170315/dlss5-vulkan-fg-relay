#pragma once
#include <cstdint>
namespace dlss5::deployment_policy {
struct State {
  uint32_t magic=0x35504c44,version=1;
  uint64_t checksum=0,generation=0,fingerprint=0;
  uint32_t selected=0,confirmed=0,active=0,healthy=0,trial=0,failures=0,safe=0,nr=0,early=0;
  uint32_t confirmed_nr=0,confirmed_early=0,reserved=0;
};
inline bool Valid(const State& s) {
  return s.selected<=1 && s.confirmed<=1 && s.active<=1 && s.healthy<=1 && s.trial<=1 &&
      s.safe<=1 && s.nr<=2 && s.early<=2 && s.confirmed_nr<=2 && s.confirmed_early<=2 && s.failures<1000;
}
inline bool Begin(State& s,uint64_t fingerprint,bool crashed) {
  bool restored=false;
  const bool environment_changed=s.fingerprint && fingerprint!=s.fingerprint;
  if(s.active && !s.healthy) {
    ++s.failures;
    const bool trial=s.trial || environment_changed;
    const uint32_t limit=trial ? (crashed?1u:2u) : (crashed?2u:3u);
    if(s.failures>=limit) {
      if(trial && !environment_changed &&
          (s.selected!=s.confirmed || s.nr!=s.confirmed_nr || s.early!=s.confirmed_early)) {
        s.selected=s.confirmed; s.nr=s.confirmed_nr; s.early=s.confirmed_early;
      } else s.safe=1;
      restored=true;
    }
  }
  s.trial=s.selected!=s.confirmed || s.nr!=s.confirmed_nr || s.early!=s.confirmed_early || environment_changed;
  s.fingerprint=fingerprint; s.active=1; s.healthy=0;
  return restored;
}
inline void Healthy(State& s,uint32_t sl) {
  s.healthy=1; s.trial=0; s.failures=0; s.confirmed=sl;
  s.confirmed_nr=s.nr; s.confirmed_early=s.early;
}
} // namespace dlss5::deployment_policy
