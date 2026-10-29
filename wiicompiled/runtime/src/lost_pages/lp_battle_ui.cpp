// Lost Pages battle overlay, TTYD style: stage curtains, the status panel, the command row beside Mario,
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
#include <initializer_list>
#include <string>
#include <vector>

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

// Paper-cut silhouettes: thick ink, offset paper edge, coloured facets and small highlights.
void CommandIcon(ImDrawList* dl, int index, ImVec2 c, float r) {
    const ImU32 dark = Rgba(31, 26, 34);
    auto p = [&](float x, float y) { return ImVec2{c.x+x*r,c.y+y*r}; };
    auto shape = [&](std::initializer_list<ImVec2> vertices, ImU32 fill) {
        dl->AddConcavePolyFilled(vertices.begin(),static_cast<int>(vertices.size()),fill);
        dl->AddPolyline(vertices.begin(),static_cast<int>(vertices.size()),dark,ImDrawFlags_Closed,r*.10f);
    };
    auto line = [&](float x,float y,float xx,float yy,ImU32 color,float width) {
        dl->AddLine(p(x,y),p(xx,yy),color,width*r);
    };
    switch (index) {
    case 0:  // Mario's cuffed boot, raised toe and stitched sole
        shape({p(-.58f,-.70f),p(.12f,-.70f),p(.14f,-.05f),p(.57f,-.16f),p(.80f,.03f),p(.85f,.47f),p(.62f,.65f),p(-.59f,.65f)},Rgba(177,83,35));
        shape({p(-.62f,-.74f),p(.17f,-.74f),p(.18f,-.43f),p(-.62f,-.43f)},Rgba(238,151,63));
        shape({p(-.61f,.40f),p(.84f,.35f),p(.81f,.60f),p(.59f,.75f),p(-.62f,.75f)},Rgba(89,49,28));
        line(-.43f,-.30f,-.43f,.22f,Rgba(247,173,87),.13f);
        line(.28f,-.01f,.56f,-.04f,Rgba(255,202,128),.13f);
        line(-.46f,.54f,.61f,.51f,Rgba(237,180,92),.045f);
        Star(dl,p(.63f,-.58f),r*.19f,r*.065f,4,Rgba(255,255,214),dark,.2f);
        break;
    case 1:  // oversized wooden mallet, iron bands and wrapped grip
        shape({p(-.08f,-.25f),p(.16f,-.18f),p(-.05f,.83f),p(-.31f,.77f)},Rgba(226,155,62));
        for(int i=0;i<4;++i)line(-.22f+i*.025f,.64f-i*.13f,.03f+i*.025f,.68f-i*.13f,Rgba(116,59,32),.055f);
        shape({p(-.76f,-.63f),p(-.51f,-.82f),p(.73f,-.52f),p(.80f,-.03f),p(.54f,.15f),p(-.72f,-.16f)},Rgba(181,105,47));
        shape({p(-.76f,-.63f),p(-.51f,-.82f),p(-.43f,-.31f),p(-.72f,-.16f)},Rgba(245,191,100));
        line(-.29f,-.66f,-.23f,-.21f,Rgba(211,224,222),.19f);
        line(.49f,-.46f,.53f,-.02f,Rgba(211,224,222),.19f);
        line(-.08f,-.53f,.29f,-.44f,Rgba(249,181,84),.065f);
        line(-.06f,-.33f,.27f,-.25f,Rgba(112,57,30),.045f);
        break;
    case 2: { // spotted Super Mushroom, face and cream underside
        dl->AddRectFilled(p(-.42f,-.10f),p(.42f,.72f),Rgba(255,229,174),r*.23f);
        dl->AddRect(p(-.42f,-.10f),p(.42f,.72f),dark,r*.23f,0,r*.1f);
        ImVec2 cap[25];
        for(int i=0;i<25;++i){float a=kPi+i*kPi/24;cap[i]=p(std::cos(a)*.81f,std::sin(a)*.80f+.10f);}
        dl->AddConvexPolyFilled(cap,25,Rgba(234,52,59));
        dl->AddPolyline(cap,25,dark,ImDrawFlags_Closed,r*.10f);
        dl->AddEllipse(p(0,-.48f),{r*.22f,r*.24f},Rgba(255,250,226),0,20,r*.21f);
        dl->AddCircleFilled(p(-.53f,-.19f),r*.16f,Rgba(255,250,226),20);
        dl->AddCircleFilled(p(.53f,-.19f),r*.16f,Rgba(255,250,226),20);
        line(-.16f,.27f,-.16f,.46f,dark,.09f);line(.16f,.27f,.16f,.46f,dark,.09f);
        line(-.37f,-.49f,-.21f,-.61f,Rgba(255,169,163),.08f);
    }break;
    case 3: // folded tactics pennant and gold finial
        line(-.50f,-.73f,-.50f,.79f,dark,.19f);
        line(-.50f,-.69f,-.50f,.74f,Rgba(244,201,113),.09f);
        shape({p(-.42f,-.66f),p(.16f,-.74f),p(.77f,-.40f),p(.39f,-.15f),p(.66f,.18f),p(.03f,.04f),p(-.42f,.13f)},Rgba(59,159,235));
        shape({p(.16f,-.74f),p(.18f,-.12f),p(.66f,.18f),p(.39f,-.15f),p(.77f,-.40f)},Rgba(41,86,178));
        Star(dl,p(-.08f,-.30f),r*.22f,r*.10f,5,Rgba(255,238,135),dark);
        dl->AddCircleFilled(p(-.50f,-.79f),r*.13f,Rgba(255,219,112),16);
        break;
    case 4: { // Pure Heart: rainbow halo with a faceted pink crystal heart
        const ImU32 rainbow[]={Rgba(255,96,127),Rgba(255,175,75),Rgba(255,232,105),Rgba(99,235,144),Rgba(90,201,255),Rgba(183,135,255)};
        for(int i=0;i<6;++i){dl->PathArcTo(c,r*.89f,-kPi+i*kPi/3.f,-kPi+(i+1)*kPi/3.f,10);dl->PathStroke(rainbow[i],0,r*.13f);}
        shape({p(0,-.31f),p(-.29f,-.60f),p(-.61f,-.48f),p(-.73f,-.14f),p(-.51f,.22f),p(0,.74f),p(.51f,.22f),p(.73f,-.14f),p(.61f,-.48f),p(.29f,-.60f)},Rgba(249,91,154));
        dl->AddTriangleFilled(p(0,-.22f),p(-.48f,-.27f),p(0,.57f),Rgba(255,164,200));
        dl->AddTriangleFilled(p(0,-.22f),p(.49f,-.27f),p(0,.57f),Rgba(203,55,134));
        line(-.44f,-.32f,-.30f,-.43f,Rgba(255,249,234),.10f);
        Star(dl,p(.64f,-.65f),r*.21f,r*.06f,4,Rgba(255,255,237),dark);
    }break;
    default:
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

ImU32 Rainbow(float phase, float alpha = 1.f);

// One counter in the game's own HUD style (its HP / SCORE / COINS row): an icon, a coloured label, the value in
// white, all heavily outlined, over a zigzag underline in the label's colour. No panel behind it.
float NativeCounter(ImDrawList* dl, ImVec2 at, float s, int icon, const char* label, ImU32 color, const char* value) {
    const ImU32 ink = Rgba(40, 20, 50);
    const ImVec2 c{at.x + 12.f * s, at.y + 13.f * s};
    if (icon == 0) Heart(dl, c, 12.f * s, Rgba(240, 60, 110));
    else if (icon == 1) Star(dl, c, 13.f * s, 6.f * s, 5, Rgba(255, 215, 40), Rgba(150, 90, 10));
    else if (icon == 2) {  // a flower, for FP
        for (int i = 0; i < 5; ++i) {
            const float a = i * 2.f * kPi / 5.f - kPi / 2.f;
            dl->AddCircleFilled({c.x + std::cos(a) * 6.f * s, c.y + std::sin(a) * 6.f * s}, 5.f * s, Rgba(255, 140, 60), 12);
        }
        dl->AddCircleFilled(c, 4.5f * s, Rgba(255, 240, 120), 12);
    } else Star(dl, c, 12.f * s, 9.f * s, 8, Rgba(120, 240, 255), Rgba(20, 90, 120));  // energy
    float x = at.x + 28.f * s;
    OutlinedText(dl, {x, at.y + 3.f * s}, 19.f * s, color, label, 2.5f * s, ink);
    x += TextSize(19.f * s, label).x + 6.f * s;
    OutlinedText(dl, {x, at.y + 1.f * s}, 22.f * s, IM_COL32_WHITE, value, 2.5f * s, ink);
    x += TextSize(22.f * s, value).x;
    // The zigzag underline.
    const float y = at.y + 30.f * s;
    ImVec2 prev{at.x + 22.f * s, y};
    for (float px = prev.x + 6.f * s; px <= x + 6.f * s; px += 6.f * s) {
        const ImVec2 p{px, y + ((static_cast<int>((px - at.x) / (6.f * s)) % 2) ? -3.f : 3.f) * s};
        dl->AddLine(prev, p, ink, 5.f * s);
        dl->AddLine(prev, p, color, 2.5f * s);
        prev = p;
    }
    return x + 18.f * s;
}

// In battle the map (and the game's own HUD row with it) is hidden by the diorama, so the row is drawn in its
// place: HP, FP, Star Points (SP) and Star Energy, along the top like the game's HP / SCORE / COINS.
void StatusPanel(ImDrawList* dl, const BattleUi& ui, float s, float y) {
    char hp[32], sp[32], fp[32], energy[32];
    std::snprintf(hp, sizeof hp, "%d/%d", ui.hp, ui.maxHp);
    std::snprintf(sp, sizeof sp, "%d", ui.starPoints);
    std::snprintf(fp, sizeof fp, "%d/%d", ui.fp, ui.maxFp);
    std::snprintf(energy, sizeof energy, "%d", ui.energy);
    float x = 52.f * s;
    x = NativeCounter(dl, {x, y}, s, 0, "HP", Rgba(255, 110, 170), hp);
    x = NativeCounter(dl, {x, y}, s, 2, "FP", Rgba(255, 170, 70), fp);
    x = NativeCounter(dl, {x, y}, s, 1, "SP", Rgba(255, 215, 50), sp);
    NativeCounter(dl, {x, y}, s, 3, "ENERGY", Rgba(120, 225, 255), energy);
}

const char* const kCommandNames[] = {"Jump", "Hammer", "Items", "Tactics", "Special"};
const char* const kCommandHelp[] = {
    "Jump on an enemy. Press A right before you land for a second stomp!",
    "Swing at an enemy. Hold Left, and let go when the last star lights up!",
    "Use an item from your bag.",
    "Defend, or try to run away.",
    "Call on the Pure Hearts.  Left/Right: Browse    A: Select",
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
    const int count = ui.hasSpecial ? 5 : 4;
    const float margin = ((count-1)*34.f+37.f)*s;
    const float centre=std::clamp(mario.x,margin,size.x-margin);
    for (int i = 0; i < count; ++i) {
        const ImVec2 c{centre+(i-(count-1)*.5f)*68.f*s,std::max(175.f*s,mario.y-110.f*s)};
        const bool sel = i == ui.menuIndex;
        const float r = (sel ? 30.f : 25.f) * s;
        const ImVec2 cc{c.x, c.y + (sel ? bob : 0.f)};
        if(sel)Star(dl,cc,r*1.22f,r*1.06f,12,Rgba(255,235,157,105),Rgba(255,255,229,160),ui.phaseFrame*.008f);
        dl->AddCircleFilled({cc.x + 2*s, cc.y + 4*s}, r, IM_COL32(0, 0, 0, 90), 32);
        dl->AddCircleFilled(cc, r, sel ? IM_COL32(255, 240, 170, 255) : IM_COL32(235, 230, 215, 235), 32);
        dl->AddCircle(cc, r, sel ? IM_COL32(230, 120, 20, 255) : IM_COL32(110, 90, 70, 255), 32, sel ? 4.f * s : 2.5f * s);
        dl->AddCircle(cc,r-4*s,Rgba(255,255,241,210),32,s);
        CommandIcon(dl, i, cc, r * 0.86f);
        if (sel) {
            const char* name = kCommandNames[i];
            const ImVec2 ts = TextSize(26.f * s, name);
            const ImVec2 a{cc.x-ts.x*.5f-12.f*s,cc.y+39.f*s};
            Panel(dl, a, {a.x + ts.x + 24.f * s, a.y + 36.f * s}, 16.f * s);
            OutlinedText(dl, {a.x + 12.f * s, a.y + 4.f * s}, 26.f * s, IM_COL32_WHITE, name, 2.f);
        }
    }
    Description(dl,size,s,ui.fracktail && !ui.weakPointOpen?"Tactics > Thoreau: Throw a Frackle to lower the head.":kCommandHelp[std::clamp(ui.menuIndex,0,count-1)]);
}

void ListMenu(ImDrawList* dl, const BattleUi& ui, const Mapping& m, float s, ImVec2 size, const char* title) {
    if(ui.list.empty())return;
    const ImVec2 mario=m(ui.mario.x,ui.mario.y);
    const float cx=std::clamp(mario.x,210.f*s,std::max(210.f*s,size.x-210.f*s));
    const float cy=std::max(205.f*s,mario.y-100.f*s);
    CenteredText(dl,{cx,cy-58*s},22*s,Rgba(255,230,140),title,s);
    for(int offset=-1;offset<=1;++offset) {
        const int index=ui.listIndex+offset;
        if(index<0 || index>=static_cast<int>(ui.list.size()))continue;
        const float width=(offset==0?205.f:155.f)*s;
        const ImVec2 a{cx+offset*190.f*s-width*.5f,cy-30*s};
        const ImVec2 b{a.x+width,cy+30*s};
        Panel(dl,a,b,10*s);
        if(offset==0)dl->AddRect(a,b,Rgba(100,255,160),10*s,0,3*s);
        const char* text=ui.list[index].c_str();
        const float font=std::min(18.f*s,(width-16*s)/std::max(1.f,TextSize(18.f*s,text).x)*18.f*s);
        CenteredText(dl,{(a.x+b.x)*.5f,cy},font,offset==0?IM_COL32_WHITE:Rgba(170,190,180),text,s);
    }
    const auto counter=std::to_string(ui.listIndex+1)+" / "+std::to_string(ui.list.size())+"    Left/Right: Browse    A: Select    B: Back";
    CenteredText(dl,{cx,cy+49*s},14*s,Rgba(140,255,170),counter.c_str(),s);
}

// The test area's F5 / F6 menu: the story battles, each replayed from its intro, or the cutscene teleporter.
void StoryMenu(ImDrawList* dl, float s, ImVec2 size) {
    struct Row {
        const char* name;
        const char* where;
        const char* about;
    };
    std::vector<Row> rows;
    const bool scenes = CutsceneMenu();
    if (scenes)
        for (const auto& c : Cutscenes()) rows.push_back({c.name, c.where, c.about});
    else
        for (const auto& b : StoryBattles()) rows.push_back({b.boss, b.where, b.about});
    const int selected = StoryMenuSelection();
    const float rowH = 46.f * s, w = 520.f * s;
    const float h = rowH * static_cast<float>(rows.size()) + 120.f * s;
    const ImVec2 a{size.x * 0.5f - w * 0.5f, std::max(40.f * s, size.y * 0.5f - h * 0.5f)};
    dl->AddRectFilled({0, 0}, size, IM_COL32(0, 8, 4, 120));
    Panel(dl, a, {a.x + w, a.y + h}, 14.f * s);
    OutlinedText(dl, {a.x + 18.f * s, a.y + 10.f * s}, 24.f * s, IM_COL32(255, 230, 140, 255),
                 scenes ? "Cutscene teleporter" : "Story battles", 1.5f);
    for (size_t i = 0; i < rows.size(); ++i) {
        const ImVec2 r0{a.x + 10.f * s, a.y + 48.f * s + rowH * static_cast<float>(i)};
        const bool sel = static_cast<int>(i) == selected;
        if (sel) {
            dl->AddRectFilled(r0, {r0.x + w - 20.f * s, r0.y + rowH - 4.f * s}, IM_COL32(255, 220, 120, 70), 8.f * s);
            dl->AddTriangleFilled({r0.x + 4 * s, r0.y + 12 * s}, {r0.x + 4 * s, r0.y + 28 * s}, {r0.x + 16 * s, r0.y + 20 * s},
                                  IM_COL32(255, 200, 60, 255));
        }
        OutlinedText(dl, {r0.x + 24.f * s, r0.y + 3.f * s}, 20.f * s, sel ? IM_COL32_WHITE : IM_COL32(200, 200, 215, 255),
                     rows[i].name, 1.5f);
        const ImVec2 ws = TextSize(16.f * s, rows[i].where);
        OutlinedText(dl, {r0.x + w - 34.f * s - ws.x, r0.y + 6.f * s}, 16.f * s, Rgba(140, 240, 255), rows[i].where, s);
        OutlinedText(dl, {r0.x + 24.f * s, r0.y + 25.f * s}, 13.f * s, Rgba(190, 210, 200), rows[i].about, s);
    }
    OutlinedText(dl, {a.x + 18.f * s, a.y + h - 62.f * s}, 14.f * s, Rgba(200, 255, 210),
                 scenes ? "Warps to the scene's room at the right story point and starts it."
                        : "Plays the story scene, the battle, and the story after it.", s);
    OutlinedText(dl, {a.x + 18.f * s, a.y + h - 34.f * s}, 14.f * s, Rgba(140, 255, 170),
                 "Up / Down: choose     Enter / A: go     Esc / B: close", s);
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
        if(!ui.timingTutor)break;
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

ImU32 Rainbow(float phase, float alpha) {
    const float hue=phase-std::floor(phase);
    float red,green,blue;
    ImGui::ColorConvertHSVtoRGB(hue,.57f,1.f,red,green,blue);
    return Rgba(static_cast<int>(red*255),static_cast<int>(green*255),static_cast<int>(blue*255),static_cast<int>(255*std::clamp(alpha,0.f,1.f)));
}

void PureShield(ImDrawList* dl,ImVec2 c,float s,float time,float alpha) {
    const float pulse=1.f+.025f*std::sin(time*.08f);
    const float rx=60*s*pulse,ry=83*s*pulse;
    dl->AddEllipse(c,{rx,ry},Rgba(202,235,255,static_cast<int>(alpha*24)),0,64,14*s);
    for(int i=0;i<48;++i) {
        float a=i*2*kPi/48,b=(i+1)*2*kPi/48;
        ImVec2 p{c.x+std::cos(a)*rx,c.y+std::sin(a)*ry},q{c.x+std::cos(b)*rx,c.y+std::sin(b)*ry};
        dl->AddLine(p,q,Rainbow(i/48.f+time*.003f,alpha*.22f),10*s);
        dl->AddLine(p,q,Rainbow(i/48.f+time*.003f,alpha),3*s);
        if(i%8==0)Star(dl,p,5*s,2*s,4,WithAlpha(IM_COL32_WHITE,alpha),Rainbow(i/48.f,alpha),a);
    }
    dl->AddEllipse(c,{rx*.84f,ry*.88f},WithAlpha(IM_COL32_WHITE,alpha*.45f),0,48,s);
}

void Effects(ImDrawList* dl,const BattleUi& ui,const Mapping& m,float s) {
    if(ui.finalBoss!=0 && ui.pureShield && ui.mario.alive)
        PureShield(dl,m(ui.mario.x,ui.mario.y-54),s,static_cast<float>(ui.serial),.85f);
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
        case 30: // rainbow Pure Heart barrier; coordinates are the shield's centre
            PureShield(dl,a,s*(1.f+.15f*t),static_cast<float>(fx.age),std::sin(t*kPi));
            break;
        case 31: { // void beam: dark core, violet corona and a white hot centre
            const float envelope=std::sin(t*kPi);
            dl->AddLine(a,b,WithAlpha(Rgba(127,52,232),envelope*.20f),32*s*envelope);
            dl->AddLine(a,b,WithAlpha(Rgba(189,97,255),envelope*.65f),17*s*envelope);
            dl->AddLine(a,b,WithAlpha(Rgba(15,4,30),envelope),10*s*envelope);
            dl->AddLine(a,b,WithAlpha(Rgba(242,220,255),envelope),2*s);
            for(int i=0;i<9;++i){float q=std::fmod(t*2+i/9.f,1.f);
                ImVec2 p{a.x+(b.x-a.x)*q,a.y+(b.y-a.y)*q};
                Star(dl,p,(4+3*std::sin(q*kPi))*s,2*s,4,WithAlpha(Rgba(218,168,255),envelope),color,q+t*3);}
            Star(dl,b,(14+16*envelope)*s,6*s,8,WithAlpha(Rgba(221,175,255),envelope),color,t*4);
        }break;
        case 32: { // tumbling, outlined cards unfold into a checkered block barrage
            for(int i=0;i<7;++i){const float q=std::clamp(t*1.5f-i*.065f,0.f,1.f);
                if(q<=0.f || q>=1.f)continue;
                const ImVec2 c{a.x+(b.x-a.x)*q,a.y+(b.y-a.y)*q-std::sin(q*kPi)*(42+i%3*17)*s};
                const float angle=q*5+i*.7f,cr=std::cos(angle),sr=std::sin(angle);
                auto p=[&](float x,float y){return ImVec2{c.x+(x*cr-y*sr)*s,c.y+(x*sr+y*cr)*s};};
                const ImVec2 corners[]={p(-11,-15),p(11,-15),p(11,15),p(-11,15)};
                const ImU32 ink=WithAlpha(Rgba(34,11,52),1-t);
                dl->AddConvexPolyFilled(corners,4,WithAlpha(i%2?Rgba(245,230,255):Rgba(154,244,180),1-t));
                dl->AddPolyline(corners,4,ink,ImDrawFlags_Closed,2*s);
                dl->AddQuadFilled(p(-7,-11),p(0,-11),p(0,0),p(-7,0),ink);
                dl->AddQuadFilled(p(0,0),p(7,0),p(7,11),p(0,11),ink);
                Star(dl,c,4*s,2*s,4,WithAlpha(Rgba(209,90,244),1-t),ink,angle);}
        }break;
        case 33: { // Reality Split: broken glass seams; global flash is ui.whiteFlash
            const float radius=(35+230*t)*s;
            for(int i=0;i<7;++i){const float angle=i*2*kPi/7+.17f;
                ImVec2 mid{b.x+std::cos(angle+.13f)*radius*.46f,b.y+std::sin(angle+.13f)*radius*.46f};
                ImVec2 end{b.x+std::cos(angle)*radius,b.y+std::sin(angle)*radius};
                dl->AddLine(b,mid,WithAlpha(Rgba(145,68,230),1-t),7*s);
                dl->AddLine(mid,end,WithAlpha(Rgba(145,68,230),1-t),5*s);
                dl->AddLine(b,mid,WithAlpha(IM_COL32_WHITE,1-t),2*s);
                dl->AddLine(mid,end,WithAlpha(IM_COL32_WHITE,1-t),s);
                dl->AddTriangleFilled(mid,end,{end.x+18*s*std::sin(angle),end.y-18*s*std::cos(angle)},WithAlpha(Rgba(227,209,255),.15f*(1-t)));}
        }break;
        case 34: { // Voidsplitter: green and purple blades shear past one another
            const ImVec2 c{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
            for(int j=0;j<2;++j){const float sign=j?1.f:-1.f;
                const ImU32 blade=WithAlpha(j?Rgba(154,78,255):Rgba(100,255,124),std::sin(t*kPi));
                const ImVec2 start{c.x-sign*60*s,c.y-66*s},end{c.x+sign*60*s,c.y+66*s};
                dl->AddLine(start,end,WithAlpha(blade,.18f),20*s);
                dl->AddTriangleFilled(start,{c.x+sign*14*s,c.y-4*s},end,blade);
                dl->AddLine(start,end,WithAlpha(IM_COL32_WHITE,std::sin(t*kPi)*.8f),s);}
        }break;
        case 35: { // Lover's Grief: pink hearts bound by green thorn ribbons
            for(int i=0;i<8;++i){float q=std::fmod(t+i/8.f,1.f),angle=q*2*kPi+t*3;
                ImVec2 c{b.x+std::cos(angle)*(30+25*t)*s,b.y-45*s+std::sin(angle)*38*s-30*t*s};
                Heart(dl,c,(6+3*std::sin(q*kPi))*s,WithAlpha(Rgba(255,118,196),1-t));
                ImVec2 next{b.x+std::cos(angle+.42f)*42*s,b.y-45*s+std::sin(angle+.42f)*38*s-30*t*s};
                dl->AddLine(c,next,WithAlpha(Rgba(121,255,146),.8f*(1-t)),2*s);
                dl->AddTriangleFilled(next,{next.x-7*s,next.y-9*s},{next.x-3*s,next.y+3*s},WithAlpha(Rgba(121,255,146),1-t));}
        }break;
        case 36: // minion spawn motes only; native actor renderer owns sprites / mini-Luigis
            for(int i=0;i<8;++i){float angle=i*kPi/4+t*3;
                Star(dl,{a.x+std::cos(angle)*r,a.y+std::sin(angle)*r},5*s,2*s,4,
                    WithAlpha(i%2?Rgba(143,255,156):Rgba(186,112,255),1-t),color,angle);}
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

void FieldChip(ImDrawList* dl, const BattleUi& ui, float s, ImVec2 size) {
    // Outside battle: Star Points and level as one more counter in the game's HUD style, right under its HP. The
    // game lays its HUD out in a centred 16:9 frame and slides it up out of view (hudSlide, 0..150 HUD units).
    char text[48];
    std::snprintf(text, sizeof text, "%d  Lv %d", ui.starPoints, ui.level);
    const float hudLeft = std::max(0.f, size.x * 0.5f - size.y * (16.f / 9.f) * 0.5f);
    const float k = s * 1.2f;  // the game's counters are drawn a little larger than the battle row's
    NativeCounter(dl, {hudLeft + 70.f * s, (72.f - ui.hudSlide) * s}, k, 1, "STAR PTS", Rgba(255, 215, 50), text);
}

void LevelPicker(ImDrawList* dl, const BattleUi& ui, float s, ImVec2 size) {
    const float w = 360.f * s, h = 190.f * s;
    const ImVec2 a{size.x * 0.5f - w * 0.5f, size.y * 0.5f - h * 0.5f};
    dl->AddRectFilled({0, 0}, size, IM_COL32(0, 8, 4, 120));
    Panel(dl, a, {a.x + w, a.y + h}, 14.f * s);
    OutlinedText(dl, {a.x + 18.f * s, a.y + 10.f * s}, 24.f * s, IM_COL32(255, 230, 140, 255), "Mario's level", 1.5f);
    char level[16];
    std::snprintf(level, sizeof level, "Lv %d", ui.levelPick);
    CenteredText(dl, {size.x * 0.5f, a.y + 82.f * s}, 46.f * s, IM_COL32_WHITE, level, 3.f);
    const float arrowY = a.y + 82.f * s;
    for (int side : {-1, 1}) {
        const float x = size.x * 0.5f + side * 120.f * s;
        dl->AddTriangleFilled({x + side * 14.f * s, arrowY}, {x - side * 4.f * s, arrowY - 14.f * s}, {x - side * 4.f * s, arrowY + 14.f * s},
                              IM_COL32(255, 200, 60, 255));
    }
    const int ups = ui.levelPick - 1;
    char stats[64];
    std::snprintf(stats, sizeof stats, "HP %d    ATK %d    BP %d", 10 + 5 * ((ups + 2) / 3), 1 + (ups + 1) / 3, std::min(99, 3 + 3 * (ups / 3)));
    CenteredText(dl, {size.x * 0.5f, a.y + 126.f * s}, 18.f * s, Rgba(140, 240, 255), stats, s);
    OutlinedText(dl, {a.x + 18.f * s, a.y + h - 34.f * s}, 14.f * s, Rgba(140, 255, 170),
                 "Left/Right: 1   Up/Down: 10   Enter: set   Esc: close", s);
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

        if(!ui.pickupNotice.empty())Description(dl,size,s,ui.pickupNotice.c_str());
        if (TestAreaActive()) {
            CenteredText(dl, {size.x * 0.5f, 132.f * s}, 30.f * s, IM_COL32(40, 140, 60, 255), "TEST AREA", 3.f);
            std::string roster = "Test enemies:";
            for (const auto& name : TestAreaRoster()) roster += "   " + name;
            CenteredText(dl, {size.x * 0.5f, 162.f * s}, 18.f * s, IM_COL32_WHITE, roster.c_str(), 1.5f);
            CenteredText(dl,{size.x*.5f,187*s},17*s,IM_COL32_WHITE,"F5: story battles     F6: cutscenes     F7: level     F10 settings: Challenge Fracktail",s);
            if (StoryMenuOpen()) StoryMenu(dl, s, size);
            if (ui.levelPicker) LevelPicker(dl, ui, s, size);
        }
        // Only alongside the game's own HUD: it slides away in cutscenes, dialogue and menus, and so does this.
        if (ui.fieldHud && ui.hudSlide < 140.f) FieldChip(dl, ui, s, size);
        return;
    }

    const float drawOn = ui.phase == BattlePhase::Intro ? std::min(1.f, std::max(0,ui.phaseFrame-56) / 20.f) : 1.f;
    // Stage flash precedes menus, numbers and banners so action feedback remains readable.
    if(ui.whiteFlash>0.f)dl->AddRectFilled({0,0},size,WithAlpha(IM_COL32_WHITE,ui.whiteFlash));
    ZigzagBorder(dl, size, s, drawOn, ui.phaseFrame);
    StatusPanel(dl, ui, s, 38.f * s);

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
    case BattlePhase::ItemMenu: ListMenu(dl, ui, m, s, size, "Items"); break;
    case BattlePhase::MoveMenu: ListMenu(dl,ui,m,s,size,ui.moveMenuType==0?"Jump moves":"Hammer moves");break;
    case BattlePhase::TacticsMenu: ListMenu(dl, ui, m, s, size, "Tactics"); break;
    case BattlePhase::SpecialMenu: ListMenu(dl, ui, m, s, size, "Special - Pure Hearts"); break;
    case BattlePhase::LevelUp: LevelUp(dl, ui, s, size); break;
    default: break;
    }
    Prompts(dl, ui, m, s, size);
    Effects(dl,ui,m,s);
    Popups(dl, ui, m, s);
    Banner(dl, ui, s, size);
    if(ui.phase==BattlePhase::Intro && ui.phaseFrame<56) {
        const int frame=ui.phaseFrame;
        const float cover=frame<18?frame/18.f:frame<34?1.f:std::max(0.f,(56-frame)/22.f);
        if(frame>=18 && frame<=34)dl->AddRectFilled({0,0},size,Rgba(0,14,6));
        const float cell=32.f*s;
        for(int row=0;row*cell<size.y;++row)for(int col=0;col*cell<size.x;++col) {
            const float threshold=((row*37+col*19)%101)/100.f;
            if(cover>=1.f || threshold<cover) {
                const ImVec2 a{col*cell,row*cell},b{std::min(size.x,a.x+cell+1),std::min(size.y,a.y+cell+1)};
                dl->AddRectFilled(a,b,(row+col)%3?Rgba(0,18,7):Rgba(8,74,28));
                dl->AddLine(a,{b.x,a.y},Rgba(45,255,115,160),s);
            }
        }
    }

}

}  // namespace LostPages
