#pragma once
#include <string>

namespace hb {

// Turns whatever the user typed into a navigable URL.
//  - http(s)://, file:/// and about:blank pass through
//  - "example.com", "localhost:3000", "192.168.1.1/x" become URLs
//  - javascript:, data:, vbscript: are NEVER navigated to from the address bar (treated as search text)
//  - everything else becomes a web search using searchTemplate ("%s" is replaced by the encoded query)
// Returns "" for empty input.
std::wstring ResolveInput(const std::wstring& input, const std::wstring& searchTemplate);

// true for absolute http:// or https:// URLs with a non-empty host part.
bool IsHttpUrl(const std::wstring& url);

}  // namespace hb
