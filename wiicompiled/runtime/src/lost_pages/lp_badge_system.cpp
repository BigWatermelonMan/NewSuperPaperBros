#include "lp_badge_system.h"
#include "lp_progress.h"
#include "lp_testarea.h"
#include "runtime_config.h"
#include "runtime_log.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

namespace LostPages {
namespace {
std::map<unsigned,BadgeProfile> profiles;
BadgeProfile emptyProfile;
std::filesystem::path ProfilePath(unsigned token) {
    return RuntimeConfigFile::ApplicationDataDirectory()/"LostPages"/"Badges"/("profile-"+std::to_string(token)+".txt");
}
unsigned Token(Guest& g) {
    const auto pouch=g.PouchPtr();
    if (!Memory::Contains(pouch,4)) return 0;
    const auto word=Memory::Read32(pouch);
    return word>>24==0x4D ? word&0x3FFu : 0;
}
bool WriteProfile(unsigned token,const BadgeProfile& p) {
    const auto path=ProfilePath(token), temp=std::filesystem::path(path.string()+".tmp");
    std::error_code ec; std::filesystem::create_directories(path.parent_path(),ec);
    if (ec) return false;
    { std::ofstream f(temp,std::ios::trunc); if (!f) return false;
      WriteBadgeProfile(f,p);
      f.flush(); if (!f) return false; }
#ifdef _WIN32
    return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temp,path,ec); return !ec;
#endif
}
BadgeProfile ReadProfile(unsigned token) {
    std::ifstream f(ProfilePath(token));return ReadBadgeProfile(f);
}
BadgeProfile& TestProfile() {
    static BadgeProfile test=[] {
        BadgeProfile p; p.owned.fill(true); p.fp=30;
        if (const char* list=std::getenv("SPM_LP_TEST_EQUIP")) {
            std::string text=list; std::replace(text.begin(),text.end(),',',' '); std::istringstream in(text); std::string key;
            while(in>>key) for(int i=0;i<kBadgeCount;++i)
                if (key==std::to_string(i+1) || key==kBadges[i].name) p.equipped[i]=true;
        }
        return p;
    }();
    return test;
}
}
bool BadgeTesting() { return TestAreaActive() || std::getenv("SPM_LP_TEST_BADGES")!=nullptr || std::getenv("SPM_LP_TEST_POWER_BOUNCE")!=nullptr; }
BadgeProfile& Badges(Guest& g,bool create) {
    if (BadgeTesting()) return TestProfile();
    unsigned token=Token(g);
    if (!token && create) {
        for (unsigned i=1;i<=1023;++i) if (!std::filesystem::exists(ProfilePath(i))) {
            BadgeProfile p;
            if (!WriteProfile(i,p)) break;
            SaveProgress(g,LoadProgress(g));
            const auto pouch=g.PouchPtr(); Memory::Write32(pouch,(Memory::Read32(pouch)&~0x3FFu)|i);
            token=i; profiles.emplace(i,p); break;
        }
    }
    if (!token) return emptyProfile;
    auto it=profiles.find(token);
    if(it==profiles.end()) {
        auto p=ReadProfile(token);
        if(p.bp<0)p.bp=std::min(99,3+LoadProgress(g).level-1);
        it=profiles.emplace(token,p).first;
    }
    return it->second;
}
void SaveBadges(Guest& g) {
    if(BadgeTesting()) return;
    const auto token=Token(g);
    if(token && !WriteProfile(token,Badges(g))) RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] failed to save profile %u\n",token);
}
bool BadgeOwned(Guest& g,BadgeId id) { return Badges(g).owned[static_cast<int>(id)]; }
bool BadgeEquipped(Guest& g,BadgeId id) { return Badges(g).equipped[static_cast<int>(id)]; }
int BadgeCount(Guest& g,BadgeId family) {
    int count=0; const auto& p=Badges(g);
    const auto mario=g.MarioPtr();
    const bool alternate=Memory::Contains(mario+0x38,1) && Memory::Read8(mario+0x38)!=0;
    for(int i=0;i<kBadgeCount;++i) if(p.equipped[i] && kBadges[i].family==family && (!kBadges[i].alternate || alternate)) ++count;
    return count;
}
int BadgeBpUsed(Guest& g) { int bp=0;const auto& p=Badges(g);for(int i=0;i<kBadgeCount;++i)if(p.equipped[i])bp+=kBadges[i].bp;return bp; }
int BadgeBpMax(Guest& g) { return BadgeTesting()?999:Badges(g).bp; }
bool ToggleBadge(Guest& g,BadgeId id,std::string& reason) {
    auto& p=Badges(g,true); const int index=static_cast<int>(id);
    if(!BadgeTesting() && !Token(g)){reason="Could not save badge collection";return false;}
    if(!p.owned[index]) {reason="Not obtained";return false;}
    if(!p.equipped[index]) {
        if(BadgeBpUsed(g)+kBadges[index].bp>BadgeBpMax(g)) {reason="Not enough BP";return false;}
        if((id==BadgeId::Jumpman && p.equipped[static_cast<int>(BadgeId::Hammerman)]) ||
           (id==BadgeId::Hammerman && p.equipped[static_cast<int>(BadgeId::Jumpman)])) {reason="Jumpman and Hammerman conflict";return false;}
    }
    p.equipped[index]=!p.equipped[index];
    p.fp=std::min(p.fp,BattleMaxFp(g)); SaveBadges(g);
    const auto pouch=g.PouchPtr();
    Memory::Write32(pouch+Pouch::hp,std::min(static_cast<int>(Memory::Read32(pouch+Pouch::hp)),BattleMaxHp(g)));
    reason=p.equipped[index]?"Equipped":"Unequipped";
    RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] %s %s\n",kBadges[index].name,reason.c_str());return true;
}
void GrantBadge(Guest& g,BadgeId id) {auto& p=Badges(g,true);if(!BadgeTesting() && !Token(g))return;p.owned[static_cast<int>(id)]=true;SaveBadges(g);}
int BattleMaxHp(Guest& g) {return static_cast<int>(Memory::Read32(g.PouchPtr()+Pouch::maxHp))+5*BadgeCount(g,BadgeId::HpPlus);}
int BattleMaxFp(Guest& g) {return (BadgeTesting()?30:10+LoadProgress(g).level/3)+5*BadgeCount(g,BadgeId::FpPlus);}
int BattleFp(Guest& g) {return std::clamp(Badges(g).fp,0,BattleMaxFp(g));}
bool SpendFp(Guest& g,int amount) {auto& p=Badges(g,true);if(BattleFp(g)<amount)return false;p.fp=BattleFp(g)-amount;return true;}  // saved when the battle ends
void RecoverFp(Guest& g,int amount) {auto& p=Badges(g,true);p.fp=std::clamp(BattleFp(g)+amount,0,BattleMaxFp(g));}
int MoveFpCost(Guest& g,BadgeId id) {const int base=kBadges[static_cast<int>(id)].fp;return base?std::max(1,base-BadgeCount(g,BadgeId::FlowerSaver)):0;}
void AddStarEnergy(Guest& g,int amount) {auto& p=Badges(g,true);p.energy=std::clamp(p.energy+amount,0,100);}

void BadgeFieldTick(Guest& g) {
    if(g.Call(Fn::seqGetSeq)!=2) return;
    const auto mario=g.MarioPtr(); if(!Memory::Contains(mario,0x200)) return;
    extern void BadgePickupTick(Guest&);BadgePickupTick(g);
    static Vec3 previous; static bool had=false;
    const auto pos=ReadVec3(mario+Mario::position);
    if(had && BadgeCount(g,BadgeId::SlowGo) && !Memory::Read8(mario+Mario::keyOff)) {
        const float dx=pos.x-previous.x,dz=pos.z-previous.z,d=std::sqrt(dx*dx+dz*dz);
        if(d>1.1f && d<15.f) WriteVec3(mario+Mario::position,{previous.x+dx/d,pos.y,previous.z+dz/d});
    }
    previous=ReadVec3(mario+Mario::position);had=true;

}
} // namespace LostPages

extern "C" void func_8014C920(CpuContext* ctx);
extern "C" void pouchSetHp_Badges(CpuContext* ctx) {
    LostPages::Guest g(ctx); const auto pouch=g.PouchPtr();
    if(!Memory::Contains(pouch,0x14)) { func_8014C920(ctx);return; }
    const auto base=Memory::Read32(pouch+LostPages::Pouch::maxHp);
    Memory::Write32(pouch+LostPages::Pouch::maxHp,static_cast<uint32_t>(LostPages::BattleMaxHp(g)));
    func_8014C920(ctx);
    Memory::Write32(pouch+LostPages::Pouch::maxHp,base);
}
REGISTER_NATIVE_FUNCTION_AS(0x8014C920,pouchSetHp_Badges,"pouchSetHp_Badges");
extern "C" void pouchGetMaxHp_Badges(CpuContext* ctx) {LostPages::Guest g(ctx);ctx->gpr[3]=LostPages::BattleMaxHp(g);}
REGISTER_NATIVE_FUNCTION(0x8014C9F4,pouchGetMaxHp_Badges);

// Enable the animation material's color pass after Mario prepares its draw callbacks.
extern "C" void func_80126D98(CpuContext* ctx);
extern "C" void marioPreDisp_Badges(CpuContext* ctx) {
    func_80126D98(ctx);
    LostPages::Guest g(ctx);const auto mario=g.MarioPtr();
    if(!Memory::Contains(mario+0x24C,32))return;
    const int palette=(LostPages::BadgeCount(g,LostPages::BadgeId::LEmblem)?1:0)|
                      (LostPages::BadgeCount(g,LostPages::BadgeId::WEmblem)?2:0);
    static std::map<int,uint32_t> restore;
    const uint32_t colors[]={0xFFFFFFFFu,0x95FF95FFu,0xFFE066FFu,0xBD87FFFFu};
    Memory::Write32(g.Scratch(0x300),colors[palette]);
    for(int i=0;i<8;++i) {
        const int pose=static_cast<int32_t>(Memory::Read32(mario+0x24C+i*4));
        if(pose<0 || pose>=4096)continue;
        if(palette) {
            if(!restore.count(pose))restore[pose]=g.Call(0x80044D50,{static_cast<uint32_t>(pose)});
            g.Call(0x8004461C,{static_cast<uint32_t>(pose),0x2000});
            const auto nativeColor=g.Call(0x80044EF0,{static_cast<uint32_t>(pose)});
            Memory::Write32(g.Scratch(0x300),(colors[palette]&0xFFFFFF00u)|(nativeColor&255u));
            g.Call(0x80044B74,{static_cast<uint32_t>(pose),g.Scratch(0x300)});
        } else if(auto it=restore.find(pose);it!=restore.end()) {
            if(!(it->second&0x2000))g.Call(0x80044700,{static_cast<uint32_t>(pose),0x2000});
            g.Call(0x80044B74,{static_cast<uint32_t>(pose),g.Scratch(0x300)});restore.erase(it);
        }
    }
    static int logs=0;if(palette && logs++<3)RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] emblem prepared palette %d\n",palette);
}
REGISTER_NATIVE_FUNCTION_AS(0x80126D98,marioPreDisp_Badges,"marioPreDisp_Badges");
