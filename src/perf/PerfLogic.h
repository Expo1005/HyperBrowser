// Platform-independent performance logic (unit-tested on any OS): tab-sleep policy,
// performance modes and number formatting for the HUD/panel.
#pragma once
#include <string>
#include <vector>

namespace hb {

struct TabSleepInfo {
  int id = 0;
  bool isActive = false;
  bool isSleeping = false;
  bool playingAudio = false;
  bool loading = false;
  unsigned long long lastActiveMs = 0;
};

struct SleepConfig {
  bool enabled = false;
  unsigned long long idleMs = 0;
};

// mode: "balanced" | "memory_saver" | "off". Gaming Mode overrides with a short idle time
// (still never sleeps audible/loading/active tabs).
SleepConfig MakeSleepConfig(const std::wstring& mode, int tabSleepMinutes, bool gamingOn);

// Which tabs should be put to sleep now. A tab is NEVER chosen if it is active, already asleep,
// playing audio, or still loading - so music, calls and downloads-in-progress pages are not broken.
std::vector<int> PickTabsToSleep(const std::vector<TabSleepInfo>& tabs, unsigned long long nowMs,
                                 const SleepConfig& cfg);

// Hidden tabs get the engine's low-memory target unless the user turned optimisation off.
bool UsesBackgroundMemoryTarget(const std::wstring& mode, bool gamingOn);

std::wstring NextPerfMode(const std::wstring& mode);   // balanced -> memory_saver -> off -> balanced
std::wstring PerfModeLabel(const std::wstring& mode);  // "Balanced" / "Memory Saver" / "Off"

std::wstring FormatBytes(unsigned long long bytes);        // "420 MB", "1.2 GB"
std::wstring FormatRate(double bytesPerSecond);            // "1.2 MB/s"
std::wstring FormatPercent(double pct, bool oneDecimal);   // "3%" / "3.2%"; negative -> "n/a"

}  // namespace hb
