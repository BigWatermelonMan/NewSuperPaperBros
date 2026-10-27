// Lost Pages battle overlay, TTYD style: stage curtains, the status panel, the command stack beside Mario,
// target cursor, action command prompts, damage starbursts, banners and the level-up choice.

#include "lp_battle.h"
#include "lp_testarea.h"
#include "lp_progress.h"

#include <aurora/gfx.h>
#include <aurora/imgui.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace LostPages {
namespace {

constexpr float kPi = 3.14159265f;

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
ImU32 WithAlpha(ImU32 c, float a) {
    const int alpha = static_cast<int>(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.f, 1.f));
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

// The game's 608x480 screen space -> window pixels (the picture keeps its height; wider screens add sides).
struct Mapping {
    float scale = 1, ox = 0;
    ImVec2 operator()(float x, float y) const { return {ox + x * scale, y * scale}; }
};

ImFont* Display() {
    ImFont* f = aurora_imgui_display_font();
    return f ? f : ImGui::GetFont();
}

ImVec2 TextSize(float size, const char* text) { return Display()->CalcTextSizeA(size, FLT_MAX, 0.f, text); }

// Bold text with a dark outline, the way Paper Mario labels pop off the scenery.
void OutlinedText(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const char* text, float outline = 2.5f,
                  ImU32 outlineColor = IM_COL32(30, 20, 40, 255)) {
    ImFont* f = Display();
    const float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.f;
    const ImU32 oc = WithAlpha(outlineColor, a);
    for (int i = 0; i < 8; ++i) {
        const float ang = i * kPi / 4.f;
        dl->AddText(f, size, {pos.x + std::cos(ang) * outline, pos.y + std::sin(ang) * outline}, oc, text);
    }
    dl->AddText(f, size, pos, color, text);
}
void CenteredText(ImDrawList* dl, ImVec2 center, float size, ImU32 color, const char* text, float outline = 2.5f) {
    const ImVec2 ts = TextSize(size, text);
    OutlinedText(dl, {center.x - ts.x * 0.5f, center.y - ts.y * 0.5f}, size, color, text, outline);
}

void Star(ImDrawList* dl, ImVec2 c, float outer, float inner, int points, ImU32 fill, ImU32 edge, float rot = 0.f) {
    ImVec2 pts[32];
    const int n = std::min(points * 2, 32);
    for (int i = 0; i < n; ++i) {
        const float r = (i % 2 == 0) ? outer : inner;
        const float a = rot - kPi / 2.f + i * kPi / points;
        pts[i] = {c.x + std::cos(a) * r, c.y + std::sin(a) * r};
    }
    dl->AddConcavePolyFilled(pts, n, fill);
    dl->AddPolyline(pts, n, edge, ImDrawFlags_Closed, std::max(1.5f, outer * 0.08f));
}

void Heart(ImDrawList* dl, ImVec2 c, float r, ImU32 fill) {
    dl->AddCircleFilled({c.x - r * 0.5f, c.y - r * 0.25f}, r * 0.55f, fill, 20);
    dl->AddCircleFilled({c.x + r * 0.5f, c.y - r * 0.25f}, r * 0.55f, fill, 20);
    dl->AddTriangleFilled({c.x - r * 1.03f, c.y - r * 0.05f}, {c.x + r * 1.03f, c.y - r * 0.05f}, {c.x, c.y + r * 1.0f},
                          fill);
}

// A Wii-style round button glyph ("A", "B") with a press flash.
void ButtonGlyph(ImDrawList* dl, ImVec2 c, float r, const char* label, ImU32 color, bool lit) {
    if (lit) dl->AddCircleFilled(c, r * 1.45f, IM_COL32(255, 255, 160, 110), 32);
    dl->AddCircleFilled({c.x, c.y + r * 0.12f}, r, IM_COL32(20, 20, 30, 200), 32);
    dl->AddCircleFilled(c, r, color, 32);
    dl->AddCircle(c, r, IM_COL32(255, 255, 255, 230), 32, r * 0.14f);
    CenteredText(dl, {c.x, c.y - r * 0.05f}, r * 1.25f, IM_COL32(255, 255, 255, 255), label, 1.5f);
}

// Command icons, drawn rather than loaded: boot (Jump), hammer, mushroom (Items), flag (Tactics).
void CommandIcon(ImDrawList* dl, int index, ImVec2 c, float r) {
    const ImU32 dark = IM_COL32(40, 25, 20, 255);
    switch (index) {
    case 0:  // boot
        dl->AddRectFilled({c.x - r * 0.35f, c.y - r * 0.55f}, {c.x + r * 0.15f, c.y + r * 0.2f}, Rgba(120, 70, 40), r * 0.12f);
        dl->AddRectFilled({c.x - r * 0.45f, c.y + r * 0.05f}, {c.x + r * 0.6f, c.y + r * 0.5f}, Rgba(150, 90, 50), r * 0.25f);
        dl->AddRect({c.x - r * 0.45f, c.y + r * 0.05f}, {c.x + r * 0.6f, c.y + r * 0.5f}, dark, r * 0.25f, 0, 2.f);
        break;
    case 1:  // hammer
        dl->AddRectFilled({c.x - r * 0.08f, c.y - r * 0.2f}, {c.x + r * 0.08f, c.y + r * 0.65f}, Rgba(170, 110, 60), 2.f);
        dl->AddRectFilled({c.x - r * 0.55f, c.y - r * 0.6f}, {c.x + r * 0.55f, c.y - r * 0.12f}, Rgba(150, 150, 165), r * 0.12f);
        dl->AddRect({c.x - r * 0.55f, c.y - r * 0.6f}, {c.x + r * 0.55f, c.y - r * 0.12f}, dark, r * 0.12f, 0, 2.f);
        break;
    case 2:  // mushroom
        dl->AddRectFilled({c.x - r * 0.25f, c.y - r * 0.05f}, {c.x + r * 0.25f, c.y + r * 0.55f}, Rgba(250, 235, 200), r * 0.1f);
        dl->PathArcTo({c.x, c.y}, r * 0.6f, kPi, 2.f * kPi, 20);
        dl->PathFillConvex(Rgba(225, 40, 40));
        dl->AddCircleFilled({c.x - r * 0.25f, c.y - r * 0.3f}, r * 0.12f, IM_COL32_WHITE, 12);
        dl->AddCircleFilled({c.x + r * 0.22f, c.y - r * 0.22f}, r * 0.1f, IM_COL32_WHITE, 12);
        break;
    default:  // flag
        dl->AddRectFilled({c.x - r * 0.4f, c.y - r * 0.6f}, {c.x - r * 0.3f, c.y + r * 0.6f}, Rgba(90, 70, 60));
        dl->AddTriangleFilled({c.x - r * 0.3f, c.y - r * 0.6f}, {c.x + r * 0.55f, c.y - r * 0.32f}, {c.x - r * 0.3f, c.y - r * 0.05f},
                              Rgba(60, 140, 230));
        break;
    }
}

void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImU32 fill = IM_COL32(6, 14, 9, 232),
           ImU32 edge = IM_COL32(90, 255, 130, 255)) {
    dl->AddRectFilled({a.x + 3, a.y + 4}, {b.x + 3, b.y + 4}, IM_COL32(0, 0, 0, 90), r);
    dl->AddRectFilled(a, b, fill, r);
    dl->AddRect(a, b, edge, r, 0, std::max(2.f, r * 0.18f));
}

// SPM's digital look: a black frame whose inner edge is a green zigzag, drawn on around the screen (top, right,
// bottom, left) as `progress` goes from 0 to 1.
void ZigzagBorder(ImDrawList* dl, ImVec2 size, float s, float progress, int frame) {
    const float inset = 20.f * s, amp = 7.f * s, period = 26.f * s;
    const ImU32 black = IM_COL32(4, 8, 6, 235), green = IM_COL32(80, 255, 120, 255), glow = IM_COL32(80, 255, 120, 70);
    const float perim = 2.f * (size.x + size.y);
    float budget = progress * perim;
    auto tri = [&](float d) {
        const float ph = std::fmod(d / period + frame * 0.02f, 1.f);
        return (ph < 0.5f ? ph : 1.f - ph) * 2.f;  // 0..1..0
    };
    struct Edge {
        ImVec2 a, b, inward;
    };
    const Edge edges[4] = {
        {{0, 0}, {size.x, 0}, {0, 1}},
        {{size.x, 0}, {size.x, size.y}, {-1, 0}},
        {{size.x, size.y}, {0, size.y}, {0, -1}},
        {{0, size.y}, {0, 0}, {1, 0}},
    };
    for (const Edge& e : edges) {
        const float len = std::hypot(e.b.x - e.a.x, e.b.y - e.a.y);
        const float drawn = std::min(len, budget);
        budget -= drawn;
        if (drawn <= 0.f) break;
        const ImVec2 dir{(e.b.x - e.a.x) / len, (e.b.y - e.a.y) / len};
        const int steps = std::max(2, static_cast<int>(drawn / (period * 0.5f)) + 1);
        ImVec2 prevEdge = e.a, prevZig{e.a.x + e.inward.x * inset, e.a.y + e.inward.y * inset};
        for (int i = 1; i <= steps; ++i) {
            const float d = drawn * i / steps;
            const ImVec2 onEdge{e.a.x + dir.x * d, e.a.y + dir.y * d};
            const float depth = inset + amp * (tri(d) - 0.5f) * 2.f;
            const ImVec2 zig{onEdge.x + e.inward.x * depth, onEdge.y + e.inward.y * depth};
            dl->AddQuadFilled(prevEdge, onEdge, zig, prevZig, black);
            dl->AddLine(prevZig, zig, glow, 9.f * s);
            dl->AddLine(prevZig, zig, IM_COL32(0, 0, 0, 255), 5.f * s);
            dl->AddLine(prevZig, zig, green, 2.5f * s);
            prevEdge = onEdge;
            prevZig = zig;
        }
        if (drawn < len) {  // the pen tip
            dl->AddCircleFilled(prevZig, 5.f * s, IM_COL32(200, 255, 210, 255), 12);
            break;
        }
    }
}

void StatusPanel(ImDrawList* dl, const BattleUi& ui, float s, float y) {
    char hp[32], sp[48];
    std::snprintf(hp, sizeof hp, "%d/%d", ui.hp, ui.maxHp);
    std::snprintf(sp, sizeof sp, "%d", ui.starPoints);
    const ImVec2 a{44.f * s, y}, b{a.x + 250.f * s, y + 46.f * s};
    Panel(dl, a, b, 14.f * s);
    Heart(dl, {a.x + 24.f * s, a.y + 23.f * s}, 13.f * s, IM_COL32(235, 40, 60, 255));
    OutlinedText(dl, {a.x + 44.f * s, a.y + 9.f * s}, 27.f * s, IM_COL32_WHITE, hp, 2.f);
    Star(dl, {a.x + 148.f * s, a.y + 23.f * s}, 14.f * s, 6.5f * s, 5, IM_COL32(255, 215, 40, 255), IM_COL32(150, 90, 10, 255));
    OutlinedText(dl, {a.x + 166.f * s, a.y + 9.f * s}, 27.f * s, IM_COL32(255, 235, 120, 255), sp, 2.f);
    char lv[32];
    std::snprintf(lv, sizeof lv, "Lv %d  ATK %d  BP %d/%d", ui.level, ui.attack,ui.bpUsed,ui.bp);
    OutlinedText(dl, {a.x + 10.f * s, b.y + 4.f * s}, 18.f * s, IM_COL32(230, 230, 255, 255), lv, 1.5f);
    char resources[80];
    std::snprintf(resources,sizeof resources,"FP %d/%d    Energy %d/100    Turn %d",ui.fp,ui.maxFp,ui.energy,ui.round);
    Panel(dl,{a.x,b.y+28*s},{a.x+330*s,b.y+56*s},8*s);
    OutlinedText(dl,{a.x+10*s,b.y+32*s},17*s,Rgba(140,240,255),resources,1*s);
}

const char* const kCommandNames[] = {"Jump", "Hammer", "Items", "Tactics"};
const char* const kCommandHelp[] = {
    "Jump on an enemy. Press A right before you land for a second stomp!",
    "Swing at an enemy. Hold Left, and let go when the last star lights up!",
    "Use an item from your bag.",
    "Defend, or try to run away.",
};

void Description(ImDrawList* dl, ImVec2 size, float s, const char* text) {
    const ImVec2 ts = TextSize(20.f * s, text);
    const ImVec2 a{size.x * 0.5f - ts.x * 0.5f - 20.f * s, size.y - 70.f * s};
    const ImVec2 b{size.x * 0.5f + ts.x * 0.5f + 20.f * s, size.y - 30.f * s};
    Panel(dl, a, b, 12.f * s);
    dl->AddText(Display(), 20.f * s, {a.x + 20.f * s, a.y + 9.f * s}, IM_COL32(220, 255, 225, 255), text);
}

void CommandMenu(ImDrawList* dl, const BattleUi& ui, const Mapping& m, float s, ImVec2 size) {
    const ImVec2 mario = m(ui.mario.x, ui.mario.y);
    const float bob = std::sin(ui.phaseFrame * 0.15f) * 3.f * s;
    for (int i = 0; i < 4; ++i) {
        // A gentle arc above and behind Mario, like TTYD's command wheel.
        const float ang = (-150.f + 30.f * static_cast<float>(i)) * kPi / 180.f;
        const ImVec2 c{mario.x + std::cos(ang) * 95.f * s, mario.y - 70.f * s + std::sin(ang) * 95.f * s};
        const bool sel = i == ui.menuIndex;
        const float r = (sel ? 27.f : 21.f) * s;
        const ImVec2 cc{c.x, c.y + (sel ? bob : 0.f)};
        dl->AddCircleFilled({cc.x + 2, cc.y + 4}, r, IM_COL32(0, 0, 0, 90), 32);
        dl->AddCircleFilled(cc, r, sel ? IM_COL32(255, 240, 170, 255) : IM_COL32(235, 230, 215, 235), 32);
        dl->AddCircle(cc, r, sel ? IM_COL32(230, 120, 20, 255) : IM_COL32(110, 90, 70, 255), 32, sel ? 4.f * s : 2.5f * s);
        CommandIcon(dl, i, cc, r * 0.85f);
        if (sel) {
            const char* name = kCommandNames[i];
            const ImVec2 ts = TextSize(26.f * s, name);
            const ImVec2 a{cc.x + r + 8.f * s, cc.y - 18.f * s};
            Panel(dl, a, {a.x + ts.x + 24.f * s, a.y + 36.f * s}, 16.f * s);
            OutlinedText(dl, {a.x + 12.f * s, a.y + 4.f * s}, 26.f * s, IM_COL32_WHITE, name, 2.f);
        }
    }
    Description(dl,size,s,ui.fracktail && !ui.weakPointOpen?"Tactics > Thoreau: Throw a Frackle to lower the head.":kCommandHelp[std::clamp(ui.menuIndex,0,3)]);
}

void ListMenu(ImDrawList* dl, const BattleUi& ui, const Mapping& m, float s, const char* title) {
    const ImVec2 mario = m(ui.mario.x, ui.mario.y);
    const float rowH = 30.f * s, w = 300.f * s;
    const int visible=std::min(6,static_cast<int>(ui.list.size()));
    const int first=std::clamp(ui.listIndex-2,0,std::max(0,static_cast<int>(ui.list.size())-visible));
    const float h = rowH * visible + 65.f * s;
    ImVec2 a{mario.x - w - 40.f * s, mario.y - h - 40.f * s};
    a.x = std::max(a.x, 40.f * s);
    a.y = std::max(a.y, 150.f * s);
    Panel(dl, a, {a.x + w, a.y + h}, 14.f * s);
    OutlinedText(dl, {a.x + 14.f * s, a.y + 8.f * s}, 22.f * s, IM_COL32(255, 230, 140, 255), title, 1.5f);
    for (int i = first; i < first+visible; ++i) {
        const ImVec2 r0{a.x + 8.f * s, a.y + 40.f * s + rowH * static_cast<float>(i-first)};
        const bool sel = static_cast<int>(i) == ui.listIndex;
        if (sel) dl->AddRectFilled(r0, {r0.x + w - 16.f * s, r0.y + rowH - 2.f * s}, IM_COL32(255, 220, 120, 70), 8.f * s);
        if (sel) dl->AddTriangleFilled({r0.x + 4 * s, r0.y + 7 * s}, {r0.x + 4 * s, r0.y + 23 * s}, {r0.x + 16 * s, r0.y + 15 * s},
                                       IM_COL32(255, 200, 60, 255));
        OutlinedText(dl, {r0.x + 22.f * s, r0.y + 3.f * s}, 18.f * s, sel ? IM_COL32_WHITE : IM_COL32(200, 200, 215, 255),
                     ui.list[i].c_str(), 1.5f);
    }
    const auto counter=std::to_string(ui.listIndex+1)+" / "+std::to_string(ui.list.size())+"     A: Select  B: Back";
    OutlinedText(dl,{a.x+15*s,a.y+h-22*s},14*s,Rgba(140,255,170),counter.c_str(),s);
}

// The test area's F5 menu: the story battles, each replayed from its intro.
void StoryMenu(ImDrawList* dl, float s, ImVec2 size) {
    const auto& battles = StoryBattles();
    const int selected = StoryMenuSelection();
    const float rowH = 46.f * s, w = 460.f * s;
    const float h = rowH * static_cast<float>(battles.size()) + 120.f * s;
    const ImVec2 a{size.x * 0.5f - w * 0.5f, std::max(40.f * s, size.y * 0.5f - h * 0.5f)};
    dl->AddRectFilled({0, 0}, size, IM_COL32(0, 8, 4, 120));
    Panel(dl, a, {a.x + w, a.y + h}, 14.f * s);
    OutlinedText(dl, {a.x + 18.f * s, a.y + 10.f * s}, 24.f * s, IM_COL32(255, 230, 140, 255), "Story battles", 1.5f);
    for (size_t i = 0; i < battles.size(); ++i) {
        const ImVec2 r0{a.x + 10.f * s, a.y + 48.f * s + rowH * static_cast<float>(i)};
        const bool sel = static_cast<int>(i) == selected;
        if (sel) {
            dl->AddRectFilled(r0, {r0.x + w - 20.f * s, r0.y + rowH - 4.f * s}, IM_COL32(255, 220, 120, 70), 8.f * s);
            dl->AddTriangleFilled({r0.x + 4 * s, r0.y + 12 * s}, {r0.x + 4 * s, r0.y + 28 * s}, {r0.x + 16 * s, r0.y + 20 * s},
                                  IM_COL32(255, 200, 60, 255));
        }
        OutlinedText(dl, {r0.x + 24.f * s, r0.y + 3.f * s}, 20.f * s, sel ? IM_COL32_WHITE : IM_COL32(200, 200, 215, 255),
                     battles[i].boss, 1.5f);
        OutlinedText(dl, {r0.x + 210.f * s, r0.y + 6.f * s}, 16.f * s, Rgba(140, 240, 255), battles[i].where, s);
        OutlinedText(dl, {r0.x + 24.f * s, r0.y + 25.f * s}, 13.f * s, Rgba(190, 210, 200), battles[i].about, s);
    }
    OutlinedText(dl, {a.x + 18.f * s, a.y + h - 62.f * s}, 14.f * s, Rgba(200, 255, 210),
                 "Plays the story scene, the battle, and the story after it.", s);
    OutlinedText(dl, {a.x + 18.f * s, a.y + h - 34.f * s}, 14.f * s, Rgba(140, 255, 170),
                 "Up / Down: choose     Enter / A: play     Esc / B: close", s);
}

void Prompts(ImDrawList* dl, const BattleUi& ui, const Mapping& m, float s, ImVec2 size) {
    const ImVec2 mario = m(ui.mario.x, ui.mario.y);
    const ImVec2 above{mario.x, mario.y - 110.f * s};
    switch (ui.prompt) {
    case UiPrompt::Stylish:
        ButtonGlyph(dl,above,18*s,"A",Rgba(170,80,240),ui.promptLit);
        CenteredText(dl,{above.x,above.y-30*s},20*s,Rgba(220,170,255),"Stylish!",s);
        break;
    case UiPrompt::PressA:
        if (ui.powerBounce) {
            const std::string count = "Power Bounce  " + std::to_string(ui.jumpHits + 1) + " / 7";
            CenteredText(dl, {above.x, above.y - 35.f*s}, 18.f*s, IM_COL32(170,255,120,255), count.c_str(), 2.f);
        }
        ButtonGlyph(dl, above, (ui.promptLit ? 22.f : 17.f) * s, "A", IM_COL32(40, 160, 60, 255), ui.promptLit);
        break;
    case UiPrompt::HoldLeft: {
        // Hold the stick left: four stars light one by one; let go on the big last one.
        CenteredText(dl, {above.x, above.y - 34.f * s}, 22.f * s, IM_COL32_WHITE, "Hold Left!", 2.f);
        for (int i = 0; i < 4; ++i) {
            const bool lit = ui.gauge >= (i + 1) * 0.25f;
            const float r = (i == 3 ? 15.f : 10.f) * s;
            Star(dl, {above.x - 48.f * s + 32.f * s * i, above.y}, r, r * 0.45f, 5,
                 lit ? IM_COL32(255, 220, 40, 255) : IM_COL32(90, 90, 110, 220), IM_COL32(40, 30, 10, 255));
        }
        if (ui.promptLit) CenteredText(dl, {above.x, above.y + 30.f * s}, 26.f * s, IM_COL32(255, 240, 120, 255), "NOW!", 2.f);
    } break;
    case UiPrompt::Mash: {
        CenteredText(dl, {above.x, above.y - 34.f * s}, 22.f * s, IM_COL32_WHITE, "Mash A!", 2.f);
        const ImVec2 a{above.x - 70.f * s, above.y - 10.f * s}, b{above.x + 70.f * s, above.y + 10.f * s};
        Panel(dl, a, b, 8.f * s);
        dl->AddRectFilled({a.x + 3 * s, a.y + 3 * s}, {a.x + 3 * s + (b.x - a.x - 6 * s) * ui.gauge, b.y - 3 * s},
                          IM_COL32(80, 200, 255, 255), 6.f * s);
        ButtonGlyph(dl, {b.x + 22.f * s, above.y}, 14.f * s, "A", IM_COL32(40, 160, 60, 255), (ui.phaseFrame / 4) % 2 == 0);
    } break;
    case UiPrompt::Guard: {
        const ImU32 c = ui.promptLit ? IM_COL32_WHITE : IM_COL32(220, 220, 230, 170);
        const char* text = "Guard just before the hit:";
        const ImVec2 ts = TextSize(20.f * s, text);
        const float y = size.y - 52.f * s;
        const float x0 = size.x * 0.5f - (ts.x + 220.f * s) * 0.5f;
        OutlinedText(dl, {x0, y - ts.y * 0.5f}, 20.f * s, c, text, 1.5f);
        ButtonGlyph(dl, {x0 + ts.x + 26.f * s, y}, 13.f * s, "A", IM_COL32(40, 160, 60, 255), false);
        OutlinedText(dl, {x0 + ts.x + 44.f * s, y - ts.y * 0.5f}, 20.f * s, c, "Guard", 1.5f);
        ButtonGlyph(dl, {x0 + ts.x + 130.f * s, y}, 13.f * s, "B", IM_COL32(200, 50, 50, 255), false);
        OutlinedText(dl, {x0 + ts.x + 148.f * s, y - ts.y * 0.5f}, 20.f * s, c, "Super", 1.5f);
    } break;
    default: break;
    }
}

void Effects(ImDrawList* dl,const BattleUi& ui,const Mapping& m,float s) {
    for(const auto& fx:ui.effects) {
        const float t=std::clamp(static_cast<float>(fx.age)/std::max(1,fx.life),0.f,1.f);
        ImVec2 a=m(fx.x,fx.y),b=m(fx.tx,fx.ty);
        a.y-=24*s;b.y-=24*s;
        const ImU32 color=WithAlpha(fx.color,1.f-t);
        const float r=(12+26*t)*s;
        switch(fx.type) {
        case 0:case 1: { // a digital shield, then a superguard flash
            const ImVec2 p[]={{a.x-20*s,a.y-26*s},{a.x+20*s,a.y-26*s},{a.x+24*s,a.y+4*s},{a.x,a.y+25*s},{a.x-24*s,a.y+4*s}};
            dl->AddConvexPolyFilled(p,5,WithAlpha(color,.22f));
            dl->AddPolyline(p,5,color,ImDrawFlags_Closed,(fx.type==1?5:3)*s);
            if(fx.type==1)Star(dl,a,r*1.6f,r*.75f,8,color,Rgba(255,255,255),t);
        }break;
        case 2:case 3: { // Cudge-shaped hammer; thrown hammers travel along the arc
            ImVec2 c=fx.type==3?ImVec2{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t-std::sin(t*kPi)*50*s}:b;
            CommandIcon(dl,1,c,24*s);
            dl->AddCircle(c,r,color,12,2*s);
        }break;
        case 4:
            for(int i=0;i<7;++i){const float q=std::fmod(t+i/7.f,1.f);ImVec2 c{a.x+(b.x-a.x)*q,a.y+(b.y-a.y)*q};
                Star(dl,c,(12+8*std::sin(q*kPi))*s,7*s,4,WithAlpha(Rgba(255,120,30),1-t),color,q);}
            break;
        case 5:
            dl->AddRectFilled({b.x-20*s,b.y-35*s},{b.x+20*s,b.y+22*s},WithAlpha(Rgba(80,210,255),.45f*(1-t)),2*s);
            dl->AddRect({b.x-20*s,b.y-35*s},{b.x+20*s,b.y+22*s},color,2*s,0,3*s);
            for(int i=0;i<3;++i)dl->AddLine({b.x-16*s,b.y+15*s-i*15*s},{b.x+16*s,b.y-i*15*s},color,2*s);
            break;
        case 6:
            for(int i=0;i<3;++i)dl->AddEllipse({b.x,b.y+25*s},ImVec2{r*(1+i*.5f),r*.2f},color,0,24,2*s);
            break;
        case 7:
            for(int i=0;i<5;++i){float y=b.y+30*s-i*17*s;float width=(10+i*7)*s;
                dl->AddEllipse({b.x+std::sin(t*16+i)*6*s,y},ImVec2{width,6*s},color,t*2,20,3*s);}
            break;
        case 8: {
            ImVec2 last=a;
            for(int i=1;i<=8;++i){const float q=i/8.f;ImVec2 p{a.x+(b.x-a.x)*q+(i%2?7:-7)*s,a.y+(b.y-a.y)*q};dl->AddLine(last,p,color,3*s);last=p;}
        }break;
        case 9:
            Heart(dl,{a.x,a.y-35*t*s},14*s,color);break;
        case 10:
            dl->AddCircle(a,r,color,16,3*s);Star(dl,a,12*s,5*s,4,color,Rgba(255,255,255),t*6);break;
        case 20:Star(dl,a,r,r*.35f,9,color,Rgba(255,230,100),t*3);break;
        case 21:
            for(int i=0;i<4;++i)Star(dl,{a.x+std::sin(i*2.f+t*8)*r,a.y+std::cos(i*2.f+t*8)*r},8*s,4*s,5,color,color,t);
            break;
        case 22:
            dl->AddCircle(a,r,color,24,3*s);dl->AddCircle(a,r*.6f,color,24,2*s);break;
        case 23:
            for(int i=-2;i<2;++i)dl->AddLine({a.x+i*12*s,a.y+(i%2?8:-8)*s},{a.x+(i+1)*12*s,a.y+(i%2?-8:8)*s},color,4*s);
            break;
        case 24:
            dl->AddCircleFilled({a.x,a.y+8*s},6*s,color,12);dl->AddLine({a.x+5*s,a.y+8*s},{a.x+5*s,a.y-20*s},color,3*s);
            dl->AddLine({a.x+5*s,a.y-20*s},{a.x+16*s,a.y-12*s},color,4*s);break;
        case 25:
            for(int i=0;i<20;++i) {
                const float q=std::fmod(t*1.8f+i*.05f,1.f),spread=8+15*q;
                const ImVec2 p{a.x+(b.x-a.x)*q,a.y+(b.y-a.y)*q+std::sin(i*2.f+t*12)*spread*s};
                const float r=(3+i%3)*s;
                dl->AddRectFilled({p.x-r,p.y-r},{p.x+r,p.y+r},WithAlpha(i%2?Rgba(80,255,180):Rgba(80,190,255),1-t));
            }break;
        case 26:
            for(int i=0;i<9;++i){const float q=i/8.f;ImVec2 p{a.x+(b.x-a.x)*q,a.y+(b.y-a.y)*q+(i%2?10:-10)*s};
                dl->AddRect(p,{p.x+12*s,p.y+8*s},color,0,0,2*s);}
            break;
        default:
            for(int i=0;i<8;++i){float angle=i*kPi/4+t*2;ImVec2 p{a.x+std::cos(angle)*r,a.y+std::sin(angle)*r};
                if(fx.type==12)dl->AddRect(p,{p.x+5*s,p.y+5*s},color,0,0,2*s);
                else Star(dl,p,6*s,3*s,fx.type==14?4:5,color,color,angle);}
            break;
        }
    }
}

void Popups(ImDrawList* dl, const BattleUi& ui, const Mapping& m, float s) {
    for (const auto& p : ui.popups) {
        const ImVec2 at = m(p.x, p.y);
        const float fade = p.age > 50 ? 1.f - (p.age - 50) / 20.f : 1.f;
        const float pop = std::min(1.f, p.age / 6.f);
        if (p.kind == 0) {
            // TTYD's damage starburst with the number in the middle.
            const float r = 24.f * s * (0.6f + 0.4f * pop);
            Star(dl, at, r, r * 0.68f, 12, WithAlpha(IM_COL32(255, 230, 60, 255), fade), WithAlpha(IM_COL32(200, 80, 20, 255), fade),
                 p.age * 0.02f);
            CenteredText(dl, at, 28.f * s * pop, WithAlpha(IM_COL32(230, 30, 30, 255), fade), p.text.c_str(), 2.f);
        } else {
            const ImU32 c = (p.color & 0x00FFFFFFu) | 0xFF000000u;
            CenteredText(dl, at, (p.kind == 2 ? 26.f : 30.f) * s * (0.7f + 0.3f * pop), WithAlpha(c, fade), p.text.c_str(), 2.5f);
        }
    }
}

void Banner(ImDrawList* dl, const BattleUi& ui, float s, ImVec2 size) {
    if (ui.banner.empty() || ui.bannerAge > 90) return;
    const float t = ui.bannerAge;
    const float in = std::min(1.f, t / 10.f);
    const float fade = t > 70 ? 1.f - (t - 70) / 20.f : 1.f;
    const float y = size.y * 0.36f;
    dl->AddRectFilled({0, y - 34.f * s * in}, {size.x, y + 34.f * s * in}, WithAlpha(IM_COL32(4, 12, 6, 200), fade));
    dl->AddRectFilled({0, y - 34.f * s * in}, {size.x, y - 31.f * s * in}, WithAlpha(IM_COL32(80, 255, 120, 255), fade));
    dl->AddRectFilled({0, y + 31.f * s * in}, {size.x, y + 34.f * s * in}, WithAlpha(IM_COL32(80, 255, 120, 255), fade));
    const float slide = (1.f - in) * size.x * 0.3f;
    CenteredText(dl, {size.x * 0.5f + slide, y}, 50.f * s, WithAlpha(IM_COL32(255, 240, 150, 255), fade), ui.banner.c_str(), 3.5f);
}

void LevelUp(ImDrawList* dl, const BattleUi& ui, float s, ImVec2 size) {
    const ImVec2 c{size.x * 0.5f, size.y * 0.62f};
    char title[48];
    std::snprintf(title, sizeof title, "Mario is now Level %d!  Choose one:", ui.level);
    CenteredText(dl, {c.x, c.y - 86.f * s}, 28.f * s, IM_COL32(255, 240, 140, 255), title, 3.f);
    const char* names[] = {"HP Up", "ATK Up", "BP Up"};
    const char* info[] = {"+5 max HP", "+1 attack", "+3 badge points"};
    for (int i = 0; i < 3; ++i) {
        const bool sel = i == ui.levelUpChoice;
        const float lift = sel ? -8.f * s + std::sin(ui.phaseFrame * 0.2f) * 3.f * s : 0.f;
        const ImVec2 a{c.x + (-255.f+175.f*i) * s, c.y - 60.f * s + lift};
        const ImVec2 b{a.x + 160.f * s, a.y + 130.f * s};
        Panel(dl, a, b, 16.f * s, sel ? IM_COL32(60, 40, 110, 240) : IM_COL32(30, 25, 50, 220),
              sel ? IM_COL32(255, 210, 80, 255) : IM_COL32(150, 140, 170, 255));
        if (i == 0) Heart(dl, {a.x + 85.f * s, a.y + 40.f * s}, 22.f * s, IM_COL32(235, 40, 60, 255));
        else if(i==1)CommandIcon(dl, 1, {a.x + 85.f * s, a.y + 42.f * s}, 30.f * s);
        else Star(dl,{a.x+80*s,a.y+42*s},26*s,15*s,6,Rgba(80,240,180),Rgba(10,70,40));
        CenteredText(dl, {a.x + 85.f * s, a.y + 84.f * s}, 26.f * s, IM_COL32_WHITE, names[i], 2.f);
        CenteredText(dl, {a.x + 85.f * s, a.y + 110.f * s}, 18.f * s, IM_COL32(220, 220, 240, 255), info[i], 1.5f);
    }
}

void FieldChip(ImDrawList* dl, const BattleUi& ui, float s) {
    // Outside battle: Star Points and level, tucked under the game's HP display.
    char text[48];
    std::snprintf(text, sizeof text, "%d SP  Lv %d  BP %d/%d", ui.starPoints, ui.level,ui.bpUsed,ui.bp);
    const ImVec2 ts = TextSize(18.f * s, text);
    const ImVec2 a{52.f * s, 74.f * s};
    dl->AddRectFilled(a, {a.x + ts.x + 40.f * s, a.y + 26.f * s}, IM_COL32(20, 15, 40, 150), 13.f * s);
    Star(dl, {a.x + 14.f * s, a.y + 13.f * s}, 9.f * s, 4.f * s, 5, IM_COL32(255, 215, 40, 255), IM_COL32(150, 90, 10, 255));
    OutlinedText(dl, {a.x + 28.f * s, a.y + 3.f * s}, 18.f * s, IM_COL32(255, 245, 200, 255), text, 1.5f);
}

}  // namespace

void DrawBattleUi() {
    using Clock = std::chrono::steady_clock;
    static uint32_t lastSerial = 0;
    static Clock::time_point lastPublish{};
    BattleUi ui = BattleUiSnapshot();
    // The game publishes from npcMain: when that stops (pause menu, map change, title) hide everything.
    if (ui.serial != lastSerial) {
        lastSerial = ui.serial;
        lastPublish = Clock::now();
    }
    if (ui.maxHp <= 0 || Clock::now() - lastPublish > std::chrono::milliseconds(250)) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // Everything is laid out in the game picture's own space (it can be letterboxed or pillarboxed), then moved.
    const ImVec2 window = ImGui::GetIO().DisplaySize;
    float rx = 0, ry = 0, rw = 1, rh = 1;
    aurora_get_present_rect(&rx, &ry, &rw, &rh);
    const ImVec2 origin{rx * window.x, ry * window.y};
    const ImVec2 size{std::max(1.f, rw * window.x), std::max(1.f, rh * window.y)};
    const int firstVertex = dl->VtxBuffer.Size;
    struct MoveIntoPicture {
        ImDrawList* dl;
        int first;
        ImVec2 by;
        ~MoveIntoPicture() {
            for (int i = first; i < dl->VtxBuffer.Size; ++i) {
                dl->VtxBuffer[i].pos.x += by.x;
                dl->VtxBuffer[i].pos.y += by.y;
            }
        }
    } move{dl, firstVertex, origin};
    Mapping m;
    m.scale = size.y / 480.f;
    m.ox = size.x * 0.5f - 304.f * m.scale;
    const float s = m.scale;

    if (!ui.active) {
        if(NativePauseOpen())return;
        FieldChip(dl, ui, s);
        if(!ui.pickupNotice.empty())Description(dl,size,s,ui.pickupNotice.c_str());
        if (TestAreaActive()) {
            CenteredText(dl, {size.x * 0.5f, 132.f * s}, 30.f * s, IM_COL32(40, 140, 60, 255), "TEST AREA", 3.f);
            std::string roster = "Test enemies:";
            for (const auto& name : TestAreaRoster()) roster += "   " + name;
            CenteredText(dl, {size.x * 0.5f, 162.f * s}, 18.f * s, IM_COL32_WHITE, roster.c_str(), 1.5f);
            CenteredText(dl,{size.x*.5f,187*s},17*s,IM_COL32_WHITE,"F5: story battles     F10 settings: Challenge Fracktail",s);
            if (StoryMenuOpen()) StoryMenu(dl, s, size);
        }
        return;
    }

    const float drawOn = ui.phase == BattlePhase::Intro ? std::min(1.f, ui.phaseFrame / 36.f) : 1.f;
    ZigzagBorder(dl, size, s, drawOn, ui.phaseFrame);
    StatusPanel(dl, ui, s, 46.f * s);
    if(ui.fracktail && !ui.enemies.empty()) {
        const auto& boss=ui.enemies[0];const ImVec2 a{size.x-275*s,46*s},b{size.x-40*s,130*s};
        Panel(dl,a,b,12*s);
        const auto hp="Fracktail  "+std::to_string(boss.hp)+" / "+std::to_string(boss.maxHp);
        CenteredText(dl,{(a.x+b.x)*.5f,a.y+20*s},21*s,Rgba(255,220,140),hp.c_str(),s);
        CenteredText(dl,{(a.x+b.x)*.5f,a.y+48*s},16*s,ui.weakPointOpen?Rgba(100,255,170):Rgba(140,220,255),ui.weakPointOpen?"ANTENNA EXPOSED":"Head raised - use Thoreau",s);
        CenteredText(dl,{(a.x+b.x)*.5f,a.y+69*s},14*s,Rgba(200,255,230),"Partner: Thoreau",s);
    }
    if(ui.glitchFrames>0)for(int i=0;i<8;++i) {
        const float y=std::fmod(i*79.f+ui.glitchFrames*3.f,440.f)*s;
        const float x=std::fmod(i*113.f+ui.glitchFrames*5.f,450.f)*s;
        dl->AddRectFilled({x,y},{std::min(size.x,x+(65+i*17)*s),y+(i%3+2)*s},Rgba(i%2?50:5,220,130,100));
        dl->AddLine({0,y+7*s},{size.x,y+7*s},Rgba(40,255,200,35),s);
    }
    for(size_t j=0;j<ui.mario.statuses.size();++j)
        CenteredText(dl,m(ui.mario.x,ui.mario.y-90-18*j),14*s,Rgba(200,160,255),ui.mario.statuses[j].c_str(),s);

    // Enemy HP bars at their feet.
    for (size_t i = 0; i < ui.enemies.size(); ++i) {
        const auto& e = ui.enemies[i];
        if (!e.alive) continue;
        if(ui.fracktail && i==0)continue;
        const ImVec2 feet = m(e.x, e.y);
        for(const auto& status:e.statuses)if(status.find("Frozen")==0) {
            dl->AddRectFilled({feet.x-23*s,feet.y-65*s},{feet.x+23*s,feet.y+5*s},Rgba(80,210,255,70),3*s);
            dl->AddRect({feet.x-23*s,feet.y-65*s},{feet.x+23*s,feet.y+5*s},Rgba(150,240,255,200),3*s,0,2*s);
        }
        for(size_t j=0;j<e.statuses.size();++j)
            CenteredText(dl,{feet.x,feet.y-(90+18*j)*s},14*s,Rgba(140,240,255),e.statuses[j].c_str(),s);
        if(!ui.revealHp)continue;
        const ImVec2 a{feet.x - 26.f * s, feet.y + 8.f * s}, b{feet.x + 26.f * s, feet.y + 16.f * s};
        dl->AddRectFilled({a.x - 2, a.y - 2}, {b.x + 2, b.y + 2}, IM_COL32(20, 15, 30, 220), 4.f);
        dl->AddRectFilled(a, {a.x + (b.x - a.x) * e.hp / std::max(1.f, static_cast<float>(e.maxHp)), b.y},
                          IM_COL32(240, 60, 60, 255), 3.f);
        char hp[16];
        std::snprintf(hp, sizeof hp, "%d", e.hp);
        OutlinedText(dl, {b.x + 6.f * s, a.y - 6.f * s}, 16.f * s, IM_COL32_WHITE, hp, 1.5f);
        const auto armor="DEF "+std::to_string(e.defense);
        CenteredText(dl,{feet.x,feet.y+30*s},13*s,Rgba(170,225,255),armor.c_str(),s);
    }

    switch (ui.phase) {
    case BattlePhase::Menu: CommandMenu(dl, ui, m, s, size); break;
    case BattlePhase::Target:
        if (ui.targetIndex >= 0 && ui.targetIndex < static_cast<int>(ui.enemies.size())) {
            const auto& e = ui.enemies[ui.targetIndex];
            const ImVec2 feet = m(e.x, e.y);
            const float bounce = std::fabs(std::sin(ui.phaseFrame * 0.18f)) * 8.f * s;
            const ImVec2 tip{feet.x, feet.y - 64.f * s - bounce};
            dl->AddTriangleFilled({tip.x - 14 * s, tip.y - 20 * s}, {tip.x + 14 * s, tip.y - 20 * s}, tip, IM_COL32(255, 210, 40, 255));
            dl->AddTriangle({tip.x - 14 * s, tip.y - 20 * s}, {tip.x + 14 * s, tip.y - 20 * s}, tip, IM_COL32(120, 60, 10, 255), 2.f * s);
            CenteredText(dl, {tip.x, tip.y - 36.f * s}, 22.f * s, IM_COL32_WHITE, e.name.c_str(), 2.f);
            Description(dl, size, s, "Choose a target.  A: OK   B: Back");
        }
        break;
    case BattlePhase::ItemMenu: ListMenu(dl, ui, m, s, "Items"); break;
    case BattlePhase::MoveMenu: ListMenu(dl,ui,m,s,ui.moveMenuType==0?"Jump moves":"Hammer moves");break;
    case BattlePhase::TacticsMenu: ListMenu(dl, ui, m, s, "Tactics"); break;
    case BattlePhase::LevelUp: LevelUp(dl, ui, s, size); break;
    default: break;
    }
    Prompts(dl, ui, m, s, size);
    Effects(dl,ui,m,s);
    Popups(dl, ui, m, s);
    Banner(dl, ui, s, size);
}

}  // namespace LostPages
