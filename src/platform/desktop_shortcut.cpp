/**
 * @file    platform/desktop_shortcut.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/desktop_shortcut.h"

#if defined(_WIN32)
#include "core/windows_lean.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <string>

#include "core/encoding.h"

namespace bd::platform {
namespace {

// A shortcut is named by its file, so a character Windows reserves in a path
// cannot reach one. A colon is the case that matters: 're:Blue.lnk' names an
// alternate data stream on a file called 're'.
std::wstring ShortcutFileName(std::string_view name) {
  std::wstring out = bd::Utf8ToWide(name);
  std::erase_if(out, [](wchar_t c) {
    return c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
           c == L'\\' || c == L'|' || c == L'?' || c == L'*' || c < 32;
  });
  return out;
}

bool WriteShortcut(const std::filesystem::path &target, std::string_view name,
                   std::string &error) {
  IShellLinkW *link = nullptr;
  HRESULT hr =
      CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                       IID_IShellLinkW, reinterpret_cast<void **>(&link));
  if (FAILED(hr)) {
    error = "CoCreateInstance(CLSID_ShellLink) failed";
    return false;
  }

  hr = link->SetPath(target.c_str());
  if (FAILED(hr)) {
    link->Release();
    error = "IShellLinkW::SetPath failed";
    return false;
  }
  hr = link->SetWorkingDirectory(target.parent_path().c_str());
  if (FAILED(hr)) {
    link->Release();
    error = "IShellLinkW::SetWorkingDirectory failed";
    return false;
  }

  IPersistFile *persist_file = nullptr;
  hr = link->QueryInterface(IID_IPersistFile,
                            reinterpret_cast<void **>(&persist_file));
  if (FAILED(hr)) {
    link->Release();
    error = "QueryInterface(IID_IPersistFile) failed";
    return false;
  }

  PWSTR desktop = nullptr;
  hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop);
  if (FAILED(hr)) {
    persist_file->Release();
    link->Release();
    error = "SHGetKnownFolderPath(FOLDERID_Desktop) failed";
    return false;
  }
  const std::wstring file = ShortcutFileName(name);
  if (file.empty()) {
    CoTaskMemFree(desktop);
    persist_file->Release();
    link->Release();
    error = "shortcut name is empty once path characters are removed";
    return false;
  }
  const std::filesystem::path shortcut_path =
      std::filesystem::path(desktop) / (file + L".lnk");
  CoTaskMemFree(desktop);

  hr = persist_file->Save(shortcut_path.c_str(), TRUE);
  persist_file->Release();
  link->Release();
  if (FAILED(hr)) {
    error = "IPersistFile::Save failed";
    return false;
  }
  return true;
}

} // namespace

bool CreateDesktopShortcut(const std::filesystem::path &target,
                           std::string_view name, std::string &error) {
  const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(co_hr) && co_hr != RPC_E_CHANGED_MODE) {
    error = "CoInitializeEx failed";
    return false;
  }
  // S_FALSE still took a reference and has to be released. Only
  // RPC_E_CHANGED_MODE leaves nothing to tear down.
  const bool we_own_com = SUCCEEDED(co_hr);

  const bool ok = WriteShortcut(target, name, error);

  if (we_own_com)
    CoUninitialize();
  return ok;
}

} // namespace bd::platform

#else

namespace bd::platform {

bool CreateDesktopShortcut(const std::filesystem::path &, std::string_view,
                           std::string &error) {
  error = "Desktop shortcuts are not supported on this platform";
  return false;
}

} // namespace bd::platform

#endif
