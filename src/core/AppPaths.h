#pragma once
#include <string>

namespace hb {

struct AppPaths {
  std::wstring exeDir;
  std::wstring dataDir;       // settings + profile live here
  std::wstring profileDir;    // engine user-data folder (cookies, cache, ...)
  std::wstring settingsFile;
  std::wstring resourcesDir;
  bool portable = false;      // true when portable.flag sits next to the exe

  static AppPaths Detect();
};

}  // namespace hb
