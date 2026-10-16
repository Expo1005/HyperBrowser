#include "ui/MainWindow.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>

#include "core/Url.h"
#include "core/Util.h"
#include "perf/PerfLogic.h"

namespace hb {
namespace {

constexpr UINT WM_APP_CMD = WM_APP + 1;   // wParam = command, lParam = argument
constexpr UINT WM_APP_OPEN = WM_APP + 2;  // lParam = heap-allocated std::wstring* (URL to open in a new tab)
constexpr int kIdAddress = 100;
constexpr UINT_PTR kTimerPerf = 1;   // 1 Hz resource sampling (only while the HUD/panel/Gaming Mode need it)
constexpr UINT_PTR kTimerSleep = 2;  // background-tab sleeping check (15 s, or 5 s in Gaming Mode)
constexpr wchar_t kClassName[] = L"HyperBrowserMainWindow";

// Dark "gaming" palette. Flat colours only: no blur, gradients or animation (cheap to draw).
constexpr COLORREF kBar = RGB(16, 17, 22);
constexpr COLORREF kTool = RGB(37, 40, 47);
constexpr COLORREF kTabHover = RGB(28, 30, 38);
constexpr COLORREF kAddr = RGB(22, 24, 30);
constexpr COLORREF kHoverBtn = RGB(58, 63, 75);
constexpr COLORREF kText = RGB(230, 232, 240);
constexpr COLORREF kDim = RGB(140, 146, 160);
constexpr COLORREF kDisabled = RGB(88, 93, 104);
constexpr COLORREF kAccent = RGB(0, 229, 160);
constexpr COLORREF kAccentDark = RGB(8, 66, 52);

constexpr int kMenuNewTab = 1, kMenuReopen = 2, kMenuSettings = 3, kMenuAbout = 4, kMenuExit = 5, kMenuHud = 6,
              kMenuGaming = 7, kMenuModeBalanced = 10, kMenuModeSaver = 11, kMenuModeOff = 12;

void FillRectColor(HDC dc, const RECT& r, COLORREF c) {
  SetDCBrushColor(dc, c);
  FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void RoundFill(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border = CLR_INVALID) {
  SetDCBrushColor(dc, fill);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
  HPEN pen = border == CLR_INVALID ? static_cast<HPEN>(GetStockObject(NULL_PEN)) : CreatePen(PS_SOLID, 1, border);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius * 2, radius * 2);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  if (border != CLR_INVALID) DeleteObject(pen);
}

bool InRect(const RECT& r, POINT p) { return PtInRect(&r, p) != 0; }

}  // namespace

MainWindow::MainWindow(const AppPaths& paths, const Settings& settings, std::unique_ptr<IBrowserEngine> engine)
    : paths_(paths), settings_(settings), engine_(std::move(engine)) {}

MainWindow::~MainWindow() {
  *alive_ = false;
  gaming_.Disable();  // always hand priorities back
  panel_.reset();
  tabs_.clear();      // views must die before the engine
  engine_.reset();
  if (uiFont_) DeleteObject(uiFont_);
  if (symFont_) DeleteObject(symFont_);
  if (editFont_) DeleteObject(editFont_);
  if (smallFont_) DeleteObject(smallFont_);
  if (editBrush_) DeleteObject(editBrush_);
}

bool MainWindow::Create(HINSTANCE hInst, int nCmdShow, const std::wstring& initialUrl) {
  hInst_ = hInst;
  initialUrl_ = initialUrl;

  HDC sdc = GetDC(nullptr);
  dpi_ = GetDeviceCaps(sdc, LOGPIXELSX);
  ReleaseDC(nullptr, sdc);
  if (dpi_ <= 0) dpi_ = 96;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &MainWindow::WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc)) return false;

  auto font = [&](int px, int weight, const wchar_t* face) {
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
  };
  uiFont_ = font(13, FW_NORMAL, L"Segoe UI");
  smallFont_ = font(12, FW_SEMIBOLD, L"Segoe UI");
  symFont_ = font(16, FW_NORMAL, L"Segoe UI Symbol");
  editFont_ = font(14, FW_NORMAL, L"Segoe UI");
  editBrush_ = CreateSolidBrush(kAddr);

  hwnd_ = CreateWindowExW(0, kClassName, L"HyperBrowser", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                          CW_USEDEFAULT, S(1280), S(800), nullptr, nullptr, hInst, this);
  if (!hwnd_) return false;

  // Dark title bar (Windows 10 20H1+ / 11). Loaded dynamically so older systems simply skip it.
  if (HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")))) {
      BOOL on = TRUE;
      fn(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &on, sizeof(on));
    }
  }

  edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAddress)), hInst, nullptr);
  SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
  SetWindowLongPtrW(edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  oldEditProc_ = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MainWindow::EditProc)));

  // Performance panel (hidden until the HUD is clicked). Callbacks only post messages.
  PerfPanel::Callbacks cb;
  cb.onToggleGaming = [this] { PostCmd(CmdToggleGaming, 0); };
  cb.onCycleMode = [this] { PostCmd(CmdCycleMode, 0); };
  cb.onHidden = [this] { PostCmd(CmdPanelHidden, 0); };
  panel_ = std::make_unique<PerfPanel>(hInst, hwnd_, dpi_, cb);
  if (!panel_->Create()) panel_.reset();  // HUD still works; only the detail popup is lost

  Layout();
  ShowWindow(hwnd_, nCmdShow);
  UpdateWindow(hwnd_);

  // Start the engine asynchronously; the window is already visible and responsive.
  EngineOptions opt;
  opt.appDir = paths_.exeDir;
  opt.profileDir = paths_.profileDir;
  opt.hardwareAcceleration = settings_.hardwareAcceleration;
  auto alive = alive_;
  engine_->Initialize(opt, [this, alive](bool ok, std::wstring err) {
    if (*alive) OnEngineReady(ok, err);
  });
  UpdateTimers();  // HUD starts sampling right away if enabled
  return true;
}

void MainWindow::OnEngineReady(bool ok, const std::wstring& error) {
  if (!ok) {
    MessageBoxW(hwnd_, error.c_str(), L"HyperBrowser - cannot start the browser engine", MB_ICONERROR);
    DestroyWindow(hwnd_);
    return;
  }
  engineReady_ = true;
  std::wstring first = initialUrl_;
  if (first.empty() && settings_.startup == L"homepage") first = settings_.homepage;
  AddTab(first, true);
  UpdateTimers();
}

// ---------------------------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------------------------
MainWindow::Tab* MainWindow::FindTab(int id) {
  for (auto& t : tabs_) if (t->id == id) return t.get();
  return nullptr;
}
int MainWindow::IndexOfTab(int id) const {
  for (size_t i = 0; i < tabs_.size(); ++i) if (tabs_[i]->id == id) return static_cast<int>(i);
  return -1;
}
MainWindow::Tab* MainWindow::Active() {
  return active_ >= 0 && active_ < static_cast<int>(tabs_.size()) ? tabs_[active_].get() : nullptr;
}

std::wstring MainWindow::NewTabUrl() const { return PathToFileUrl(paths_.resourcesDir + L"\\newtab.html"); }

bool MainWindow::IsNewTabUrl(const std::wstring& url) {
  std::wstring l = ToLower(url);
  return StartsWith(l, L"file:///") && l.find(L"/resources/newtab.html") != std::wstring::npos;
}

void MainWindow::AddTab(const std::wstring& urlIn, bool activate) {
  if (!engineReady_) return;
  auto tab = std::make_unique<Tab>();
  const int id = nextTabId_++;
  tab->id = id;
  tab->lastActiveMs = GetTickCount64();
  std::wstring url = urlIn.empty() ? NewTabUrl() : urlIn;
  tab->isNewTab = IsNewTabUrl(url);
  tab->url = tab->isNewTab ? L"" : url;

  ViewEvents ev;
  ev.onTitleChanged = [this, id](const std::wstring& t) {
    if (Tab* tb = FindTab(id)) { tb->title = t; UpdateChrome(); }
  };
  ev.onUrlChanged = [this, id](const std::wstring& u) {
    if (Tab* tb = FindTab(id)) {
      tb->isNewTab = IsNewTabUrl(u);
      tb->url = tb->isNewTab ? L"" : u;
      UpdateChrome();
    }
  };
  ev.onLoadingChanged = [this, id](bool l) {
    if (Tab* tb = FindTab(id)) {
      if (l) tb->loadStartMs = GetTickCount64();
      else if (tb->loadStartMs) { tb->lastLoadMs = GetTickCount64() - tb->loadStartMs; tb->loadStartMs = 0; }
      tb->loading = l;
      InvalidateChrome();
    }
  };
  ev.onNavStateChanged = [this, id](bool b, bool f) {
    if (Tab* tb = FindTab(id)) { tb->canBack = b; tb->canForward = f; InvalidateChrome(); }
  };
  ev.onNewWindowRequested = [this](const std::wstring& u) {
    // Posted, not executed inline: we are inside an engine callback.
    PostMessageW(hwnd_, WM_APP_OPEN, 0, reinterpret_cast<LPARAM>(new std::wstring(u)));
  };
  ev.onCrashed = [this, id](bool fatal) {
    if (fatal) {
      MessageBoxW(hwnd_,
                  L"The browser engine process stopped unexpectedly. Your open tabs cannot be recovered in this "
                  L"version.\n\nPlease restart HyperBrowser.",
                  L"HyperBrowser", MB_ICONERROR);
    } else if (Tab* tb = FindTab(id)) {
      tb->title = L"Page crashed - press F5 to reload";
      UpdateChrome();
    }
  };
  ev.onShortcut = [this](unsigned vk, bool c, bool s, bool a) { return HandleShortcut(vk, c, s, a); };
  ev.onError = [this, id](const std::wstring& msg) {
    if (Tab* tb = FindTab(id)) { tb->title = L"Error"; UpdateChrome(); }
    MessageBoxW(hwnd_, msg.c_str(), L"HyperBrowser", MB_ICONWARNING);
  };
  ev.onAudioChanged = [this, id](bool playing) {
    if (Tab* tb = FindTab(id)) { tb->audio = playing; InvalidateChrome(); }
  };
  ev.onSleepChanged = [this, id](bool sleeping) {
    if (Tab* tb = FindTab(id)) { tb->sleeping = sleeping; InvalidateChrome(); InvalidateStatus(); }
  };

  tab->view = engine_->CreateView(hwnd_, std::move(ev));
  tab->view->SetBounds(contentR_);
  tab->view->SetVisible(false);
  tab->view->Navigate(url);
  tabs_.push_back(std::move(tab));
  if (activate || active_ < 0) ActivateTab(static_cast<int>(tabs_.size()) - 1);
  else {
    Layout();
    if (UsesBackgroundMemoryTarget(settings_.performanceMode, gaming_.IsOn())) tabs_.back()->view->SetPower(TabPower::Background);
  }
}

void MainWindow::ActivateTab(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
  const ULONGLONG now = GetTickCount64();
  if (Tab* old = Active()) {
    old->view->SetVisible(false);  // must be hidden before it can be suspended
    old->lastActiveMs = now;
    if (UsesBackgroundMemoryTarget(settings_.performanceMode, gaming_.IsOn())) old->view->SetPower(TabPower::Background);
  }
  active_ = index;
  Tab* t = Active();
  t->lastActiveMs = now;
  t->view->SetPower(TabPower::Active);  // wakes a sleeping tab and restores the normal memory target
  t->view->SetBounds(contentR_);
  t->view->SetVisible(true);
  Layout();
  UpdateChrome();
  InvalidateStatus();
  if (t->isNewTab) { SetFocus(edit_); } else { t->view->Focus(); }
}

void MainWindow::CloseTabAt(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
  Tab& t = *tabs_[index];
  if (!t.isNewTab && !t.url.empty()) {
    closedUrls_.push_back(t.url);
    if (closedUrls_.size() > 20) closedUrls_.erase(closedUrls_.begin());
  }
  tabs_.erase(tabs_.begin() + index);  // destroys the view and releases its engine resources
  if (tabs_.empty()) {
    active_ = -1;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);  // closing the last tab closes the window (like Chrome)
    return;
  }
  if (active_ > index || active_ >= static_cast<int>(tabs_.size())) --active_;
  if (active_ < 0) active_ = 0;
  int target = active_;
  active_ = -1;  // force a full re-activate of the surviving tab
  ActivateTab(target);
}

// ---------------------------------------------------------------------------------------------
// Commands & shortcuts
// ---------------------------------------------------------------------------------------------
void MainWindow::PostCmd(int cmd, int arg) { PostMessageW(hwnd_, WM_APP_CMD, static_cast<WPARAM>(cmd), arg); }

bool MainWindow::HandleShortcut(unsigned vk, bool ctrl, bool shift, bool alt) {
  int cmd = CmdNone, arg = 0;
  if (ctrl && !alt) {
    switch (vk) {
      case 'T': cmd = shift ? CmdReopenTab : CmdNewTab; break;
      case 'G': if (shift) cmd = CmdToggleGaming; break;
      case 'W': case VK_F4: cmd = CmdCloseTab; break;
      case 'L': cmd = CmdFocusAddress; break;
      case 'R': cmd = CmdReload; break;
      case VK_TAB: cmd = shift ? CmdPrevTab : CmdNextTab; break;
      case VK_NEXT: cmd = CmdNextTab; break;
      case VK_PRIOR: cmd = CmdPrevTab; break;
      default:
        if (vk >= '1' && vk <= '9') { cmd = CmdSelectIndex; arg = static_cast<int>(vk - '1'); }
    }
  } else if (alt && !ctrl && !shift) {
    if (vk == VK_LEFT) cmd = CmdBack;
    else if (vk == VK_RIGHT) cmd = CmdForward;
    else if (vk == VK_HOME) cmd = CmdHome;
  } else if (!ctrl && !alt) {
    if (vk == VK_F5) cmd = CmdReload;
    else if (vk == VK_F6) cmd = CmdFocusAddress;
  }
  if (cmd == CmdNone) return false;
  PostCmd(cmd, arg);
  return true;
}

void MainWindow::Execute(int cmd, int arg) {
  Tab* t = Active();
  switch (cmd) {
    case CmdNewTab: AddTab(L"", true); break;
    case CmdCloseTab: if (active_ >= 0) CloseTabAt(active_); break;
    case CmdCloseTabId: CloseTabAt(IndexOfTab(arg)); break;
    case CmdReopenTab:
      if (!closedUrls_.empty()) {
        std::wstring u = closedUrls_.back();
        closedUrls_.pop_back();
        AddTab(u, true);
      }
      break;
    case CmdNextTab: if (!tabs_.empty()) ActivateTab((active_ + 1) % static_cast<int>(tabs_.size())); break;
    case CmdPrevTab:
      if (!tabs_.empty()) ActivateTab((active_ + static_cast<int>(tabs_.size()) - 1) % static_cast<int>(tabs_.size()));
      break;
    case CmdSelectIndex:
      if (!tabs_.empty()) ActivateTab(arg == 8 ? static_cast<int>(tabs_.size()) - 1 : std::min(arg, static_cast<int>(tabs_.size()) - 1));
      break;
    case CmdFocusAddress: SetFocus(edit_); SendMessageW(edit_, EM_SETSEL, 0, -1); break;
    case CmdReload: if (t) t->view->Reload(); break;
    case CmdBack: if (t && t->canBack) t->view->GoBack(); break;
    case CmdForward: if (t && t->canForward) t->view->GoForward(); break;
    case CmdHome: if (t) t->view->Navigate(settings_.homepage); break;
    case CmdToggleGaming: SetGaming(!gaming_.IsOn()); break;
    case CmdCycleMode: CyclePerfMode(); break;
    case CmdToggleHud: ToggleHud(); break;
    case CmdPanelHidden: UpdateTimers(); break;
  }
}

// ---------------------------------------------------------------------------------------------
// Performance: HUD, panel, tab sleeping, Gaming Mode
// ---------------------------------------------------------------------------------------------
void MainWindow::SaveSettings() {
  if (!settings_.Save(paths_.settingsFile)) {
    MessageBoxW(hwnd_, L"Settings could not be saved (is the data folder writable?). The change applies for this session only.",
                L"HyperBrowser", MB_ICONWARNING);
  }
}

// Timers exist only while needed, so an idle browser with the HUD hidden does no periodic work of its own.
void MainWindow::UpdateTimers() {
  if (!hwnd_) return;
  const bool needPerf = settings_.showHud || (panel_ && panel_->Visible()) || gaming_.IsOn();
  if (needPerf && !perfTimerOn_) {
    SetTimer(hwnd_, kTimerPerf, 1000, nullptr);
    perfTimerOn_ = true;
    OnPerfTick();
  } else if (!needPerf && perfTimerOn_) {
    KillTimer(hwnd_, kTimerPerf);
    perfTimerOn_ = false;
  }

  const SleepConfig cfg = MakeSleepConfig(settings_.performanceMode, settings_.tabSleepMinutes, gaming_.IsOn());
  const UINT want = (engineReady_ && cfg.enabled) ? (gaming_.IsOn() ? 5000u : 15000u) : 0u;
  if (want != sleepIntervalMs_) {
    if (sleepIntervalMs_) KillTimer(hwnd_, kTimerSleep);
    if (want) SetTimer(hwnd_, kTimerSleep, want, nullptr);
    sleepIntervalMs_ = want;
  }
}

void MainWindow::OnPerfTick() {
  perf_.Sample();
  sample_ = perf_.Last();
  if (gaming_.IsOn()) gaming_.Apply(perf_.TreePids());  // catches engine processes spawned since the last tick
  InvalidateStatus();
  if (panel_ && panel_->Visible()) panel_->Update(BuildPanelModel());
}

void MainWindow::OnSleepTick() {
  const SleepConfig cfg = MakeSleepConfig(settings_.performanceMode, settings_.tabSleepMinutes, gaming_.IsOn());
  if (!cfg.enabled) return;
  std::vector<TabSleepInfo> infos;
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    const Tab& t = *tabs_[i];
    TabSleepInfo s;
    s.id = t.id;
    s.isActive = i == active_;
    s.isSleeping = t.sleeping;
    s.playingAudio = t.audio;
    s.loading = t.loading;
    s.lastActiveMs = t.lastActiveMs;
    infos.push_back(s);
  }
  for (int id : PickTabsToSleep(infos, GetTickCount64(), cfg)) {
    if (Tab* t = FindTab(id)) t->view->SetPower(TabPower::Sleeping);  // engine may refuse; onSleepChanged tells us
  }
}

void MainWindow::ApplyBackgroundPower() {
  const bool bg = UsesBackgroundMemoryTarget(settings_.performanceMode, gaming_.IsOn());
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    if (i == active_) continue;
    Tab& t = *tabs_[i];
    if (bg) { if (!t.sleeping) t.view->SetPower(TabPower::Background); }
    else t.view->SetPower(TabPower::Active);  // optimisation off: wake and restore normal memory target
  }
}

void MainWindow::SetGaming(bool on) {
  if (on == gaming_.IsOn()) return;
  if (on) {
    perf_.Sample();  // refresh the process list so we know every engine process right now
    gaming_.Enable(perf_.TreePids());
  } else {
    gaming_.Disable();
  }
  ApplyBackgroundPower();
  UpdateTimers();
  if (on) OnSleepTick();  // idle tabs go to sleep right away instead of waiting for the next tick
  InvalidateChrome();
  InvalidateStatus();
  if (panel_ && panel_->Visible()) panel_->Update(BuildPanelModel());
}

void MainWindow::CyclePerfMode() {
  settings_.performanceMode = NextPerfMode(settings_.performanceMode);
  SaveSettings();
  ApplyBackgroundPower();
  UpdateTimers();
  if (panel_ && panel_->Visible()) panel_->Update(BuildPanelModel());
}

void MainWindow::ToggleHud() {
  settings_.showHud = !settings_.showHud;
  SaveSettings();
  UpdateTimers();
  Layout();
}

std::wstring MainWindow::HudText() const {
  if (!sample_.valid) return L"CPU - | RAM - | GPU -";
  return L"CPU " + FormatPercent(sample_.cpuPercent, false) + L" | RAM " + FormatBytes(sample_.ramBytes) + L" | GPU " +
         FormatPercent(sample_.gpuPercent, false);
}

PerfPanel::Model MainWindow::BuildPanelModel() const {
  PerfPanel::Model m;
  unsigned asleep = 0;
  for (const auto& t : tabs_) if (t->sleeping) ++asleep;
  const Tab* at = (active_ >= 0 && active_ < static_cast<int>(tabs_.size())) ? tabs_[active_].get() : nullptr;
  const bool v = sample_.valid;
  m.rows.push_back({L"CPU", v ? FormatPercent(sample_.cpuPercent, true) : L"-"});
  m.rows.push_back({L"RAM", v ? FormatBytes(sample_.ramBytes) : L"-"});
  m.rows.push_back({L"GPU", v ? FormatPercent(sample_.gpuPercent, true) : L"-"});
  m.rows.push_back({L"Tabs", std::to_wstring(tabs_.size()) + (asleep ? L"  (" + std::to_wstring(asleep) + L" asleep)" : L"")});
  m.rows.push_back({L"Browser processes", v ? std::to_wstring(sample_.processCount) : L"-"});
  m.rows.push_back({L"Network (whole PC)", v ? FormatRate(sample_.netBytesPerSec) : L"-"});
  m.rows.push_back({L"Last page load", (at && at->lastLoadMs) ? std::to_wstring(at->lastLoadMs) + L" ms" : L"-"});
  m.rows.push_back({L"Performance Mode", PerfModeLabel(settings_.performanceMode)});
  m.rows.push_back({L"Gaming Mode", gaming_.IsOn() ? L"ON" : L"OFF"});
  m.gamingOn = gaming_.IsOn();
  m.modeLabel = PerfModeLabel(settings_.performanceMode);
  return m;
}

void MainWindow::TogglePanel() {
  if (!panel_) return;
  if (panel_->Visible()) { panel_->Hide(); return; }
  if (panel_->HiddenRecently(300)) return;  // this very click just closed it (it lost focus)
  POINT anchor{contentR_.right - S(8), contentR_.bottom};  // bottom-right, above the status strip
  ClientToScreen(hwnd_, &anchor);
  panel_->ShowAt(anchor, BuildPanelModel());
  UpdateTimers();  // keep sampling while the panel is open, even if the HUD is hidden
}

// ---------------------------------------------------------------------------------------------
// Address bar
// ---------------------------------------------------------------------------------------------
void MainWindow::SetAddressText(const std::wstring& text) {
  int n = GetWindowTextLengthW(edit_);
  std::wstring cur(static_cast<size_t>(n) + 1, L'\0');
  GetWindowTextW(edit_, &cur[0], n + 1);
  cur.resize(static_cast<size_t>(n));
  if (cur != text) SetWindowTextW(edit_, text.c_str());
}

void MainWindow::CommitAddress() {
  Tab* t = Active();
  if (!t) return;
  int n = GetWindowTextLengthW(edit_);
  std::wstring text(static_cast<size_t>(n) + 1, L'\0');
  GetWindowTextW(edit_, &text[0], n + 1);
  text.resize(static_cast<size_t>(n));
  std::wstring target = ResolveInput(text, settings_.searchUrl);
  if (target.empty()) return;
  t->view->Navigate(target);
  t->view->Focus();
}

void MainWindow::RevertAddress() {
  if (Tab* t = Active()) { SetAddressText(t->url); t->view->Focus(); }
}

void MainWindow::UpdateChrome() {
  Tab* t = Active();
  if (t) {
    if (GetFocus() != edit_) SetAddressText(t->url);
    std::wstring title = t->title.empty() ? t->url : t->title;
    SetWindowTextW(hwnd_, (title.empty() ? L"HyperBrowser" : title + L" - HyperBrowser").c_str());
  }
  InvalidateChrome();
}

void MainWindow::InvalidateChrome() {
  if (!hwnd_) return;
  RECT r{0, 0, 32000, barH_ + toolH_};
  InvalidateRect(hwnd_, &r, FALSE);
}

void MainWindow::InvalidateStatus() {
  if (!hwnd_ || statusH_ <= 0) return;
  InvalidateRect(hwnd_, &statusR_, FALSE);
}

// ---------------------------------------------------------------------------------------------
// Layout & painting
// ---------------------------------------------------------------------------------------------
void MainWindow::Layout() {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int w = rc.right, b = S(32);
  barH_ = S(38);
  toolH_ = S(44);
  statusH_ = settings_.showHud ? S(22) : 0;

  const int n = static_cast<int>(tabs_.size());
  const int left = S(8), newW = S(32);
  const int avail = w - left - newW - S(12);
  const int tw = n ? std::min(S(220), std::max(S(44), avail / n)) : 0;
  tabRects_.assign(static_cast<size_t>(n), RECT{});
  for (int i = 0; i < n; ++i) tabRects_[i] = RECT{left + i * tw, S(6), left + (i + 1) * tw - S(2), barH_};
  newTabR_ = RECT{left + n * tw + S(4), S(8), left + n * tw + S(4) + newW, barH_ - S(4)};

  const int y = barH_ + (toolH_ - b) / 2;
  int x = S(8);
  backR_ = RECT{x, y, x + b, y + b}; x += b + S(2);
  fwdR_ = RECT{x, y, x + b, y + b}; x += b + S(2);
  reloadR_ = RECT{x, y, x + b, y + b}; x += b + S(2);
  homeR_ = RECT{x, y, x + b, y + b}; x += b + S(8);
  menuR_ = RECT{w - S(8) - b, y, w - S(8), y + b};
  const int pillW = S(150);
  gamingR_ = RECT{menuR_.left - S(6) - pillW, y + S(2), menuR_.left - S(6), y + b - S(2)};
  addrR_ = RECT{x, y, std::max(x + S(100), static_cast<int>(gamingR_.left) - S(8)), y + b};

  const int editH = S(20);
  MoveWindow(edit_, addrR_.left + S(16), y + (b - editH) / 2, std::max(10, static_cast<int>(addrR_.right - addrR_.left) - S(32)),
             editH, TRUE);

  const int contentBottom = std::max(static_cast<int>(rc.bottom) - statusH_, barH_ + toolH_);
  contentR_ = RECT{0, barH_ + toolH_, w, contentBottom};
  statusR_ = RECT{0, contentBottom, w, contentBottom + statusH_};
  for (auto& t : tabs_) t->view->SetBounds(contentR_);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT MainWindow::CloseRect(int i) const {
  const RECT& r = tabRects_[i];
  const int s = S(18);
  return RECT{r.right - s - S(6), r.top + (r.bottom - r.top - s) / 2, r.right - S(6), r.top + (r.bottom - r.top - s) / 2 + s};
}

MainWindow::Hit MainWindow::HitTest(POINT p) const {
  Hit h;
  for (int i = 0; i < static_cast<int>(tabRects_.size()); ++i) {
    if (InRect(tabRects_[i], p)) {
      h.kind = InRect(CloseRect(i), p) ? HitTabClose : HitTab;
      h.index = i;
      return h;
    }
  }
  if (InRect(newTabR_, p)) h.kind = HitNewTab;
  else if (InRect(backR_, p)) h.kind = HitBack;
  else if (InRect(fwdR_, p)) h.kind = HitForward;
  else if (InRect(reloadR_, p)) h.kind = HitReload;
  else if (InRect(homeR_, p)) h.kind = HitHome;
  else if (InRect(gamingR_, p)) h.kind = HitGaming;
  else if (InRect(menuR_, p)) h.kind = HitMenu;
  else if (InRect(addrR_, p)) h.kind = HitAddress;
  else if (statusH_ > 0 && InRect(hudR_, p)) h.kind = HitHud;
  return h;
}

void MainWindow::Paint() {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd_, &ps);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int w = rc.right, chromeH = barH_ + toolH_;

  HDC dc = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(w, 1), std::max(chromeH, 1));
  HGDIOBJ oldBmp = SelectObject(dc, bmp);
  SetBkMode(dc, TRANSPARENT);

  FillRectColor(dc, RECT{0, 0, w, barH_}, kBar);

  // --- tabs
  SelectObject(dc, uiFont_);
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    const RECT& r = tabRects_[i];
    const bool isActive = i == active_;
    const bool hov = hover_.index == i && (hover_.kind == HitTab || hover_.kind == HitTabClose);
    if (isActive || hov) {
      RECT shape{r.left, r.top, r.right, r.bottom + S(10)};  // overflow is covered by the toolbar fill
      RoundFill(dc, shape, S(8), isActive ? kTool : kTabHover);
    }
    if (isActive) FillRectColor(dc, RECT{r.left + S(12), r.top, r.right - S(12), r.top + S(2)}, kAccent);

    const Tab& t = *tabs_[i];
    RECT tr{r.left + S(12), r.top, r.right - S(30), r.bottom};
    if (t.loading) {
      RECT dot{r.left + S(10), r.top + (r.bottom - r.top) / 2 - S(3), r.left + S(16), r.top + (r.bottom - r.top) / 2 + S(3)};
      RoundFill(dc, dot, S(3), kAccent);
      tr.left += S(12);
    }
    // Sleeping tabs are drawn dimmed so the user can see what the browser has put to sleep.
    SetTextColor(dc, t.sleeping ? kDisabled : (isActive ? kText : kDim));
    std::wstring title = t.title.empty() ? t.url : t.title;
    if (t.audio) title = L"♪ " + title;
    DrawTextW(dc, title.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (isActive || hov) {
      RECT cr = CloseRect(i);
      if (hover_.kind == HitTabClose && hover_.index == i) RoundFill(dc, cr, S(6), kHoverBtn);
      SelectObject(dc, symFont_);
      SetTextColor(dc, kText);
      DrawTextW(dc, L"×", -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
      SelectObject(dc, uiFont_);
    }
  }
  SelectObject(dc, symFont_);
  if (hover_.kind == HitNewTab) RoundFill(dc, newTabR_, S(8), kHoverBtn);
  SetTextColor(dc, kText);
  DrawTextW(dc, L"+", -1, &newTabR_, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

  // --- toolbar
  FillRectColor(dc, RECT{0, barH_, w, chromeH}, kTool);
  const Tab* at = (active_ >= 0 && active_ < static_cast<int>(tabs_.size())) ? tabs_[active_].get() : nullptr;
  struct Btn { RECT r; const wchar_t* glyph; bool enabled; HitKind kind; };
  const Btn btns[] = {
      {backR_, L"←", at && at->canBack, HitBack},
      {fwdR_, L"→", at && at->canForward, HitForward},
      {reloadR_, (at && at->loading) ? L"✕" : L"↻", at != nullptr, HitReload},
      {homeR_, L"⌂", at != nullptr, HitHome},
      {menuR_, L"⋮", true, HitMenu},
  };
  for (const Btn& bt : btns) {
    if (bt.enabled && hover_.kind == bt.kind) RoundFill(dc, bt.r, S(8), kHoverBtn);
    SetTextColor(dc, bt.enabled ? kText : kDisabled);
    RECT r = bt.r;
    DrawTextW(dc, bt.glyph, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
  }

  const bool focused = GetFocus() == edit_;
  RoundFill(dc, addrR_, S(16), kAddr, focused ? kAccent : kAddr);

  // Gaming Mode pill
  const bool g = gaming_.IsOn();
  RoundFill(dc, gamingR_, S(14), g ? kAccentDark : (hover_.kind == HitGaming ? kHoverBtn : kAddr), g ? kAccent : CLR_INVALID);
  SelectObject(dc, smallFont_);
  SetTextColor(dc, g ? kAccent : kDim);
  RECT gt = gamingR_;
  DrawTextW(dc, g ? L"Gaming Mode: ON" : L"Gaming Mode: OFF", -1, &gt, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

  BitBlt(hdc, 0, 0, w, chromeH, dc, 0, 0, SRCCOPY);
  SelectObject(dc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(dc);

  // --- status strip with the performance HUD (bottom-right). Drawn directly: tiny and cheap.
  if (statusH_ > 0) {
    FillRectColor(hdc, statusR_, kBar);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, smallFont_);
    const std::wstring hud = HudText();
    RECT calc{0, 0, 0, 0};
    DrawTextW(hdc, hud.c_str(), -1, &calc, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    const int tw = calc.right - calc.left;
    hudR_ = RECT{w - S(8) - tw - S(10), statusR_.top, w, statusR_.bottom};
    if (hover_.kind == HitHud) RoundFill(hdc, RECT{hudR_.left, hudR_.top + S(2), hudR_.right - S(4), hudR_.bottom - S(2)}, S(6), kHoverBtn);
    RECT tr{hudR_.left + S(10), hudR_.top, hudR_.right - S(8), hudR_.bottom};
    SetTextColor(hdc, kText);
    DrawTextW(hdc, hud.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

    // left side: state the user should always be able to see
    unsigned asleep = 0;
    for (const auto& t : tabs_) if (t->sleeping) ++asleep;
    std::wstring left;
    if (gaming_.IsOn()) left = L"Gaming Mode: ON";
    if (asleep) left += (left.empty() ? L"" : L"   |   ") + std::to_wstring(asleep) + (asleep == 1 ? L" tab asleep" : L" tabs asleep");
    if (!left.empty()) {
      RECT lr{S(12), statusR_.top, hudR_.left - S(8), statusR_.bottom};
      SetTextColor(hdc, gaming_.IsOn() ? kAccent : kDim);
      DrawTextW(hdc, left.c_str(), -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
  }

  // --- content area: only visible while the engine is starting or when no tab exists
  if (!engineReady_ || tabs_.empty()) {
    RECT content{0, chromeH, w, contentR_.bottom};
    FillRectColor(hdc, content, kBar);
    SelectObject(hdc, uiFont_);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kDim);
    DrawTextW(hdc, engineReady_ ? L"No open tabs" : L"Starting browser engine…", -1, &content,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
  }
  EndPaint(hwnd_, &ps);
}

void MainWindow::OnLeftClick(POINT pt) {
  Hit h = HitTest(pt);
  Tab* t = Active();
  switch (h.kind) {
    case HitTab: ActivateTab(h.index); break;
    case HitTabClose: PostCmd(CmdCloseTabId, tabs_[h.index]->id); break;
    case HitNewTab: PostCmd(CmdNewTab, 0); break;
    case HitBack: PostCmd(CmdBack, 0); break;
    case HitForward: PostCmd(CmdForward, 0); break;
    case HitReload:
      if (t && t->loading) t->view->Stop(); else PostCmd(CmdReload, 0);
      break;
    case HitHome: PostCmd(CmdHome, 0); break;
    case HitGaming: PostCmd(CmdToggleGaming, 0); break;
    case HitMenu: ShowMenu(); break;
    case HitAddress: SetFocus(edit_); break;
    case HitHud: TogglePanel(); break;
    default: break;
  }
}

void MainWindow::ShowMenu() {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, kMenuNewTab, L"New tab\tCtrl+T");
  AppendMenuW(m, MF_STRING | (closedUrls_.empty() ? MF_GRAYED : 0), kMenuReopen, L"Reopen closed tab\tCtrl+Shift+T");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (gaming_.IsOn() ? MF_CHECKED : 0), kMenuGaming, L"Gaming Mode\tCtrl+Shift+G");
  AppendMenuW(m, MF_STRING | (settings_.showHud ? MF_CHECKED : 0), kMenuHud, L"Show performance HUD");
  HMENU sub = CreatePopupMenu();
  const std::wstring& pm = settings_.performanceMode;
  AppendMenuW(sub, MF_STRING | (pm == L"balanced" ? MF_CHECKED : 0), kMenuModeBalanced, L"Balanced");
  AppendMenuW(sub, MF_STRING | (pm == L"memory_saver" ? MF_CHECKED : 0), kMenuModeSaver, L"Memory Saver");
  AppendMenuW(sub, MF_STRING | (pm == L"off" ? MF_CHECKED : 0), kMenuModeOff, L"Off (no tab sleeping)");
  AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"Performance mode");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, kMenuSettings, L"Edit settings file…");  // TODO: full settings page (stage 3)
  AppendMenuW(m, MF_STRING, kMenuAbout, L"About HyperBrowser");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, kMenuExit, L"Exit");
  POINT p{menuR_.right, menuR_.bottom};
  ClientToScreen(hwnd_, &p);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd_, nullptr);
  DestroyMenu(m);  // also destroys the submenu
  auto setMode = [this](const wchar_t* mode) {
    settings_.performanceMode = mode;
    SaveSettings();
    ApplyBackgroundPower();
    UpdateTimers();
  };
  switch (cmd) {
    case kMenuNewTab: PostCmd(CmdNewTab, 0); break;
    case kMenuReopen: PostCmd(CmdReopenTab, 0); break;
    case kMenuGaming: PostCmd(CmdToggleGaming, 0); break;
    case kMenuHud: PostCmd(CmdToggleHud, 0); break;
    case kMenuModeBalanced: setMode(L"balanced"); break;
    case kMenuModeSaver: setMode(L"memory_saver"); break;
    case kMenuModeOff: setMode(L"off"); break;
    case kMenuSettings: {
      std::wstring args = L"\"" + paths_.settingsFile + L"\"";
      ShellExecuteW(hwnd_, L"open", L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
      break;
    }
    case kMenuAbout: {
      std::wstring msg = L"HyperBrowser " + FromUtf8(HB_VERSION) + L"\nEngine: " + engine_->Name() +
                         L"\nProfile: " + paths_.profileDir + (paths_.portable ? L"\n(portable mode)" : L"");
      MessageBoxW(hwnd_, msg.c_str(), L"About", MB_ICONINFORMATION);
      break;
    }
    case kMenuExit: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
  }
}

// ---------------------------------------------------------------------------------------------
// Window procedures
// ---------------------------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  MainWindow* self;
  if (m == WM_NCCREATE) {
    self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    self->hwnd_ = h;
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  return self ? self->OnMessage(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT MainWindow::OnMessage(UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(); return 0;
    case WM_SIZE:
      if (w != SIZE_MINIMIZED && edit_) Layout();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(l);
      mm->ptMinTrackSize.x = S(560);
      mm->ptMinTrackSize.y = S(340);
      return 0;
    }
    case WM_SETFOCUS:
      if (Tab* t = Active()) t->view->Focus();
      return 0;
    case WM_TIMER:
      if (w == kTimerPerf) OnPerfTick();
      else if (w == kTimerSleep) OnSleepTick();
      return 0;
    case WM_COMMAND:
      if (LOWORD(w) == kIdAddress && (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS)) InvalidateChrome();
      return 0;
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(w);
      SetTextColor(dc, kText);
      SetBkColor(dc, kAddr);
      return reinterpret_cast<LRESULT>(editBrush_);
    }
    case WM_MOUSEMOVE: {
      POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      Hit h = HitTest(pt);
      if (h.kind != hover_.kind || h.index != hover_.index) {
        hover_ = h;
        InvalidateChrome();
        InvalidateStatus();
      }
      if (!trackingMouse_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        trackingMouse_ = TrackMouseEvent(&tme) != 0;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      trackingMouse_ = false;
      hover_ = Hit{};
      InvalidateChrome();
      InvalidateStatus();
      return 0;
    case WM_LBUTTONDOWN: OnLeftClick(POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}); return 0;
    case WM_MBUTTONUP: {  // middle-click closes a tab
      Hit h = HitTest(POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)});
      if ((h.kind == HitTab || h.kind == HitTabClose) && h.index >= 0) PostCmd(CmdCloseTabId, tabs_[h.index]->id);
      return 0;
    }
    case WM_APP_CMD: Execute(static_cast<int>(w), static_cast<int>(l)); return 0;
    case WM_APP_OPEN: {
      std::unique_ptr<std::wstring> url(reinterpret_cast<std::wstring*>(l));
      if (url) AddTab(*url, true);
      return 0;
    }
    case WM_CLOSE:
      *alive_ = false;
      KillTimer(hwnd_, kTimerPerf);
      KillTimer(hwnd_, kTimerSleep);
      perfTimerOn_ = false;
      sleepIntervalMs_ = 0;
      gaming_.Disable();  // restore priorities before anything else
      if (panel_) panel_->Hide();
      tabs_.clear();      // release every tab's engine resources before the window goes away
      DestroyWindow(hwnd_);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, m, w, l);
}

LRESULT CALLBACK MainWindow::EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (!self) return DefWindowProcW(h, m, w, l);
  switch (m) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (w == VK_RETURN) { self->CommitAddress(); return 0; }
      if (w == VK_ESCAPE) { self->RevertAddress(); return 0; }
      if (self->HandleShortcut(static_cast<unsigned>(w), GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0,
                               GetKeyState(VK_MENU) < 0))
        return 0;
      break;
    case WM_CHAR:
      if (w == VK_RETURN || w == VK_ESCAPE) return 0;  // no "ding"
      break;
    case WM_SETFOCUS: {
      LRESULT r = CallWindowProcW(self->oldEditProc_, h, m, w, l);
      SendMessageW(h, EM_SETSEL, 0, -1);
      return r;
    }
  }
  return CallWindowProcW(self->oldEditProc_, h, m, w, l);
}

}  // namespace hb
