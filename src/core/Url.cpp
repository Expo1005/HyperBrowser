#include "core/Url.h"

#include "core/Util.h"

namespace hb {
namespace {

bool IsDigits(const std::wstring& s) {
  if (s.empty()) return false;
  for (wchar_t c : s) if (c < L'0' || c > L'9') return false;
  return true;
}

// "host", "host:port" or "[v6]:port". Requires a dot (or localhost) so plain words become searches.
bool LooksLikeHost(const std::wstring& hostport, std::wstring& hostOut) {
  if (hostport.empty()) return false;
  std::wstring host = hostport;
  if (host[0] == L'[') {
    size_t close = host.find(L']');
    if (close == std::wstring::npos) return false;
    hostOut = host.substr(0, close + 1);
    std::wstring rest = host.substr(close + 1);
    return rest.empty() || (rest[0] == L':' && IsDigits(rest.substr(1)));
  }
  size_t colon = host.rfind(L':');
  if (colon != std::wstring::npos) {
    if (!IsDigits(host.substr(colon + 1))) return false;
    host = host.substr(0, colon);
  }
  if (host.empty() || host.front() == L'.' || host.back() == L'.') return false;
  bool hasDot = false;
  for (wchar_t c : host) {
    bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
              c == L'-' || c == L'_' || c == L'.' || static_cast<unsigned>(c) > 127;
    if (!ok) return false;
    if (c == L'.') hasDot = true;
  }
  if (!hasDot && ToLower(host) != L"localhost") return false;
  hostOut = host;
  return true;
}

bool IsPrivateHost(const std::wstring& host) {
  std::wstring h = ToLower(host);
  return h == L"localhost" || StartsWith(h, L"127.") || StartsWith(h, L"192.168.") || StartsWith(h, L"10.") ||
         h == L"[::1]";
}

std::wstring EncodeSpaces(const std::wstring& s) {
  std::wstring out;
  for (wchar_t c : s) {
    if (c == L' ') out += L"%20"; else out += c;
  }
  return out;
}

}  // namespace

bool IsHttpUrl(const std::wstring& url) {
  std::wstring l = ToLower(url);
  size_t start = 0;
  if (StartsWith(l, L"https://")) start = 8;
  else if (StartsWith(l, L"http://")) start = 7;
  else return false;
  return url.size() > start && url[start] != L'/' && url.find(L' ') == std::wstring::npos;
}

std::wstring ResolveInput(const std::wstring& input, const std::wstring& searchTemplate) {
  std::wstring s = Trim(input);
  if (s.empty()) return L"";
  std::wstring lower = ToLower(s);

  if (StartsWith(lower, L"http://") || StartsWith(lower, L"https://") || StartsWith(lower, L"file:///"))
    return EncodeSpaces(s);
  if (lower == L"about:blank") return s;

  // Windows drive path -> file URL (C:\x or C:/x)
  if (s.size() >= 3 && ((s[0] >= L'A' && s[0] <= L'Z') || (s[0] >= L'a' && s[0] <= L'z')) && s[1] == L':' &&
      (s[2] == L'\\' || s[2] == L'/')) {
    std::wstring p = s;
    for (auto& c : p) if (c == L'\\') c = L'/';
    return L"file:///" + FromUtf8(PercentEncode(ToUtf8(p), "/:", false));
  }

  bool dangerous = StartsWith(lower, L"javascript:") || StartsWith(lower, L"data:") ||
                   StartsWith(lower, L"vbscript:") || StartsWith(lower, L"blob:");

  if (!dangerous && s.find(L' ') == std::wstring::npos) {
    size_t cut = s.find_first_of(L"/?#");
    std::wstring hostport = cut == std::wstring::npos ? s : s.substr(0, cut);
    std::wstring host;
    if (LooksLikeHost(hostport, host)) {
      return (IsPrivateHost(host) ? L"http://" : L"https://") + s;
    }
  }

  std::wstring tpl = searchTemplate.empty() ? L"https://www.google.com/search?q=%s" : searchTemplate;
  std::wstring q = FromUtf8(PercentEncode(ToUtf8(s), "", true));
  size_t pos = tpl.find(L"%s");
  if (pos == std::wstring::npos) return tpl + q;
  return tpl.substr(0, pos) + q + tpl.substr(pos + 2);
}

}  // namespace hb
