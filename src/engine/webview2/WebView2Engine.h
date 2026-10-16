#pragma once
#include <memory>

#include "core/IBrowserEngine.h"

namespace hb {
// WebView2 types are intentionally kept out of this header.
std::unique_ptr<IBrowserEngine> CreateWebView2Engine();
}  // namespace hb
