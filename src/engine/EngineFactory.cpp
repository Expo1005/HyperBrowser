#include "core/IBrowserEngine.h"

#if defined(HB_ENGINE_WEBVIEW2)
#include "engine/webview2/WebView2Engine.h"
#endif

namespace hb {

std::unique_ptr<IBrowserEngine> CreateDefaultEngine() {
#if defined(HB_ENGINE_WEBVIEW2)
  return CreateWebView2Engine();
#else
#error "No browser engine selected. Configure with -DBROWSER_ENGINE=WEBVIEW2."
#endif
}

}  // namespace hb
