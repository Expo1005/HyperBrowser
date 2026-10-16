#include "perf/GamingMode.h"

namespace hb {
namespace {

constexpr int kProcessPowerThrottling = 4;  // PROCESS_INFORMATION_CLASS::ProcessPowerThrottling
constexpr ULONG kThrottleVersion = 1;
constexpr ULONG kThrottleExecutionSpeed = 1;

using SetProcInfoFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
using GetProcInfoFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);

// Loaded dynamically: these exist on Windows 8+/10 1709+, and older MinGW headers don't declare them.
FARPROC Kernel32Proc(const char* name) {
  return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
}

int Rank(DWORD pc) {
  switch (pc) {
    case IDLE_PRIORITY_CLASS: return 0;
    case BELOW_NORMAL_PRIORITY_CLASS: return 1;
    case NORMAL_PRIORITY_CLASS: return 2;
    case ABOVE_NORMAL_PRIORITY_CLASS: return 3;
    case HIGH_PRIORITY_CLASS: return 4;
    case REALTIME_PRIORITY_CLASS: return 5;
    default: return 2;
  }
}

unsigned long long CreationKey(HANDLE h) {
  FILETIME c, e, k, u;
  if (!GetProcessTimes(h, &c, &e, &k, &u)) return 0;
  return (static_cast<unsigned long long>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}

}  // namespace

void GamingMode::Enable(const std::vector<DWORD>& pids) {
  on_ = true;
  Apply(pids);
}

void GamingMode::Apply(const std::vector<DWORD>& pids) {
  if (!on_) return;
  auto setInfo = reinterpret_cast<SetProcInfoFn>(reinterpret_cast<void*>(Kernel32Proc("SetProcessInformation")));
  auto getInfo = reinterpret_cast<GetProcInfoFn>(reinterpret_cast<void*>(Kernel32Proc("GetProcessInformation")));

  for (DWORD pid : pids) {
    if (saved_.count(pid)) continue;
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) continue;  // not ours to touch (or already gone): skip silently

    Saved s;
    s.creationKey = CreationKey(h);
    s.priorityClass = GetPriorityClass(h);
    if (s.creationKey == 0 || s.priorityClass == 0) { CloseHandle(h); continue; }
    if (getInfo) {
      Throttle t{kThrottleVersion, 0, 0};
      if (getInfo(h, kProcessPowerThrottling, &t, sizeof(t))) { s.throttle = t; s.haveThrottle = true; }
    }
    saved_[pid] = s;

    // Never raise anything: the engine already lowers some of its own processes.
    if (Rank(s.priorityClass) > Rank(BELOW_NORMAL_PRIORITY_CLASS)) SetPriorityClass(h, BELOW_NORMAL_PRIORITY_CLASS);
    if (setInfo) {
      Throttle on{kThrottleVersion, kThrottleExecutionSpeed, kThrottleExecutionSpeed};  // EcoQoS on
      setInfo(h, kProcessPowerThrottling, &on, sizeof(on));
    }
    CloseHandle(h);
  }
}

void GamingMode::Disable() {
  if (!on_ && saved_.empty()) return;
  auto setInfo = reinterpret_cast<SetProcInfoFn>(reinterpret_cast<void*>(Kernel32Proc("SetProcessInformation")));
  for (const auto& kv : saved_) {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, kv.first);
    if (!h) continue;
    if (CreationKey(h) == kv.second.creationKey) {  // same process, not a recycled PID
      SetPriorityClass(h, kv.second.priorityClass);
      if (setInfo) {
        Throttle restore = kv.second.haveThrottle ? kv.second.throttle : Throttle{kThrottleVersion, 0, 0};
        setInfo(h, kProcessPowerThrottling, &restore, sizeof(restore));
      }
    }
    CloseHandle(h);
  }
  saved_.clear();
  on_ = false;
}

}  // namespace hb
