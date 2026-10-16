#include "core/AppPaths.h"

#include <windows.h>

#include "core/Util.h"

namespace hb {

AppPaths AppPaths::Detect() {
  AppPaths p;
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  std::wstring exe(buf, n);
  size_t slash = exe.find_last_of(L"\\/");
  p.exeDir = slash == std::wstring::npos ? L"." : exe.substr(0, slash);

  p.portable = FileExists(p.exeDir + L"\\portable.flag");
  std::wstring local = GetEnvVar(L"LOCALAPPDATA");
  if (p.portable || local.empty()) p.dataDir = p.exeDir + L"\\data";
  else p.dataDir = local + L"\\HyperBrowser";

  p.profileDir = p.dataDir + L"\\Profile";
  p.settingsFile = p.dataDir + L"\\settings.ini";
  p.resourcesDir = p.exeDir + L"\\resources";
  EnsureDir(p.dataDir);
  EnsureDir(p.profileDir);
  return p;
}

}  // namespace hb
