# Door and paper-transition rendering

The October 23, 2026 fix addresses three connected rendering problems:

* SPM flushes captured textures to native guest RAM before reusing them. The runtime treated that flush as a
  modification and destroyed the high-resolution GPU copy. The runtime now hashes completed readbacks and keeps
  a GPU capture while its downloaded bytes remain unchanged. A CPU or DMA change still invalidates it.
* Small offscreen object bakes could consume the scene-effects pass before the room was drawn. Copy-clear
  boundaries now reset the scene-effects bookkeeping for these small bakes, and completed world captures get
  their lighting before the copy erases their scene depth.
* Door meshes use SPM's fixed screen-pixel perspective camera (25-degree FOV, 4:3 pixel aspect). Its projection now
  uses the world's widescreen correction. Copy cropping uses the active camera's correction and viewport centre
  (304 guest pixels for a 608-pixel viewport), rather than the EFB centre at 320. A screenshot replayed through an
  unmodified pixel camera therefore isn't cropped a second time with the world's correction.

The change lives in `gx_copy.cpp`, `command_processor.cpp`, `GXFrameBuffer.cpp`, and `gfx/common.cpp`.
The new `SPM_TRANSITION_LOG=1` diagnostic logs the world draw count and effects state at texture copies.

Private in-game captures: `autotest/door-before`, `autotest/door-final-wide`, and `autotest/door-final-169`.
The cinematic test opens Lineland's star door and proceeds to the next map at 2.25x internal resolution.
The opening now matches the closed doorway's width, and the room's effects remain active during the animation.
The shared copy path is also exercised by `autotest/transition-flip-cropfix` at 120 FPS interpolation.
That check revealed a separate character-flip texture-tile artifact. `autotest/flip-baseline-compare` reproduces
it with the engine backed up before this fix, so it is an existing issue and remains unresolved here.
`autotest/door-installed-final-check` rechecks the door after the active-camera crop adjustment.
