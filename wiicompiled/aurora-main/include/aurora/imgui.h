#ifndef AURORA_IMGUI_H
#define AURORA_IMGUI_H

#include <imgui.h>

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#endif

ImTextureID aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8);

#ifdef __cplusplus
}
// A bold display font for big UI text (null if the system has none); its native size is 44px.
ImFont* aurora_imgui_display_font();
#endif

#endif
