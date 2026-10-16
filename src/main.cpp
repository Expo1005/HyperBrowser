#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include "core/AppPaths.h"
#include "core/IBrowserEngine.h"
#include "core/Settings.h"
#include "core/Url.h"
#include "core/Util.h"
#include "ui/MainWindow.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
  SetProcessDPIAware();  // system-DPI aware; per-monitor DPI is a TODO

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
    MessageBoxW(nullptr, L"COM could not be initialised.", L"HyperBrowser", MB_ICONERROR);
    return 1;
  }

  int exitCode = 0;
  {
    hb::AppPaths paths = hb::AppPaths::Detect();

    std::wstring warning;
    hb::Settings settings = hb::Settings::Load(paths.settingsFile, &warning);
    if (!hb::FileExists(paths.settingsFile)) settings.Save(paths.settingsFile);  // give the user a file to edit
    if (!warning.empty()) {
      MessageBoxW(nullptr, (warning + L"\n\nDefaults were used for the affected settings.").c_str(),
                  L"HyperBrowser - settings problem", MB_ICONWARNING);
    }

    // Optional first argument: a URL / search text to open.
    std::wstring initial;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
      if (argc > 1 && argv[1][0] != L'-') initial = hb::ResolveInput(argv[1], settings.searchUrl);
      LocalFree(argv);
    }

    hb::MainWindow window(paths, settings, hb::CreateDefaultEngine());
    if (!window.Create(hInst, nCmdShow, initial)) {
      MessageBoxW(nullptr, L"The main window could not be created.", L"HyperBrowser", MB_ICONERROR);
      exitCode = 1;
    } else {
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      exitCode = static_cast<int>(msg.wParam);
    }
  }  // window + engine destroyed here, before COM shuts down

  CoUninitialize();
  return exitCode;
}
