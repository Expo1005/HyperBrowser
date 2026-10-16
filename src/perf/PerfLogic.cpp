#include "perf/PerfLogic.h"

#include <algorithm>
#include <cstdio>

#include "core/Util.h"

namespace hb {

SleepConfig MakeSleepConfig(const std::wstring& mode, int minutes, bool gamingOn) {
  SleepConfig c;
  if (gamingOn) {
    c.enabled = true;
    c.idleMs = 30ull * 1000ull;
    return c;
  }
  minutes = std::max(1, std::min(240, minutes));
  if (mode == L"balanced") {
    c.enabled = true;
    c.idleMs = static_cast<unsigned long long>(minutes) * 60000ull;
  } else if (mode == L"memory_saver") {
    c.enabled = true;
    c.idleMs = static_cast<unsigned long long>(std::max(1, minutes / 3)) * 60000ull;
  }
  return c;
}

std::vector<int> PickTabsToSleep(const std::vector<TabSleepInfo>& tabs, unsigned long long nowMs,
                                 const SleepConfig& cfg) {
  std::vector<int> out;
  if (!cfg.enabled) return out;
  for (const auto& t : tabs) {
    if (t.isActive || t.isSleeping || t.playingAudio || t.loading) continue;
    if (nowMs < t.lastActiveMs) continue;  // clock oddity: never sleep on bad data
    if (nowMs - t.lastActiveMs >= cfg.idleMs) out.push_back(t.id);
  }
  return out;
}

bool UsesBackgroundMemoryTarget(const std::wstring& mode, bool gamingOn) { return gamingOn || mode != L"off"; }

std::wstring NextPerfMode(const std::wstring& mode) {
  if (mode == L"balanced") return L"memory_saver";
  if (mode == L"memory_saver") return L"off";
  return L"balanced";
}

std::wstring PerfModeLabel(const std::wstring& mode) {
  if (mode == L"memory_saver") return L"Memory Saver";
  if (mode == L"off") return L"Off";
  return L"Balanced";
}

namespace {
std::wstring Fmt(const char* f, double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), f, v);
  return FromUtf8(buf);
}
}  // namespace

std::wstring FormatBytes(unsigned long long bytes) {
  const double mb = 1024.0 * 1024.0;
  if (bytes >= 1024ull * 1024ull * 1024ull) return Fmt("%.1f GB", static_cast<double>(bytes) / (mb * 1024.0));
  if (bytes >= 1024ull * 1024ull) return Fmt("%.0f MB", static_cast<double>(bytes) / mb);
  return Fmt("%.0f KB", static_cast<double>(bytes) / 1024.0);
}

std::wstring FormatRate(double r) {
  if (r < 0) return L"n/a";
  if (r < 1024.0) return Fmt("%.0f B/s", r);
  if (r < 1024.0 * 1024.0) return Fmt("%.0f KB/s", r / 1024.0);
  return Fmt("%.1f MB/s", r / (1024.0 * 1024.0));
}

std::wstring FormatPercent(double pct, bool oneDecimal) {
  if (pct < 0) return L"n/a";
  if (pct > 100) pct = 100;
  return Fmt(oneDecimal ? "%.1f%%" : "%.0f%%", pct);
}

}  // namespace hb
