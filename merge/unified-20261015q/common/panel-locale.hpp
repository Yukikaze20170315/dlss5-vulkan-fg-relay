#pragma once
#include "durable.hpp"
#include <string>

namespace dlss5_panel {
inline unsigned ChineseCount(const std::string& value) {
  unsigned count=0;
  for(unsigned char ch:value) if(ch>=0xe3 && ch<=0xe9) ++count;
  return count;
}
// Legacy status records retain both translations for support logs. Select only
// one at the UI boundary; identifiers and slash-separated paths remain intact.
inline std::string SelectText(const std::string& value,bool chinese) {
  std::string result;
  size_t begin=0;
  do {
    const auto end=value.find('\n',begin);
    auto line=value.substr(begin,end==std::string::npos ? end:end-begin);
    const auto id_begin=line.find("##");
    const auto id=id_begin==std::string::npos ? std::string{}:line.substr(id_begin);
    if(id_begin!=std::string::npos) line.resize(id_begin);
    const auto split=line.find(" / ");
    if(split!=std::string::npos) {
      auto left=line.substr(0,split),right=line.substr(split+3);
      const auto lc=ChineseCount(left),rc=ChineseCount(right);
      if(lc!=rc) line=(lc>rc)==chinese ? left:right;
    }
    result+=line+id;
    if(end==std::string::npos) break;
    result+='\n'; begin=end+1;
  } while(begin<value.size());
  return result;
}
struct Preference {
  struct Record {
    uint32_t magic=0x3549554c,version=1;
    uint64_t generation=0;
    uint32_t chinese=1,reserved=0;
    uint64_t checksum=0;
  };
  bool chinese=true,loaded=false;
  void Load(const std::filesystem::path& dir) {
    if(loaded || dir.empty()) return;
    Record record;
    if(dlss5::durable::Read(dir/L"panel-language.state",record,record.magic)==dlss5::durable::ReadResult::Valid && record.chinese<=1)
      chinese=record.chinese!=0;
    loaded=true;
  }
  void Store(const std::filesystem::path& dir,bool value) {
    chinese=value;
    Record record; record.chinese=value?1u:0u;
    if(!dir.empty()) dlss5::durable::Save(dir/L"panel-language.state",record);
  }
}
;
}
