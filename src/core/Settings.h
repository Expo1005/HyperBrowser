#pragma once
#include <string>

namespace hb {

// User settings stored as UTF-8 "key=value" lines in settings.ini.
// Stage 1 only contains settings that are actually wired up.
struct Settings {
  std::wstring homepage = L"https://www.google.com";
  std::wstring searchUrl = L"https://www.google.com/search?q=%s";
  std::wstring startup = L"newtab";  // "newtab" or "homepage"
  bool hardwareAcceleration = true;
  bool showHud = true;                         // performance HUD (status strip, bottom-right)
  std::wstring performanceMode = L"balanced";  // balanced | memory_saver | off
  int tabSleepMinutes = 10;                    // idle time before a background tab sleeps (balanced mode)

  // Never throws and never fails hard: bad/unknown lines are skipped, bad values fall back to defaults,
  // and a human-readable description of every problem is appended to *warning.
  static Settings Parse(const std::string& text, std::wstring* warning);
  std::string Serialize() const;

#ifdef _WIN32
  static Settings Load(const std::wstring& path, std::wstring* warning);
  bool Save(const std::wstring& path) const;
#endif
};

}  // namespace hb
