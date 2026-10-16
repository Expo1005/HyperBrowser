// Gaming Mode: lowers the CPU priority and enables Windows "efficiency mode" (EcoQoS) for the
// browser's OWN processes only, so a running game gets the CPU first. It never touches any other
// process, service, driver, registry key or Windows setting, and restores everything it changed.
#pragma once
#include <windows.h>

#include <map>
#include <vector>

namespace hb {

class GamingMode {
 public:
  ~GamingMode() { Disable(); }
  bool IsOn() const { return on_; }
  void Enable(const std::vector<DWORD>& ourPids);
  void Apply(const std::vector<DWORD>& ourPids);  // call periodically: the engine spawns new processes
  void Disable();                                  // puts every process back exactly as it was

 private:
  struct Throttle { ULONG version; ULONG controlMask; ULONG stateMask; };
  struct Saved {
    DWORD priorityClass = 0;
    unsigned long long creationKey = 0;  // guards against PID reuse: we only restore the same process
    Throttle throttle{};
    bool haveThrottle = false;
  };
  std::map<DWORD, Saved> saved_;
  bool on_ = false;
};

}  // namespace hb
