/**
 * @file    installer/installer_wizard.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/installer_wizard.h"

#include <imgui.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/platform/env.h>
#include <stb_image.h>
#include <vdflib.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <stdexcept>

#include "core/encoding.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/settings_model.h"
#include "embedded.h"
#include "platform/platform.h"
#include "ui/ui.h"
#include "vfs/vfs.h"

namespace bd::installer {
namespace {
const char *kDiscLabels[kDiscCount] = {"DVD 1", "DVD 2", "DVD 3"};

const char *T(const char *key) { return i18n::Text(key).c_str(); }

// Pushed only while the wizard draws, so other overlays keep the default font.
ImFont *g_body_font = nullptr;
ImFont *g_title_font = nullptr;
ImFont *g_path_font = nullptr;

std::filesystem::path ExecutablePath() {
#if !defined(_WIN32)
  if (auto appimage = rex::platform::env::get("APPIMAGE");
      appimage && !appimage->empty())
    return std::filesystem::absolute(*appimage);
#endif
  auto path = rex::filesystem::GetExecutableFolder();
#if defined(_WIN32)
  return path / "reblue.exe";
#elif defined(__APPLE__)
  // Inside a .app bundle the executable sits at Contents/MacOS/reblue; point
  // Steam at the bundle itself so it launches (and shows an icon) like any
  // other Mac app instead of a bare Unix binary.
  if (path.filename() == "MacOS" && path.parent_path().filename() == "Contents") {
    const auto bundle = path.parent_path().parent_path();
    if (bundle.extension() == ".app")
      return bundle;
  }
  return path / "reblue";
#else
  return path / "reblue";
#endif
}

// Writes each embedded grid image out to a scratch dir (vdflib installs
// artwork from a source file path, not a memory buffer) and hands it off to
// vdflib to copy into Steam's per-user grid directory under the right name
// for its slot.
void InstallSteamArtwork(const std::filesystem::path& grid_dir,
                         uint32_t app_id) {
  struct ArtworkFile {
    const char* name;
    bd::EmbeddedAsset asset;
    vdflib::ArtworkSlot slot;
  };
  constexpr auto kCover = bd::Embedded("installer/steamgrid/cover.png");
  constexpr auto kHero = bd::Embedded("installer/steamgrid/hero.png");
  constexpr auto kLogo = bd::Embedded("installer/steamgrid/logo.png");
  constexpr auto kBackdrop = bd::Embedded("installer/steamgrid/backdropbd.png");
  const ArtworkFile files[] = {
      {"cover.png", kCover, vdflib::ArtworkSlot::Portrait},
      {"hero.png", kHero, vdflib::ArtworkSlot::Hero},
      {"logo.png", kLogo, vdflib::ArtworkSlot::Logo},
      {"backdropbd.png", kBackdrop, vdflib::ArtworkSlot::Capsule},
  };

  const auto temp_dir = std::filesystem::temp_directory_path() /
                        ("reblue-steamgrid-" + std::to_string(app_id));
  std::filesystem::create_directories(temp_dir);
  struct TempDirGuard {
    std::filesystem::path path;
    ~TempDirGuard() {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  } guard{temp_dir};

  for (const auto& file : files) {
    const auto source = temp_dir / file.name;
    std::ofstream out(source, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(file.asset.data),
              static_cast<std::streamsize>(file.asset.size));
    out.close();
    if (!out || !vdflib::installLocalArtwork(grid_dir, app_id, file.slot, source))
      throw std::runtime_error(std::string("failed to install ") + file.name);
  }
}

std::string AddSteamShortcut() {
  try {
    const auto steam = vdflib::findSteamInstallPath();
    if (!steam) return i18n::Text("installer.steam.not_found");
    const auto users = vdflib::listLocalSteamUserIds(*steam);
    if (users.empty()) return i18n::Text("installer.steam.no_users");

    const auto exe = ExecutablePath();
    auto shortcut = vdflib::Shortcut::create(
        "re:Blue", exe.string(), exe.parent_path().string());
    const uint32_t app_id = shortcut.appid;
    vdflib::ShortcutRepository repository(
        vdflib::getShortcutsVdfPath(*steam, users.front()));
    repository.load();
    if (!repository.findByAppId(shortcut.appid)) {
      repository.addShortcut(std::move(shortcut));
      repository.save();
    }
    InstallSteamArtwork(vdflib::getGridDirectory(*steam, users.front()), app_id);
    BD_INFO("InstallerWizard: added Steam shortcut for user {}", users.front());
    return i18n::Text("installer.steam.added");
  } catch (const std::exception& e) {
    BD_WARN("InstallerWizard: could not add Steam shortcut: {}", e.what());
    return i18n::Fmt("installer.steam.failed", e.what());
  }
}
} // namespace

void InitInstallerFonts(ImFontAtlas *atlas) {
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false; // blob is static, imgui must not free it
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;

  auto load = [&](float px) {
    constexpr auto kFont = bd::Embedded("installer/HelveticaNeueRoman.otf");
    return atlas->AddFontFromMemoryTTF(const_cast<u8 *>(kFont.data),
                                       static_cast<int>(kFont.size),
                                       px, &cfg);
  };
  g_body_font = load(18.0f);
  g_title_font = load(40.0f);
  g_path_font = load(13.0f);
  if (!g_body_font) {
    BD_WARN(
        "Failed to load installer body font, wizard will use drawer default");
  }
}

InstallerWizard::InstallerWizard(
    rex::ui::ImGuiDrawer *drawer, rex::ui::ImmediateDrawer *immediate_drawer,
    rex::ui::WindowedAppContext &app_context,
    const std::filesystem::path &default_install_dir, bool repair,
    const InstallConfig *existing, CompletionCallback on_done)
    : ImGuiDialog(drawer), app_context_(app_context),
      immediate_drawer_(immediate_drawer), on_done_(std::move(on_done)),
      repair_(repair), install_dir_(default_install_dir) {
  // No guest yet, so this resolves to the bd_language cvar or the OS language.
  i18n::SyncLocale();

  update_check_ = bd::Settings::Get().UpdateCheck();

  // Repair on an existing install: seed the recorded disc fingerprints so the
  // user can finish (Done) and boot without re-selecting the DVDs.
  if (existing) {
    iso_fingerprints_ = existing->iso_fingerprints;
  }
}

InstallerWizard::~InstallerWizard() {
  if (install_thread_.joinable()) {
    progress_.canceled.store(true);
    install_thread_.join();
  }
}

void InstallerWizard::Finish(bool completed) {
  if (finished_)
    return;
  finished_ = true;

  InstallConfig cfg;
  cfg.install_root = std::filesystem::absolute(install_dir_);
  cfg.iso_fingerprints = iso_fingerprints_;

  BD_INFO("InstallerWizard: finished, completed={}", completed);

  auto cb = on_done_;
  const WizardChoices choices = choices_;
  app_context_.CallInUIThreadDeferred(
      [cb, completed, cfg, choices]() { cb(completed, cfg, choices); });
}

void InstallerWizard::ValidateISO(int index) {
  iso_valid_[index] = false;
  iso_fingerprints_[index].clear();
  iso_languages_[index].clear();
  if (iso_paths_[index].empty()) {
    iso_status_[index].clear();
    return;
  }

  auto disc = OpenDiscImage(iso_paths_[index]);
  if (!disc) {
    iso_status_[index] = i18n::Text("installer.status.bad_image");
    return;
  }
  if (!ValidateDisc(*disc, index + 1)) {
    iso_status_[index] =
        i18n::Fmt("installer.status.wrong_disc", kDiscLabels[index]);
    return;
  }
  iso_valid_[index] = true;
  iso_fingerprints_[index] =
      DiscFingerprint(iso_paths_[index], *disc, index + 1);
  iso_languages_[index] = ParseDiscLanguages(*disc).ui;
  iso_status_[index] = i18n::Text("installer.status.valid");
}

bool InstallerWizard::InputsReady() const {
  return std::all_of(iso_valid_.begin(), iso_valid_.end(),
                     [](bool v) { return v; }) &&
         !install_dir_.empty();
}

void InstallerWizard::PickISO(int index) {
  const std::wstring isoLabel = Utf8ToWide(i18n::Text("installer.filter.iso"));
  const std::wstring anyLabel = Utf8ToWide(i18n::Text("installer.filter.any"));
  const bd::platform::FileFilter kIsoFilters[] = {
      {isoLabel.c_str(), L"*.iso"},
      {anyLabel.c_str(), L"*.*"},
  };
  auto picked = bd::platform::ShowOpenFileDialog(
      Utf8ToWide(i18n::Text("installer.dialog.select_iso")).c_str(),
      kIsoFilters);
  if (!picked)
    return;
  iso_paths_[index] = *picked;
  ValidateISO(index);
}

void InstallerWizard::PickInstallDir() {
  auto picked = bd::platform::ShowOpenFolderDialog(
      Utf8ToWide(i18n::Text("installer.dialog.select_folder")).c_str());
  if (!picked)
    return;
  install_dir_ = *picked;
  install_status_.clear();
}

void InstallerWizard::StartInstall() {
  // InstallProgress is non-assignable (atomics/mutex), so reset fields in
  // place.
  progress_.files_done.store(0);
  progress_.files_total.store(0);
  progress_.bytes_done.store(0);
  progress_.bytes_total.store(0);
  progress_.complete.store(false);
  progress_.failed.store(false);
  progress_.canceled.store(false);
  progress_.SetCurrentFile("");
  progress_.SetError("");
  done_message_.clear();
  done_success_ = false;
  install_status_.clear();
  page_ = Page::Installing;

  const auto abs_install = std::filesystem::absolute(install_dir_);
  const auto abs_game = abs_install / "game";
  const auto abs_user = abs_install / "user";
  BD_INFO("InstallerWizard: install root -> '{}'", abs_install.string());
  BD_INFO("InstallerWizard:   game data  -> '{}'", abs_game.string());
  BD_INFO("InstallerWizard:   user data  -> '{}'", abs_user.string());

  // Installer thread only touches game data, so create the user tree here.
  std::error_code ec;
  std::filesystem::create_directories(abs_user / "dlc", ec);
  if (ec) {
    BD_WARN("InstallerWizard: could not create user/dlc dir '{}': {}",
            (abs_user / "dlc").string(), ec.message());
  }

  try {
    install_thread_ =
        Installer::RunAsync(iso_paths_, abs_game, repair_, progress_);
  } catch (const std::system_error &e) {
    BD_ERROR("Installer::RunAsync failed to spawn worker: {}", e.what());
    progress_.SetError(i18n::Fmt("installer.error.spawn", e.what()));
    progress_.failed.store(true);
    progress_.complete.store(true);
  }
}

void InstallerWizard::OnDraw(ImGuiIO &) {
  if (!background_texture_ && !background_tried_ && immediate_drawer_) {
    background_tried_ = true;
    int w = 0, h = 0, channels = 0;
    constexpr auto kLogo = bd::Embedded("installer/installer.png");
    u8 *rgba = stbi_load_from_memory(kLogo.data,
                                     static_cast<int>(kLogo.size), &w,
                                     &h, &channels, /*req_comp=*/4);
    if (!rgba) {
      BD_ERROR("InstallerWizard: stbi_load_from_memory failed: {}",
               stbi_failure_reason());
    } else {
      background_texture_ = immediate_drawer_->CreateTexture(
          static_cast<u32>(w), static_cast<u32>(h),
          rex::ui::ImmediateTextureFilter::kLinear, /*is_repeated=*/false,
          rgba);
      stbi_image_free(rgba);
      if (!background_texture_) {
        BD_ERROR("InstallerWizard: failed to create background texture");
      }
    }
  }

  auto *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowBgAlpha(
      0.0f); // transparent, the background image fills instead
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("##installer", nullptr, flags)) {
    if (background_texture_) {
      ImVec2 p0 = vp->WorkPos;
      ImVec2 p1 = ImVec2(p0.x + vp->WorkSize.x, p0.y + vp->WorkSize.y);
      ImGui::GetWindowDrawList()->AddImage(
          reinterpret_cast<ImTextureID>(background_texture_.get()), p0, p1);
    }

    // Dark panel keeps controls readable over the background image.
    const float panel_width = 760.0f;
    const float panel_margin = 32.0f;
    ImGui::SetCursorPos(ImVec2(panel_margin, panel_margin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bd::ui::Theme::kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));
    if (ImGui::BeginChild("##installer_panel", ImVec2(panel_width, 0),
                          ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoSavedSettings)) {
      if (g_body_font)
        ImGui::PushFont(g_body_font);
      switch (page_) {
      case Page::SelectInputs:
        DrawSelectInputs();
        break;
      case Page::Installing:
        DrawInstalling();
        break;
      case Page::Done:
        DrawDone();
        break;
      case Page::AddDLC:
        DrawAddDLC();
        break;
      }
      if (g_body_font)
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
  }
  ImGui::End();
}

namespace {
void DrawTitle(const char *text) {
  if (g_title_font)
    ImGui::PushFont(g_title_font);
  ImGui::TextUnformatted(text);
  if (g_title_font)
    ImGui::PopFont();
}

void SectionHeader(const char *text) {
  ImGui::TextUnformatted(text);
  ImGui::Separator();
}

// Read-only row: each of the ten language codes lights green when present in
// the discs' [Language] set, otherwise dim.
void DrawLanguageLights(const std::set<std::string> &present) {
  static const char *kUpper[] = {"US", "JP", "DE", "FR", "ES",
                                 "IT", "KR", "TW", "CN", "PO"};
  static const char *kLower[] = {"us", "jp", "de", "fr", "es",
                                 "it", "kr", "tw", "cn", "po"};
  for (int i = 0; i < 10; ++i) {
    if (i)
      ImGui::SameLine(0, 12);
    const bool on = present.count(kLower[i]) != 0;
    const ImVec4 col = on ? ImVec4(0.30f, 0.90f, 0.30f, 1.0f)  // lit
                          : ImVec4(0.32f, 0.34f, 0.40f, 1.0f); // dim
    ImGui::TextColored(col, "%s", kUpper[i]);
  }
}

void FilenameCell(const std::filesystem::path &path) {
  if (path.empty()) {
    ImGui::TextDisabled("%s", T("installer.status.not_selected"));
    return;
  }
  ImGui::TextUnformatted(path.filename().string().c_str());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", path.string().c_str());
  }
}

void DirectoryRow(const char *heading, const char *sublabel,
                  const std::filesystem::path &path, const char *id,
                  const std::function<void()> &on_change) {
  ImGui::PushID(id);
  // Align text baseline to the Change button so the row reads as one line.
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(heading);
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, bd::ui::Theme::White(0.55f));
  ImGui::TextUnformatted(sublabel);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (ImGui::Button(T("installer.button.change")))
    on_change();

  if (g_path_font)
    ImGui::PushFont(g_path_font);
  ImGui::Indent(12.0f);
  if (path.empty()) {
    ImGui::TextDisabled("not selected");
  } else {
    ImGui::TextWrapped("%s", path.string().c_str());
  }
  ImGui::Unindent(12.0f);
  if (g_path_font)
    ImGui::PopFont();
  ImGui::PopID();
}
} // namespace

void InstallerWizard::DrawSelectInputs() {
  DrawTitle(T(repair_ ? "installer.title.repair" : "installer.title.main"));
  ImGui::Spacing();

  if (repair_) {
    ImGui::TextWrapped("%s", T("installer.repair_notice"));
    ImGui::Spacing();
  }

  SectionHeader(T("installer.section.sources"));
  const ImGuiTableFlags flags =
      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody;
  if (ImGui::BeginTable("##inputs", 3, flags)) {
    ImGui::TableSetupColumn("##btn", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("##path", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthFixed,
                            100.0f);

    for (int i = 0; i < kDiscCount; ++i) {
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      const std::string btn =
          i18n::Fmt("installer.button.select_disc", kDiscLabels[i]);
      if (ImGui::Button(btn.c_str(), ImVec2(-FLT_MIN, 0)))
        PickISO(i);

      ImGui::TableSetColumnIndex(1);
      FilenameCell(iso_paths_[i]);

      ImGui::TableSetColumnIndex(2);
      if (!iso_status_[i].empty()) {
        const ImVec4 color = iso_valid_[i] ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f)
                                           : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", iso_status_[i].c_str());
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Dummy(ImVec2(0, 8));
  SectionHeader(T("installer.section.languages"));
  std::set<std::string> detected;
  for (const auto &s : iso_languages_)
    detected.insert(s.begin(), s.end());
  DrawLanguageLights(detected);

  ImGui::Dummy(ImVec2(0, 12));
  SectionHeader(T("installer.section.install_dir"));
  DirectoryRow(T("installer.install_location"),
               T(repair_ ? "installer.hint.existing" : "installer.hint.space"),
               install_dir_, "install_dir", [this]() { PickInstallDir(); });

  ImGui::Checkbox(T("installer.steam.checkbox"), &add_steam_shortcut_);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", T("installer.steam.hint"));

  ImGui::Dummy(ImVec2(0, 12));
  DrawQualityPreset();

  ImGui::Dummy(ImVec2(0, 12));
  DrawUpdateCheck();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (!install_status_.empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s",
                       install_status_.c_str());
    ImGui::Spacing();
  }

  ImGui::BeginDisabled(!InputsReady());
  if (ImGui::Button(
          T(repair_ ? "installer.button.repair" : "installer.button.install"),
          ImVec2(120, 0)))
    StartInstall();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (repair_) {
    if (ImGui::Button(T("installer.button.add_dlc"), ImVec2(120, 0)))
      EnterAddDLC();
    ImGui::SameLine();
    // Existing install: finish and boot without re-selecting discs.
    if (ImGui::Button(T("installer.button.done"), ImVec2(120, 0)))
      Finish(true);
    ImGui::SameLine();
  }
  if (ImGui::Button(T("installer.button.cancel"), ImVec2(120, 0)))
    Finish(false);
}

// Quality tier row: writes the same cvar bundle as the in-game settings menu's
// "Quality Preset". The highlighted tier is read back from the live cvars, so
// nothing is highlighted while they match no tier ("Custom").
void InstallerWizard::DrawQualityPreset() {
  SectionHeader(T("installer.section.quality"));

  const int current = bd::CurrentQualityPreset();
  const int count = bd::QualityPresetCount();
  for (int i = 0; i < count; ++i) {
    if (i)
      ImGui::SameLine(0, 8);
    const bool selected = i == current;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, bd::ui::Theme::kAccentSelected);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            bd::ui::Theme::kAccentSelected);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            bd::ui::Theme::kAccentSelected);
    }
    if (ImGui::Button(bd::QualityPresetName(i), ImVec2(120, 0))) {
      bd::ApplyQualityPreset(i);
      choices_.quality_preset = i;
    }
    if (selected)
      ImGui::PopStyleColor(3);
  }
}

// The URL sits under the checkbox so a person turning this on can see where
// the build is about to call.
void InstallerWizard::DrawUpdateCheck() {
  SectionHeader(T("installer.section.updates"));

  if (ImGui::Checkbox(T("installer.update_check"), &update_check_)) {
    choices_.update_check = update_check_;
    bd::Settings::Get().SetUpdateCheck(update_check_);
  }

  if (g_path_font)
    ImGui::PushFont(g_path_font);
  ImGui::Indent(12.0f);
  ImGui::TextDisabled("%s", bd::Settings::Get().UpdateUrl().c_str());
  ImGui::Unindent(12.0f);
  if (g_path_font)
    ImGui::PopFont();
}

void InstallerWizard::DrawInstalling() {
  DrawTitle(T(repair_ ? "installer.progress.repairing"
                      : "installer.progress.installing"));
  ImGui::Spacing();

  size_t total_bytes = progress_.bytes_total.load();
  size_t done_bytes = progress_.bytes_done.load();
  float fraction = total_bytes == 0 ? 0.0f
                                    : static_cast<float>(done_bytes) /
                                          static_cast<float>(total_bytes);
  ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), nullptr);

  auto format_bytes = [](size_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    constexpr double kMiB = 1024.0 * 1024.0;
    char buf[32];
    double b = static_cast<double>(bytes);
    if (b >= kGiB) {
      std::snprintf(buf, sizeof(buf), "%.2f GiB", b / kGiB);
    } else {
      std::snprintf(buf, sizeof(buf), "%.1f MiB", b / kMiB);
    }
    return std::string(buf);
  };
  ImGui::Text("%s / %s", format_bytes(done_bytes).c_str(),
              format_bytes(total_bytes).c_str());

  auto current = progress_.GetCurrentFile();
  if (!current.empty())
    ImGui::Text("%s", i18n::Fmt("installer.progress.current", current).c_str());

  ImGui::Spacing();
  if (ImGui::Button(T("installer.button.cancel"), ImVec2(120, 0))) {
    progress_.canceled.store(true);
  }

  if (progress_.complete.load()) {
    if (install_thread_.joinable())
      install_thread_.join();
    if (progress_.canceled.load()) {
      install_status_ = i18n::Text("installer.canceled_notice");
      page_ = Page::SelectInputs;
    } else if (progress_.failed.load()) {
      done_success_ = false;
      done_message_ = i18n::Fmt("installer.done.failed", progress_.GetError());
      page_ = Page::Done;
    } else {
      done_success_ = true;
      done_message_ = i18n::Text(repair_ ? "installer.done.repair_complete"
                                         : "installer.done.complete");
      if (add_steam_shortcut_)
        done_message_ += "\n\n" + AddSteamShortcut();
      page_ = Page::Done;
    }
  }
}

void InstallerWizard::DrawDone() {
  DrawTitle(
      T(done_success_ ? "installer.done.title" : "installer.done.stopped"));
  ImGui::Spacing();
  ImGui::TextWrapped("%s", done_message_.c_str());
  ImGui::Spacing();
  if (done_success_) {
    if (ImGui::Button(T("installer.button.continue"), ImVec2(120, 0)))
      Finish(true);
    ImGui::SameLine();
    if (ImGui::Button("Add DLC", ImVec2(120, 0)))
      EnterAddDLC();
  } else {
    if (ImGui::Button(T("installer.button.quit"), ImVec2(120, 0)))
      Finish(false);
  }
}

void InstallerWizard::EnterAddDLC() {
  dlc_return_page_ = page_;
  dlc_results_.clear();
  // Only <install>/dlc is needed here, so this derives it locally instead of
  // reaching for VFS::Get().Init(): that call also opens the access log and
  // prunes the user's detail_*.csv files, which at wizard time would run
  // against default settings, not the profile's, and do so on the UI thread.
  const bd::vfs::Paths paths(std::filesystem::absolute(install_dir_) / "game",
                             {});
  dlc_root_ = paths.DLC();
  auto &dlc = bd::vfs::VFS::Get().DLC();
  dlc.Init(dlc_root_);
  dlc.Reload();
  page_ = Page::AddDLC;
}

void InstallerWizard::PickAndInstallDLC() {
  const std::wstring dlcLabel = Utf8ToWide(i18n::Text("installer.filter.dlc"));
  const bd::platform::FileFilter kDLCFilters[] = {
      {dlcLabel.c_str(), L"*.*"},
  };
  auto picked = bd::platform::ShowOpenFileDialog(
      Utf8ToWide(i18n::Text("installer.dialog.select_dlc")).c_str(),
      kDLCFilters);
  if (!picked)
    return;

  auto validation = bd::vfs::DLCCatalog::Validate(*picked);
  if (!validation.ok) {
    dlc_results_.push_back(
        {false, picked->filename().string() + ": " + validation.error});
    return;
  }
  if (!bd::vfs::VFS::Get().DLC().Install(*picked)) {
    dlc_results_.push_back({false, i18n::Fmt("installer.dlc.install_failed",
                                             validation.display_name)});
    return;
  }
  dlc_results_.push_back(
      {true, i18n::Fmt("installer.dlc.added", validation.display_name)});
}

void InstallerWizard::DrawAddDLC() {
  DrawTitle(T("installer.dlc.title"));
  ImGui::Spacing();

  SectionHeader(T("installer.install_location"));
  if (g_path_font)
    ImGui::PushFont(g_path_font);
  ImGui::TextWrapped("%s", dlc_root_.string().c_str());
  if (g_path_font)
    ImGui::PopFont();
  ImGui::Spacing();

  if (ImGui::Button(T("installer.dlc.add_file"), ImVec2(160, 0)))
    PickAndInstallDLC();

  ImGui::Dummy(ImVec2(0, 8));
  SectionHeader(T("installer.dlc.installed"));
  auto &dlc = bd::vfs::VFS::Get().DLC();
  const size_t dlc_count = dlc.Count();
  if (dlc_count == 0) {
    ImGui::TextDisabled("%s", T("installer.dlc.none"));
  } else {
    for (size_t i = 0; i < dlc_count; ++i)
      ImGui::BulletText("%s", dlc.At(i).display_name.c_str());
  }

  if (!dlc_results_.empty()) {
    ImGui::Dummy(ImVec2(0, 8));
    SectionHeader(T("installer.dlc.results"));
    for (const auto &r : dlc_results_) {
      const ImVec4 col = r.ok ? ImVec4(0.30f, 0.90f, 0.30f, 1.0f)
                              : ImVec4(0.90f, 0.40f, 0.30f, 1.0f);
      ImGui::TextColored(col, "%s", r.message.c_str());
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button(T("installer.button.back"), ImVec2(120, 0))) {
    dlc_results_.clear();
    page_ = dlc_return_page_;
  }
}

} // namespace bd::installer
