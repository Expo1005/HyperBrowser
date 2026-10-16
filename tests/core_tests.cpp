// Host-side tests for the platform-independent core (Url, Settings, text utils).
// Build: cmake -S . -B build-tests -DHB_BUILD_TESTS=ON && cmake --build build-tests && ./build-tests/core_tests
#include <cstdio>
#include <string>
#include <vector>

#include "core/Settings.h"
#include "core/Url.h"
#include "core/Util.h"
#include "perf/PerfLogic.h"

static int failures = 0;
#define CHECK_EQ(a, b)                                                                              \
  do {                                                                                              \
    std::wstring _a = (a), _b = (b);                                                                \
    if (_a != _b) {                                                                                 \
      ++failures;                                                                                   \
      std::printf("FAIL line %d: got [%s] expected [%s]\n", __LINE__, hb::ToUtf8(_a).c_str(),       \
                  hb::ToUtf8(_b).c_str());                                                          \
    }                                                                                               \
  } while (0)
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #c); } \
  } while (0)

int main() {
  const std::wstring g = L"https://www.google.com/search?q=%s";
  CHECK_EQ(hb::ResolveInput(L"", g), L"");
  CHECK_EQ(hb::ResolveInput(L"   ", g), L"");
  CHECK_EQ(hb::ResolveInput(L"https://example.com/a b", g), L"https://example.com/a%20b");
  CHECK_EQ(hb::ResolveInput(L"example.com", g), L"https://example.com");
  CHECK_EQ(hb::ResolveInput(L"example.com/path?x=1", g), L"https://example.com/path?x=1");
  CHECK_EQ(hb::ResolveInput(L"localhost:3000", g), L"http://localhost:3000");
  CHECK_EQ(hb::ResolveInput(L"192.168.1.1/admin", g), L"http://192.168.1.1/admin");
  CHECK_EQ(hb::ResolveInput(L"cats", g), L"https://www.google.com/search?q=cats");
  CHECK_EQ(hb::ResolveInput(L"best fps games", g), L"https://www.google.com/search?q=best+fps+games");
  CHECK_EQ(hb::ResolveInput(L"a&b=c d", g), L"https://www.google.com/search?q=a%26b%3Dc+d");
  CHECK_EQ(hb::ResolveInput(L"javascript:alert(1)", g), L"https://www.google.com/search?q=javascript%3Aalert%281%29");
  CHECK_EQ(hb::ResolveInput(L"data:text/html,hi", g), L"https://www.google.com/search?q=data%3Atext%2Fhtml%2Chi");
  CHECK_EQ(hb::ResolveInput(L"C:\\Users\\me\\a b.html", g), L"file:///C:/Users/me/a%20b.html");
  CHECK_EQ(hb::ResolveInput(L"caf\u00E9", g), L"https://www.google.com/search?q=caf%C3%A9");
  CHECK_EQ(hb::ResolveInput(L"hello.", g), L"https://www.google.com/search?q=hello.");
  CHECK_EQ(hb::ResolveInput(L"cats", L""), L"https://www.google.com/search?q=cats");  // empty template falls back

  CHECK(hb::IsHttpUrl(L"https://a.b"));
  CHECK(!hb::IsHttpUrl(L"https://"));
  CHECK(!hb::IsHttpUrl(L"ftp://a.b"));
  CHECK(!hb::IsHttpUrl(L"javascript:1"));

  // Settings: defaults, valid, corrupt
  std::wstring w;
  hb::Settings d = hb::Settings::Parse("", &w);
  CHECK_EQ(d.homepage, L"https://www.google.com");
  CHECK(w.empty());

  hb::Settings s = hb::Settings::Parse("\xEF\xBB\xBF# c\nhomepage = https://duckduckgo.com\nstartup=homepage\n"
                                       "hardware_acceleration=false\nunknown=1\n", &w);
  CHECK_EQ(s.homepage, L"https://duckduckgo.com");
  CHECK_EQ(s.startup, L"homepage");
  CHECK(!s.hardwareAcceleration);
  CHECK(w.empty());

  w.clear();
  hb::Settings bad = hb::Settings::Parse("homepage=javascript:alert(1)\nsearch_url=https://x.y/\nstartup=banana\n"
                                         "hardware_acceleration=maybe\ngarbage line\n\x01\x02\xFF", &w);
  CHECK_EQ(bad.homepage, L"https://www.google.com");
  CHECK_EQ(bad.searchUrl, L"https://www.google.com/search?q=%s");
  CHECK_EQ(bad.startup, L"newtab");
  CHECK(bad.hardwareAcceleration);
  CHECK(!w.empty());

  // Round trip
  hb::Settings rt = hb::Settings::Parse(s.Serialize(), &w);
  CHECK_EQ(rt.homepage, s.homepage);
  CHECK_EQ(rt.startup, s.startup);

  // New Stage 2 settings
  w.clear();
  hb::Settings p2 = hb::Settings::Parse("show_hud=false\nperformance_mode=Memory_Saver\ntab_sleep_minutes=30\n", &w);
  CHECK(!p2.showHud);
  CHECK_EQ(p2.performanceMode, L"memory_saver");
  CHECK(p2.tabSleepMinutes == 30);
  CHECK(w.empty());
  w.clear();
  hb::Settings p3 = hb::Settings::Parse("performance_mode=turbo\ntab_sleep_minutes=0\n", &w);
  CHECK_EQ(p3.performanceMode, L"balanced");
  CHECK(p3.tabSleepMinutes == 10);
  CHECK(!w.empty());
  hb::Settings p4 = hb::Settings::Parse(p2.Serialize(), &w);
  CHECK(!p4.showHud && p4.tabSleepMinutes == 30 && p4.performanceMode == L"memory_saver");

  // Sleep policy
  using hb::TabSleepInfo;
  hb::SleepConfig bal = hb::MakeSleepConfig(L"balanced", 10, false);
  CHECK(bal.enabled && bal.idleMs == 600000ull);
  CHECK(hb::MakeSleepConfig(L"memory_saver", 10, false).idleMs == 180000ull);
  CHECK(hb::MakeSleepConfig(L"memory_saver", 2, false).idleMs == 60000ull);
  CHECK(!hb::MakeSleepConfig(L"off", 10, false).enabled);
  CHECK(hb::MakeSleepConfig(L"off", 10, true).enabled && hb::MakeSleepConfig(L"off", 10, true).idleMs == 30000ull);
  CHECK(hb::MakeSleepConfig(L"balanced", 9999, false).idleMs == 240ull * 60000ull);

  std::vector<TabSleepInfo> tabs(6);
  for (int i = 0; i < 6; ++i) { tabs[i].id = i + 1; tabs[i].lastActiveMs = 1000; }
  tabs[0].isActive = true;       // never
  tabs[1].isSleeping = true;     // already asleep
  tabs[2].playingAudio = true;   // never: would cut music/calls
  tabs[3].loading = true;        // never: still loading
  tabs[5].lastActiveMs = 900000; // not idle long enough yet at now=1000000 (idle 100 s < 600 s)
  std::vector<int> pick = hb::PickTabsToSleep(tabs, 1000000, bal);
  CHECK(pick.size() == 1 && pick[0] == 5);  // only tab id 5 (index 4) qualifies
  CHECK(hb::PickTabsToSleep(tabs, 1000000, hb::MakeSleepConfig(L"off", 10, false)).empty());
  CHECK(hb::PickTabsToSleep(tabs, 500, bal).empty());  // clock went backwards: sleep nothing
  CHECK(hb::UsesBackgroundMemoryTarget(L"balanced", false) && !hb::UsesBackgroundMemoryTarget(L"off", false) &&
        hb::UsesBackgroundMemoryTarget(L"off", true));
  CHECK_EQ(hb::NextPerfMode(L"balanced"), L"memory_saver");
  CHECK_EQ(hb::NextPerfMode(L"memory_saver"), L"off");
  CHECK_EQ(hb::NextPerfMode(L"off"), L"balanced");
  CHECK_EQ(hb::PerfModeLabel(L"memory_saver"), L"Memory Saver");

  // Formatting
  CHECK_EQ(hb::FormatBytes(440ull * 1024 * 1024), L"440 MB");
  CHECK_EQ(hb::FormatBytes(1536ull * 1024 * 1024), L"1.5 GB");
  CHECK_EQ(hb::FormatBytes(512ull * 1024), L"512 KB");
  CHECK_EQ(hb::FormatRate(1.2 * 1024 * 1024), L"1.2 MB/s");
  CHECK_EQ(hb::FormatRate(300 * 1024), L"300 KB/s");
  CHECK_EQ(hb::FormatRate(0), L"0 B/s");
  CHECK_EQ(hb::FormatPercent(3.24, false), L"3%");
  CHECK_EQ(hb::FormatPercent(3.24, true), L"3.2%");
  CHECK_EQ(hb::FormatPercent(-1, true), L"n/a");
  CHECK_EQ(hb::FormatPercent(250, false), L"100%");

  // UTF-8
  CHECK_EQ(hb::FromUtf8(hb::ToUtf8(L"h\u00E9llo \u4E16\u754C")), L"h\u00E9llo \u4E16\u754C");

  if (failures == 0) std::printf("All core tests passed.\n");
  return failures == 0 ? 0 : 1;
}
