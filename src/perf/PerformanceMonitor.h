// Samples resource usage of the browser's own process tree (our exe + every engine child process).
// No threads: Sample() is called from a UI timer (1 Hz) and only while something needs the numbers.
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <pdh.h>

#include <map>
#include <string>
#include <vector>

namespace hb {

struct ProcessStat {
  DWORD pid = 0;
  DWORD parentPid = 0;
  std::wstring exe;
  double cpuPercent = 0;               // share of the whole machine (like Task Manager)
  unsigned long long privateBytes = 0;
};

struct PerfSample {
  bool valid = false;
  double cpuPercent = 0;               // whole browser process tree, share of the whole machine
  unsigned long long ramBytes = 0;     // sum of private bytes of the tree (approximation of "RAM")
  double gpuPercent = -1;              // -1 = unavailable (old Windows / counters disabled)
  unsigned processCount = 0;
  double netBytesPerSec = -1;          // SYSTEM-wide (all apps), -1 = unavailable
};

class PerformanceMonitor {
 public:
  PerformanceMonitor();
  ~PerformanceMonitor();
  PerformanceMonitor(const PerformanceMonitor&) = delete;
  PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

  void Sample();
  const PerfSample& Last() const { return last_; }
  const std::vector<DWORD>& TreePids() const { return tree_; }        // includes our own pid
  const std::vector<ProcessStat>& Processes() const { return procs_; }  // for the future task manager

 private:
  double SampleGpu();
  double SampleNet(double dtSeconds);

  PerfSample last_;
  std::vector<DWORD> tree_;
  std::vector<ProcessStat> procs_;
  std::map<DWORD, unsigned long long> prevCpu_;  // per-pid CPU time (100 ns units) at the previous sample
  LARGE_INTEGER freq_{}, prevQpc_{};
  bool havePrev_ = false;
  unsigned ncpu_ = 1;

  PDH_HQUERY pdhQuery_ = nullptr;
  PDH_HCOUNTER pdhCounter_ = nullptr;
  bool gpuInit_ = false, gpuOk_ = false;

  DWORD prevNetTotal_ = 0;  // wraps at 2^32; deltas are computed with unsigned wrap-around arithmetic
  bool havePrevNet_ = false;
};

}  // namespace hb
