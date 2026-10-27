#include "lp_progress.h"
#include "lp_badge_system.h"
#include "lp_badge_label_art.h"
#include "runtime_log.h"
#include <aurora/gfx.h>
#include <imgui.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <vector>

extern "C" void GX_HLE_FIFO_WriteFloat(float value);
extern "C" void GX_HLE_FIFO_Write32(uint32_t value);

extern "C" void func_80183534(CpuContext* ctx);
extern "C" void func_801851E8(CpuContext* ctx);
extern "C" void func_80185800(CpuContext* ctx);
extern "C" void func_80185A58(CpuContext* ctx);
extern "C" void func_801862A4(CpuContext* ctx);

namespace LostPages {
namespace {
std::atomic<bool> paused{false}, showing{false};
std::atomic<int> badgeSelection{0};
bool browsingBadges=false;
std::string badgeNotice;
std::atomic<int64_t> lastTick{0};
int badgeId = -1;
int badgeMenuFrame = 0;
uint32_t helpText = 0;
constexpr uint32_t kBadgeLabel = 0x817F0100; // native-only display callback, never guest code
int64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
uint32_t Plus(Guest& g) { return Memory::Read32(g.Sda()-32272); }
uint32_t Entry(Guest& g, int id) {
    const auto work = Memory::Read32(g.Sda()-30160);
    if (!Memory::Contains(work, 28) || id<0 || id>=static_cast<int>(Memory::Read32(work))) return 0;
    const auto p = Memory::Read32(work+4) + id*120;
    return Memory::Contains(p,120) && (Memory::Read16(p)&1) ? p : 0;
}
uint32_t Row(Guest& g, int index) { return Entry(g, static_cast<int>(Memory::Read32(Plus(g)+4+index*4))); }
void Page(Guest& g, bool badge) {
    showing.store(badge);
    if(!badge) browsingBadges=false;
    if (badge) {
        const int selected=static_cast<int>(Memory::Read32(Plus(g)+136));
        if (const auto old=Row(g,selected)) Memory::Write16(old,Memory::Read16(old)&~0x110u);
    }
    if (const auto entry=Entry(g,badgeId))
        Memory::Write16(entry,badge ? Memory::Read16(entry)|0x110u : Memory::Read16(entry)&~0x110u);
    // The original stats texture includes colored stat rows. Use the game's plain paper panel for badges.
    if (const auto panel=Row(g,10)) Memory::Write32(panel+64,badge?1:0);
}
void Text(Guest& g, const char* text, float x, float y, float scale=0.8f, uint32_t color=0x151520FF) {
    const auto ptr=g.Scratch(0x180);
    const auto n=std::strlen(text);
    for (size_t i=0;i<=n;++i) Memory::Write8(ptr+static_cast<uint32_t>(i),text[i]);
    g.Call(0x8019F2E4); // FontDrawStart: the original game's font and GX path
    Memory::Write32(g.Scratch(0x170),color);
    g.Call(0x8019F72C,{g.Scratch(0x170)});
    g.Call(0x8019F858,{}, {scale,scale});
    g.Call(0x8019FC48,{ptr},{x,y});
}
void Help(Guest& g) {
    if (const auto box=Row(g,9); box && helpText) {
        std::string message;
        if(browsingBadges) {
            message=kBadges[badgeSelection.load()].description;
            if(!badgeNotice.empty()) message=badgeNotice+". "+message;
        } else message="Browse badges and equip their battle abilities. 2: Open list. 1: Back";
        size_t line=0,lastSpace=0;
        for(size_t i=0;i<message.size();++i) {
            if(message[i]==' ') lastSpace=i;
            if(i-line>=51 && lastSpace>line) {message[lastSpace]='\n';line=lastSpace+1;}
        }
        if(message.size()>480) message.resize(480);
        for(size_t i=0;i<=message.size();++i) Memory::Write8(helpText+static_cast<uint32_t>(i),message[i]);
        Memory::Write32(box+84,helpText);
        Memory::Write32(box+76,0);
        Memory::Write32(box+80,static_cast<uint32_t>(1+std::count(message.begin(),message.end(),'\n')));
        Memory::WriteFloat32(box+88,0.f);
    }
}
void Cursor(Guest& g) {
    const auto plus=Plus(g), badge=Entry(g,badgeId);
    if (!badge) return;
    // Same target offsets as pluswinMain, and the game's own animated glove renderer.
    const float x=Memory::ReadFloat32(badge+36)-Memory::ReadFloat32(0x80576280-14856);
    const float y=Memory::ReadFloat32(badge+40)-Memory::ReadFloat32(0x80576280-14868);
    Memory::WriteFloat32(plus+144,x); Memory::WriteFloat32(plus+148,y);
    Memory::WriteFloat32(plus+152,x); Memory::WriteFloat32(plus+156,y);
    g.Call(0x8005ADEC,{11,4,0x80186238,0});
}

struct BadgeButtonPoint { float x, y; };
// Use the game's GX command path so the button participates in the native pause camera and transitions.
void BeginBadgeButton(Guest& g) {
    const auto camera=g.Call(0x80055A20); // same current-camera view used by native FontDrawStringMtx
    g.Call(0x80291788,{camera+0x54,0});
    g.Call(0x80291830,{0});
    g.Call(0x8028BEC8); // direct XYZ + RGBA vertices, private vertex format 7
    g.Call(0x8028B638,{9,1});
    g.Call(0x8028B638,{11,1});
    g.Call(0x8028BEFC,{7,9,1,4,0});
    g.Call(0x8028BEFC,{7,11,1,5,0});
    g.Call(0x8028C838,{0});
    g.Call(0x8028E778,{1});
    g.Call(0x8028E79C,{4,0,0,1,0,0,2}); // unlit vertex color, including alpha
    g.Call(0x8028FD44,{0});
    g.Call(0x8028FD64,{0});
    g.Call(0x802904B4,{1});
    g.Call(0x80290358,{0,255,255,4});
    g.Call(0x8028FE58,{0,4}); // pass raster color
    g.Call(0x8028D67C,{0});
    g.Call(0x802908D0,{0,7,0});
    g.Call(0x80290294,{7,0,0,7,0});
    g.Call(0x80290828,{1,4,5,15});
    g.Call(0x80290878,{1});
}

std::vector<BadgeButtonPoint> BadgeButtonShape(float w,float h,float cut) {
    return {{0,cut},{cut,0},{w,0},{w,h-cut},{w-cut,h},{0,h}};
}
void BadgeButtonPoly(Guest& g,const std::vector<BadgeButtonPoint>& points,float x,float y,
                     float height,uint32_t top,uint32_t bottom,float alpha) {
    if (points.size()<3) return;
    g.Call(0x8028D3B4,{0xA0,7,static_cast<uint32_t>(points.size())}); // triangle fan
    for (const auto& p:points) {
        const float mix=std::clamp(p.y/height,0.f,1.f);
        uint32_t color=0;
        for (int shift : {24,16,8,0}) {
            float channel=((top>>shift)&255)*(1.f-mix)+((bottom>>shift)&255)*mix;
            if (shift==0) channel*=alpha;
            color|=static_cast<uint32_t>(channel)<<shift;
        }
        GX_HLE_FIFO_WriteFloat(x+p.x);
        GX_HLE_FIFO_WriteFloat(y-p.y);
        GX_HLE_FIFO_WriteFloat(0.f);
        GX_HLE_FIFO_Write32(color);
    }
}
// Clip the moving glint to the angled face, keeping it inside the paper outline.
std::vector<BadgeButtonPoint> BadgeGlint(std::vector<BadgeButtonPoint> points,float edge,bool lower) {
    std::vector<BadgeButtonPoint> out;
    auto distance=[=](const BadgeButtonPoint& p) { const float d=p.x+p.y*.6f-edge; return lower?d:-d; };
    auto previous=points.back(); float pd=distance(previous);
    for (const auto& current:points) {
        const float cd=distance(current);
        if ((pd>=0)!=(cd>=0)) {
            const float t=pd/(pd-cd);
            out.push_back({previous.x+(current.x-previous.x)*t,previous.y+(current.y-previous.y)*t});
        }
        if (cd>=0) out.push_back(current);
        previous=current; pd=cd;
    }
    return out;
}

template<size_t N>
void BadgeBlockLetters(Guest& g,const BadgeLabelArt::Run (&runs)[N],float x,float y,uint32_t color,float alpha) {
    g.Call(0x8028D3B4,{0x80,7,static_cast<uint32_t>(N*4)});
    for (const auto& run:runs) {
        const float left=x+run.x*BadgeLabelArt::kPixel,top=y-run.y*BadgeLabelArt::kPixel;
        const float right=left+run.width*BadgeLabelArt::kPixel,bottom=top-run.height*BadgeLabelArt::kPixel;
        const uint32_t tint=(color&0xFFFFFF00u)|static_cast<uint32_t>(run.alpha*alpha);
        for (const auto& p: {BadgeButtonPoint{left,top},BadgeButtonPoint{right,top},
                             BadgeButtonPoint{right,bottom},BadgeButtonPoint{left,bottom}}) {
            GX_HLE_FIFO_WriteFloat(p.x); GX_HLE_FIFO_WriteFloat(p.y); GX_HLE_FIFO_WriteFloat(0.f);
            GX_HLE_FIFO_Write32(tint);
        }
    }
}

void DrawBadgeButton(Guest& g,uint32_t entry) {
    const bool selected=showing.load();
    const float appear=std::clamp(badgeMenuFrame/12.f,0.f,1.f);
    const float alpha=appear*std::clamp(Memory::Read32(entry+52)/255.f,0.f,1.f);
    // Native 120x40 textures have two transparent columns on each side and two bottom rows.
    // Include the extrusion INSIDE that same visible footprint; selection must never resize the button.
    // The native textured tabs extend five pixels left/up of the logical entry origin.
    const float x=Memory::ReadFloat32(entry+4)-3.f-(1.f-appear)*10.f;
    const float y=Memory::ReadFloat32(entry+8)+5.f;
    const float w=Memory::ReadFloat32(entry+12)-6.5f,h=Memory::ReadFloat32(entry+16)-5.f;
    BeginBadgeButton(g);
    auto layer=[&](float inset,float dx,float dy,uint32_t top,uint32_t bottom) {
        const float height=h-inset*2;
        BadgeButtonPoly(g,BadgeButtonShape(w-inset*2,height,7.f),x+inset+dx,y-inset+dy,height,top,bottom,alpha);
    };
    layer(0,2.5f,-3,0x15111CFF,0x15111CFF); // paper extrusion
    layer(0,0,0,0x17131FFF,0x17131FFF);
    layer(1.5f,0,0,0xFFF9EDFF,0xDDD5D5FF); // white rim, like the other tabs
    layer(3.3f,0,0,0x214952FF,0x10343BFF);
    layer(4.5f,0,0,selected?0x38DBC6FF:0x48B7ADFF,selected?0x078C9EFF:0x246A80FF);
    const float innerW=w-9,innerH=h-9;
    const float sweep=std::fmod(badgeMenuFrame*(selected?1.1f:.45f),210.f)-40.f;
    auto glint=BadgeGlint(BadgeButtonShape(innerW,innerH,7.f),sweep,true);
    if (!glint.empty()) glint=BadgeGlint(glint,sweep+11.f,false);
    BadgeButtonPoly(g,glint,x+4.5f,y-4.5f,innerH,0xC6FFF57A,0xA8F8FF15,alpha);
    // Baked, heavy Mario-style tab lettering, not the game's thin dialogue font.
    const float tx=x+(w-BadgeLabelArt::kWidth)*.5f,ty=y-8.5f;
    BadgeBlockLetters(g,BadgeLabelArt::kOutline,tx+.5f,ty-.8f,0x16212DFF,alpha*.8f);
    BadgeBlockLetters(g,BadgeLabelArt::kOutline,tx,ty,0x17212DFF,alpha);
    BadgeBlockLetters(g,BadgeLabelArt::kFace,tx,ty,selected?0xFFFDF2FF:0xC9D0CDFF,alpha);
}
// Code-native vector art, like the other battle command icons: a red boot and seven bounce nodes.
void PowerBounceIcon(ImDrawList* dl, ImVec2 p, float size) {
    const float s = size / 100.f;
    const auto point = [=](float x, float y) { return ImVec2(p.x + x*s, p.y + y*s); };
    const ImU32 ink = IM_COL32(15, 25, 20, 255);
    dl->AddRectFilled(point(0,0), point(100,100), ink, 13*s);
    dl->AddRect(point(2,2), point(98,98), IM_COL32(130,255,100,255), 12*s, 0, 3*s);
    for (int i=0; i<7; ++i) {
        const float x=13.f+i*12.f, y=30.f-(i%2)*10.f;
        if (i) dl->AddLine(point(x-12,30.f-((i-1)%2)*10.f), point(x,y), IM_COL32(80,180,70,255), 2*s);
        dl->AddCircleFilled(point(x,y), 4*s, IM_COL32(220,255,140,255));
    }
    const ImVec2 boot[] = {point(30,40),point(59,40),point(59,61),point(79,65),point(84,78),point(78,85),point(29,85),point(23,77),point(30,65)};
    dl->AddRectFilled(point(30,40),point(59,78),IM_COL32(245,75,70,255));
    const ImVec2 foot[] = {point(23,77),point(30,65),point(59,61),point(79,65),point(84,78),point(78,85),point(29,85)};
    dl->AddConvexPolyFilled(foot,7,IM_COL32(245,75,70,255));
    dl->AddPolyline(boot,9,ink,ImDrawFlags_Closed,3*s);
    dl->AddLine(point(28,79),point(79,79),IM_COL32(255,215,100,255),5*s);
    dl->AddRectFilled(point(31,41),point(58,48),IM_COL32(255,245,210,255),2*s);
}
}

void BadgeLabel(CpuContext* ctx) {
    Guest g(ctx);
    DrawBadgeButton(g,ctx->gpr[3]);
}

void BadgeOpen(CpuContext* ctx) {
    func_801851E8(ctx);
    Guest g(ctx);
    Page(g,false);
    badgeMenuFrame=0;
    // A real pausewin entry, sorted/drawn by pausewinMain with the other left-column rows.
    const auto reference=Row(g,3);
    const float width=reference?Memory::ReadFloat32(reference+12):120.f;
    const float height=reference?Memory::ReadFloat32(reference+16):40.f;
    badgeId=static_cast<int>(g.Call(0x80183F2C,{2,0,10,0,0,0,kBadgeLabel,0},{-249.,95.,width,height}));
    if (const auto badge=Entry(g,badgeId)) Memory::Write32(badge+52,255);
    RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] native size %.0fx%.0f, block label ink height 18\n",width,height);
    // Fit all original unlockable rows plus Badges above the original description box.
    float y=55.f;
    for (int i=4;i<8;++i) if (const auto row=Row(g,i)) {
        for (int off : {8,24,40}) Memory::WriteFloat32(row+off,y);
        y-=40.f;
    }
    helpText=g.Call(0x801A5634,{0,512});
    if (Memory::Contains(helpText,512)) {
        constexpr char help[]="Equip badges to change your battle abilities.\n2: Equip / unequip    1: Back";
        for (size_t i=0;i<sizeof help;++i) Memory::Write8(helpText+static_cast<uint32_t>(i),help[i]);
    } else helpText=0;
    RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] native row created: %d\n",badgeId);
}

void BadgeClose(CpuContext* ctx) {
    Guest g(ctx);
    showing.store(false);
    if (const auto badge=Entry(g,badgeId); badge && Memory::Read32(badge+112)==kBadgeLabel)
        g.Call(0x80184588,{static_cast<uint32_t>(badgeId)});
    badgeId=-1;
    // Restore the description before releasing its backing string.
    if (helpText) {
        if (const auto box=Row(g,9); box && Memory::Read32(box+84)==helpText) Memory::Write32(box+84,0);
        g.Call(0x801A56B8,{0,helpText});
        helpText=0;
    }
    func_80185800(ctx);
}

void BadgePauseTick(CpuContext* ctx) {
    Guest g(ctx);
    const auto work=Memory::Read32(g.Sda()-30160);
    paused.store(Memory::Contains(work,0x38) && Memory::Read32(work+28)==50);
    lastTick.store(Now());
    if (paused.load()) ++badgeMenuFrame;
    if (!paused.load()) showing.store(false);
    func_80183534(ctx);
}

void BadgeMenuTick(CpuContext* ctx) {
    Guest g(ctx);
    const auto plus=Plus(g);
    if (Memory::Read32(plus+132)!=0 || !Entry(g,badgeId)) { func_80185A58(ctx); return; }
    const auto pressed=g.Call(Fn::wpadGetButtonsPressed,{0});
    const auto movement=g.Call(0x802350F4,{0}); // native pause-menu repeat timing, including held directions
    const int selected=static_cast<int>(Memory::Read32(plus+136));
    int next=0;
    for (int i=4;i<8;++i) if (Row(g,i)) { next=i; break; }
    if (!showing.load()) {
        if ((selected==3 && (movement&Btn::screenDown)) ||
            (selected==next && (movement&Btn::screenUp))) {
            Page(g,true);
            Help(g); Cursor(g);
            RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] selected native Badges row\n");
            ctx->gpr[3]=0;
            return;
        }
        func_80185A58(ctx);
        return;
    }
    if (pressed&Btn::plus) {
        Page(g,false);
        func_80185A58(ctx);
        return;
    }
    if(browsingBadges) {
        int index=badgeSelection.load();
        if(movement&Btn::screenUp) index=(index+kBadgeCount-1)%kBadgeCount;
        if(movement&Btn::screenDown) index=(index+1)%kBadgeCount;
        if(movement&Btn::screenLeft) index=(index+kBadgeCount-5)%kBadgeCount;
        if(movement&Btn::screenRight) index=(index+5)%kBadgeCount;
        if(index!=badgeSelection.load()) badgeNotice.clear();
        badgeSelection.store(index);
        if(pressed&(Btn::one|Btn::b)) {browsingBadges=false;badgeNotice.clear();}
        else if(pressed&(Btn::two|Btn::a)) ToggleBadge(g,static_cast<BadgeId>(index),badgeNotice);
        Help(g);Cursor(g);ctx->gpr[3]=0;return;
    }
    int target=-1;
    if ((movement&Btn::screenUp) || (pressed&(Btn::one|Btn::b))) target=3;
    else if (movement&Btn::screenDown) target=next;
    else if (movement&Btn::screenLeft) target=2;
    else if (movement&Btn::screenRight) target=Row(g,1)?1:0;
    if (target>=0) {
        Page(g,false);
        Memory::Write32(plus+136,target);
        char key[32];
        std::snprintf(key,sizeof key,"menu_help_%03d",target);
        const auto text=g.Scratch(0x100);
        for (size_t i=0;i<=std::strlen(key);++i) Memory::Write8(text+static_cast<uint32_t>(i),key[i]);
        if (const auto box=Row(g,9)) g.Call(0x8018781C,{box,0,text});
        // Let pluswinMain update its highlight/help on the next frame, without reading this press twice.
        if (const auto row=Row(g,target)) {
            Memory::Write16(row,Memory::Read16(row)|0x110u);
            const auto x=Memory::ReadFloat32(row+36)-Memory::ReadFloat32(0x80576280-14856);
            const auto y=Memory::ReadFloat32(row+40)-Memory::ReadFloat32(0x80576280-14868);
            Memory::WriteFloat32(plus+144,x); Memory::WriteFloat32(plus+148,y);
        }
        RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] leave row -> %d\n",target);
    } else {
        if (pressed&(Btn::two|Btn::a)) {
            browsingBadges=true;badgeNotice.clear();
        }
        Help(g); Cursor(g);
    }
    ctx->gpr[3]=0;
}

void BadgeStats(CpuContext* ctx) {
    if (!showing.load()) { func_801862A4(ctx); return; }
    Guest g(ctx);
    int owned=0;for(const auto& b:kBadges) if(BadgeOwned(g,b.id)) ++owned;
    Text(g,BadgeTesting()?"BADGES - TEST COLLECTION":"BADGES",-85,120,0.65f);
    const auto info=std::to_string(owned)+"/85 owned    BP "+std::to_string(BadgeBpUsed(g))+"/"+std::to_string(BadgeBpMax(g));
    Text(g,info.c_str(),-85,94,0.5f);
    const int index=badgeSelection.load();const int start=std::clamp(index-2,0,kBadgeCount-5);
    for(int r=0;r<5;++r) {
        const auto& b=kBadges[start+r];
        const std::string row=(start+r==index?"> ":"  ")+std::string(b.name)+
            (BadgeEquipped(g,b.id)?" [E]":BadgeOwned(g,b.id)?"":" [locked]");
        Text(g,row.c_str(),-68,60.f-r*29.f,0.53f,start+r==index?0x147F72FF:0x35303AFF);
    }
    const auto& b=kBadges[index];
    const auto footer="BP "+std::to_string(b.bp)+"   FP "+std::to_string(b.fp)+"    "+std::to_string(index+1)+"/85";
    Text(g,footer.c_str(),-85,-77,0.5f);
    Text(g,browsingBadges?"2: Equip   1: Back   Left/Right: Page":"2: Browse badges",-85,-91,0.42f);
}

void DrawBadgesMenu() {
    if (!paused.load() || !showing.load() || Now()-lastTick.load()>250) return;
    // Only the existing procedural badge icon is drawn here. The row, glove, font, page and help are native GX.
    const auto display=ImGui::GetIO().DisplaySize;
    float rx=0,ry=0,rw=1,rh=1;
    aurora_get_present_rect(&rx,&ry,&rw,&rh);
    const float s=display.y*rh/480.f;
    const float center=display.x*(rx+rw*.5f);
    const int index=badgeSelection.load(),start=std::clamp(index-2,0,kBadgeCount-5);
    for(int r=0;r<5;++r) {
        const auto& def=kBadges[start+r];const auto kind=def.kind;
        const ImVec2 at{center-96*s,display.y*ry+(179+r*29)*s};
        if(def.id==BadgeId::PowerBounce) PowerBounceIcon(ImGui::GetForegroundDrawList(),at,20*s);
        else {
            auto* dl=ImGui::GetForegroundDrawList();const auto c=ImVec2(at.x+10*s,at.y+10*s);
            const ImU32 color=IM_COL32(80+(start+r)*31%150,130+(start+r)*17%120,120+(start+r)*23%130,255);
            dl->AddQuadFilled({c.x,c.y-10*s},{c.x+10*s,c.y},{c.x,c.y+10*s},{c.x-10*s,c.y},color);
            dl->AddQuad({c.x,c.y-10*s},{c.x+10*s,c.y},{c.x,c.y+10*s},{c.x-10*s,c.y},IM_COL32(12,25,20,255),1.5f*s);
            if(kind==BadgeKind::Jump) dl->AddTriangleFilled({c.x-4*s,c.y+4*s},{c.x,c.y-5*s},{c.x+4*s,c.y+4*s},IM_COL32_WHITE);
            else if(kind==BadgeKind::Hammer) {dl->AddLine({c.x-3*s,c.y+5*s},{c.x+2*s,c.y-4*s},IM_COL32_WHITE,3*s);dl->AddRectFilled({c.x-2*s,c.y-6*s},{c.x+6*s,c.y-2*s},IM_COL32_WHITE);}
            else dl->AddCircleFilled(c,3.5f*s,IM_COL32_WHITE,6);
        }
    }
}

bool NativePauseOpen(){return paused.load() && Now()-lastTick.load()<250;}
}

extern "C" void pausewinMain_LostPagesBadges(CpuContext* ctx) { LostPages::BadgePauseTick(ctx); }
REGISTER_NATIVE_FUNCTION_AS(0x80183534, pausewinMain_LostPagesBadges, "pausewinMain_LostPagesBadges");
extern "C" void pluswinOpen_LostPagesBadges(CpuContext* ctx) { LostPages::BadgeOpen(ctx); }
REGISTER_NATIVE_FUNCTION_AS(0x801851E8, pluswinOpen_LostPagesBadges, "pluswinOpen_LostPagesBadges");
extern "C" void pluswinClose_LostPagesBadges(CpuContext* ctx) { LostPages::BadgeClose(ctx); }
REGISTER_NATIVE_FUNCTION_AS(0x80185800, pluswinClose_LostPagesBadges, "pluswinClose_LostPagesBadges");
extern "C" void pluswinMain_LostPagesBadges(CpuContext* ctx) { LostPages::BadgeMenuTick(ctx); }
REGISTER_NATIVE_FUNCTION_AS(0x80185A58, pluswinMain_LostPagesBadges, "pluswinMain_LostPagesBadges");
extern "C" void pluswinStats_LostPagesBadges(CpuContext* ctx) { LostPages::BadgeStats(ctx); }
REGISTER_NATIVE_FUNCTION_AS(0x801862A4, pluswinStats_LostPagesBadges, "pluswinStats_LostPagesBadges");
extern "C" void lostPagesBadgeLabel(CpuContext* ctx) { LostPages::BadgeLabel(ctx); }
REGISTER_NATIVE_FUNCTION(0x817F0100, lostPagesBadgeLabel);
