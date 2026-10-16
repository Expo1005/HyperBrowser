#include "ui/PerfPanel.h"

#include <windowsx.h>

#include <algorithm>

namespace hb {
namespace {
constexpr wchar_t kPanelClass[] = L"HyperBrowserPerfPanel";
constexpr COLORREF kBg = RGB(30, 33, 40);
constexpr COLORREF kBorder = RGB(0, 229, 160);
constexpr COLORREF kText = RGB(230, 232, 240);
constexpr COLORREF kDim = RGB(140, 146, 160);
constexpr COLORREF kBtn = RGB(48, 53, 64);
constexpr COLORREF kBtnHover = RGB(66, 73, 88);
constexpr COLORREF kAccent = RGB(0, 229, 160);

void Fill(HDC dc, const RECT& r, COLORREF c) {
  SetDCBrushColor(dc, c);
  FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void RoundFill(HDC dc, const RECT& r, int radius, COLORREF c) {
  SetDCBrushColor(dc, c);
  HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
  HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius * 2, radius * 2);
  SelectObject(dc, op);
  SelectObject(dc, ob);
}
}  // namespace

PerfPanel::PerfPanel(HINSTANCE inst, HWND owner, int dpi, Callbacks cb)
    : inst_(inst), owner_(owner), dpi_(dpi), cb_(std::move(cb)) {}

PerfPanel::~PerfPanel() {
  if (hwnd_) DestroyWindow(hwnd_);
  if (font_) DeleteObject(font_);
  if (bigFont_) DeleteObject(bigFont_);
  if (titleFont_) DeleteObject(titleFont_);
}

bool PerfPanel::Create() {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &PerfPanel::WndProc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPanelClass;
    if (!RegisterClassExW(&wc)) return false;
    registered = true;
  }
  font_ = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  bigFont_ = CreateFontW(-S(14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  titleFont_ = CreateFontW(-S(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kPanelClass, L"Browser Performance", WS_POPUP, 0, 0, S(300), S(300), owner_,
                          nullptr, inst_, this);
  return hwnd_ != nullptr;
}

int PerfPanel::Height() const {
  return S(46) + static_cast<int>(model_.rows.size()) * S(26) + S(12) + 2 * S(34) + S(18);
}

void PerfPanel::ShowAt(POINT anchor, const Model& model) {
  if (!hwnd_) return;
  model_ = model;
  const int w = S(300), h = Height();
  SetWindowPos(hwnd_, HWND_TOP, anchor.x - w, anchor.y - h, w, h, SWP_NOACTIVATE);
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);  // so WM_ACTIVATE(inactive) fires when the user clicks elsewhere
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PerfPanel::Hide() {
  if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
  ShowWindow(hwnd_, SW_HIDE);
  lastHiddenTick_ = GetTickCount64();
  if (cb_.onHidden) cb_.onHidden();
}

void PerfPanel::Update(const Model& model) {
  model_ = model;
  if (Visible()) InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK PerfPanel::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  PerfPanel* self;
  if (m == WM_NCCREATE) {
    self = static_cast<PerfPanel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = h;
  } else {
    self = reinterpret_cast<PerfPanel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  return self ? self->OnMessage(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT PerfPanel::OnMessage(UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(); return 0;
    case WM_ACTIVATE:
      if (LOWORD(w) == WA_INACTIVE) Hide();
      return 0;
    case WM_KEYDOWN:
      if (w == VK_ESCAPE) Hide();
      return 0;
    case WM_MOUSEMOVE: {
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      int which = PtInRect(&gamingBtn_, p) ? 1 : (PtInRect(&modeBtn_, p) ? 2 : 0);
      if (which != hoverBtn_) { hoverBtn_ = which; InvalidateRect(hwnd_, nullptr, FALSE); }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      if (PtInRect(&gamingBtn_, p) && cb_.onToggleGaming) cb_.onToggleGaming();
      else if (PtInRect(&modeBtn_, p) && cb_.onCycleMode) cb_.onCycleMode();
      return 0;
    }
  }
  return DefWindowProcW(hwnd_, m, w, l);
}

void PerfPanel::Paint() {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd_, &ps);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  HDC dc = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
  HGDIOBJ old = SelectObject(dc, bmp);
  SetBkMode(dc, TRANSPARENT);

  Fill(dc, rc, kBorder);
  RECT inner{1, 1, rc.right - 1, rc.bottom - 1};
  Fill(dc, inner, kBg);

  SelectObject(dc, titleFont_);
  SetTextColor(dc, kText);
  RECT title{S(16), S(10), rc.right - S(16), S(36)};
  DrawTextW(dc, L"Browser Performance", -1, &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

  int y = S(46);
  for (const Row& r : model_.rows) {
    RECT lr{S(16), y, S(150), y + S(26)};
    RECT vr{S(150), y, rc.right - S(16), y + S(26)};
    SelectObject(dc, font_);
    SetTextColor(dc, kDim);
    DrawTextW(dc, r.label.c_str(), -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, bigFont_);
    SetTextColor(dc, kText);
    DrawTextW(dc, r.value.c_str(), -1, &vr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
    y += S(26);
  }

  y += S(12);
  gamingBtn_ = RECT{S(16), y, rc.right - S(16), y + S(30)};
  modeBtn_ = RECT{S(16), y + S(34), rc.right - S(16), y + S(64)};
  SelectObject(dc, bigFont_);

  RoundFill(dc, gamingBtn_, S(8), hoverBtn_ == 1 ? kBtnHover : kBtn);
  SetTextColor(dc, model_.gamingOn ? kAccent : kText);
  RECT t1 = gamingBtn_;
  DrawTextW(dc, model_.gamingOn ? L"Gaming Mode: ON  (click to turn off)" : L"Gaming Mode: OFF  (click to turn on)", -1, &t1,
            DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

  RoundFill(dc, modeBtn_, S(8), hoverBtn_ == 2 ? kBtnHover : kBtn);
  SetTextColor(dc, kText);
  RECT t2 = modeBtn_;
  std::wstring mt = L"Performance Mode: " + model_.modeLabel + L"  (click to change)";
  DrawTextW(dc, mt.c_str(), -1, &t2, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

  BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old);
  DeleteObject(bmp);
  DeleteDC(dc);
  EndPaint(hwnd_, &ps);
}

}  // namespace hb
