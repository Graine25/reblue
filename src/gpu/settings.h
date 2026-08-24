/**
 * @file    gpu/settings.h
 * @brief   Graphics settings. The AA path and the quality presets are policy
 *          over several settings at once, so they live here, not in the menu.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd::gpu {

// Which anti-aliasing path the scene takes. One multiplier, two paths:
// supersampling renders above output resolution and downsamples, multisampling
// samples at output resolution.
enum class AAMode : u32 {
  MultiSample = 0,
  SuperSample = 1,
};

// The ratio BD's render target is fit to inside the window. Every mode but
// Original reprojects the scene to hold the vertical view while the 2D layer
// stays centered on its design canvas. Auto takes the window's own ratio, and
// Stretch fills it without reprojecting, so the picture distorts.
enum class AspectMode : i32 {
  Original = 0, // 16:9
  Standard = 1, // 4:3
  Wide = 2,     // 16:10
  Ultrawide = 3,
  SuperUltrawide = 4,
  Auto = 5,
  Stretch = 6,
};

// The field of view the game frames itself at, horizontal degrees at 16:9.
// bdCameraInit seeds every camera with 3*pi/20 of vertical view, 27 degrees,
// which is this across that ratio. bd_fov_offset moves off it, and 0 keeps it.
inline constexpr i32 kAuthoredFOVDegrees = 46;

// Cost-ranked bundles over the five quality settings. Medium is exactly the
// shipped defaults, so a fresh install reads Medium rather than Custom.
enum class QualityPreset : u32 {
  Low = 0,
  Medium = 1,
  High = 2,
  Ultra = 3,
  Custom = 4,
};
inline constexpr u32 kQualityPresetCount = 4; // Custom is a state, not a target

// Catalog keys, so the menu and the installer label a preset from one place.
constexpr const char *ToString(QualityPreset preset) {
  switch (preset) {
  case QualityPreset::Low:
    return "opt.preset.low";
  case QualityPreset::Medium:
    return "opt.preset.medium";
  case QualityPreset::High:
    return "opt.preset.high";
  case QualityPreset::Ultra:
    return "opt.preset.ultra";
  case QualityPreset::Custom:
    return "opt.preset.custom";
  }
  return "";
}

class Settings {
public:
  static Settings &Get();

  // Adopts the current cvar values, then registers a change callback per
  // setting so console, config file and launch argument writes reach here.
  // Called once from ReblueApp::OnPostInitLogging, the first consumer hook
  // after rex::cvar::LoadConfig has run.
  void Init();

  // Re-reads every setting from cvar storage. rex::cvar::ResetToDefault and
  // ResetAllToDefaults write the storage without firing a change callback, so
  // anything that calls them calls this after.
  void AdoptCvars();

  i32 Anisotropy() const { return anisotropy_; }
  bool SetAnisotropy(i32 v);

  bool NTSCFilter() const { return ntscFilter_; }
  bool SetNTSCFilter(bool v);

  // Fraction of BD's own depth-of-field blur to keep, 1 being all of it.
  f64 DOFStrength() const { return dofStrength_; }
  bool SetDOFStrength(f64 v);

  f64 ShadowDistance() const { return shadowDistance_; }
  bool SetShadowDistance(f64 v);

  // How far above the size BD asks for the planar reflection may be scaled.
  // The game's own distance LOD still picks that size. This only bounds how
  // far the render rect and supersampling are allowed to lift it.
  f64 ReflectionUpscale() const { return reflectionUpscale_; }

  // Coverage and map dimension are one setting between them: coverage is live
  // and the dimension restart-bound, but a step that moved only one would
  // leave texel density wrong for as long as the coverage change is visible.
  bool SetShadowQuality(f64 distance, i32 dimension);

  bool Vsync() const { return vsync_; }
  bool SetVsync(bool v);

  i32 DiagVerbosity() const { return diagVerbosity_; }
  bool SetDiagVerbosity(i32 v);

  // The raw cvar value, for the menu row that cycles it.
  // Output::ConfiguredAspect turns it into a ratio.
  i32 AspectRatio() const { return aspectRatio_; }
  bool SetAspectRatio(i32 v);

  // Horizontal degrees added to the game's own framing at 16:9, which the menu
  // counts off 45. An offset rather than an absolute, so 0 is the authored view
  // whatever a scene frames itself at.
  i32 FOVOffset() const { return fovOffset_; }
  bool SetFOVOffset(i32 v);

  // What a camera's half-angle tangent is multiplied by to reach that field of
  // view. Exactly 1 at the default, so the projection is left alone rather than
  // rebuilt and moved by the float residue, and a scene framed tighter or wider
  // than the default keeps its relationship to it.
  f64 FOVTanScale() const;

  // Restart-bound: read once when the device and its targets are built.
  i32 ShadowDimension() const { return shadowDimension_; }
  bool PSOPrecache() const { return psoPrecache_; }
  bool GeometryGPUUpload() const { return geometryGPUUpload_; }
  bool DRED() const { return dred_; }
  i32 SurfacePoolBudgetMB() const { return surfacePoolBudgetMB_; }

  // The AA path and its multiplier, as the user asked for them. Clamping to
  // what the device actually supports stays with the device, the only place
  // that knows.
  gpu::AAMode AAMode() const;
  i32 AALevel() const;
  i32 SuperSampling() const { return superSampling_; }
  i32 MSAA() const { return msaa_; }

  // Both are restart-bound, and both write the pair, because the two settings
  // encode one setting between them.
  bool SetAAMode(gpu::AAMode mode);
  bool SetAALevel(i32 level);

  // The highest level the path accepts. A menu that grays out levels asks for
  // this rather than repeating the cap, so it cannot disagree with what
  // SetAALevel will actually take.
  static i32 MaxAALevel(gpu::AAMode mode);

  // The preset the five quality settings currently match, or Custom.
  gpu::QualityPreset QualityPreset() const;
  bool SetQualityPreset(gpu::QualityPreset preset);

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  // Each pulls its own setting from cvar storage and nothing else. AdoptCvars
  // calls every one. Each setting's registered change callback calls only its
  // own, so one setting changing never re-reads the others.
  void AdoptAnisotropy();
  void AdoptNTSCFilter();
  void AdoptDOFStrength();
  void AdoptShadowDistance();
  void AdoptReflectionUpscale();
  void AdoptVsync();
  void AdoptDiagVerbosity();
  void AdoptAspectRatio();
  void AdoptFOVOffset();
  void AdoptShadowDimension();
  void AdoptPSOPrecache();
  void AdoptGeometryGPUUpload();
  void AdoptDRED();
  void AdoptSurfacePoolBudgetMB();
  void AdoptSuperSampling();
  void AdoptMSAA();

  bool SetAAPair(i32 superSampling, i32 msaa);

  i32 anisotropy_ = 16;
  i32 superSampling_ = 1;
  i32 msaa_ = 4;
  bool ntscFilter_ = false;
  f64 dofStrength_ = 1.0;
  i32 shadowDimension_ = 4096;
  f64 shadowDistance_ = 2.0;
  f64 reflectionUpscale_ = 2.0;
  i32 aspectRatio_ = 0;
  i32 fovOffset_ = 0;
  bool vsync_ = true;
  bool psoPrecache_ = true;
  bool geometryGPUUpload_ = true;
  bool dred_ = true;
  i32 surfacePoolBudgetMB_ = 0;
  i32 diagVerbosity_ = 2;
};

} // namespace bd::gpu
