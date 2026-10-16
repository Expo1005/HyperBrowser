#include "core/Util.h"

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hb {

std::string ToUtf8(const std::wstring& w) {
  std::string out;
  out.reserve(w.size());
  for (size_t i = 0; i < w.size(); ++i) {
    uint32_t c = static_cast<uint32_t>(w[i]);
    if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
      uint32_t d = static_cast<uint32_t>(w[i + 1]);
      if (d >= 0xDC00 && d <= 0xDFFF) {
        c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
        ++i;
      }
    }
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

std::wstring FromUtf8(const std::string& s) {
  std::wstring out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    unsigned char b = static_cast<unsigned char>(s[i]);
    uint32_t cp = 0xFFFD;
    size_t len = 1;
    if (b < 0x80) {
      cp = b;
    } else if ((b & 0xE0) == 0xC0) {
      len = 2; cp = b & 0x1F;
    } else if ((b & 0xF0) == 0xE0) {
      len = 3; cp = b & 0x0F;
    } else if ((b & 0xF8) == 0xF0) {
      len = 4; cp = b & 0x07;
    }
    if (len > 1) {
      if (i + len > s.size()) {
        cp = 0xFFFD; len = 1;
      } else {
        for (size_t k = 1; k < len; ++k) {
          unsigned char cb = static_cast<unsigned char>(s[i + k]);
          if ((cb & 0xC0) != 0x80) { cp = 0xFFFD; len = 1; break; }
          cp = (cp << 6) | (cb & 0x3F);
        }
      }
    }
    i += len;
    if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
      cp -= 0x10000;
      out += static_cast<wchar_t>(0xD800 + (cp >> 10));
      out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
    } else {
      out += static_cast<wchar_t>(cp);
    }
  }
  return out;
}

std::wstring Trim(const std::wstring& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
  while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
  return s.substr(b, e - b);
}

std::string TrimA(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

std::wstring ToLower(std::wstring s) {
  for (auto& ch : s) {
    if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
  }
  return s;
}

bool StartsWith(const std::wstring& s, const std::wstring& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string PercentEncode(const std::string& s, const char* keep, bool spaceAsPlus) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '.' || c == '_' || c == '~';
    bool kept = false;
    for (const char* k = keep; k && *k; ++k) {
      if (static_cast<unsigned char>(*k) == c) { kept = true; break; }
    }
    if (c == ' ' && spaceAsPlus) {
      out += '+';
    } else if (unreserved || kept) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

#ifdef _WIN32

bool FileExists(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileBytes(const std::wstring& path, std::string& out, size_t maxBytes) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 &&
            static_cast<unsigned long long>(size.QuadPart) <= maxBytes;
  if (ok) {
    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    size_t done = 0;
    while (ok && done < out.size()) {
      DWORD got = 0;
      ok = ReadFile(h, &out[done], static_cast<DWORD>(out.size() - done), &got, nullptr) && got > 0;
      done += got;
    }
    ok = ok && done == out.size();
  }
  CloseHandle(h);
  return ok;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
  std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  bool ok = true;
  size_t done = 0;
  while (ok && done < data.size()) {
    DWORD wrote = 0;
    ok = WriteFile(h, data.data() + done, static_cast<DWORD>(data.size() - done), &wrote, nullptr) && wrote > 0;
    done += wrote;
  }
  ok = ok && FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok) { DeleteFileW(tmp.c_str()); return false; }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

bool EnsureDir(const std::wstring& path) {
  if (path.empty()) return false;
  DWORD a = GetFileAttributesW(path.c_str());
  if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
  size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos && slash > 2) EnsureDir(path.substr(0, slash));
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring GetEnvVar(const wchar_t* name) {
  DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
  if (n == 0) return L"";
  std::wstring v(n, L'\0');
  DWORD got = GetEnvironmentVariableW(name, &v[0], n);
  v.resize(got);
  return v;
}

std::wstring PathToFileUrl(const std::wstring& path) {
  std::wstring p = path;
  for (auto& ch : p) if (ch == L'\\') ch = L'/';
  return L"file:///" + FromUtf8(PercentEncode(ToUtf8(p), "/:", false));
}

#endif  // _WIN32

}  // namespace hb
