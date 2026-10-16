// Small platform-independent text helpers (unit-testable on any OS) plus
// a few Windows file helpers.
#pragma once
#include <string>

namespace hb {

std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);
std::wstring Trim(const std::wstring& s);
std::string TrimA(const std::string& s);
std::wstring ToLower(std::wstring s);
bool StartsWith(const std::wstring& s, const std::wstring& prefix);
// Percent-encodes UTF-8 bytes. Unreserved chars and any char in `keep` pass through.
// If spaceAsPlus, ' ' becomes '+' (query strings).
std::string PercentEncode(const std::string& s, const char* keep, bool spaceAsPlus);

#ifdef _WIN32
bool FileExists(const std::wstring& path);
// Reads up to maxBytes. Returns false on failure or if the file is larger than maxBytes.
bool ReadFileBytes(const std::wstring& path, std::string& out, size_t maxBytes = 1 << 20);
// Writes to path.tmp, flushes, then atomically replaces path (no half-written settings).
bool WriteFileAtomic(const std::wstring& path, const std::string& data);
bool EnsureDir(const std::wstring& path);  // creates intermediate directories too
std::wstring GetEnvVar(const wchar_t* name);
std::wstring PathToFileUrl(const std::wstring& path);
#endif

}  // namespace hb
