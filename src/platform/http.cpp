/**
 * @file    platform/http.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/http.h"

// First so every header below it finds Windows.h already included, lean.
#if defined(_WIN32)
#include "core/windows_lean.h"

#include <winhttp.h>
#elif defined(REBLUE_HAVE_CURL)
#include <curl/curl.h>
#endif

#include <fstream>
#include <functional>
#include <mutex>
#include <system_error>

#include "core/build_info.h"
#include "core/encoding.h"

namespace bd::platform {
namespace {

constexpr u32 kTimeoutMs = 30000;
constexpr size_t kReadChunk = 64 * 1024;

using BodySink = std::function<void(const char *, size_t)>;

#if defined(_WIN32)

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h)
      WinHttpCloseHandle(h);
  }
};

HTTPResult Perform(const std::string &url, const BodySink &sink,
                   const DownloadProgress &progress) {
  HTTPResult out;
  const std::wstring wide = bd::Utf8ToWide(url);

  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
    out.error = "malformed URL: " + url;
    return out;
  }
  if (parts.lpszHostName == nullptr || parts.dwHostNameLength == 0) {
    out.error = "no host in " + url;
    return out;
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
    out.error = "WinHttpOpen failed";
    return out;
  }
  WinHttpSetTimeouts(session.h, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

  Handle connection{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
  if (!connection.h) {
    out.error = "cannot reach " + bd::WideToUtf8(host);
    return out;
  }

  const DWORD flags =
      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  Handle request{WinHttpOpenRequest(connection.h, L"GET", path.c_str(),
                                    nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
  if (!request.h) {
    out.error = "WinHttpOpenRequest failed";
    return out;
  }

  if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    out.error = "no reply from " + bd::WideToUtf8(host);
    return out;
  }

  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
          WINHTTP_NO_HEADER_INDEX)) {
    out.error = "reply carried no status";
    return out;
  }
  out.status = static_cast<int>(status);
  out.ok = true;

  u64 total = 0;
  DWORD length = 0;
  size = sizeof(length);
  if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH |
                                         WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
                          WINHTTP_NO_HEADER_INDEX)) {
    total = length;
  }

  std::string chunk(kReadChunk, '\0');
  u64 done = 0;
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.h, chunk.data(),
                         static_cast<DWORD>(chunk.size()), &read)) {
      out.ok = false;
      out.error = "read failed after " + std::to_string(done) + " bytes";
      return out;
    }
    if (read == 0)
      break;
    sink(chunk.data(), read);
    done += read;
    if (progress)
      progress(done, total);
  }
  return out;
}

#elif defined(REBLUE_HAVE_CURL)

// A transfer moving fewer bytes than this for this long is dead. There is no
// cap on total time: a content pack takes minutes.
constexpr long kStallBytesPerSec = 512;
constexpr long kStallSeconds = 30;

size_t WriteThunk(char *data, size_t size, size_t count, void *user) {
  const auto *sink = static_cast<const BodySink *>(user);
  (*sink)(data, size * count);
  return size * count;
}

int ProgressThunk(void *user, curl_off_t total, curl_off_t done, curl_off_t,
                  curl_off_t) {
  const auto *progress = static_cast<const DownloadProgress *>(user);
  if (*progress)
    (*progress)(static_cast<u64>(done), static_cast<u64>(total));
  return 0;
}

HTTPResult Perform(const std::string &url, const BodySink &sink,
                   const DownloadProgress &progress) {
  HTTPResult out;
  static std::once_flag curl_once;
  std::call_once(curl_once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    out.error = "curl_easy_init failed";
    return out;
  }

  char message[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(kTimeoutMs));
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kStallBytesPerSec);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kStallSeconds);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "reblue/" REBLUE_VERSION_STRING);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, message);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteThunk);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,
                   const_cast<void *>(static_cast<const void *>(&sink)));
  if (progress) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressThunk);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,
                     const_cast<void *>(static_cast<const void *>(&progress)));
  }

  const CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    out.error = message[0] != '\0' ? message : curl_easy_strerror(result);
    curl_easy_cleanup(curl);
    return out;
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  out.status = static_cast<int>(status);
  out.ok = true;
  curl_easy_cleanup(curl);
  return out;
}

#else

HTTPResult Perform(const std::string &, const BodySink &,
                   const DownloadProgress &) {
  HTTPResult out;
  out.error = "this build has no HTTP client (libcurl was not found)";
  return out;
}

#endif

} // namespace

HTTPResult HTTP::Get(const std::string &url, std::string &body) {
  std::string buffer;
  const BodySink sink = [&buffer](const char *data, size_t n) {
    buffer.append(data, n);
  };
  HTTPResult out = Perform(url, sink, nullptr);
  if (out.Succeeded())
    body = std::move(buffer);
  return out;
}

HTTPResult HTTP::Download(const std::string &url,
                          const std::filesystem::path &dest,
                          const DownloadProgress &progress) {
  HTTPResult out;
  std::error_code ec;
  std::filesystem::create_directories(dest.parent_path(), ec);

  std::ofstream file(dest, std::ios::binary);
  if (!file) {
    out.error = "cannot write " + dest.string();
    return out;
  }

  const BodySink sink = [&file](const char *data, size_t n) {
    file.write(data, static_cast<std::streamsize>(n));
  };
  out = Perform(url, sink, progress);
  file.close();
  if (!file.good()) {
    out.ok = false;
    if (out.error.empty())
      out.error = "write failed for " + dest.string();
  }

  if (!out.Succeeded()) {
    std::filesystem::remove(dest, ec);
    if (out.error.empty())
      out.error = "HTTP " + std::to_string(out.status);
  }
  return out;
}

} // namespace bd::platform
