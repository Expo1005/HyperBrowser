#include "perf/PerformanceMonitor.h"

#include <iphlpapi.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <deque>
#include <set>

namespace hb {
namespace {

unsigned long long FileTimeToU64(const FILETIME& ft) {
  return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

}  // namespace

PerformanceMonitor::PerformanceMonitor() {
  QueryPerformanceFrequency(&freq_);
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  ncpu_ = std::max<DWORD>(1, si.dwNumberOfProcessors);
}

PerformanceMonitor::~PerformanceMonitor() {
  if (pdhQuery_) PdhCloseQuery(pdhQuery_);
}

void PerformanceMonitor::Sample() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  const double dt = havePrev_ ? static_cast<double>(now.QuadPart - prevQpc_.QuadPart) / static_cast<double>(freq_.QuadPart) : 0.0;

  // --- 1. process tree: our pid + all descendants (engine browser/renderer/GPU/utility processes)
  struct Entry { DWORD parent; std::wstring exe; };
  std::map<DWORD, Entry> all;
  std::multimap<DWORD, DWORD> children;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
      do {
        all[pe.th32ProcessID] = Entry{pe.th32ParentProcessID, pe.szExeFile};
        children.insert({pe.th32ParentProcessID, pe.th32ProcessID});
      } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
  }
  const DWORD self = GetCurrentProcessId();
  tree_.clear();
  std::set<DWORD> seen{self};
  std::deque<DWORD> queue{self};
  while (!queue.empty()) {
    DWORD p = queue.front();
    queue.pop_front();
    tree_.push_back(p);
    auto range = children.equal_range(p);
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second != p && seen.insert(it->second).second) queue.push_back(it->second);
    }
  }

  // --- 2. per-process CPU + memory
  procs_.clear();
  std::map<DWORD, unsigned long long> curCpu;
  double cpuSum = 0;
  unsigned long long ramSum = 0;
  for (DWORD pid : tree_) {
    ProcessStat st;
    st.pid = pid;
    auto it = all.find(pid);
    if (it != all.end()) { st.parentPid = it->second.parent; st.exe = it->second.exe; }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
      FILETIME c, e, k, u;
      if (GetProcessTimes(h, &c, &e, &k, &u)) {
        const unsigned long long total = FileTimeToU64(k) + FileTimeToU64(u);
        curCpu[pid] = total;
        auto prev = prevCpu_.find(pid);
        if (prev != prevCpu_.end() && dt > 0 && total >= prev->second) {
          st.cpuPercent = static_cast<double>(total - prev->second) / (dt * 1e7 * ncpu_) * 100.0;
          cpuSum += st.cpuPercent;
        }
      }
      PROCESS_MEMORY_COUNTERS_EX pmc{};
      pmc.cb = sizeof(pmc);
      if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        st.privateBytes = pmc.PrivateUsage;
        ramSum += pmc.PrivateUsage;
      }
      CloseHandle(h);
    }
    procs_.push_back(std::move(st));
  }
  prevCpu_ = std::move(curCpu);  // processes that exited drop out here

  last_.valid = true;
  last_.cpuPercent = std::min(100.0, cpuSum);
  last_.ramBytes = ramSum;
  last_.processCount = static_cast<unsigned>(tree_.size());
  last_.gpuPercent = SampleGpu();
  last_.netBytesPerSec = SampleNet(dt);

  prevQpc_ = now;
  havePrev_ = true;
}

// GPU use of our process tree via the Windows "GPU Engine" performance counters (Windows 10 1709+).
// Instance names look like "pid_1234_luid_0x..._phys_0_eng_0_engtype_3D". Like Task Manager we take the
// busiest engine type (3D, Copy, VideoDecode, ...) summed over our processes.
double PerformanceMonitor::SampleGpu() {
  if (!gpuInit_) {
    gpuInit_ = true;
    if (PdhOpenQueryW(nullptr, 0, &pdhQuery_) == ERROR_SUCCESS &&
        PdhAddEnglishCounterW(pdhQuery_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &pdhCounter_) == ERROR_SUCCESS) {
      gpuOk_ = true;
      PdhCollectQueryData(pdhQuery_);  // rate counters need a first sample as baseline
      return -1;
    }
    if (pdhQuery_) { PdhCloseQuery(pdhQuery_); pdhQuery_ = nullptr; }
  }
  if (!gpuOk_) return -1;
  if (PdhCollectQueryData(pdhQuery_) != ERROR_SUCCESS) return -1;

  DWORD size = 0, count = 0;
  PDH_STATUS st = PdhGetFormattedCounterArrayW(pdhCounter_, PDH_FMT_DOUBLE, &size, &count, nullptr);
  if (st != PDH_MORE_DATA || size == 0) return -1;
  std::vector<BYTE> buf(size);
  auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
  st = PdhGetFormattedCounterArrayW(pdhCounter_, PDH_FMT_DOUBLE, &size, &count, items);
  if (st != ERROR_SUCCESS) return -1;

  const std::set<DWORD> pids(tree_.begin(), tree_.end());
  std::map<std::wstring, double> perType;
  for (DWORD i = 0; i < count; ++i) {
    if (!items[i].szName) continue;
    if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
    const std::wstring name = items[i].szName;
    size_t p = name.find(L"pid_");
    size_t t = name.find(L"engtype_");
    if (p == std::wstring::npos || t == std::wstring::npos) continue;
    DWORD pid = static_cast<DWORD>(wcstoul(name.c_str() + p + 4, nullptr, 10));
    if (!pids.count(pid)) continue;
    perType[name.substr(t + 8)] += items[i].FmtValue.doubleValue;
  }
  double best = 0;
  for (const auto& kv : perType) best = std::max(best, kv.second);
  return std::min(100.0, best);
}

// SYSTEM-wide network throughput (all applications, download + upload). Per-process network accounting
// needs ETW/admin rights, so it is deliberately not attempted here (TODO).
double PerformanceMonitor::SampleNet(double dt) {
  ULONG size = 0;
  if (GetIfTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0) return -1;
  std::vector<BYTE> buf(size);
  auto* table = reinterpret_cast<MIB_IFTABLE*>(buf.data());
  if (GetIfTable(table, &size, FALSE) != NO_ERROR) return -1;
  DWORD total = 0;
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    const MIB_IFROW& r = table->table[i];
    if (r.dwType == MIB_IF_TYPE_LOOPBACK) continue;
    total += r.dwInOctets + r.dwOutOctets;  // intentional modular arithmetic
  }
  double rate = -1;
  if (havePrevNet_ && dt > 0) {
    const DWORD delta = total - prevNetTotal_;
    rate = delta > 0x40000000u ? 0.0 : static_cast<double>(delta) / dt;  // >1 GiB in one tick = interface set changed
  }
  prevNetTotal_ = total;
  havePrevNet_ = true;
  return rate;
}

}  // namespace hb
