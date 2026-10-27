#include "lp_fracktail.h"
#include "runtime_log.h"
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
namespace LostPages {
namespace {
FracktailVisual visual;
std::array<int,4> poses{{-1,-1,-1,-1}};
std::array<int,4> animationState{{-1,-1,-1,-1}};
std::array<int,4> flightPoses{{-1,-1,-1,-1}};
int flightFrame=0;
int antennaGroup=-1;
constexpr uint32_t kDraw=0x817F0200;
uint32_t ModelString(Guest& g,const char* text) {
    static std::map<std::string,uint32_t> strings;
    if(auto it=strings.find(text);it!=strings.end())return it->second;
    auto pointer=GameString(text);
    if(!pointer) {
        const std::string value=text;
        pointer=g.Call(0x801A5634,{0,static_cast<uint32_t>(value.size()+1)});
        if(!Memory::Contains(pointer,value.size()+1))return 0;
        for(unsigned i=0;i<=value.size();++i)Memory::Write8(pointer+i,text[i]);
    }
    strings.emplace(text,pointer);return pointer;
}
int CreatePose(Guest& g,const char* model,const char* animation) {
    const auto name=ModelString(g,model),anim=ModelString(g,animation);if(!name || !anim)return -1;
    const int id=static_cast<int32_t>(g.Call(0x80042384,{name,0}));
    if(id>=0){g.Call(0x800432F8,{static_cast<uint32_t>(id),anim,1});RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] model %s pose %d anim %s\n",model,id,animation);}
    return id;
}
void DrawPose(Guest& g,int pose,const Vec3& p,float facing,float scale) {
    if(pose<0)return;
    for(uint32_t pass=1;pass<=2;++pass)g.Call(0x80048EE4,{static_cast<uint32_t>(pose),pass},{p.x,p.y,p.z,facing,scale});
}
}
void UpdateFracktailVisual(Guest& g,const FracktailVisual& view) {
    visual=view;if(!view.active)return;
    // A dedicated visible connector fills the head-to-body joint. The original
    // neck part's default pose does not fill that joint in this straight rig.
    const char* flightModels[]={"e_zun_hed","e_zun_nomal","e_zun_nomal","e_zun_tail"};
    const char* flightAnims[]={"zun_hed_S_1A","zun_nomal_Z_1","zun_nomal_Z_1","zun_tail_S_1"};
    for(int i=0;i<4;++i) {
        if(flightPoses[i]<0)flightPoses[i]=CreatePose(g,flightModels[i],flightAnims[i]);
        if(view.charging && flightPoses[i]>=0)g.Call(0x80045170,{static_cast<uint32_t>(flightPoses[i])});
    }
    if(view.charging)++flightFrame;else flightFrame=0;
    const char* models[]={"e_zun_all","e_kozun","e_kozun","FRY_throw"};
    const char* anims[]={"zun_all_S_1B","S_1","S_1","S_1"};
    for(int i=0;i<4;++i){
        if(poses[i]<0) {
            poses[i]=CreatePose(g,models[i],anims[i]);
            if(i==0 && poses[i]>=0) {
                g.Call(0x8004D16C,{static_cast<uint32_t>(poses[i])});
                g.Call(0x8004D2C0,{static_cast<uint32_t>(poses[i])});
                // The lit orb is the hit point; Antena is the stalk's parent pivot.
                for(const char* group:{"antena_light","Antena"}) {
                    for(unsigned j=0;;++j){Memory::Write8(g.Scratch(0x200)+j,group[j]);if(!group[j])break;}
                    antennaGroup=static_cast<int32_t>(g.Call(0x8004DD00,{static_cast<uint32_t>(poses[i]),g.Scratch(0x200)}));
                    if(antennaGroup>=0)break;
                }
                RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] antenna joint %d\n",antennaGroup);
            }
        }
        if(poses[i]>=0) {
            const int state=i==0?view.breathing:i==3?view.throwing:view.minionHurt[i-1];
            if(state!=animationState[i]) {
                const char* animation=state?(i==0?"zun_all_M_1":i==3?"A_1":"D_1"):anims[i];
                g.Call(0x800432F8,{static_cast<uint32_t>(poses[i]),GameString(animation),1});animationState[i]=state;
                if(i==0)RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] animation %s\n",animation);
            }
            g.Call(0x80045170,{static_cast<uint32_t>(poses[i])});
        }
    }
    const auto camera=Memory::Read32(g.MarioPtr()+0x120);
    g.Call(0x8005ADEC,{camera,1,kDraw,0},{100.f});
}
void ReleaseFracktailVisual(Guest& g) {
    visual.active=false;for(auto& id:poses){if(id>=0)g.Call(0x80049EAC,{static_cast<uint32_t>(id)});id=-1;}
    for(auto& id:flightPoses){if(id>=0)g.Call(0x80049EAC,{static_cast<uint32_t>(id)});id=-1;}
    antennaGroup=-1;animationState.fill(-1);
}
bool FracktailAntenna(Guest& g,Vec3& point) {
    if(poses[0]<0 || antennaGroup<0)return false;
    const auto pose=g.Call(0x8004C548,{static_cast<uint32_t>(poses[0])});
    if(!Memory::Contains(pose,392))return false;
    const auto count=Memory::Read32(pose+376),allocation=Memory::Read32(pose+372);
    // Native smartAlloc returns a movable allocation handle, whose first word is the data pointer.
    if(!Memory::Contains(allocation,4))return false;
    const auto positions=Memory::Read32(allocation);
    if(static_cast<unsigned>(antennaGroup)>=count || !Memory::Contains(positions+antennaGroup*12,12))return false;
    const auto p=ReadVec3(positions+antennaGroup*12);
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || (p.x==0 && p.y==0 && p.z==0))return false;
    // The orb's visible center is offset from its group's shared origin. Transform
    // this model-space point with the evaluated matrix, including draw scale/animation.
    point={p.x-13.f*visual.scale,p.y+24.f*visual.scale,p.z};
    const auto matrixAllocation=Memory::Read32(pose+380);
    if(Memory::Contains(matrixAllocation,4)) {
        const auto matrices=Memory::Read32(matrixAllocation);
        const auto matrix=matrices+antennaGroup*48;
        if(Memory::Contains(matrix,48)) {
            Vec3 center;
            center.x=-13.f*Memory::ReadFloat32(matrix)+24.f*Memory::ReadFloat32(matrix+4)+Memory::ReadFloat32(matrix+12);
            center.y=-13.f*Memory::ReadFloat32(matrix+16)+24.f*Memory::ReadFloat32(matrix+20)+Memory::ReadFloat32(matrix+28);
            center.z=-13.f*Memory::ReadFloat32(matrix+32)+24.f*Memory::ReadFloat32(matrix+36)+Memory::ReadFloat32(matrix+44);
            if(std::isfinite(center.x) && std::isfinite(center.y) && (center.x!=0 || center.y!=0))point=center;
        }
    }
    static int probes=0;
    if(std::getenv("SPM_LP_TEST_FRACKTAIL") && probes++<3) {
        float x=0,y=0;g.ScreenPos(point,x,y);
        RT_LOGF(RT_TAG_RUNTIME,"[lpfracktail] antenna %.1f %.1f %.1f screen %.1f %.1f root %.1f %.1f %.1f scale %.2f\n",point.x,point.y,point.z,x,y,visual.root.x,visual.root.y,visual.root.z,visual.scale);
    }
    return true;
}
void DrawFracktail(CpuContext* ctx) {
    if(!visual.active)return;Guest g(ctx);
    const auto scratch=g.Scratch(0x200);
    g.Call(0x80291AB4,{scratch,scratch+4,scratch+8,scratch+12});
    const auto left=Memory::Read32(scratch),top=Memory::Read32(scratch+4),width=Memory::Read32(scratch+8),height=Memory::Read32(scratch+12);
    float floorX=0,floorY=528;g.ScreenPos(visual.floor,floorX,floorY);
    g.Call(0x80291A4C,{left,top,width,static_cast<uint32_t>(std::clamp(static_cast<int>(floorY)-static_cast<int>(top),1,static_cast<int>(height)))});
    if(visual.charging) {
        const auto at=[&](float x,float y=0.f){return Vec3{visual.root.x+x*visual.scale,visual.root.y+y,visual.root.z};};
        DrawPose(g,flightPoses[3],at(112.f),visual.facing,visual.scale);
        for(int i=7;i>=0;--i)DrawPose(g,flightPoses[2],at(7.f+i*13.f,std::sin(flightFrame*.18f-i*.35f)*2.f),visual.facing,visual.scale);
        DrawPose(g,flightPoses[1],at(-6.f),visual.facing,visual.scale);
        DrawPose(g,flightPoses[0],visual.root,visual.facing,visual.scale);
    } else DrawPose(g,poses[0],visual.root,visual.facing,visual.scale);
    g.Call(0x80291A4C,{left,top,width,height});
    // Frackles larger than their native model, about two-thirds of Mario's height, as in the original fight.
    constexpr float kFrackleScale=1.5f;
    for(int i=0;i<2;++i)if(visual.minionAlive[i])DrawPose(g,poses[i+1],visual.minions[i],0,kFrackleScale);
    if(visual.throwing)DrawPose(g,poses[1],visual.thrown,0,kFrackleScale);
    DrawPose(g,poses[3],visual.partner,0,.85f);
}
}
extern "C" void lostPagesFracktailDraw(CpuContext* ctx){LostPages::DrawFracktail(ctx);}
REGISTER_NATIVE_FUNCTION(0x817F0200,lostPagesFracktailDraw);
