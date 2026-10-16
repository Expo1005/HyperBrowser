// Browser engine abstraction. The UI and all application logic talk ONLY to these
// interfaces, so the engine (WebView2 today, CEF or another one later) and the
// compiler can be swapped without touching the rest of the app.
#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <string>

namespace hb {

// All callbacks are invoked on the UI thread. A view never calls them after it has been destroyed.
struct ViewEvents {
  std::function<void(const std::wstring&)> onTitleChanged;
  std::function<void(const std::wstring&)> onUrlChanged;
  std::function<void(bool loading)> onLoadingChanged;
  std::function<void(bool canGoBack, bool canGoForward)> onNavStateChanged;
  std::function<void(const std::wstring& url)> onNewWindowRequested;
  // fatal=true: the whole engine process died and every tab is dead. false: only this page's renderer.
  std::function<void(bool fatal)> onCrashed;
  // Keyboard shortcuts pressed while the web page has focus. Return true if handled.
  std::function<bool(unsigned vk, bool ctrl, bool shift, bool alt)> onShortcut;
  std::function<void(const std::wstring& message)> onError;
  // The page started/stopped playing audio (tabs playing audio are never put to sleep).
  std::function<void(bool playing)> onAudioChanged;
  // The tab actually went to sleep / woke up (sleeping is asynchronous and can be refused by the engine).
  std::function<void(bool sleeping)> onSleepChanged;
};

// Resource state of a tab. Active = visible, full resources. Background = hidden, engine is asked to
// keep memory low (page keeps running). Sleeping = hidden and suspended (timers/scripts paused).
enum class TabPower { Active, Background, Sleeping };

class IBrowserView {  // one per tab
 public:
  virtual ~IBrowserView() = default;
  virtual void Navigate(const std::wstring& url) = 0;  // safe to call before the view is ready (queued)
  virtual void GoBack() = 0;
  virtual void GoForward() = 0;
  virtual void Reload() = 0;
  virtual void Stop() = 0;
  virtual void SetBounds(const RECT& clientRect) = 0;  // in parent-window client coordinates
  virtual void SetVisible(bool visible) = 0;
  virtual void Focus() = 0;
  // Sleeping is only honoured for hidden views and may be refused by the engine; onSleepChanged reports the outcome.
  virtual void SetPower(TabPower power) = 0;
  virtual bool IsSleeping() const = 0;
  virtual bool IsPlayingAudio() const = 0;
};

struct EngineOptions {
  std::wstring appDir;      // folder containing the exe and engine DLLs
  std::wstring profileDir;  // user data folder
  bool hardwareAcceleration = true;
};

class IBrowserEngine {
 public:
  virtual ~IBrowserEngine() = default;
  // Asynchronous. done(ok, errorMessage) is called on the UI thread. errorMessage is user-presentable.
  virtual void Initialize(const EngineOptions& options, std::function<void(bool ok, std::wstring error)> done) = 0;
  // Only valid after Initialize succeeded. Views must be destroyed before the engine.
  virtual std::unique_ptr<IBrowserView> CreateView(HWND parent, ViewEvents events) = 0;
  virtual const wchar_t* Name() const = 0;
};

// Picks the backend selected at build time (see BROWSER_ENGINE in CMake).
std::unique_ptr<IBrowserEngine> CreateDefaultEngine();

}  // namespace hb
