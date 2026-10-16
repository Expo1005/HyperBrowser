#pragma once
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "core/AppPaths.h"
#include "core/IBrowserEngine.h"
#include "core/Settings.h"
#include "perf/GamingMode.h"
#include "perf/PerformanceMonitor.h"
#include "ui/PerfPanel.h"

namespace hb {

// Top-level window: custom-drawn (GDI, double-buffered, no animations) tab strip + toolbar,
// one EDIT control for the address bar, one engine view per tab, and a slim status strip with the
// performance HUD at the bottom. Timers only run while something needs them (see UpdateTimers).
class MainWindow {
 public:
  MainWindow(const AppPaths& paths, const Settings& settings, std::unique_ptr<IBrowserEngine> engine);
  ~MainWindow();
  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  bool Create(HINSTANCE hInst, int nCmdShow, const std::wstring& initialUrl);

  // Returns true if the key combination was a browser shortcut (the action itself is posted
  // to the message queue, so it is safe to call from inside engine callbacks).
  bool HandleShortcut(unsigned vk, bool ctrl, bool shift, bool alt);

 private:
  struct Tab {
    int id = 0;
    std::unique_ptr<IBrowserView> view;
    std::wstring title = L"New Tab";
    std::wstring url;
    bool loading = false, canBack = false, canForward = false, isNewTab = false;
    bool audio = false, sleeping = false;
    ULONGLONG lastActiveMs = 0;   // when the tab was last visible
    ULONGLONG loadStartMs = 0;
    ULONGLONG lastLoadMs = 0;     // duration of the most recent completed page load
  };
  enum Cmd { CmdNone, CmdNewTab, CmdCloseTab, CmdReopenTab, CmdNextTab, CmdPrevTab, CmdSelectIndex, CmdFocusAddress,
             CmdReload, CmdBack, CmdForward, CmdHome, CmdCloseTabId, CmdToggleGaming, CmdCycleMode, CmdPanelHidden,
             CmdToggleHud };
  enum HitKind { HitNone, HitTab, HitTabClose, HitNewTab, HitBack, HitForward, HitReload, HitHome, HitMenu, HitAddress,
                 HitGaming, HitHud };
  struct Hit { HitKind kind = HitNone; int index = -1; };

  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT OnMessage(UINT, WPARAM, LPARAM);

  void OnEngineReady(bool ok, const std::wstring& error);
  void Execute(int cmd, int arg);
  void PostCmd(int cmd, int arg);

  // tabs
  Tab* FindTab(int id);
  int IndexOfTab(int id) const;
  Tab* Active();
  void AddTab(const std::wstring& url, bool activate);
  void CloseTabAt(int index);
  void ActivateTab(int index);
  std::wstring NewTabUrl() const;
  static bool IsNewTabUrl(const std::wstring& url);

  // performance
  void UpdateTimers();
  void OnPerfTick();
  void OnSleepTick();
  void SetGaming(bool on);
  void CyclePerfMode();
  void ToggleHud();
  void ApplyBackgroundPower();
  void TogglePanel();
  PerfPanel::Model BuildPanelModel() const;
  std::wstring HudText() const;
  void SaveSettings();

  // chrome
  void Layout();
  void Paint();
  Hit HitTest(POINT pt) const;
  RECT CloseRect(int tabIndex) const;
  void OnLeftClick(POINT pt);
  void ShowMenu();
  void UpdateChrome();           // title bar text, address text, repaint
  void SetAddressText(const std::wstring& text);
  void CommitAddress();
  void RevertAddress();
  void InvalidateChrome();
  void InvalidateStatus();
  int S(int v) const { return MulDiv(v, dpi_, 96); }

  AppPaths paths_;
  Settings settings_;
  std::unique_ptr<IBrowserEngine> engine_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  HINSTANCE hInst_ = nullptr;
  HWND hwnd_ = nullptr, edit_ = nullptr;
  WNDPROC oldEditProc_ = nullptr;
  HFONT uiFont_ = nullptr, symFont_ = nullptr, editFont_ = nullptr, smallFont_ = nullptr;
  HBRUSH editBrush_ = nullptr;
  int dpi_ = 96;
  bool engineReady_ = false;
  bool trackingMouse_ = false;
  std::wstring initialUrl_;

  std::vector<std::unique_ptr<Tab>> tabs_;
  std::vector<std::wstring> closedUrls_;
  int active_ = -1;
  int nextTabId_ = 1;

  // performance
  PerformanceMonitor perf_;
  PerfSample sample_;
  GamingMode gaming_;
  std::unique_ptr<PerfPanel> panel_;
  bool perfTimerOn_ = false;
  UINT sleepIntervalMs_ = 0;  // 0 = sleep timer not running

  // layout cache
  int barH_ = 0, toolH_ = 0, statusH_ = 0;
  std::vector<RECT> tabRects_;
  RECT newTabR_{}, backR_{}, fwdR_{}, reloadR_{}, homeR_{}, gamingR_{}, menuR_{}, addrR_{}, contentR_{}, statusR_{}, hudR_{};
  Hit hover_;
};

}  // namespace hb
