/**
 * @file    platform/updates.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/updates.h"

// First in the file so any header below finds Windows.h already in, lean.
#if defined(_WIN32)
#include "core/windows_lean.h"

#include <winhttp.h>
#elif defined(REBLUE_HAVE_CURL)
#include <curl/curl.h>
#endif

#include <charconv>
#include <string_view>
#include <system_error>
#include <thread>

#include <rex/types.h>

#include "core/build_info.h"
#include "core/encoding.h"
#include "core/logging.h"
#include "core/settings.h"

namespace bd::platform {
namespace {

constexpr u32 kTimeoutMs = 10000;

struct Redirect {
  int status = 0;
  std::string location;
};

#if defined(_WIN32)

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h)
      WinHttpCloseHandle(h);
  }
};

bool FetchRedirect(const std::string &url, Redirect &out, std::string &error) {
  const std::wstring wide = bd::Utf8ToWide(url);

  // Non-zero lengths against null pointers ask for offsets into 'wide'.
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
    error = "cannot parse " + url;
    return false;
  }
  if (parts.lpszHostName == nullptr || parts.dwHostNameLength == 0) {
    error = "no host in " + url;
    return false;
  }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path;
  if (parts.lpszUrlPath != nullptr)
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.lpszExtraInfo != nullptr)
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  if (path.empty())
    path = L"/";

  const std::wstring agent = bd::Utf8ToWide("reblue/" REBLUE_VERSION_STRING);
  Handle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                             0)};
  if (!session.h) {
    error = "WinHttpOpen failed";
    return false;
  }
  WinHttpSetTimeouts(session.h, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

  Handle connection{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
  if (!connection.h) {
    error = "cannot reach " + bd::WideToUtf8(host);
    return false;
  }

  const DWORD flags =
      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  Handle request{WinHttpOpenRequest(connection.h, L"HEAD", path.c_str(),
                                    nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
  if (!request.h) {
    error = "WinHttpOpenRequest failed";
    return false;
  }

  // The redirect is the answer, so following it would only fetch a page of
  // HTML nothing here reads.
  DWORD disable = WINHTTP_DISABLE_REDIRECTS;
  WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disable,
                   sizeof(disable));

  if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    error = "no reply from " + bd::WideToUtf8(host);
    return false;
  }

  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
          WINHTTP_NO_HEADER_INDEX)) {
    error = "reply carried no status";
    return false;
  }
  out.status = static_cast<int>(status);

  size = 0;
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION,
                      WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                      WINHTTP_NO_HEADER_INDEX);
  if (size > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    std::wstring location(size / sizeof(wchar_t), L'\0');
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION,
                            WINHTTP_HEADER_NAME_BY_INDEX, location.data(),
                            &size, WINHTTP_NO_HEADER_INDEX)) {
      while (!location.empty() && location.back() == L'\0')
        location.pop_back();
      out.location = bd::WideToUtf8(location);
    }
  }
  return true;
}

#elif defined(REBLUE_HAVE_CURL)

bool FetchRedirect(const std::string &url, Redirect &out, std::string &error) {
  static std::once_flag curl_once;
  std::call_once(curl_once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    error = "curl_easy_init failed";
    return false;
  }

  char message[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(kTimeoutMs));
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "reblue/" REBLUE_VERSION_STRING);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, message);

  const CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    error = message[0] != '\0' ? message : curl_easy_strerror(result);
    curl_easy_cleanup(curl);
    return false;
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  out.status = static_cast<int>(status);
  char *location = nullptr;
  curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
  if (location != nullptr)
    out.location = location;
  curl_easy_cleanup(curl);
  return true;
}

#else

bool FetchRedirect(const std::string &, Redirect &, std::string &error) {
  error = "this build has no HTTP client (libcurl was not found)";
  return false;
}

#endif

// A published release redirects to /releases/tag/<tag>. A repository with none
// answers 404 or redirects to the releases index, and neither carries a tag.
bool TagFromLocation(const std::string &location, std::string &tag) {
  constexpr std::string_view kMarker = "/releases/tag/";
  const size_t at = location.find(kMarker);
  if (at == std::string::npos)
    return false;
  tag = location.substr(at + kMarker.size());
  const size_t extra = tag.find_first_of("?#");
  if (extra != std::string::npos)
    tag.resize(extra);
  return !tag.empty();
}

// Anything that is not a digit separates components, so a leading v and an
// -rc suffix both fall out.
int CompareVersions(std::string_view a, std::string_view b) {
  auto next = [](std::string_view &s) -> u32 {
    while (!s.empty() && (s.front() < '0' || s.front() > '9'))
      s.remove_prefix(1);
    u32 value = 0;
    const auto [end, ec] =
        std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc()) {
      s = {};
      return 0;
    }
    s.remove_prefix(static_cast<size_t>(end - s.data()));
    return value;
  };
  while (!a.empty() || !b.empty()) {
    const u32 lhs = next(a);
    const u32 rhs = next(b);
    if (lhs != rhs)
      return lhs < rhs ? -1 : 1;
  }
  return 0;
}

} // namespace

Updates &Updates::Get() {
  static Updates s;
  return s;
}

void Updates::Start() {
  if (!bd::Settings::Get().UpdateCheck())
    return;
  const std::string url = bd::Settings::Get().UpdateUrl();
  if (url.empty())
    return;
  if (started_.exchange(true))
    return;

  // Detached: the ordered exit kills the process outright, so a check still
  // waiting on the network never holds shutdown up.
  std::thread([this, url] { Check(url); }).detach();
}

std::optional<Release> Updates::Newer() const {
  std::lock_guard lock(mutex_);
  return newer_;
}

void Updates::Check(const std::string &url) {
  std::string error;
  Redirect redirect;
  if (!FetchRedirect(url, redirect, error)) {
    BD_WARN("Update check against {} failed: {}", url, error);
    return;
  }

  std::string tag;
  if (!TagFromLocation(redirect.location, tag)) {
    BD_INFO("Update check: {} names no release yet (HTTP {})", url,
            redirect.status);
    return;
  }

  if (CompareVersions(tag, REBLUE_VERSION_STRING) <= 0) {
    BD_INFO("Update check: v" REBLUE_VERSION_STRING " is current, latest is {}",
            tag);
    return;
  }

  BD_INFO("Update available: {} (this build is v" REBLUE_VERSION_STRING ")",
          tag);
  BD_INFO("  {}", redirect.location);

  std::lock_guard lock(mutex_);
  newer_ = Release{tag, redirect.location};
}

} // namespace bd::platform
