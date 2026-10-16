#include "core/Settings.h"

#include "core/Url.h"
#include "core/Util.h"

namespace hb {
namespace {

bool ParseBool(const std::wstring& v, bool& out) {
  std::wstring l = ToLower(v);
  if (l == L"true" || l == L"1" || l == L"yes" || l == L"on") { out = true; return true; }
  if (l == L"false" || l == L"0" || l == L"no" || l == L"off") { out = false; return true; }
  return false;
}

bool ParseIntRange(const std::wstring& v, int lo, int hi, int& out) {
  if (v.empty() || v.size() > 6) return false;
  long n = 0;
  for (wchar_t c : v) {
    if (c < L'0' || c > L'9') return false;
    n = n * 10 + (c - L'0');
  }
  if (n < lo || n > hi) return false;
  out = static_cast<int>(n);
  return true;
}

void Warn(std::wstring* w, const std::wstring& msg) {
  if (!w || w->size() > 600) return;
  if (!w->empty()) *w += L"\n";
  *w += msg;
}

}  // namespace

Settings Settings::Parse(const std::string& textIn, std::wstring* warning) {
  Settings s;
  std::string text = textIn;
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF)
    text.erase(0, 3);

  size_t pos = 0;
  while (pos <= text.size()) {
    size_t eol = text.find('\n', pos);
    std::string line = TrimA(text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos));
    pos = eol == std::string::npos ? text.size() + 1 : eol + 1;
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;

    size_t eq = line.find('=');
    if (eq == std::string::npos) { Warn(warning, L"Ignored malformed line in settings.ini."); continue; }
    std::wstring key = ToLower(FromUtf8(TrimA(line.substr(0, eq))));
    std::wstring val = FromUtf8(TrimA(line.substr(eq + 1)));

    if (key == L"homepage") {
      if (IsHttpUrl(val)) s.homepage = val; else Warn(warning, L"Invalid 'homepage' (must be an http/https URL); using default.");
    } else if (key == L"search_url") {
      if (IsHttpUrl(val) && val.find(L"%s") != std::wstring::npos) s.searchUrl = val;
      else Warn(warning, L"Invalid 'search_url' (needs http/https and a %s placeholder); using default.");
    } else if (key == L"startup") {
      std::wstring l = ToLower(val);
      if (l == L"newtab" || l == L"homepage") s.startup = l; else Warn(warning, L"Invalid 'startup' (newtab or homepage); using default.");
    } else if (key == L"hardware_acceleration") {
      if (!ParseBool(val, s.hardwareAcceleration)) Warn(warning, L"Invalid 'hardware_acceleration' (true/false); using default.");
    } else if (key == L"show_hud") {
      if (!ParseBool(val, s.showHud)) Warn(warning, L"Invalid 'show_hud' (true/false); using default.");
    } else if (key == L"performance_mode") {
      std::wstring l = ToLower(val);
      if (l == L"balanced" || l == L"memory_saver" || l == L"off") s.performanceMode = l;
      else Warn(warning, L"Invalid 'performance_mode' (balanced, memory_saver or off); using default.");
    } else if (key == L"tab_sleep_minutes") {
      if (!ParseIntRange(val, 1, 240, s.tabSleepMinutes)) Warn(warning, L"Invalid 'tab_sleep_minutes' (1-240); using default.");
    }
    // Unknown keys are ignored on purpose (forward compatibility).
  }
  return s;
}

std::string Settings::Serialize() const {
  std::string o;
  o += "# HyperBrowser settings. Edit and restart the browser to apply.\n";
  o += "# If this file is damaged, invalid lines are ignored and defaults are used.\n\n";
  o += "# Page opened by the Home button (and at startup if startup=homepage)\n";
  o += "homepage=" + ToUtf8(homepage) + "\n\n";
  o += "# Search URL used by the address bar. %s is replaced by the query.\n";
  o += "search_url=" + ToUtf8(searchUrl) + "\n\n";
  o += "# newtab | homepage\n";
  o += "startup=" + ToUtf8(startup) + "\n\n";
  o += "# Set to false to start the engine with --disable-gpu (helps on broken GPU drivers)\n";
  o += std::string("hardware_acceleration=") + (hardwareAcceleration ? "true" : "false") + "\n\n";
  o += "# Show the performance HUD (CPU/RAM/GPU) in the bottom-right corner\n";
  o += std::string("show_hud=") + (showHud ? "true" : "false") + "\n\n";
  o += "# balanced | memory_saver | off  (controls background-tab sleeping and memory targets)\n";
  o += "performance_mode=" + ToUtf8(performanceMode) + "\n\n";
  o += "# Idle minutes before a background tab sleeps in balanced mode (memory_saver uses a third of this)\n";
  o += "tab_sleep_minutes=" + std::to_string(tabSleepMinutes) + "\n";
  return o;
}

#ifdef _WIN32
Settings Settings::Load(const std::wstring& path, std::wstring* warning) {
  if (!FileExists(path)) return Settings{};
  std::string text;
  if (!ReadFileBytes(path, text)) {
    Warn(warning, L"settings.ini could not be read (too large or locked).");
    return Settings{};
  }
  return Parse(text, warning);
}

bool Settings::Save(const std::wstring& path) const { return WriteFileAtomic(path, Serialize()); }
#endif

}  // namespace hb
