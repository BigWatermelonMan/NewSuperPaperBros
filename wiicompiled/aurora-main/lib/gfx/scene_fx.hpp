#pragma once

#include "../webgpu/gpu.hpp"

#include <aurora/math.hpp>

namespace aurora::gfx::scene_fx {

// Lost Pages enhanced lighting: screen-space ambient occlusion, paper diorama / key light shadows,
// glossy floors and reflections, normal-based key lighting, bloom and area-tinted sun rays, applied
// to the finished 3D scene before the game draws its HUD on top. Amounts are 0..1; 0 turns one off.
struct Settings {
  bool enabled = true;
  float ambientOcclusion = 0.85f;
  float shadows = 0.9f;
  float reflections = 0.7f;
  float bloom = 0.f;  // off: SPM's white and pastel backdrops bloom over the art
  float lighting = 0.7f;
  float sunRays = 0.8f;
};

void set_settings(const Settings& settings) noexcept;
Settings get_settings() noexcept;
bool active() noexcept;

// Lost Pages battle stage / test room, set by the game thread each frame.
// mode 0 off, 2 pop-up diorama battle stage, 3 test room.
// floorV: the walking line in screen UV; originU: x of world x = 0 and unitU/unitV: screen units per world unit, in
// the game's 608x480 space. preset: which area's diorama (StagePreset; 0 the test room).
struct Stage {
  int mode = 0;
  float floorV = 0.8f, originU = 0.5f, unitU = 0.002f, unitV = 0.002f;
  int preset = 0;
};
void set_stage(const Stage& stage) noexcept;
int stage_mode() noexcept;
// 0 for rooms the game knows are indoors (sun rays off there), 1 otherwise.
void set_outdoor_hint(float hint) noexcept;

// Matches `struct Fx` in the shader.
struct Uniform {
  float proj[4];     // P00, P11, P02, P12 of the GX perspective projection
  float depth[4];    // a, b (host depth d = a + b / z), stored depth min, 1 / stored depth range
  float viewport[4]; // 3D viewport in render target pixels: left, top, width, height
  float screen[4];   // render target width, height, 1 / width, 1 / height
  float light[4];    // view-space direction towards the key light, w unused
  float amount[4];   // ambient occlusion, shadows, reflections, bloom
  float amount2[4];  // key lighting, AO radius, shadow ray length, bloom threshold
  float misc[4];     // orthographic camera (1/0), debug view, 3D shadow thickness, sun rays
  float sun[4];      // sun position in screen UV (may be off-screen), seconds, outdoor hint
  float stage[4];    // stage mode, seconds in mode, floor V, unused
  float stage2[4];   // test room: origin U, U per unit, V per unit, unused
  float palette[10][4];  // the battle diorama's colours (see the shader)
  float motif[4];        // its back, far skyline, mid and near props
  float motif2[4];       // its ground pattern, preset
  float motif3[4];       // its style, wing prop, open sky
};

// Builds the pass parameters from the scene camera; false when the camera is not usable.
bool make_uniform(Uniform& out, const Mat4x4<float>& gxProj, bool ortho, const webgpu::Viewport& renderViewport,
                  wgpu::Extent3D targetSize) noexcept;

void initialize();
void shutdown();
// Runs the effects on the EFB: reads `colorTexture` (single-sampled) and `depthView`, writes the
// result back into `colorView`. Called between two render passes.
void run(const wgpu::CommandEncoder& cmd, const Uniform& uniform, const wgpu::Texture& colorTexture,
         const wgpu::TextureView& colorView, const wgpu::TextureView& depthView, wgpu::Extent3D size);

} // namespace aurora::gfx::scene_fx
