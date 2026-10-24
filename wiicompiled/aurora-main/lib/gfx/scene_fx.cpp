#include "scene_fx.hpp"

#include "../internal.hpp"
#include "../gx/gx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace aurora::gfx::scene_fx {
static Module Log("aurora::gfx::scene_fx");

using webgpu::g_device;
using webgpu::g_queue;

static std::mutex g_settingsMutex;
static Settings g_settings;
static std::atomic_bool g_active{true};

static bool settings_active(const Settings& s) noexcept {
  return s.enabled && (s.ambientOcclusion > 0.f || s.shadows > 0.f || s.reflections > 0.f || s.bloom > 0.f ||
                       s.lighting > 0.f || s.sunRays > 0.f);
}

void set_settings(const Settings& settings) noexcept {
  std::lock_guard lock{g_settingsMutex};
  g_settings = settings;
  const auto clamp01 = [](float& v) { v = std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; };
  clamp01(g_settings.ambientOcclusion);
  clamp01(g_settings.shadows);
  clamp01(g_settings.reflections);
  clamp01(g_settings.bloom);
  clamp01(g_settings.lighting);
  clamp01(g_settings.sunRays);
  g_active = settings_active(g_settings);
}

Settings get_settings() noexcept {
  std::lock_guard lock{g_settingsMutex};
  return g_settings;
}

static std::atomic<int> g_stageMode{0};
static std::atomic<float> g_stageFloorV{0.8f}, g_stageOriginU{0.5f}, g_stageUnitU{0.002f}, g_stageUnitV{0.002f};
static std::atomic<int> g_stagePreset{0};

bool active() noexcept { return g_active.load(std::memory_order_relaxed) || g_stageMode.load(std::memory_order_relaxed) != 0; }

int stage_mode() noexcept { return g_stageMode.load(std::memory_order_relaxed); }

void set_stage(const Stage& stage) noexcept {
  g_stageMode = stage.mode;
  g_stageFloorV = stage.floorV;
  g_stageOriginU = stage.originU;
  g_stageUnitU = stage.unitU;
  g_stageUnitV = stage.unitV;
  g_stagePreset = stage.preset;
}

static std::atomic<float> g_outdoorHint{1.f};
void set_outdoor_hint(float hint) noexcept { g_outdoorHint = std::clamp(hint, 0.f, 1.f); }

// World-space sizes are in SPM map units (Mario is roughly 40 units tall).
static constexpr float AoRadius = 26.f;
static constexpr float ShadowRayLength = 90.f;
static constexpr float DioramaShadowLength = 36.f;  // how far a paper layer's drop shadow reaches
static constexpr float BloomThreshold = 0.78f;
// Sun rays come from above the top-left of the screen, the same side as the key light.
static constexpr float SunU = 0.2f;
static constexpr float SunV = -0.3f;

// The battle dioramas, one per area: shapes and styles (see the shader's stage section) and colours.
struct StagePreset {
  float back, far, mid, near, ground, style, wing, openSky;
  float colors[10][3];  // back top, back bottom, far, mid, near, ground top, ground face, accent, detail, glow
};
// clang-format off
static constexpr StagePreset StagePresets[] = {
  /* 0 test room */ {0, 8, 0, 0, 0, 0, 0, 0, {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}}},
  /* 1 Lineland Road */ {1, 9, 15, 11, 0, 0, 15, 1, {{.3f, .55f, .95f}, {.62f, .82f, .98f}, {.42f, .7f, .62f}, {.38f, .72f, .4f}, {.33f, .7f, .3f}, {.3f, .66f, .24f}, {.62f, .4f, .18f}, {.55f, .35f, .15f}, {1, 1, 1}, {.95f, .55f, .7f}}},
  /* 2 Mount Lineland */ {1, 14, 23, 13, 0, 0, 23, 1, {{.3f, .55f, .95f}, {.62f, .82f, .98f}, {.82f, .52f, .3f}, {.42f, .28f, .16f}, {.55f, .62f, .25f}, {.8f, .58f, .32f}, {.66f, .42f, .22f}, {.42f, .28f, .16f}, {1, 1, 1}, {1, 1, 1}}},
  /* 3 Yold Desert */ {0, 2, 16, 3, 3, 0, 16, 1, {{.22f, .58f, .86f}, {.7f, .88f, .92f}, {.92f, .64f, .36f}, {.3f, .58f, .26f}, {.4f, .62f, .32f}, {.96f, .76f, .46f}, {.88f, .62f, .34f}, {.62f, .42f, .22f}, {1, 1, 1}, {1, 1, 1}}},
  /* 4 Yold Ruins */ {4, 8, 4, 18, 10, 0, 4, 0, {{.24f, .28f, .32f}, {.14f, .16f, .2f}, {.3f, .34f, .32f}, {.4f, .44f, .38f}, {.3f, .26f, .2f}, {.38f, .42f, .38f}, {.26f, .3f, .28f}, {.28f, .24f, .18f}, {.5f, .5f, .5f}, {1, .62f, .22f}}},
  /* 5 Gloam Valley */ {9, 17, 17, 17, 4, 0, 17, 0, {{.97f, .97f, .95f}, {.92f, .93f, .9f}, {.99f, .99f, .98f}, {.22f, .52f, .22f}, {.28f, .58f, .26f}, {.97f, .97f, .95f}, {.9f, .9f, .88f}, {.2f, .45f, .2f}, {.8f, .8f, .8f}, {1, 1, 1}}},
  /* 6 Merlee's Mansion */ {15, 5, 4, 8, 6, 0, 4, 0, {{.46f, .42f, .54f}, {.36f, .3f, .34f}, {.4f, .34f, .42f}, {.5f, .36f, .3f}, {.28f, .22f, .22f}, {.48f, .32f, .24f}, {.38f, .26f, .2f}, {.3f, .22f, .18f}, {.62f, .56f, .68f}, {1, .64f, .28f}}},
  /* 7 Merlee's Mansion basement */ {4, 16, 0, 14, 6, 0, 0, 0, {{.46f, .34f, .28f}, {.32f, .24f, .2f}, {.42f, .3f, .2f}, {.5f, .4f, .3f}, {.58f, .42f, .26f}, {.46f, .32f, .22f}, {.34f, .24f, .18f}, {.4f, .3f, .2f}, {.5f, .5f, .5f}, {1, .7f, .3f}}},
  /* 8 The Bitlands */ {5, 10, 24, 14, 9, 3, 24, 1, {{.32f, .62f, .95f}, {.72f, .88f, 1}, {.56f, .64f, .7f}, {.36f, .7f, .32f}, {.86f, .56f, .26f}, {.36f, .72f, .26f}, {.66f, .38f, .2f}, {.5f, .3f, .18f}, {1, 1, 1}, {1, .9f, .3f}}},
  /* 9 The Tile Pool */ {14, 15, 0, 14, 4, 3, 0, 0, {{.3f, .36f, .74f}, {.18f, .22f, .56f}, {.5f, .44f, .74f}, {.4f, .4f, .7f}, {.4f, .4f, .68f}, {.52f, .52f, .74f}, {.36f, .36f, .58f}, {.3f, .3f, .5f}, {.46f, .54f, .86f}, {1, 1, 1}}},
  /* 10 The Dotwood Tree */ {13, 8, 0, 14, 1, 3, 0, 0, {{.74f, .54f, .28f}, {.62f, .44f, .22f}, {.5f, .4f, .3f}, {.5f, .4f, .3f}, {.62f, .36f, .14f}, {.72f, .46f, .2f}, {.58f, .36f, .16f}, {.4f, .3f, .2f}, {.5f, .5f, .5f}, {1, 1, 1}}},
  /* 11 Fort Francis */ {16, 4, 0, 14, 10, 3, 0, 0, {{.2f, .24f, .3f}, {.12f, .15f, .2f}, {.88f, .88f, .9f}, {.6f, .6f, .7f}, {.82f, .3f, .32f}, {.92f, .92f, .94f}, {.78f, .78f, .82f}, {.5f, .5f, .6f}, {.28f, .33f, .4f}, {1, 1, 1}}},
  /* 12 Outer Space */ {6, 7, 25, 0, 5, 0, 0, 0, {{.12f, .14f, .32f}, {.06f, .08f, .2f}, {.66f, .3f, .58f}, {.4f, .4f, .52f}, {.4f, .4f, .5f}, {.34f, .34f, .48f}, {.22f, .22f, .34f}, {.5f, .5f, .6f}, {.03f, .03f, .09f}, {1, 1, 1}}},
  /* 13 Planet Blobule */ {6, 8, 19, 12, 5, 0, 19, 0, {{.12f, .14f, .32f}, {.06f, .08f, .2f}, {.5f, .5f, .5f}, {.3f, .7f, .55f}, {.76f, .68f, .5f}, {.82f, .76f, .6f}, {.7f, .64f, .48f}, {.5f, .5f, .5f}, {.03f, .03f, .09f}, {.95f, .35f, .85f}}},
  /* 14 The Whoa Zone */ {0, 8, 22, 0, 4, 0, 22, 0, {{.96f, .97f, .98f}, {.84f, .86f, .88f}, {.5f, .5f, .5f}, {.36f, .62f, .6f}, {.4f, .6f, .6f}, {.4f, .78f, .44f}, {.86f, .92f, .88f}, {.5f, .5f, .5f}, {.5f, .5f, .5f}, {1, 1, 1}}},
  /* 15 Land of the Cragnons */ {5, 11, 2, 13, 3, 0, 2, 1, {{.25f, .55f, .95f}, {.7f, .86f, .98f}, {.84f, .72f, .64f}, {.66f, .66f, .62f}, {.42f, .6f, .3f}, {.9f, .78f, .55f}, {.8f, .66f, .45f}, {.5f, .4f, .3f}, {1, 1, 1}, {1, 1, 1}}},
  /* 16 Floro Caverns */ {12, 16, 0, 6, 5, 0, 0, 0, {{.3f, .3f, .32f}, {.14f, .14f, .16f}, {.36f, .26f, .18f}, {.5f, .5f, .5f}, {.45f, .85f, .95f}, {.32f, .32f, .34f}, {.2f, .2f, .22f}, {.36f, .26f, .18f}, {.4f, .4f, .42f}, {.45f, .9f, .98f}}},
  /* 17 Sammer's Kingdom */ {0, 12, 20, 13, 5, 0, 20, 1, {{.5f, .86f, .78f}, {.86f, .96f, .92f}, {.42f, .52f, .86f}, {.05f, .05f, .06f}, {.06f, .06f, .07f}, {.58f, .58f, .6f}, {.42f, .42f, .46f}, {.96f, .8f, .34f}, {1, 1, 1}, {1, 1, 1}}},
  /* 18 Sammer's Kingdom in the void */ {10, 12, 20, 13, 5, 0, 20, 0, {{.36f, .1f, .52f}, {.08f, .26f, .22f}, {.86f, .46f, .72f}, {.05f, .05f, .06f}, {.06f, .06f, .07f}, {.58f, .58f, .6f}, {.42f, .42f, .46f}, {.3f, .82f, .56f}, {.04f, .02f, .06f}, {1, 1, 1}}},
  /* 19 The Underwhere */ {0, 10, 24, 13, 1, 2, 24, 0, {{.1f, .08f, .18f}, {.04f, .06f, .1f}, {.06f, .06f, .12f}, {.07f, .08f, .14f}, {.06f, .08f, .12f}, {.22f, .18f, .26f}, {.14f, .12f, .18f}, {.06f, .07f, .12f}, {.3f, .3f, .4f}, {.3f, .95f, .85f}}},
  /* 20 The Overthere */ {11, 13, 4, 10, 7, 0, 4, 1, {{.45f, .78f, .98f}, {.96f, .97f, .88f}, {.99f, .99f, 1}, {.96f, .95f, .9f}, {.99f, .99f, 1}, {1, 1, 1}, {.86f, .9f, .98f}, {.8f, .8f, .8f}, {1, 1, 1}, {1, 1, 1}}},
  /* 21 Castle Bleck */ {7, 8, 21, 0, 8, 1, 0, 0, {{0, 0, 0}, {.02f, .02f, .03f}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}}},
  /* 22 Flipside and Flopside */ {4, 8, 26, 0, 10, 0, 0, 0, {{.52f, .4f, .32f}, {.4f, .3f, .24f}, {.5f, .5f, .5f}, {.36f, .3f, .22f}, {.4f, .4f, .4f}, {.48f, .5f, .38f}, {.36f, .38f, .3f}, {.3f, .26f, .2f}, {.5f, .5f, .5f}, {.62f, .86f, .58f}}},
  /* 23 The Pit of 100 Trials */ {0, 3, 0, 14, 1, 0, 0, 0, {{.8f, .77f, .62f}, {.7f, .68f, .54f}, {.16f, .14f, .13f}, {.5f, .5f, .5f}, {.18f, .16f, .15f}, {.6f, .34f, .36f}, {.48f, .26f, .28f}, {.3f, .3f, .3f}, {.5f, .5f, .5f}, {1, 1, 1}}},
};
// clang-format on

bool make_uniform(Uniform& out, const Mat4x4<float>& gxProj, bool ortho, const webgpu::Viewport& vp,
                  wgpu::Extent3D targetSize) noexcept {
  // Perspective: x/y offsets in column 2 (scaled by z); orthographic: in column 3.
  const float p00 = gxProj.m0[0], p11 = gxProj.m1[1];
  const float px = ortho ? gxProj.m0[3] : gxProj.m0[2], py = ortho ? gxProj.m1[3] : gxProj.m1[2];
  const float a = gxProj.m2[2], b = gxProj.m2[3];
  if (!(std::abs(p00) > 1e-9f) || !(std::abs(p11) > 1e-9f) || !(std::abs(ortho ? a : b) > 1e-12f) ||
      vp.width <= 0.f || vp.height <= 0.f || targetSize.width == 0 || targetSize.height == 0) {
    return false;
  }
  // SetViewport stores GX depth range [znear, zfar] reversed: [1 - zfar, 1 - znear].
  const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
  const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
  const float range = maxDepth - minDepth;
  if (!(range > 1e-6f)) {
    return false;
  }
  const Settings s = get_settings();
  static uint32_t s_frames = 0;  // drives the beams' slow drift; one scene pass per game frame
  // The 2D camera sees flat paper layers: a key light from just above-left of the camera drops a
  // soft shadow of each layer onto the ones behind it. In 3D a higher light shapes the geometry.
  const float lx = ortho ? -0.12f : -0.42f, ly = ortho ? 0.2f : 0.78f, lz = ortho ? 1.f : 0.46f;
  const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
  const float shadowLength = ortho ? DioramaShadowLength : ShadowRayLength;
  const float w = static_cast<float>(targetSize.width), h = static_cast<float>(targetSize.height);
  out = Uniform{
      .proj = {p00, p11, px, py},
      .depth = {a, b, minDepth, 1.f / range},
      .viewport = {vp.left, vp.top, vp.width, vp.height},
      .screen = {w, h, 1.f / w, 1.f / h},
      .light = {lx / ll, ly / ll, lz / ll, 0.f},
      .amount = {s.ambientOcclusion, s.shadows, s.reflections, s.bloom},
      .amount2 = {s.lighting, AoRadius, shadowLength, BloomThreshold},
      .misc = {ortho ? 1.f : 0.f, 0.f, ShadowRayLength * 0.75f, s.sunRays},
      .sun = {SunU, SunV, static_cast<float>(++s_frames % 216000u) / 60.f, g_outdoorHint.load(std::memory_order_relaxed)},
  };
  // Battle stage / test room: seconds since the mode began drive the pop-up animation.
  static int s_lastMode = 0;
  static uint32_t s_modeStart = 0;
  const int mode = g_stageMode.load(std::memory_order_relaxed);
  if (mode != s_lastMode) {
    s_lastMode = mode;
    s_modeStart = s_frames;
  }
  out.stage[0] = static_cast<float>(mode);
  out.stage[1] = static_cast<float>(s_frames - s_modeStart) / 60.f;
  out.stage[2] = g_stageFloorV.load(std::memory_order_relaxed);
  out.stage[3] = 0.f;
  const int count = static_cast<int>(std::size(StagePresets));
  const StagePreset& preset = StagePresets[std::clamp(g_stagePreset.load(std::memory_order_relaxed), 0, count - 1)];
  for (int i = 0; i < 10; ++i) {
    out.palette[i][0] = preset.colors[i][0];
    out.palette[i][1] = preset.colors[i][1];
    out.palette[i][2] = preset.colors[i][2];
    out.palette[i][3] = 1.f;
  }
  out.motif[0] = preset.back;
  out.motif[1] = preset.far;
  out.motif[2] = preset.mid;
  out.motif[3] = preset.near;
  out.motif2[0] = preset.ground;
  out.motif2[1] = static_cast<float>(&preset - StagePresets);
  out.motif2[2] = out.motif2[3] = 0.f;
  out.motif3[0] = preset.style;
  out.motif3[1] = preset.wing;
  out.motif3[2] = preset.openSky;
  out.motif3[3] = 0.f;
  out.stage2[0] = g_stageOriginU.load(std::memory_order_relaxed);
  out.stage2[1] = g_stageUnitU.load(std::memory_order_relaxed);
  out.stage2[2] = g_stageUnitV.load(std::memory_order_relaxed);
  out.stage2[3] = 0.f;
  return true;
}

static constexpr std::string_view Shader = R"(
struct Fx {
    proj: vec4f,
    depth: vec4f,
    viewport: vec4f,
    screen: vec4f,
    light: vec4f,
    amount: vec4f,
    amount2: vec4f,
    misc: vec4f,
    sun: vec4f,
    stage: vec4f,
    stage2: vec4f,
    palette: array<vec4f, 10>,
    motif: vec4f,
    motif2: vec4f,
    motif3: vec4f,
};
// Written by the compute pass (see StatsShader): how open the sky is and the area's sun tint.
struct Stats {
    counters: array<u32, 8>,
    open: f32,
    pad0: f32,
    pad1: f32,
    pad2: f32,
    tint: vec4f,
};
@group(0) @binding(0) var samp: sampler;
@group(0) @binding(1) var scene: texture_2d<f32>;
@group(0) @binding(2) var depth_tex: texture_depth_2d;
@group(0) @binding(3) var<uniform> fx: Fx;
@group(0) @binding(4) var aux0: texture_2d<f32>;
@group(0) @binding(5) var aux1: texture_2d<f32>;
@group(0) @binding(6) var<storage, read> stats: Stats;
@group(0) @binding(7) var aux2: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

var<private> positions: array<vec2f, 3> = array(vec2f(-1.0, 1.0), vec2f(-1.0, -3.0), vec2f(3.0, 1.0));
var<private> uvs: array<vec2f, 3> = array(vec2f(0.0, 0.0), vec2f(0.0, 2.0), vec2f(2.0, 0.0));

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4f(positions[vi], 0.0, 1.0);
    out.uv = uvs[vi];
    return out;
}

fn screen_size() -> vec2i { return vec2i(fx.screen.xy); }

fn clamp_px(p: vec2i) -> vec2i { return clamp(p, vec2i(0), screen_size() - vec2i(1)); }

fn load_depth(p: vec2i) -> f32 { return textureLoad(depth_tex, clamp_px(p), 0); }

// Host depth after undoing the viewport's depth range; reversed-Z: 1 = near plane, 0 = far plane.
fn host_depth(stored: f32) -> f32 { return (stored - fx.depth.z) * fx.depth.w; }

fn is_background(stored: f32) -> bool { return host_depth(stored) <= 0.00002; }

fn is_ortho() -> bool { return fx.misc.x > 0.5; }

// GX view space (camera looks down -z). Perspective: host depth d = a + b / z; orthographic
// (SPM's 2D camera): d = -(a z + b).
fn view_z(stored: f32) -> f32 {
    let d = host_depth(stored);
    if (is_ortho()) {
        return -(d + fx.depth.y) / fx.depth.x;
    }
    let denom = d - fx.depth.x;
    return fx.depth.y / select(denom, -1e-6, abs(denom) < 1e-6);
}

fn ndc_of(px: vec2f) -> vec2f {
    let t = (px - fx.viewport.xy) / fx.viewport.zw;
    return vec2f(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0);
}

fn view_pos(px: vec2f, stored: f32) -> vec3f {
    let z = view_z(stored);
    let n = ndc_of(px);
    if (is_ortho()) {
        return vec3f((n.x - fx.proj.z) / fx.proj.x, (n.y - fx.proj.w) / fx.proj.y, z);
    }
    return vec3f(-z * (n.x + fx.proj.z) / fx.proj.x, -z * (n.y + fx.proj.w) / fx.proj.y, z);
}

fn view_pos_at(p: vec2i) -> vec3f { return view_pos(vec2f(p) + 0.5, load_depth(p)); }

fn project(v: vec3f) -> vec2f {
    var ndc: vec2f;
    if (is_ortho()) {
        ndc = vec2f(fx.proj.x * v.x + fx.proj.z, fx.proj.y * v.y + fx.proj.w);
    } else {
        let w = max(-v.z, 1e-4);
        ndc = vec2f(fx.proj.x * v.x + fx.proj.z * v.z, fx.proj.y * v.y + fx.proj.w * v.z) / w;
    }
    return fx.viewport.xy + vec2f((ndc.x + 1.0) * 0.5, (1.0 - ndc.y) * 0.5) * fx.viewport.zw;
}

// Direction from the eye to a view-space point.
fn view_dir(c: vec3f) -> vec3f { return select(normalize(c), vec3f(0.0, 0.0, -1.0), is_ortho()); }

// Pixels per world unit at view depth z.
fn pixels_per_unit(z: f32) -> f32 {
    let scale = select(abs(fx.proj.x) / max(-z, 1.0), abs(fx.proj.x), is_ortho());
    return scale * fx.viewport.z * 0.5;
}

fn on_screen(px: vec2f) -> bool {
    return all(px >= fx.viewport.xy) && all(px < fx.viewport.xy + fx.viewport.zw) && all(px >= vec2f(0.0)) &&
           all(px < fx.screen.xy);
}

// Picks the smaller depth step on each axis so silhouette edges do not bend the normal.
fn view_normal(p: vec2i, c: vec3f) -> vec3f {
    let l = view_pos_at(p - vec2i(1, 0));
    let r = view_pos_at(p + vec2i(1, 0));
    let u = view_pos_at(p - vec2i(0, 1));
    let d = view_pos_at(p + vec2i(0, 1));
    let dx = select(r - c, c - l, abs(l.z - c.z) < abs(r.z - c.z));
    let dy = select(d - c, c - u, abs(u.z - c.z) < abs(d.z - c.z));
    var n = cross(dx, dy);
    let len2 = dot(n, n);
    if (!(len2 > 1e-20)) {
        return vec3f(0.0, 0.0, 1.0);
    }
    n = n * inverseSqrt(len2);
    return select(n, -n, n.z < 0.0);
}

fn noise(px: vec2f) -> f32 {
    return fract(52.9829189 * fract(dot(px, vec2f(0.06711056, 0.00583715))));
}

fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }

// Is the pixel at q farther away than view depth z by more than `gap` (or empty sky)?
fn behind(q: vec2i, z: f32, gap: f32) -> bool {
    let d = load_depth(q);
    return is_background(d) || view_z(d) < z - gap;
}

// Paper diorama: every layer drops a soft shadow down-right onto whatever lies behind it. A pixel is
// shadowed by nearer layers found up-left of it; the penumbra widens with distance.
fn diorama_shadow(p: vec2i, c: vec3f, jitter: f32) -> f32 {
    let ppu = pixels_per_unit(c.z);
    let dir = normalize(vec2f(fx.light.x, -fx.light.y));
    let reach = fx.amount2.z;
    var occ = 0.0;
    for (var i = 0; i < 16; i++) {
        let t = (f32(i) + jitter) / 16.0;
        let ang = f32(i) * 2.399963 + jitter * 6.283185;
        let spread = (2.0 + 9.0 * t) * ppu;
        let sp = vec2f(p) + 0.5 + dir * (mix(2.0, reach, t) * ppu) + vec2f(cos(ang), sin(ang)) * spread;
        if (!on_screen(sp)) {
            continue;
        }
        let sd = load_depth(vec2i(sp));
        if (is_background(sd)) {
            continue;
        }
        let gap = view_z(sd) - c.z;
        if (gap > 4.0) {
            // Layers very far in front still cast, a bit lighter.
            occ += (1.0 - 0.35 * smoothstep(200.0, 700.0, gap)) * (1.0 - 0.55 * t);
        }
    }
    return 1.0 - clamp(occ / 16.0 * 2.6, 0.0, 1.0);
}

// Half resolution: r = ambient occlusion, g = key light visibility, b = view distance.
@fragment fn fs_occlusion(in: VertexOutput) -> @location(0) vec4f {
    let p = vec2i(in.pos.xy) * 2;
    let stored = load_depth(p);
    if (is_background(stored)) {
        return vec4f(1.0, 1.0, 1e6, 1.0);
    }
    let c = view_pos(vec2f(p) + 0.5, stored);
    let n = view_normal(p, c);
    let jitter = noise(in.pos.xy);

    var ao = 1.0;
    if (fx.amount.x > 0.0) {
        let radius = fx.amount2.y;
        let rpx = clamp(radius * pixels_per_unit(c.z), 3.0, 128.0);
        var sum = 0.0;
        for (var i = 0; i < 12; i++) {
            let t = (f32(i) + jitter) / 12.0;
            let ang = f32(i) * 2.399963 + jitter * 6.283185;
            let off = vec2f(cos(ang), sin(ang)) * rpx * sqrt(t);
            let s = view_pos_at(p + vec2i(off));
            let v = s - c;
            let len = length(v);
            if (len > 1e-4) {
                let falloff = 1.0 - smoothstep(radius * 0.6, radius * 1.4, len);
                sum += max(0.0, dot(v / len, n) - 0.12) * falloff;
            }
        }
        ao = clamp(1.0 - sum / 12.0 * 1.8, 0.0, 1.0);
    }

    var lit = 1.0;
    if (fx.amount.y > 0.0 && is_ortho()) {
        lit = diorama_shadow(p, c, jitter);
    } else if (fx.amount.y > 0.0) {
        let L = fx.light.xyz;
        let len = fx.amount2.z;
        let start = c + n * (0.001 * -c.z + 0.5);
        for (var i = 1; i <= 16; i++) {
            var t = (f32(i) - jitter) / 16.0;
            t = t * t;
            let rp = start + L * (len * t);
            if (rp.z > -1.0) {
                break;
            }
            let sp = project(rp);
            if (!on_screen(sp)) {
                break;
            }
            let sd = load_depth(vec2i(sp));
            if (is_background(sd)) {
                continue;
            }
            let diff = view_z(sd) - rp.z;
            let bias = 0.001 * -rp.z + 0.6;
            if (diff > bias && diff < fx.misc.z) {
                lit = 0.2 + 0.8 * t;
                break;
            }
        }
    }
    return vec4f(ao, lit, -c.z, 1.0);
}

@fragment fn fs_bright(in: VertexOutput) -> @location(0) vec4f {
    let c = textureSampleLevel(scene, samp, in.uv, 0.0).rgb;
    let l = luma(c);
    // Only highlights brighter than their surroundings bloom; SPM's big white and pastel backdrops
    // would otherwise wash the whole picture out.
    var around = 0.0;
    for (var i = 0; i < 8; i++) {
        let ang = f32(i) * 0.785398;
        around += luma(textureSampleLevel(scene, samp, in.uv + vec2f(cos(ang), sin(ang)) * 12.0 * fx.screen.zw, 0.0).rgb);
    }
    let w = smoothstep(fx.amount2.w, fx.amount2.w + 0.15, l) * smoothstep(0.04, 0.2, l - around / 8.0);
    return vec4f(c * w, 1.0);
}

fn blur(uv: vec2f, dir: vec2f) -> vec4f {
    let texel = dir / vec2f(textureDimensions(aux0)) * 2.0;
    var c = textureSampleLevel(aux0, samp, uv, 0.0).rgb * 0.227027;
    c += textureSampleLevel(aux0, samp, uv + texel * 1.384615, 0.0).rgb * 0.316216;
    c += textureSampleLevel(aux0, samp, uv - texel * 1.384615, 0.0).rgb * 0.316216;
    c += textureSampleLevel(aux0, samp, uv + texel * 3.230769, 0.0).rgb * 0.070270;
    c += textureSampleLevel(aux0, samp, uv - texel * 3.230769, 0.0).rgb * 0.070270;
    return vec4f(c, 1.0);
}

@fragment fn fs_blur_h(in: VertexOutput) -> @location(0) vec4f { return blur(in.uv, vec2f(1.0, 0.0)); }
@fragment fn fs_blur_v(in: VertexOutput) -> @location(0) vec4f { return blur(in.uv, vec2f(0.0, 1.0)); }

// Sun rays, half resolution. Sources are the visible bright sky; anything in front blocks them.
@fragment fn fs_sky(in: VertexOutput) -> @location(0) vec4f {
    let p = vec2i(in.pos.xy) * 2;
    if (!is_background(load_depth(p))) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }
    var l = luma(textureLoad(scene, clamp_px(p), 0).rgb);
    if (fx.stage.x > 1.5 && fx.stage.x < 2.5) {
        // Battle stage: the diorama's sky card is the sky.
        let q = to_stage((vec2f(p) + 0.5) * fx.screen.zw);
        let card = card_at(q);
        l = select(0.0, luma(back_color(card.q)), card.index == 4 && q.y > 0.0 && fx.motif3.z > 0.5);
    }
    return vec4f(smoothstep(0.2, 0.9, l), 0.0, 0.0, 1.0);
}

@fragment fn fs_rays(in: VertexOutput) -> @location(0) vec4f {
    let samples = 40;
    let delta = (in.uv - fx.sun.xy) / f32(samples) * 0.9;
    var uv = in.uv - delta * noise(in.pos.xy);
    var illum = 1.0;
    var sum = 0.0;
    var wsum = 0.0;
    for (var i = 0; i < samples; i++) {
        uv -= delta;
        sum += textureSampleLevel(aux0, samp, uv, 0.0).r * illum;
        wsum += illum;
        illum *= 0.95;
    }
    // How much of the way towards the sun is open sky: scenery in between cuts gaps into the beams.
    return vec4f(sum / wsum, 0.0, 0.0, 1.0);
}

// Stylised crepuscular beams fanning out from the sun, drifting slowly.
fn sun_beams(uv: vec2f) -> f32 {
    let d = (uv - fx.sun.xy) * vec2f(fx.screen.x * fx.screen.w, 1.0);
    let a = atan2(d.y, d.x);
    let t = fx.sun.z;
    let s1 = 0.5 + 0.5 * sin(a * 17.0 + t * 0.15);
    let s2 = 0.5 + 0.5 * sin(a * 31.0 - t * 0.11 + 1.7);
    let s3 = 0.5 + 0.5 * sin(a * 7.0 + t * 0.05 + 0.4);
    let beams = pow(s1 * s3, 2.5) + 0.5 * pow(s2, 6.0);
    // Strongest near the sun, fading across the screen.
    return beams * exp(-length(d) * 0.65);
}

// Depth-aware upsample of the half resolution occlusion, 5x5 taps (also smooths the jittered sampling).
fn occlusion_at(px: vec2f, dist: f32) -> vec2f {
    let hsize = vec2i(textureDimensions(aux0));
    let base = vec2i(floor(px * 0.5));
    var acc = vec2f(0.0);
    var wsum = 0.0;
    for (var y = -2; y <= 2; y++) {
        for (var x = -2; x <= 2; x++) {
            let q = clamp(base + vec2i(x, y), vec2i(0), hsize - vec2i(1));
            let o = textureLoad(aux0, q, 0);
            let spatial = exp(-f32(x * x + y * y) * 0.3);
            let w = spatial * exp(-abs(o.b - dist) / (0.03 * dist + 0.5)) + 1e-4;
            acc += o.rg * w;
            wsum += w;
        }
    }
    return acc / wsum;
}

// Blurred scene colour: glossy (not mirror) reflections.
fn sample_soft(uv: vec2f, radius: f32) -> vec3f {
    let r = radius * fx.screen.zw;
    var c = textureSampleLevel(scene, samp, uv, 0.0).rgb * 0.36;
    c += textureSampleLevel(scene, samp, uv + vec2f(r.x, 0.0), 0.0).rgb * 0.16;
    c += textureSampleLevel(scene, samp, uv - vec2f(r.x, 0.0), 0.0).rgb * 0.16;
    c += textureSampleLevel(scene, samp, uv + vec2f(0.0, r.y), 0.0).rgb * 0.16;
    c += textureSampleLevel(scene, samp, uv - vec2f(0.0, r.y), 0.0).rgb * 0.16;
    return c;
}

fn reflection(p: vec2i, c: vec3f, n: vec3f, jitter: f32) -> vec4f {
    let V = view_dir(c);
    let R = reflect(V, n);
    if (R.z > -0.05) {
        return vec4f(0.0);
    }
    let maxDist = fx.amount2.z * 4.0;
    var prev = c;
    for (var i = 1; i <= 24; i++) {
        let t = (f32(i) - jitter) / 24.0;
        let rp = c + R * (maxDist * t * t + 1.0);
        let sp = project(rp);
        if (!on_screen(sp)) {
            return vec4f(0.0);
        }
        let sd = load_depth(vec2i(sp));
        let sz = view_z(sd);
        let diff = sz - rp.z;
        if (!is_background(sd) && diff > 0.0 && diff < maxDist * 0.15 + 4.0) {
            var lo = prev;
            var hi = rp;
            for (var j = 0; j < 5; j++) {
                let mid = (lo + hi) * 0.5;
                let mz = view_z(load_depth(vec2i(project(mid))));
                if (mz > mid.z) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            let hp = project(hi);
            let uv = hp * fx.screen.zw;
            let edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
            let fade = smoothstep(0.0, 0.08, edge) * (1.0 - t * 0.7);
            // Farther hits blur more, like the TTYD remake's soft floor reflections.
            return vec4f(sample_soft(uv, 1.5 + 14.0 * t), fade);
        }
        prev = rp;
    }
    return vec4f(0.0);
}

// 2D glossy ground: SPM's floors are seen edge-on, so their front faces mirror what stands above
// the walking line, fading with depth below it, with a bright lip along the edge.
fn floor_gloss(p: vec2i, c: vec3f, rgb: vec3f) -> vec3f {
    let ppu = pixels_per_unit(c.z);
    let maxDown = clamp(34.0 * ppu, 8.0, 200.0);
    var edge = -1.0;
    for (var i = 1; i <= 28; i++) {
        let dy = maxDown * f32(i) / 28.0;
        if (behind(p - vec2i(0, i32(dy)), c.z, 6.0)) {
            edge = dy;
            break;
        }
    }
    if (edge < 0.0) {
        return rgb;
    }
    // A floor, not a corner or a thin post: open above the edge and floor face below it to both sides.
    let e = i32(edge);
    let side = i32(clamp(10.0 * ppu, 4.0, 48.0));
    let lAbove = behind(p + vec2i(-side, -e - 1), c.z, 6.0);
    let rAbove = behind(p + vec2i(side, -e - 1), c.z, 6.0);
    let lBelow = !behind(p + vec2i(-side, -e + 2), c.z, 6.0);
    let rBelow = !behind(p + vec2i(side, -e + 2), c.z, 6.0);
    if (!(lAbove && rAbove && lBelow && rBelow)) {
        return rgb;
    }
    let edgeY = f32(p.y) - edge;
    let mirror = vec2f(f32(p.x) + 0.5, edgeY - edge);
    if (mirror.y < fx.viewport.y) {
        return rgb;
    }
    let md = load_depth(vec2i(mirror));
    // Things standing on this floor reflect clearly; the distant backdrop only faintly.
    let standing = select(0.0, 1.0 - smoothstep(120.0, 360.0, c.z - view_z(md)), !is_background(md));
    let weight = mix(0.35, 1.0, standing);
    let fall = 1.0 - edge / maxDown;
    let refl = sample_soft(mirror * fx.screen.zw, 1.0 + edge * 0.12);
    var out = mix(rgb, refl, clamp(fx.amount.z * 0.95 * fall * fall * weight, 0.0, 0.8));
    // The lip catches the light; the drawn black outline along the edge stays black.
    let lip = (1.0 - smoothstep(0.0, max(4.0 * ppu, 3.0), edge)) * smoothstep(0.12, 0.35, luma(rgb));
    out = out + (vec3f(1.0) - out) * (fx.amount.z * 0.45 * lip);
    return out;
}

// --- Lost Pages battle stage and test room ---------------------------------------------------------

// SPM's digital void: black with a faint green grid.
fn digital_void(uv: vec2f) -> vec3f {
    let px = uv * fx.screen.xy / (fx.screen.y / 480.0);
    let g = abs(fract(px / 24.0) - 0.5);
    let line = 1.0 - smoothstep(0.0, 0.06, min(g.x, g.y));
    let glow = 0.5 + 0.5 * sin(uv.y * 40.0 - fx.stage.y * 3.0);
    return vec3f(0.01, 0.03, 0.02) + vec3f(0.05, 0.35, 0.12) * line * (0.6 + 0.4 * glow);
}

fn ease_out_back(t: f32) -> f32 {
    let c1 = 1.9;
    let x = t - 1.0;
    return 1.0 + (c1 + 1.0) * x * x * x + c1 * x * x;
}

// Battle stages are pop-up dioramas of the area, not copies of the spot: paper cards made of the things typical of
// it (Lineland's block trees and doodled sky, Gloam Valley's curly vines on white paper, Castle Bleck's line art,
// ...), chosen by `fx.motif` and coloured by `fx.palette`. Cards: 0 the wings at the screen's sides, 1 near props,
// 2 mid props, 3 the far skyline, 4 the back (sky or wall); below the walking line, the ground. Hinged on the walking
// line, the far cards stand up first.
// Stage space is in game pixels (480 per screen height): x from the screen's centre, y up from the walking line.
// palette: 0 back top, 1 back bottom, 2 far, 3 mid, 4 near, 5 ground top, 6 ground face, 7 accent (trunks, roofs),
//          8 detail (doodles, stars, grid lines), 9 glow (windows, flames, neon)
// motif: back (see back_color), far (see sd_far), mid and near (see stage_prop)
// motif2: ground (see ground_color), preset (0 the test room)
// motif3: style (0 paper, 1 line art, 2 neon, 3 pixel art), wing prop, open sky (sun rays), unused

fn game_px() -> f32 { return fx.screen.y / 480.0; }

fn to_stage(uv: vec2f) -> vec2f {
    return vec2f((uv.x - 0.5) * fx.screen.x / fx.screen.y * 480.0, (fx.stage.z - uv.y) * 480.0);
}

fn from_stage(q: vec2f) -> vec2f {
    return vec2f(0.5 + q.x / 480.0 * fx.screen.y / fx.screen.x, fx.stage.z - q.y / 480.0);
}

fn is_test_stage() -> bool { return fx.motif2.y < 0.5; }

fn stage_style() -> i32 { return i32(fx.motif3.x); }

fn hash11(n: f32) -> f32 { return fract(sin(n * 127.1 + 311.7) * 43758.5453); }

fn hash21(c: vec2f) -> f32 { return hash11(c.x * 17.13 + c.y * 91.7); }

fn pixelate(p: vec2f, size: f32) -> vec2f { return (floor(p / size) + 0.5) * size; }

fn sd_box(p: vec2f, c: vec2f, h: vec2f) -> f32 {
    let d = abs(p - c) - h;
    return length(max(d, vec2f(0.0))) + min(max(d.x, d.y), 0.0);
}

fn sd_circle(p: vec2f, c: vec2f, r: f32) -> f32 { return length(p - c) - r; }

fn sd_ring(p: vec2f, c: vec2f, r: f32, w: f32) -> f32 { return abs(length(p - c) - r) - w; }

fn sd_ellipse(p: vec2f, c: vec2f, r: vec2f) -> f32 { return (length((p - c) / r) - 1.0) * min(r.x, r.y); }

fn sd_seg(p: vec2f, a: vec2f, b: vec2f, r: f32) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// A triangle standing on y = base.
fn sd_peak(p: vec2f, base: f32, half: f32, height: f32) -> f32 {
    let t = (p.y - base) / height;
    return max(max(abs(p.x) - half * (1.0 - t), base - p.y), p.y - base - height);
}

fn sd_diamond(p: vec2f, c: vec2f, s: f32) -> f32 { return (abs(p.x - c.x) + abs(p.y - c.y) - s) * 0.7071; }

// A five-pointed star.
fn sd_star(p: vec2f, c: vec2f, r: f32, spin: f32) -> f32 {
    let d = p - c;
    let k = 0.5 + 0.5 * cos(5.0 * (atan2(d.y, d.x) + spin));
    return length(d) - r * mix(0.45, 1.0, pow(k, 3.0));
}

// Union of (distance, part) pairs. Parts: 0 the card's colour, 1 accent, 2 a lighter card colour, 3 glow.
fn sd_u(a: vec2f, b: vec2f) -> vec2f { return select(b, a, a.x < b.x); }

// One prop standing at the origin.
//  0 none, 1 round tree, 2 rock spire, 3 cactus, 4 pillar, 5 block stack, 6 crystal, 7 pine, 8 lamp post,
//  9 gloam tree, 10 cloud puff, 11 bush, 12 rock, 13 grass tuft, 14 block, 15 block tree, 16 palm, 17 curly vine,
// 18 wall torch, 19 neon swirl plant, 20 black bamboo, 21 mandala, 22 floating diamonds, 23 dead tree,
// 24 pixel tree, 25 asteroid, 26 window panel
fn stage_prop(kind: i32, p: vec2f, seed: f32) -> vec2f {
    if (kind == 1 || kind == 24) {
        var q = p;
        if (kind == 24) {
            q = pixelate(p, 8.0);
        }
        let crown = min(sd_circle(q, vec2f(0.0, 72.0), 30.0),
                        min(sd_circle(q, vec2f(-20.0, 56.0), 20.0), sd_circle(q, vec2f(20.0, 56.0), 20.0)));
        return sd_u(vec2f(sd_box(q, vec2f(0.0, 26.0), vec2f(6.0, 26.0)), 1.0), vec2f(crown, 0.0));
    }
    if (kind == 2) {
        return vec2f(sd_peak(p, 0.0, 28.0, 110.0 + 40.0 * seed), 0.0);
    }
    if (kind == 3) {
        var d = min(sd_box(p, vec2f(0.0, 40.0), vec2f(7.0, 38.0)) - 2.0, sd_box(p, vec2f(-17.0, 54.0), vec2f(4.0, 12.0)) - 1.0);
        d = min(d, sd_box(p, vec2f(-10.0, 42.0), vec2f(8.0, 4.0)));
        d = min(d, sd_box(p, vec2f(17.0, 64.0), vec2f(4.0, 10.0)) - 1.0);
        d = min(d, sd_box(p, vec2f(10.0, 54.0), vec2f(8.0, 4.0)));
        return vec2f(d, 0.0);
    }
    if (kind == 4) {
        let h = 150.0 + 60.0 * seed;
        var d = sd_box(p, vec2f(0.0, h * 0.5), vec2f(13.0, h * 0.5));
        d = min(d, sd_box(p, vec2f(0.0, h), vec2f(22.0, 7.0)));
        d = min(d, sd_box(p, vec2f(0.0, 6.0), vec2f(22.0, 6.0)));
        return vec2f(d, 0.0);
    }
    if (kind == 5) {
        let h = 36.0 * (1.0 + floor(seed * 3.0));
        return vec2f(sd_box(p, vec2f(0.0, h * 0.5), vec2f(18.0, h * 0.5)), 0.0);
    }
    if (kind == 6) {
        return vec2f((abs(p.x) / 18.0 + abs(p.y - 55.0) / 55.0 - 1.0) * 16.0, 3.0);
    }
    if (kind == 7) {
        var d = vec2f(sd_box(p, vec2f(0.0, 12.0), vec2f(5.0, 12.0)), 1.0);
        for (var k = 0; k < 3; k++) {
            d = sd_u(d, vec2f(sd_peak(p, 18.0 + 26.0 * f32(k), 34.0 - 8.0 * f32(k), 44.0), 0.0));
        }
        return d;
    }
    if (kind == 8) {
        return sd_u(vec2f(sd_box(p, vec2f(0.0, 55.0), vec2f(3.0, 55.0)), 1.0), vec2f(sd_circle(p, vec2f(0.0, 118.0), 11.0), 3.0));
    }
    if (kind == 9) {
        return sd_u(vec2f(sd_box(p, vec2f(0.0, 42.0), vec2f(5.0, 42.0)), 1.0),
                    vec2f(sd_ellipse(p, vec2f(0.0, 88.0), vec2f(38.0, 18.0)), 0.0));
    }
    if (kind == 10) {
        return vec2f(min(sd_circle(p, vec2f(0.0, 26.0), 22.0),
                         min(sd_circle(p, vec2f(-22.0, 16.0), 16.0), sd_circle(p, vec2f(22.0, 16.0), 16.0))), 0.0);
    }
    if (kind == 11) {
        let d = min(sd_circle(p, vec2f(0.0, 8.0), 17.0), min(sd_circle(p, vec2f(-15.0, 2.0), 12.0), sd_circle(p, vec2f(15.0, 2.0), 12.0)));
        return vec2f(max(d, -p.y), 0.0);
    }
    if (kind == 12) {
        return vec2f(max(sd_ellipse(p, vec2f(0.0, 0.0), vec2f(24.0, 15.0)), -p.y), 0.0);
    }
    if (kind == 13) {
        var d = sd_peak(p - vec2f(-8.0, 0.0), 0.0, 4.0, 16.0);
        d = min(d, sd_peak(p, 0.0, 4.0, 22.0));
        d = min(d, sd_peak(p - vec2f(8.0, 0.0), 0.0, 4.0, 14.0));
        return vec2f(d, 0.0);
    }
    if (kind == 14) {
        return vec2f(sd_box(p, vec2f(0.0, 12.0), vec2f(12.0, 12.0)), 0.0);
    }
    if (kind == 15) {  // Lineland's tree: a crown of overlapping squares in greens with a pink one
        var d = vec2f(sd_box(p, vec2f(0.0, 34.0), vec2f(5.0, 34.0)), 1.0);
        d = sd_u(d, vec2f(sd_box(p, vec2f(-20.0, 74.0), vec2f(20.0, 20.0)), 0.0));
        d = sd_u(d, vec2f(sd_box(p, vec2f(18.0, 82.0), vec2f(19.0, 19.0)), 2.0));
        d = sd_u(d, vec2f(sd_box(p, vec2f(-4.0, 104.0), vec2f(17.0, 17.0)), select(0.0, 3.0, seed > 0.5)));
        d = sd_u(d, vec2f(sd_box(p, vec2f(20.0, 120.0), vec2f(12.0, 12.0)), 2.0));
        return d;
    }
    if (kind == 16) {  // palm: a curved trunk and fronds
        var trunk = sd_seg(p, vec2f(0.0, 0.0), vec2f(6.0, 45.0), 5.0);
        trunk = min(trunk, sd_seg(p, vec2f(6.0, 45.0), vec2f(16.0, 90.0), 4.5));
        trunk = min(trunk, sd_seg(p, vec2f(16.0, 90.0), vec2f(20.0, 118.0), 4.0));
        let top = vec2f(20.0, 120.0);
        var fronds = sd_seg(p, top, vec2f(-18.0, 104.0), 5.0);
        fronds = min(fronds, sd_seg(p, top, vec2f(-4.0, 134.0), 5.0));
        fronds = min(fronds, sd_seg(p, top, vec2f(30.0, 140.0), 5.0));
        fronds = min(fronds, sd_seg(p, top, vec2f(54.0, 116.0), 5.0));
        fronds = min(fronds, sd_seg(p, top, vec2f(48.0, 96.0), 4.0));
        return sd_u(vec2f(trunk, 1.0), vec2f(fronds, 0.0));
    }
    if (kind == 17) {  // Gloam Valley's curly vine
        var d = sd_seg(p, vec2f(0.0, 0.0), vec2f(-2.0, 70.0), 3.0);
        d = min(d, max(sd_ring(p, vec2f(10.0, 78.0), 12.0, 2.5), -sd_box(p, vec2f(16.0, 70.0), vec2f(8.0, 8.0))));
        d = min(d, sd_ring(p, vec2f(14.0, 80.0), 5.0, 2.0));
        d = min(d, sd_seg(p, vec2f(-1.0, 40.0), vec2f(-14.0, 52.0), 2.0));
        d = min(d, sd_ring(p, vec2f(-18.0, 58.0), 7.0, 2.0));
        return vec2f(d, 0.0);
    }
    if (kind == 18) {  // a torch on the wall
        var d = vec2f(min(sd_box(p, vec2f(0.0, 108.0), vec2f(3.0, 10.0)), sd_box(p, vec2f(0.0, 120.0), vec2f(9.0, 4.0))), 1.0);
        d = sd_u(d, vec2f(sd_ellipse(p, vec2f(0.0, 134.0), vec2f(6.0, 11.0)), 3.0));
        return d;
    }
    if (kind == 19) {  // Planet Blobule's neon swirls
        var d = vec2f(min(sd_seg(p, vec2f(0.0, 0.0), vec2f(4.0, 60.0), 2.0), sd_seg(p, vec2f(2.0, 30.0), vec2f(-8.0, 36.0), 1.6)), 0.0);
        d = sd_u(d, vec2f(min(sd_ring(p, vec2f(14.0, 72.0), 12.0, 2.5), sd_ring(p, vec2f(-12.0, 40.0), 6.0, 2.0)), 3.0));
        return d;
    }
    if (kind == 20) {  // Sammer's black bamboo: thin stalks with leaf blades
        var d = sd_seg(p, vec2f(0.0, 0.0), vec2f(-8.0, 160.0), 4.5);
        d = min(d, sd_seg(p, vec2f(4.0, 0.0), vec2f(16.0, 125.0), 4.0));
        d = min(d, sd_seg(p, vec2f(-3.0, 0.0), vec2f(-24.0, 100.0), 3.6));
        for (var k = 0; k < 6; k++) {
            let t = 0.45 + 0.1 * f32(k);
            let side = select(-1.0, 1.0, (k & 1) == 0);
            let at = mix(vec2f(0.0, 0.0), vec2f(-8.0, 160.0), t);
            d = min(d, sd_seg(p, at, at + vec2f(side * 20.0, 6.0 - 8.0 * f32(k % 3)), 4.4));
            let at2 = mix(vec2f(4.0, 0.0), vec2f(16.0, 125.0), t);
            d = min(d, sd_seg(p, at2, at2 + vec2f(-side * 16.0, 4.0), 4.0));
        }
        return vec2f(d, 0.0);
    }
    if (kind == 21) {  // line-art mandala
        let c = vec2f(0.0, 170.0 + 50.0 * seed);
        var d = min(sd_ring(p, c, 34.0, 1.2), sd_ring(p, c, 22.0, 1.2));
        for (var k = 0; k < 8; k++) {
            let a = f32(k) * 0.785398 + seed;
            d = min(d, sd_seg(p, c + vec2f(cos(a), sin(a)) * 10.0, c + vec2f(cos(a), sin(a)) * 34.0, 1.0));
        }
        return vec2f(d, 0.0);
    }
    if (kind == 22) {
        var d = sd_diamond(p, vec2f(0.0, 80.0), 14.0);
        d = min(d, sd_diamond(p, vec2f(28.0, 112.0), 11.0));
        d = min(d, sd_diamond(p, vec2f(-16.0, 140.0), 9.0));
        return vec2f(d, 0.0);
    }
    if (kind == 23) {  // a bare tree
        var d = sd_seg(p, vec2f(0.0, 0.0), vec2f(2.0, 92.0), 5.0);
        d = min(d, sd_seg(p, vec2f(1.0, 58.0), vec2f(-24.0, 88.0), 3.0));
        d = min(d, sd_seg(p, vec2f(2.0, 74.0), vec2f(26.0, 104.0), 3.0));
        d = min(d, sd_seg(p, vec2f(-14.0, 76.0), vec2f(-20.0, 104.0), 2.0));
        return vec2f(d, 1.0);
    }
    if (kind == 25) {  // a floating asteroid
        let c = vec2f(0.0, 150.0 + 60.0 * seed);
        let d = p - c;
        let r = 20.0 + 8.0 * seed + 5.0 * sin(atan2(d.y, d.x) * 5.0 + seed * 20.0);
        return vec2f(length(d) - r, 0.0);
    }
    if (kind == 26) {  // a glowing window of square panes
        let c = vec2f(0.0, 210.0);
        let box = sd_box(p, c, vec2f(44.0, 44.0));
        let g = abs(fract((p - c + 44.0) / 22.0) - 0.5) * 22.0;
        return vec2f(box, select(3.0, 1.0, min(g.x, g.y) < 2.0 || box > -3.0));
    }
    return vec2f(1e5, 0.0);
}

// A row of props repeating every `period`, each one a little different.
fn prop_row(kind: i32, q: vec2f, period: f32, shift: f32, scale: f32) -> vec2f {
    if (kind == 0) {
        return vec2f(1e5, 0.0);
    }
    let x = q.x + shift;
    let cell = floor(x / period);
    var best = vec2f(1e5, 0.0);
    for (var k = -1; k <= 1; k++) {  // the neighbours too: props can be wider than their cell
        let c = cell + f32(k);
        let seed = hash11(c + shift);
        let s = scale * (0.8 + 0.4 * seed);
        let at = (c + 0.5 + (seed - 0.5) * 0.4) * period;
        let d = stage_prop(kind, vec2f(x - at, q.y) / s, seed);
        best = sd_u(best, vec2f(d.x * s, d.y));
    }
    return best;
}

// The far skyline: 0 hills, 1 mountains, 2 dunes, 3 blocky steps, 4 battlements, 5 glowing arched windows,
// 6 a cave, 7 planets, 8 none, 9 Lineland's striped domes, 10 pixel mountains, 11 the Cragnons' rock spires,
// 12 Sammer's pagodas, 13 a bank of clouds, 14 Mount Lineland's stepped cliffs, 15 pixel coral, 16 wooden mine
// beams, 17 Gloam Valley's white blocks.
fn sd_far(q: vec2f) -> vec2f {
    let shape = i32(fx.motif.y);
    let x = q.x;
    var h = 0.0;
    if (shape == 0) {
        h = 70.0 + 30.0 * sin(x * 0.010 + 1.0) + 18.0 * sin(x * 0.027 + 2.0);
    } else if (shape == 1) {
        let t = abs(fract(x / 240.0) * 2.0 - 1.0);
        h = 70.0 + 150.0 * (1.0 - t) + 12.0 * abs(sin(x * 0.06));
    } else if (shape == 2) {
        h = 45.0 + 25.0 * sin(x * 0.007) + 12.0 * sin(x * 0.018 + 1.3);
    } else if (shape == 3) {
        let bx = floor(x / 24.0) * 24.0;
        h = floor((70.0 + 40.0 * sin(bx * 0.008 + 0.5) + 25.0 * sin(bx * 0.021)) / 24.0) * 24.0;
    } else if (shape == 4) {
        h = 130.0 + select(0.0, 16.0, fract(x / 36.0) < 0.5) + select(0.0, 70.0, fract(x / 320.0 + 0.1) < 0.18);
    } else if (shape == 5) {
        let period = 170.0;
        let p = vec2f(x - (floor(x / period) + 0.5) * period, q.y);
        let window = min(sd_box(p, vec2f(0.0, 130.0), vec2f(30.0, 55.0)), sd_circle(p, vec2f(0.0, 185.0), 30.0));
        let frame = min(sd_box(p, vec2f(0.0, 130.0), vec2f(36.0, 55.0)), sd_circle(p, vec2f(0.0, 185.0), 36.0));
        return vec2f(frame, select(1.0, 3.0, window < 0.0));  // glowing windows in their frames
    } else if (shape == 6) {
        let top = 300.0 - 50.0 * abs(sin(x * 0.021)) - 80.0 * pow(abs(sin(x * 0.013 + 0.7)), 6.0);
        let rocks = 30.0 + 18.0 * sin(x * 0.015);
        return vec2f(min(top - q.y, q.y - rocks) * 0.7, 0.0);
    } else if (shape == 7) {
        var d = sd_circle(q, vec2f(-230.0, 240.0), 70.0);
        d = min(d, sd_circle(q, vec2f(260.0, 300.0), 40.0));
        d = min(d, sd_circle(q, vec2f(70.0, 340.0), 22.0));
        return vec2f(d, 0.0);
    } else if (shape == 9) {
        var d = 1e5;
        let cell = floor(x / 120.0);
        for (var k = -1; k <= 1; k++) {
            let c = cell + f32(k);
            let seed = hash11(c + 3.0);
            d = min(d, sd_ellipse(vec2f(x - (c + 0.5) * 120.0, q.y), vec2f(0.0), vec2f(64.0 + 14.0 * seed, 70.0 + 50.0 * seed)));
        }
        return vec2f(d, 0.0);
    } else if (shape == 10) {
        let px = floor(x / 16.0) * 16.0;
        let t = abs(fract(px / 300.0) * 2.0 - 1.0);
        let t2 = abs(fract(px / 190.0 + 0.3) * 2.0 - 1.0);
        h = floor(max(40.0 + 170.0 * (1.0 - t), 30.0 + 110.0 * (1.0 - t2)) / 16.0) * 16.0;
        return vec2f(q.y - h, select(0.0, 2.0, q.y > h - 24.0 && t < 0.3));  // snowy tips
    } else if (shape == 11) {
        var d = 1e5;
        let cell = floor(x / 110.0);
        for (var k = -1; k <= 1; k++) {
            let c = cell + f32(k);
            let seed = hash11(c + 9.0);
            let lx = x - (c + 0.5 + (seed - 0.5) * 0.3) * 110.0;
            let top = 150.0 + 150.0 * seed;
            let w = 16.0 + 12.0 * hash11(c + 1.0);
            d = min(d, min(sd_box(vec2f(lx, q.y), vec2f(0.0, top * 0.5), vec2f(w, top * 0.5)), sd_circle(vec2f(lx, q.y), vec2f(0.0, top), w)));
        }
        return vec2f(d, 0.0);
    } else if (shape == 12) {
        let period = 280.0;
        let p = vec2f(x - (floor(x / period) + 0.5) * period, q.y);
        let wall = sd_box(p, vec2f(0.0, 40.0), vec2f(140.0, 40.0));
        let body = sd_box(p, vec2f(0.0, 90.0), vec2f(64.0, 90.0));
        var roof = sd_peak(p, 150.0, 110.0, 60.0);
        roof = min(roof, sd_peak(p, 200.0, 70.0, 46.0));
        roof = min(roof, sd_seg(p, vec2f(0.0, 230.0), vec2f(0.0, 290.0), 4.0));
        return sd_u(vec2f(min(wall, body), 0.0), vec2f(roof, 1.0));
    } else if (shape == 13) {
        var d = 1e5;
        let cell = floor(x / 70.0);
        for (var k = -1; k <= 1; k++) {
            let c = cell + f32(k);
            let seed = hash11(c + 5.0);
            d = min(d, sd_circle(vec2f(x - (c + 0.5) * 70.0, q.y), vec2f(0.0, 20.0 + 25.0 * seed), 38.0 + 18.0 * seed));
        }
        return vec2f(d, 0.0);
    } else if (shape == 14) {
        let t = fract(x / 420.0);
        h = 50.0 + floor(t * 7.0) * 30.0;
        return vec2f(q.y - h, select(0.0, 2.0, q.y > h - 8.0));  // each step's lit top
    } else if (shape == 15) {
        let period = 130.0;
        let cell = floor(x / period);
        let seed = hash11(cell + 2.0);
        let p = pixelate(vec2f(x - (cell + 0.5) * period, q.y), 8.0);
        var d = sd_box(p, vec2f(0.0, 40.0 + 20.0 * seed), vec2f(10.0, 40.0 + 20.0 * seed));
        d = min(d, sd_box(p, vec2f(-20.0, 70.0), vec2f(6.0, 22.0)));
        d = min(d, sd_box(p, vec2f(-12.0, 52.0), vec2f(10.0, 4.0)));
        d = min(d, sd_box(p, vec2f(20.0, 90.0 * (0.7 + seed * 0.5)), vec2f(6.0, 20.0)));
        d = min(d, sd_box(p, vec2f(12.0, 66.0), vec2f(10.0, 4.0)));
        return vec2f(d, 0.0);
    } else if (shape == 16) {
        let period = 260.0;
        let p = vec2f(x - floor(x / period) * period, q.y);
        var d = min(sd_box(p, vec2f(0.0, 200.0), vec2f(9.0, 200.0)), sd_box(p, vec2f(period, 200.0), vec2f(9.0, 200.0)));
        d = min(d, sd_box(p, vec2f(period * 0.5, 230.0), vec2f(period * 0.5, 8.0)));
        d = min(d, sd_seg(p, vec2f(0.0, 70.0), vec2f(period * 0.5, 222.0), 6.0));
        d = min(d, sd_seg(p, vec2f(period, 70.0), vec2f(period * 0.5, 222.0), 6.0));
        return vec2f(d, 0.0);
    } else if (shape == 17) {
        var d = 1e5;
        let cell = floor(x / 210.0);
        for (var k = -1; k <= 1; k++) {
            let c = cell + f32(k);
            let seed = hash11(c + 4.0);
            let at = vec2f((c + 0.5) * 210.0, 50.0 + 60.0 * seed);
            d = min(d, sd_box(q, at, vec2f(60.0 + 25.0 * seed, 50.0 + 40.0 * seed)) - 10.0);
        }
        return vec2f(d, 0.0);
    } else {
        return vec2f(1e5, 0.0);
    }
    return vec2f((q.y - h) * 0.8, 0.0);
}

// The back card: the sky, or the room's back wall.
//  0 plain, 1 Lineland's doodled sky, 2 stars, 3 round clouds, 4 bricks, 5 a pixel sky, 6 Outer Space's star
//  shapes, 7 Castle Bleck's line art, 9 white paper, 10 the void, 11 the Overthere's circles, 12 cave rock,
// 13 tree rings, 14 pixel water, 15 wallpaper, 16 a dark grid
fn back_color(q: vec2f) -> vec3f {
    let motif = i32(fx.motif.x);
    let top = fx.palette[0].rgb;
    let bottom = fx.palette[1].rgb;
    let detail = fx.palette[8].rgb;
    var c = mix(bottom, top, smoothstep(0.0, 380.0, q.y));
    if (motif == 1) {  // outlined rectangle clouds and maths scribbles
        let cell = floor(q / vec2f(170.0, 105.0));
        let seed = hash21(cell);
        let at = (cell + vec2f(0.5, 0.5) + (vec2f(seed, hash11(seed * 9.0)) - 0.5) * 0.4) * vec2f(170.0, 105.0);
        let size = vec2f(30.0 + 22.0 * seed, 10.0 + 5.0 * seed);
        var line = abs(sd_box(q, at, size)) - 1.1;
        line = min(line, abs(sd_box(q, at + vec2f(12.0, 8.0), size * 0.8)) - 1.1);
        let s = q - at - vec2f(-60.0, -30.0);
        line = min(line, select(1e5, min(abs(s.x) - 1.0, abs(s.y) - 1.0), max(abs(s.x), abs(s.y)) < 5.0));  // a plus
        c = mix(c, detail, (1.0 - smoothstep(-0.5, 0.8, line)) * 0.75 * step(80.0, q.y));
    } else if (motif == 2 || motif == 6) {
        if (motif == 6) {  // big dark star silhouettes
            let cell = floor(q / 90.0);
            let seed = hash21(cell + 3.0);
            let at = (cell + 0.5 + (vec2f(seed, hash11(seed * 7.0)) - 0.5) * 0.5) * 90.0;
            c = mix(c, detail, 1.0 - smoothstep(-0.5, 0.8, sd_star(q, at, 18.0 + 16.0 * seed, seed * 6.0)));
        }
        let cell = floor(q / 26.0);
        let h = hash21(cell);
        if (h > 0.86) {
            let at = (cell + 0.25 + 0.5 * vec2f(h, hash11(h * 31.0))) * 26.0;
            let twinkle = 0.6 + 0.4 * sin(fx.stage.y * 3.0 + h * 40.0);
            c = mix(c, vec3f(1.0, 0.98, 0.88), (1.0 - smoothstep(0.6, 1.8, length(q - at))) * twinkle);
        }
    } else if (motif == 3 || motif == 5) {
        var p = q;
        if (motif == 5) {
            p = pixelate(q, 8.0);
            c = mix(bottom, top, floor(smoothstep(0.0, 380.0, p.y) * 6.0) / 6.0);  // banded, like a pixel sky
        }
        let period = 230.0;
        let x = p.x + fx.stage.y * 6.0;
        let cell = floor(x / period);
        var d = 1e5;
        for (var k = -1; k <= 1; k++) {
            let seed = hash11(cell + f32(k) + 7.0);
            let local = vec2f(x - (cell + f32(k) + 0.5) * period, p.y - 220.0 - 100.0 * seed);
            d = min(d, stage_prop(10, local / 1.5, seed).x * 1.5);
        }
        c = mix(c, detail, (1.0 - smoothstep(-1.0, 0.5, d)) * 0.95);
        if (stage_style() == 3) {
            let g = abs(fract(q / 16.0) - 0.5) * 16.0;
            c = c * select(1.0, 0.94, min(g.x, g.y) < 0.6);
        }
    } else if (motif == 4) {  // bricks, each a slightly different shade
        let row = floor(q.y / 28.0);
        let bx = (q.x + row * 28.0) / 56.0;
        let shade = 0.92 + 0.12 * hash21(vec2f(floor(bx), row));
        c = top * shade * (0.8 + 0.2 * smoothstep(0.0, 300.0, q.y));
        let mortar = min(fract(bx) * 56.0, fract(q.y / 28.0) * 28.0) < 2.5;
        c = select(c, bottom, mortar);
    } else if (motif == 7) {  // white line art on black
        let period = 250.0;
        let lx = q.x - (floor(q.x / period) + 0.5) * period;
        let kink = clamp(q.y - 200.0, 0.0, 60.0);
        var line = abs(lx - 60.0 + kink) - 1.2;
        line = min(line, abs(lx + 80.0) - 1.0);
        let center = vec2f((floor(q.x / 520.0) + 0.5) * 520.0 + 110.0, 300.0);
        line = min(line, min(sd_ring(q, center, 40.0, 1.0), sd_ring(q, center, 28.0, 1.0)));
        line = min(line, abs(sd_diamond(q, center, 40.0)) - 1.0);
        c = mix(c, detail, 1.0 - smoothstep(0.0, 1.2, max(line, -1.0)));
    } else if (motif == 9) {  // paper
        c = c * (0.985 + 0.015 * noise(floor(q * 0.7)));
    } else if (motif == 10) {  // the void: purple swirls with dark squares drifting
        let swirl = 0.5 + 0.5 * sin(q.x * 0.012 + sin(q.y * 0.017) * 2.0 + fx.stage.y * 0.2);
        c = mix(bottom, top, clamp(swirl * smoothstep(0.0, 300.0, q.y + 80.0), 0.0, 1.0));
        let cell = floor((q + vec2f(fx.stage.y * 4.0, 0.0)) / 64.0);
        let seed = hash21(cell);
        if (seed > 0.55) {
            let at = (cell + 0.5) * 64.0 - vec2f(fx.stage.y * 4.0, 0.0);
            c = select(c, detail, sd_box(q, at, vec2f(4.0 + 10.0 * seed)) < 0.0);
        }
    } else if (motif == 11) {  // the Overthere's drawn circles
        let cell = floor(q / 200.0);
        let seed = hash21(cell + 1.0);
        let at = (cell + 0.5) * 200.0 + (vec2f(seed, hash11(seed * 3.0)) - 0.5) * 60.0;
        let ring = min(sd_ring(q, at, 30.0 + 40.0 * seed, 0.9), sd_ring(q, at, 18.0 + 25.0 * seed, 0.9));
        c = mix(c, detail, (1.0 - smoothstep(0.0, 1.0, ring)) * 0.7);
    } else if (motif == 12) {  // cave rock: rounded stones with dark seams
        let cell = floor(q / 34.0);
        let local = q - (cell + 0.5) * 34.0 + (vec2f(hash21(cell), hash21(cell + 7.0)) - 0.5) * 8.0;
        let stone = sd_box(local, vec2f(0.0), vec2f(12.0, 12.0)) - 4.0;
        c = select(bottom, mix(top, detail, hash21(cell + 2.0) * 0.6), stone < 0.0);
    } else if (motif == 13) {  // the inside of a tree: rings
        let r = length((q - vec2f(0.0, 260.0)) * vec2f(1.0, 1.3));
        let band = floor(fract(r / 36.0) * 3.0);
        c = mix(top, bottom, band / 2.0);
    } else if (motif == 14) {  // pixel water
        let p = pixelate(q, 8.0);
        c = mix(bottom, top, floor(smoothstep(0.0, 380.0, p.y) * 5.0) / 5.0);
        let cell = floor(p / vec2f(56.0, 24.0));
        if (hash21(cell) > 0.65 && fract(p.y / 24.0) < 0.34 && fract(p.x / 56.0) < 0.4) {
            c = detail;
        }
    } else if (motif == 15) {  // wallpaper with a wainscot
        let stripe = fract(q.x / 44.0);
        let diamond = abs(stripe - 0.5) * 44.0 + abs(fract(q.y / 44.0) - 0.5) * 44.0 < 9.0;
        c = select(top, mix(top, detail, 0.35), diamond);
        c = select(c, bottom, q.y < 80.0);
        c = select(c, detail * 0.8, abs(q.y - 80.0) < 3.0);
    } else if (motif == 16) {  // a dark grid
        let g = abs(fract(q / 24.0) - 0.5) * 24.0;
        c = select(c, detail, min(g.x, g.y) < 0.9);
    }
    return c;
}

// Signed distance of card i at stage point q (negative inside) and which part.
fn card_sd(i: i32, q0: vec2f) -> vec2f {
    if (is_test_stage()) {
        return vec2f(1e5, 0.0);
    }
    var q = q0;
    if (stage_style() == 3) {
        q = pixelate(q0, 4.0);
    }
    if (i == 0) {  // wings: big props framing the sides, like a stage set
        let hw = 0.5 * fx.screen.x / fx.screen.y * 480.0;
        let kind = i32(fx.motif3.y);
        let l = stage_prop(kind, (q - vec2f(-hw + 26.0, 0.0)) / 1.8, 0.3);
        let r = stage_prop(kind, (q - vec2f(hw - 26.0, 0.0)) / 1.8, 0.8);
        return vec2f(min(l.x, r.x) * 1.8, select(r.y, l.y, l.x < r.x));
    }
    if (i == 1) {
        return prop_row(i32(fx.motif.w), q, 125.0, 37.0, 1.0);
    }
    if (i == 2) {
        return prop_row(i32(fx.motif.z), q, 190.0, 0.0, 1.0);
    }
    return sd_far(q);
}

fn card_lift(i: i32) -> f32 {
    let k = clamp((fx.stage.y - 0.05 - 0.11 * f32(4 - i)) / 0.42, 0.0, 1.0);
    return select(ease_out_back(k), 0.0, k <= 0.0);
}

// Once up, the cards drift a little against each other, so the stage keeps reading as paper cut-outs.
fn card_sway(i: i32) -> f32 {
    return sin(fx.stage.y * 0.8 + f32(i) * 1.7) * 1.5 * f32(i) * smoothstep(0.8, 1.6, fx.stage.y);
}

struct Card {
    index: i32,  // 5: no card here (yet)
    q: vec2f,    // the point on the card
    part: f32,
    d: f32,
};

// The nearest card covering stage point q (above the walking line).
fn card_at(q: vec2f) -> Card {
    for (var i = 0; i < 4; i++) {
        let lift = card_lift(i);
        if (lift <= 0.001) {
            continue;
        }
        let src = vec2f(q.x + card_sway(i), q.y / lift);
        let s = card_sd(i, src);
        if (s.x < 0.0) {
            return Card(i, src, s.y, s.x);
        }
    }
    let lift = card_lift(4);
    let src = vec2f(q.x, q.y / max(lift, 0.001));
    if (lift > 0.001 && src.y < fx.stage.z * 480.0 + 24.0) {  // the back card reaches just past the top
        return Card(4, src, 0.0, -1.0);
    }
    return Card(5, q, 0.0, 0.0);
}

fn card_color(card: Card) -> vec3f {
    if (card.index == 4) {
        return select(back_color(card.q), test_room(from_stage(card.q)), is_test_stage());
    }
    let style = stage_style();
    let glow = card.part > 2.5;
    let colors = array<vec3f, 4>(fx.palette[3].rgb * 0.8, fx.palette[4].rgb, fx.palette[3].rgb, fx.palette[2].rgb);
    var c = colors[card.index];
    if (card.part > 0.5 && card.part < 1.5) {
        c = fx.palette[7].rgb;
    } else if (card.part > 1.5 && card.part < 2.5) {
        c = min(c * 1.22 + 0.04, vec3f(1.0));
    } else if (glow) {
        c = fx.palette[9].rgb;
    }
    if (!glow && style != 1) {
        // Darker at the foot, the far skyline a little hazy, a touch of paper grain.
        c = c * (0.84 + 0.16 * smoothstep(0.0, 160.0, card.q.y));
        if (card.index == 3) {
            c = mix(c, fx.palette[1].rgb, 0.12);
        }
        if (card.index == 3 && i32(fx.motif.y) == 9) {
            c = c * select(1.0, 1.08, fract(card.q.x / 16.0) < 0.35);  // the domes' stripes
        }
        if (card.index == 3 && i32(fx.motif.y) == 14) {
            c = c * select(1.0, 0.9, fract(card.q.x / 12.0 + card.q.y / 24.0) < 0.3);  // the cliffs' strata
        }
        c = c * (0.98 + 0.03 * noise(floor(card.q * 1.5)));
    }
    let settle = mix(0.9, 0.7, smoothstep(0.9, 1.5, fx.stage.y));
    if (style == 1) {  // line art: black cards outlined in white
        return mix(c, fx.palette[8].rgb, smoothstep(-2.6, -1.4, card.d));
    }
    if (style == 2) {  // neon: dark cards with glowing edges
        let edge = smoothstep(-3.5, -0.8, card.d);
        return mix(c, fx.palette[9].rgb, edge);
    }
    if (style == 3) {  // pixel art: a dark outline instead of a cut edge
        return mix(c, c * 0.62, step(-4.0, card.d) * select(1.0, 0.0, glow));
    }
    // Paper: a white edge around each cut-out, brightest while it rises.
    return mix(c, vec3f(0.97, 0.97, 0.94), smoothstep(-2.8, -1.6, card.d) * settle);
}

// Nearer cards drop a soft shadow down-right onto the farther ones.
fn card_shadow(q: vec2f, card: Card) -> f32 {
    if (card.index == 0 || card.index >= 5 || stage_style() == 1) {
        return 1.0;
    }
    var occ = 0.0;
    for (var k = 1; k <= 3; k++) {
        let o = q + vec2f(-1.0, 1.0) * (4.0 * f32(k));  // the light comes from above-left
        for (var j = 0; j < card.index; j++) {
            let lift = card_lift(j);
            if (lift > 0.001 && card_sd(j, vec2f(o.x + card_sway(j), o.y / lift)).x < 0.0) {
                occ += 1.0 - 0.2 * f32(k);
                break;
            }
        }
    }
    return 1.0 - 0.45 * fx.amount.y * clamp(occ / 2.0, 0.0, 1.0);
}

// The ground block's front face below the walking line.
//  0 grass, 1 bricks, 2 checker, 3 sand, 4 tiles, 5 rock, 6 wooden planks, 7 cloud, 8 a white line on black,
//  9 pixel grass on bricks, 10 stone slabs
fn ground_color(q: vec2f) -> vec3f {
    if (is_test_stage()) {
        return test_room(from_stage(q));
    }
    let depth = -q.y;
    let pattern = i32(fx.motif2.x);
    let top = fx.palette[5].rgb;
    let face = fx.palette[6].rgb;
    var lip = 7.0;
    if (pattern == 0 || pattern == 9) {
        lip = select(6.0, 11.0, fract(q.x / 16.0) < 0.5);  // blocky grass teeth
    } else if (pattern == 7) {
        lip = 10.0 + 6.0 * abs(sin(q.x * 0.06));
    } else if (pattern == 8) {
        lip = 2.0;
    }
    if (depth < lip) {
        return top;
    }
    var c = face;
    if (pattern == 0) {  // earth in bands
        c = face * select(1.0, 0.9, fract(depth / 26.0) < 0.5);
    } else if (pattern == 1 || pattern == 9) {  // bricks
        let size = select(vec2f(40.0, 20.0), vec2f(32.0, 16.0), pattern == 9);
        let row = floor(depth / size.y);
        let bx = fract((q.x + row * size.x * 0.5) / size.x) * size.x;
        let by = fract(depth / size.y) * size.y;
        c = select(face, face * 0.72, bx < 2.0 || by < 2.0);
    } else if (pattern == 2) {  // checker
        let cell = floor(vec2f(q.x, depth) / 24.0);
        c = select(face, top * 0.9, ((i32(cell.x) + i32(cell.y)) & 1) == 0);
    } else if (pattern == 3) {  // sand ripples
        c = face * (0.94 + 0.06 * sin(q.x * 0.05 + depth * 0.4 + sin(q.x * 0.013) * 3.0));
    } else if (pattern == 4) {  // big tiles
        let g = abs(fract(vec2f(q.x, depth) / 48.0) - 0.5) * 48.0;
        c = select(face, face * 0.75, min(g.x, g.y) < 1.2);
    } else if (pattern == 6) {  // planks
        let row = floor(depth / 14.0);
        let seam = fract((q.x + hash11(row) * 120.0) / 120.0) * 120.0 < 2.0 || fract(depth / 14.0) * 14.0 < 1.5;
        c = select(face * (0.94 + 0.08 * hash11(row + 3.0)), face * 0.7, seam);
    } else if (pattern == 7) {  // cloud
        c = mix(top, face, smoothstep(lip, lip + 60.0, depth));
    } else if (pattern == 8) {
        c = face;
    } else if (pattern == 10) {  // stone slabs
        let row = floor(depth / 32.0);
        let bx = fract((q.x + row * 32.0) / 64.0) * 64.0;
        c = select(face * (0.95 + 0.08 * hash21(vec2f(floor((q.x + row * 32.0) / 64.0), row))), face * 0.7,
                   bx < 2.0 || fract(depth / 32.0) * 32.0 < 2.0);
    } else {  // rock
        let cell = floor(vec2f(q.x, depth) / 22.0);
        let h = hash11(cell.x * 13.1 + cell.y * 7.7);
        let local = fract(vec2f(q.x, depth) / 22.0) - 0.5;
        c = select(face, face * 0.85, length(local) < 0.22 + 0.15 * h);
    }
    return c * (1.0 - 0.25 * smoothstep(0.0, 200.0, depth));
}

// The battle stage without the fighters: the rising diorama (the field's test room stays put) and the ground
// sliding in below the walking line.
fn stage_scene(uv: vec2f) -> vec3f {
    if (fx.stage.x > 2.5) {
        return test_room(uv);
    }
    let q = to_stage(uv);
    if (q.y < 0.0) {
        let k = clamp(fx.stage.y / 0.35, 0.0, 1.0);
        return select(digital_void(uv), ground_color(q), uv.y < fx.stage.z + (1.0 - fx.stage.z) * k);
    }
    let card = card_at(q);
    if (card.index >= 5) {
        return digital_void(uv);
    }
    return card_color(card) * card_shadow(q, card);
}

// The fighters (the only things left in the depth buffer) cast the paper drop shadow onto the stage behind them.
fn fighter_shadow(p: vec2i) -> f32 {
    let ppu = pixels_per_unit(-640.0);
    let dir = normalize(vec2f(fx.light.x, -fx.light.y));
    // Fixed taps (no per-pixel jitter): a clean soft-edged silhouette, like a paper cut-out's shadow.
    var occ = 0.0;
    for (var i = 0; i < 16; i++) {
        let t = (f32(i % 4) + 0.5) / 4.0;
        let ang = f32(i) * 1.570796 + f32(i / 4) * 0.392699;
        let sp = vec2f(p) + 0.5 + dir * (mix(3.0, 12.0, t) * ppu) + vec2f(cos(ang), sin(ang)) * (1.5 + 3.0 * t) * ppu;
        if (on_screen(sp) && !is_background(load_depth(vec2i(sp)))) {
            occ += 1.0;
        }
    }
    return 1.0 - 0.38 * fx.amount.y * smoothstep(0.0, 0.7, occ / 16.0);
}

// The stage, lit: the fighters' shadows, and a glossy floor below the walking line that mirrors the fighters and
// the diorama, fading with depth, with a bright lip along its edge.
fn stage_lit(uv: vec2f, p: vec2i) -> vec3f {
    var col = stage_scene(uv) * fighter_shadow(p);
    let floorV = fx.stage.z;
    let below = uv.y - floorV;
    let up = select(clamp(fx.stage.y / 0.35, 0.0, 1.0), 1.0, fx.stage.x > 2.5);
    if (fx.amount.z > 0.0 && below > 0.0 && below < (1.0 - floorV) * up) {
        let fall = 1.0 - smoothstep(0.0, 0.16, below);
        let mirror = vec2f(uv.x, floorV - below);
        var refl: vec3f;
        var weight = 0.45;
        if (!is_background(load_depth(vec2i(mirror * fx.screen.xy)))) {
            refl = sample_soft(mirror, 1.0 + below * fx.screen.y * 0.06);
            weight = 1.0;
        } else {
            refl = stage_scene(mirror);
        }
        col = mix(col, refl, clamp(fx.amount.z * 0.75 * fall * fall * weight, 0.0, 0.7));
        let lip = 1.0 - smoothstep(0.0, 3.0 * game_px() * fx.screen.w, below);
        col = col + (vec3f(1.0) - col) * (fx.amount.z * 0.4 * lip * smoothstep(0.12, 0.35, luma(col)));
    }
    return col;
}

// The test room: blank green-and-white dev textures. A checkered floor block under the walking line and a
// gridded back wall, anchored to the world so they scroll with the camera.
fn test_room(uv: vec2f) -> vec3f {
    // stage2 is in the game's 608x480 screen space: x of world x = 0, then screen units per world unit.
    let x608 = 304.0 + (uv.x - 0.5) * fx.screen.x / fx.screen.y * 480.0;
    let wx = (x608 - fx.stage2.x) / fx.stage2.y;
    let wy = (fx.stage.z - uv.y) * 480.0 / fx.stage2.z;  // world units above the floor
    let green = vec3f(0.33, 0.72, 0.36);
    let white = vec3f(0.95, 0.97, 0.94);
    if (wy < 0.0) {
        let cell = floor(vec2f(wx, wy) / 32.0);
        let checker = (i32(cell.x) + i32(cell.y)) & 1;
        var c = select(white, green, checker == 0);
        if (wy > -3.0) {
            c = vec3f(0.12, 0.35, 0.15);  // the floor's top edge
        }
        return c;
    }
    let g = abs(fract(vec2f(wx, wy) / 32.0) - 0.5) * 32.0;
    let big = abs(fract(vec2f(wx, wy) / 128.0) - 0.5) * 128.0;
    var c = mix(white, vec3f(0.88, 0.94, 0.88), 0.5);
    if (min(g.x, g.y) < 0.8) {
        c = mix(c, green, 0.45);
    }
    if (min(big.x, big.y) < 1.6) {
        c = mix(c, green, 0.85);
    }
    return c;
}

@fragment fn fs_composite(in: VertexOutput) -> @location(0) vec4f {
    let p = vec2i(in.pos.xy);
    let src = textureLoad(scene, p, 0);
    var rgb = src.rgb;
    let stored = load_depth(p);
    if (!is_background(stored)) {
        let c = view_pos(vec2f(p) + 0.5, stored);
        let n = view_normal(p, c);
        let L = fx.light.xyz;

        var occ = vec2f(1.0);
        if (fx.amount.x > 0.0 || fx.amount.y > 0.0) {
            occ = occlusion_at(in.pos.xy, -c.z);
        }
        let ao = mix(1.0, occ.x, fx.amount.x);
        let ndl = dot(n, L);
        // Cast shadows only land on surfaces facing the key light; the others are already darker.
        let shadowed = (1.0 - occ.y) * smoothstep(-0.05, 0.15, ndl);
        let shade = 1.0 - 0.7 * fx.amount.y * shadowed;
        // Relative to a card facing the camera, so SPM's flat paper layers keep their brightness.
        let key = mix(1.0, (0.75 + 0.5 * clamp(ndl, 0.0, 1.0)) / (0.75 + 0.5 * max(L.z, 0.0)), fx.amount2.x);
        rgb = rgb * ao * shade * key;

        if (fx.amount.z > 0.0 && is_ortho() && n.z > 0.85) {
            rgb = floor_gloss(p, c, rgb);
        } else if (fx.amount.z > 0.0 && n.y > 0.72) {
            // Smooth floors are glossier than busy ones like grass, but every floor gets some sheen.
            let l0 = luma(src.rgb);
            let l1 = luma(textureLoad(scene, clamp_px(p + vec2i(2, 0)), 0).rgb);
            let l2 = luma(textureLoad(scene, clamp_px(p + vec2i(0, 2)), 0).rgb);
            let l3 = luma(textureLoad(scene, clamp_px(p - vec2i(2, 2)), 0).rgb);
            let contrast = abs(l1 - l0) + abs(l2 - l0) + abs(l3 - l0);
            let gloss = 1.0 - smoothstep(0.015, 0.12, contrast);
            let V = view_dir(c);
            let cosv = clamp(dot(-V, n), 0.0, 1.0);
            let fresnel = 0.04 + 0.96 * pow(1.0 - cosv, 5.0);
            let refl = reflection(p, c, n, noise(in.pos.xy + vec2f(17.0, 5.0)));
            if (refl.a > 0.0) {
                let amount = fx.amount.z * refl.a * mix(0.25, 0.65, gloss) * (0.4 + 0.6 * fresnel);
                rgb = mix(rgb, refl.rgb, clamp(amount, 0.0, 0.75));
            }
            let H = normalize(L - V);
            let spec = pow(max(dot(n, H), 0.0), mix(24.0, 64.0, gloss)) * fx.amount.z * mix(0.2, 0.45, gloss);
            rgb = rgb + (vec3f(1.0) - rgb) * spec;
        }
    }
    if (is_background(stored) && fx.stage.x >= 1.5) {
        rgb = stage_lit(in.uv, p);
    }
    if (fx.misc.y >= 2.0) {
        let o = occlusion_at(in.pos.xy, -view_z(stored));
        let z = -view_z(stored);
        if (fx.misc.y >= 3.0) {
            return vec4f(fract(z / 100.0), fract(z / 1000.0), select(0.0, 1.0, is_background(stored)), src.a);
        }
        return vec4f(o.x, o.y, select(0.0, 1.0, is_background(stored)), src.a);
    }
    if (fx.amount.w > 0.0) {
        let bloom = textureSampleLevel(aux1, samp, in.uv, 0.0).rgb;
        rgb = rgb + bloom * fx.amount.w * (1.0 - rgb * 0.5);
    }
    // Sun rays: only in open outdoor rooms (measured sky and the map's indoor/outdoor hint).
    var stageRays = 1.0;
    if (fx.stage.x > 0.5) {
        // Softer in battle: the fighters stand right in the light.
        stageRays = select(0.0, 0.5 * card_lift(4), fx.stage.x > 1.5 && fx.stage.x < 2.5 && fx.motif3.z > 0.5);
    }
    let open = stats.open * fx.sun.w * stageRays;
    if (fx.misc.w > 0.0 && open > 0.001) {
        let rays = textureSampleLevel(aux2, samp, in.uv, 0.0).r;
        let vis = smoothstep(0.1, 0.7, rays);
        // Depthless dark cut-outs (silhouette trees) count as sky but must not glow.
        let dark = select(1.0, smoothstep(0.15, 0.5, luma(src.rgb)), is_background(stored));
        // Light streaming between the scenery from the open sky, plus the stylised beams near the sun.
        let streams = smoothstep(0.04, 0.7, rays) * rays * 0.8;
        let beams = sun_beams(in.uv) * vis * 0.75;
        let shaft = (streams + beams) * dark;
        let light = clamp(stats.tint.rgb * shaft * fx.misc.w * open, vec3f(0.0), vec3f(0.4));
        rgb = vec3f(1.0) - (vec3f(1.0) - rgb) * (vec3f(1.0) - light);
    }
    return vec4f(clamp(rgb, vec3f(0.0), vec3f(1.0)), src.a);
}
)";

// Measures each frame how much bright sky the scene shows (outdoor rooms) and the area's colours,
// then eases the sun rays' strength and tint towards them so they fade in and out smoothly.
static constexpr std::string_view StatsShader = R"(
struct Fx {
    proj: vec4f,
    depth: vec4f,
    viewport: vec4f,
    screen: vec4f,
    light: vec4f,
    amount: vec4f,
    amount2: vec4f,
    misc: vec4f,
    sun: vec4f,
};
struct Stats {
    skyCount: atomic<u32>,
    topSamples: atomic<u32>,
    skyR: atomic<u32>,
    skyG: atomic<u32>,
    skyB: atomic<u32>,
    sceneR: atomic<u32>,
    sceneG: atomic<u32>,
    sceneB: atomic<u32>,
    open: f32,
    pad0: f32,
    pad1: f32,
    pad2: f32,
    tint: vec4f,
};
@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var depth_tex: texture_depth_2d;
@group(0) @binding(2) var<uniform> fx: Fx;
@group(0) @binding(3) var<storage, read_write> stats: Stats;

const GRID = vec2u(64u, 36u);

fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }

@compute @workgroup_size(8, 4) fn cs_gather(@builtin(global_invocation_id) id: vec3u) {
    if (any(id.xy >= GRID)) {
        return;
    }
    let t = (vec2f(id.xy) + 0.5) / vec2f(GRID);
    let px = vec2i(fx.viewport.xy + t * fx.viewport.zw);
    let p = clamp(px, vec2i(0), vec2i(fx.screen.xy) - vec2i(1));
    let c = textureLoad(scene, p, 0).rgb;
    let rgb = vec3u(round(clamp(c, vec3f(0.0), vec3f(1.0)) * 255.0));
    atomicAdd(&stats.sceneR, rgb.r);
    atomicAdd(&stats.sceneG, rgb.g);
    atomicAdd(&stats.sceneB, rgb.b);
    if (t.y > 0.7) {
        return;
    }
    atomicAdd(&stats.topSamples, 1u);
    let host = (textureLoad(depth_tex, p, 0) - fx.depth.z) * fx.depth.w;
    if (host <= 0.00002) {
        atomicAdd(&stats.skyCount, 1u);
        atomicAdd(&stats.skyR, rgb.r);
        atomicAdd(&stats.skyG, rgb.g);
        atomicAdd(&stats.skyB, rgb.b);
    }
}

@compute @workgroup_size(1) fn cs_resolve() {
    let sky = f32(atomicLoad(&stats.skyCount));
    let top = max(f32(atomicLoad(&stats.topSamples)), 1.0);
    let total = f32(GRID.x * GRID.y);
    let skyColor = vec3f(f32(atomicLoad(&stats.skyR)), f32(atomicLoad(&stats.skyG)), f32(atomicLoad(&stats.skyB))) /
                   (max(sky, 1.0) * 255.0);
    let sceneColor = vec3f(f32(atomicLoad(&stats.sceneR)), f32(atomicLoad(&stats.sceneG)),
                           f32(atomicLoad(&stats.sceneB))) / (total * 255.0);
    // Outdoors = a fair share of the upper screen is open, bright sky. Caves, space and rooms get none.
    let goal = smoothstep(0.04, 0.18, sky / top) * smoothstep(0.2, 0.45, luma(skyColor));
    // The area's colour: its sky and scenery, at full brightness and a little more saturated.
    var area = mix(sceneColor, skyColor, 0.35);
    area = area / max(luma(area), 0.05);
    area = clamp(mix(vec3f(1.0), area, 1.35), vec3f(0.0), vec3f(1.6));
    let tint = mix(vec3f(1.0, 0.92, 0.76), area, 0.6);
    if (stats.tint.a == 0.0) {
        stats.tint = vec4f(tint, 1.0);
        stats.open = goal;
    } else {
        stats.tint = vec4f(mix(stats.tint.rgb, tint, 0.05), 1.0);
        stats.open = mix(stats.open, goal, 0.08);
    }
    atomicStore(&stats.skyCount, 0u);
    atomicStore(&stats.topSamples, 0u);
    atomicStore(&stats.skyR, 0u);
    atomicStore(&stats.skyG, 0u);
    atomicStore(&stats.skyB, 0u);
    atomicStore(&stats.sceneR, 0u);
    atomicStore(&stats.sceneG, 0u);
    atomicStore(&stats.sceneB, 0u);
}
)";

static constexpr wgpu::TextureFormat OcclusionFormat = wgpu::TextureFormat::RGBA16Float;
static constexpr uint64_t StatsSize = 64;

struct Target {
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::Extent3D size{};
};

static wgpu::BindGroupLayout g_layout;
static wgpu::PipelineLayout g_pipelineLayout;
static wgpu::ShaderModule g_module;
static wgpu::BindGroupLayout g_statsLayout;
static wgpu::ComputePipeline g_gatherPipeline;
static wgpu::ComputePipeline g_resolvePipeline;
static wgpu::Buffer g_statsBuffer;
static wgpu::Sampler g_sampler;
static wgpu::Buffer g_uniformBuffer;
static wgpu::RenderPipeline g_occlusionPipeline;
static wgpu::RenderPipeline g_brightPipeline;
static wgpu::RenderPipeline g_blurHPipeline;
static wgpu::RenderPipeline g_blurVPipeline;
static wgpu::RenderPipeline g_skyPipeline;
static wgpu::RenderPipeline g_raysPipeline;
static wgpu::RenderPipeline g_compositePipeline;
static wgpu::TextureFormat g_compositeFormat = wgpu::TextureFormat::Undefined;
static Target g_sceneCopy;
static Target g_occlusion;
static Target g_bloomA;
static Target g_bloomB;
static Target g_skyMask;
static Target g_rays;
static Target g_dummyOcclusion;
static Target g_dummyBloom;
static bool g_failed = false;

static Target make_target(uint32_t width, uint32_t height, wgpu::TextureFormat format, const char* label,
                          wgpu::TextureUsage extraUsage = wgpu::TextureUsage::None) {
  const wgpu::TextureDescriptor desc{
      .label = label,
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | extraUsage,
      .dimension = wgpu::TextureDimension::e2D,
      .size = {std::max(width, 1u), std::max(height, 1u), 1},
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  Target t;
  t.texture = g_device.CreateTexture(&desc);
  t.view = t.texture.CreateView();
  t.size = desc.size;
  return t;
}

static wgpu::RenderPipeline make_pipeline(const char* label, const char* entry, wgpu::TextureFormat format) {
  const std::array targets{wgpu::ColorTargetState{.format = format}};
  const wgpu::FragmentState fragment{
      .module = g_module,
      .entryPoint = entry,
      .targetCount = targets.size(),
      .targets = targets.data(),
  };
  const wgpu::RenderPipelineDescriptor desc{
      .label = label,
      .layout = g_pipelineLayout,
      .vertex = wgpu::VertexState{.module = g_module, .entryPoint = "vs_main"},
      .primitive = wgpu::PrimitiveState{.topology = wgpu::PrimitiveTopology::TriangleList},
      .fragment = &fragment,
  };
  return g_device.CreateRenderPipeline(&desc);
}

static void clear_target(const wgpu::CommandEncoder& cmd, const Target& t, wgpu::Color color) {
  const std::array attachments{wgpu::RenderPassColorAttachment{
      .view = t.view,
      .loadOp = wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
      .clearValue = color,
  }};
  const wgpu::RenderPassDescriptor desc{
      .label = "Scene FX clear",
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
  };
  cmd.BeginRenderPass(&desc).End();
}

static wgpu::BindGroupLayoutEntry texture_entry(uint32_t binding, wgpu::ShaderStage stage,
                                                wgpu::TextureSampleType type = wgpu::TextureSampleType::Float) {
  return wgpu::BindGroupLayoutEntry{
      .binding = binding,
      .visibility = stage,
      .texture = {.sampleType = type, .viewDimension = wgpu::TextureViewDimension::e2D},
  };
}

static void init_stats() {
  const std::array entries{
      texture_entry(0, wgpu::ShaderStage::Compute, wgpu::TextureSampleType::UnfilterableFloat),
      texture_entry(1, wgpu::ShaderStage::Compute, wgpu::TextureSampleType::Depth),
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(Uniform)},
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 3,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer = {.type = wgpu::BufferBindingType::Storage, .minBindingSize = StatsSize},
      },
  };
  const wgpu::BindGroupLayoutDescriptor layoutDesc{
      .label = "Scene FX stats bind group layout",
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  g_statsLayout = g_device.CreateBindGroupLayout(&layoutDesc);
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{
      .label = "Scene FX stats pipeline layout",
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_statsLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDesc);
  const wgpu::ShaderSourceWGSL wgsl{wgpu::ShaderSourceWGSL::Init{.code = StatsShader.data()}};
  const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &wgsl, .label = "Scene FX stats shader"};
  const auto module = g_device.CreateShaderModule(&moduleDesc);
  const wgpu::ComputePipelineDescriptor gatherDesc{
      .label = "Scene FX stats gather",
      .layout = pipelineLayout,
      .compute = {.module = module, .entryPoint = "cs_gather"},
  };
  g_gatherPipeline = g_device.CreateComputePipeline(&gatherDesc);
  const wgpu::ComputePipelineDescriptor resolveDesc{
      .label = "Scene FX stats resolve",
      .layout = pipelineLayout,
      .compute = {.module = module, .entryPoint = "cs_resolve"},
  };
  g_resolvePipeline = g_device.CreateComputePipeline(&resolveDesc);
  // Zero-initialized: tint.a == 0 tells cs_resolve to take the first frame's values directly.
  const wgpu::BufferDescriptor bufferDesc{
      .label = "Scene FX stats",
      .usage = wgpu::BufferUsage::Storage,
      .size = StatsSize,
  };
  g_statsBuffer = g_device.CreateBuffer(&bufferDesc);
}

void initialize() {
  const std::array entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler = {.type = wgpu::SamplerBindingType::Filtering},
      },
      texture_entry(1, wgpu::ShaderStage::Fragment),
      texture_entry(2, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth),
      wgpu::BindGroupLayoutEntry{
          .binding = 3,
          .visibility = wgpu::ShaderStage::Fragment,
          .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(Uniform)},
      },
      texture_entry(4, wgpu::ShaderStage::Fragment),
      texture_entry(5, wgpu::ShaderStage::Fragment),
      wgpu::BindGroupLayoutEntry{
          .binding = 6,
          .visibility = wgpu::ShaderStage::Fragment,
          .buffer = {.type = wgpu::BufferBindingType::ReadOnlyStorage, .minBindingSize = StatsSize},
      },
      texture_entry(7, wgpu::ShaderStage::Fragment),
  };
  const wgpu::BindGroupLayoutDescriptor layoutDesc{
      .label = "Scene FX bind group layout",
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  g_layout = g_device.CreateBindGroupLayout(&layoutDesc);
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{
      .label = "Scene FX pipeline layout",
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_layout,
  };
  g_pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDesc);

  const wgpu::ShaderSourceWGSL wgsl{wgpu::ShaderSourceWGSL::Init{.code = Shader.data()}};
  const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &wgsl, .label = "Scene FX shader"};
  g_module = g_device.CreateShaderModule(&moduleDesc);

  const wgpu::SamplerDescriptor samplerDesc{
      .label = "Scene FX sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
  };
  g_sampler = g_device.CreateSampler(&samplerDesc);

  const wgpu::BufferDescriptor bufferDesc{
      .label = "Scene FX uniform",
      .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
      .size = sizeof(Uniform),
  };
  g_uniformBuffer = g_device.CreateBuffer(&bufferDesc);
  init_stats();

  g_occlusionPipeline = make_pipeline("Scene FX occlusion", "fs_occlusion", OcclusionFormat);
  g_brightPipeline = make_pipeline("Scene FX bright", "fs_bright", wgpu::TextureFormat::RGBA8Unorm);
  g_blurHPipeline = make_pipeline("Scene FX blur H", "fs_blur_h", wgpu::TextureFormat::RGBA8Unorm);
  g_blurVPipeline = make_pipeline("Scene FX blur V", "fs_blur_v", wgpu::TextureFormat::RGBA8Unorm);
  g_skyPipeline = make_pipeline("Scene FX sky mask", "fs_sky", wgpu::TextureFormat::RGBA8Unorm);
  g_raysPipeline = make_pipeline("Scene FX sun rays", "fs_rays", wgpu::TextureFormat::RGBA8Unorm);
  g_dummyOcclusion = make_target(1, 1, OcclusionFormat, "Scene FX no occlusion");
  g_dummyBloom = make_target(1, 1, wgpu::TextureFormat::RGBA8Unorm, "Scene FX no bloom");
  g_compositeFormat = wgpu::TextureFormat::Undefined;
  g_failed = false;
}

void shutdown() {
  g_occlusionPipeline = {};
  g_brightPipeline = {};
  g_blurHPipeline = {};
  g_blurVPipeline = {};
  g_skyPipeline = {};
  g_raysPipeline = {};
  g_compositePipeline = {};
  g_compositeFormat = wgpu::TextureFormat::Undefined;
  g_gatherPipeline = {};
  g_resolvePipeline = {};
  g_statsBuffer = {};
  g_statsLayout = {};
  g_sceneCopy = {};
  g_occlusion = {};
  g_bloomA = {};
  g_bloomB = {};
  g_skyMask = {};
  g_rays = {};
  g_dummyOcclusion = {};
  g_dummyBloom = {};
  g_uniformBuffer = {};
  g_sampler = {};
  g_module = {};
  g_pipelineLayout = {};
  g_layout = {};
}

static wgpu::BindGroup make_bind_group(const wgpu::TextureView& scene, const wgpu::TextureView& depth,
                                       const wgpu::TextureView& aux0, const wgpu::TextureView& aux1,
                                       const wgpu::TextureView& aux2) {
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 0, .sampler = g_sampler},
      wgpu::BindGroupEntry{.binding = 1, .textureView = scene},
      wgpu::BindGroupEntry{.binding = 2, .textureView = depth},
      wgpu::BindGroupEntry{.binding = 3, .buffer = g_uniformBuffer, .offset = 0, .size = sizeof(Uniform)},
      wgpu::BindGroupEntry{.binding = 4, .textureView = aux0},
      wgpu::BindGroupEntry{.binding = 5, .textureView = aux1},
      wgpu::BindGroupEntry{.binding = 6, .buffer = g_statsBuffer, .offset = 0, .size = StatsSize},
      wgpu::BindGroupEntry{.binding = 7, .textureView = aux2},
  };
  const wgpu::BindGroupDescriptor desc{
      .label = "Scene FX bind group",
      .layout = g_layout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return g_device.CreateBindGroup(&desc);
}

static void draw_fullscreen(const wgpu::CommandEncoder& cmd, const char* label, const wgpu::TextureView& target,
                            const wgpu::RenderPipeline& pipeline, const wgpu::BindGroup& bindGroup) {
  const std::array attachments{wgpu::RenderPassColorAttachment{
      .view = target,
      .loadOp = wgpu::LoadOp::Load,
      .storeOp = wgpu::StoreOp::Store,
  }};
  const wgpu::RenderPassDescriptor desc{
      .label = label,
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
  };
  const auto pass = cmd.BeginRenderPass(&desc);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.Draw(3);
  pass.End();
}

static void gather_stats(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView) {
  const std::array entries{
      wgpu::BindGroupEntry{.binding = 0, .textureView = g_sceneCopy.view},
      wgpu::BindGroupEntry{.binding = 1, .textureView = depthView},
      wgpu::BindGroupEntry{.binding = 2, .buffer = g_uniformBuffer, .offset = 0, .size = sizeof(Uniform)},
      wgpu::BindGroupEntry{.binding = 3, .buffer = g_statsBuffer, .offset = 0, .size = StatsSize},
  };
  const wgpu::BindGroupDescriptor desc{
      .label = "Scene FX stats bind group",
      .layout = g_statsLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&desc);
  const wgpu::ComputePassDescriptor passDesc{.label = "Scene FX stats"};
  const auto pass = cmd.BeginComputePass(&passDesc);
  pass.SetBindGroup(0, bindGroup);
  pass.SetPipeline(g_gatherPipeline);
  pass.DispatchWorkgroups(64 / 8, 36 / 4);
  pass.SetPipeline(g_resolvePipeline);
  pass.DispatchWorkgroups(1);
  pass.End();
}

static void ensure_targets(const wgpu::CommandEncoder& cmd, wgpu::Extent3D size, wgpu::TextureFormat format) {
  if (g_compositeFormat != format) {
    g_compositePipeline = make_pipeline("Scene FX composite", "fs_composite", format);
    g_compositeFormat = format;
    g_sceneCopy = {};
  }
  if (!g_sceneCopy.texture || g_sceneCopy.size.width != size.width || g_sceneCopy.size.height != size.height) {
    const uint32_t hw = (size.width + 1) / 2, hh = (size.height + 1) / 2;
    g_sceneCopy = make_target(size.width, size.height, format, "Scene FX scene copy", wgpu::TextureUsage::CopyDst);
    g_occlusion = make_target(hw, hh, OcclusionFormat, "Scene FX occlusion");
    g_bloomA = make_target(hw, hh, wgpu::TextureFormat::RGBA8Unorm, "Scene FX bloom A");
    g_bloomB = make_target(hw, hh, wgpu::TextureFormat::RGBA8Unorm, "Scene FX bloom B");
    g_skyMask = make_target(hw, hh, wgpu::TextureFormat::RGBA8Unorm, "Scene FX sky mask");
    g_rays = make_target(hw, hh, wgpu::TextureFormat::RGBA8Unorm, "Scene FX sun rays");
    clear_target(cmd, g_dummyOcclusion, {1.0, 1.0, 1e6, 1.0});
    clear_target(cmd, g_dummyBloom, {0.0, 0.0, 0.0, 1.0});
  }
}

void run(const wgpu::CommandEncoder& cmd, const Uniform& uniform, const wgpu::Texture& colorTexture,
         const wgpu::TextureView& colorView, const wgpu::TextureView& depthView, wgpu::Extent3D size) {
  if (g_failed || !g_module || !colorTexture || !colorView || !depthView || size.width == 0 || size.height == 0) {
    return;
  }
  if (colorTexture.GetSampleCount() != 1) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Log.warn("Scene effects need a single-sampled EFB; skipped with MSAA");
    }
    return;
  }
  ensure_targets(cmd, size, colorTexture.GetFormat());
  // SPM_SCENE_FX_DEBUG=1 logs the pass parameters now and then; =2 shows the occlusion buffer.
  static const int debugMode = [] {
    const char* v = std::getenv("SPM_SCENE_FX_DEBUG");
    return v == nullptr ? 0 : std::atoi(v);
  }();
  Uniform u = uniform;
  if (debugMode >= 2) {
    u.misc[1] = static_cast<float>(debugMode);
  }
  if (debugMode != 0) {
    static uint32_t calls = 0;
    if (calls++ % 300 == 0) {
      std::fprintf(stderr,
                   "[scenefx] run %u size %ux%u proj %.4f %.4f %.4f %.4f depth a=%.6f b=%.4f min=%.4f scale=%.4f "
                   "vp %.1f %.1f %.1f %.1f\n",
                   calls, size.width, size.height, u.proj[0], u.proj[1], u.proj[2], u.proj[3], u.depth[0], u.depth[1],
                   u.depth[2], u.depth[3], u.viewport[0], u.viewport[1], u.viewport[2], u.viewport[3]);
    }
  }
  g_queue.WriteBuffer(g_uniformBuffer, 0, &u, sizeof(u));

  const wgpu::TexelCopyTextureInfo src{.texture = colorTexture};
  const wgpu::TexelCopyTextureInfo dst{.texture = g_sceneCopy.texture};
  const wgpu::Extent3D copySize{size.width, size.height, 1};
  cmd.CopyTextureToTexture(&src, &dst, &copySize);

  const bool occlusion = uniform.amount[0] > 0.f || uniform.amount[1] > 0.f;
  const bool bloom = uniform.amount[3] > 0.f;
  const bool sun = uniform.misc[3] > 0.f;
  const auto& none = g_dummyBloom.view;
  if (occlusion) {
    draw_fullscreen(cmd, "Scene FX occlusion", g_occlusion.view, g_occlusionPipeline,
                    make_bind_group(g_sceneCopy.view, depthView, g_dummyOcclusion.view, none, none));
  }
  if (bloom) {
    draw_fullscreen(cmd, "Scene FX bright", g_bloomA.view, g_brightPipeline,
                    make_bind_group(g_sceneCopy.view, depthView, g_dummyOcclusion.view, none, none));
    draw_fullscreen(cmd, "Scene FX blur H", g_bloomB.view, g_blurHPipeline,
                    make_bind_group(g_sceneCopy.view, depthView, g_bloomA.view, none, none));
    draw_fullscreen(cmd, "Scene FX blur V", g_bloomA.view, g_blurVPipeline,
                    make_bind_group(g_sceneCopy.view, depthView, g_bloomB.view, none, none));
  }
  if (sun) {
    // In battle the map is hidden: keep the area's measured sky and tint from the field.
    if (uniform.stage[0] < 1.5f) {
      gather_stats(cmd, depthView);
    }
    draw_fullscreen(cmd, "Scene FX sky mask", g_skyMask.view, g_skyPipeline,
                    make_bind_group(g_sceneCopy.view, depthView, g_dummyOcclusion.view, none, none));
    draw_fullscreen(cmd, "Scene FX sun rays", g_rays.view, g_raysPipeline,
                    make_bind_group(g_sceneCopy.view, depthView, g_skyMask.view, none, none));
  }
  draw_fullscreen(cmd, "Scene FX composite", colorView, g_compositePipeline,
                  make_bind_group(g_sceneCopy.view, depthView, occlusion ? g_occlusion.view : g_dummyOcclusion.view,
                                  bloom ? g_bloomA.view : none, sun ? g_rays.view : none));
}

} // namespace aurora::gfx::scene_fx
