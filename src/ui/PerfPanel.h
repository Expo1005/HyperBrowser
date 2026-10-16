// Small popup window with the detailed performance numbers, opened by clicking the HUD.
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace hb {

class PerfPanel {
 public:
  struct Row { std::wstring label, value; };
  struct Model {
    std::vector<Row> rows;
    bool gamingOn = false;
    std::wstring modeLabel;
  };
  struct Callbacks {
    std::function<void()> onToggleGaming, onCycleMode, onHidden;
  };

  PerfPanel(HINSTANCE inst, HWND owner, int dpi, Callbacks cb);
  ~PerfPanel();
  PerfPanel(const PerfPanel&) = delete;
  PerfPanel& operator=(const PerfPanel&) = delete;

  bool Create();
  void ShowAt(POINT anchorBottomRightScreen, const Model& model);
  void Hide();
  bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
  // True if the panel closed within the last `ms` milliseconds (stops the click that dismissed it from reopening it).
  bool HiddenRecently(ULONGLONG ms) const { return GetTickCount64() - lastHiddenTick_ < ms; }
  void Update(const Model& model);

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT OnMessage(UINT, WPARAM, LPARAM);
  void Paint();
  int S(int v) const { return MulDiv(v, dpi_, 96); }
  int Height() const;

  HINSTANCE inst_;
  HWND owner_, hwnd_ = nullptr;
  int dpi_;
  Callbacks cb_;
  Model model_;
  HFONT font_ = nullptr, bigFont_ = nullptr, titleFont_ = nullptr;
  RECT gamingBtn_{}, modeBtn_{};
  int hoverBtn_ = 0;  // 1 gaming, 2 mode
  ULONGLONG lastHiddenTick_ = 0;
};

}  // namespace hb
