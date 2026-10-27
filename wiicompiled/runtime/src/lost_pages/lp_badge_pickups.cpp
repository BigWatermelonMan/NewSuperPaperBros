#include "lp_badge_pickups.h"
#include "lp_badge_system.h"
#include "runtime_log.h"
#include <cstdlib>
namespace LostPages {
namespace {std::string notice;int noticeFrames=0;}
std::string BadgePickupNotice(){return noticeFrames>0?notice:std::string{};}
void ShowFieldNotice(const std::string& text,int frames){notice=text;noticeFrames=frames;}
bool AcquireStatBadge(Guest& g,unsigned item) {
    const auto id=BadgeForPickup(item);if(id==BadgeId::Count)return false;
    const bool already=BadgeOwned(g,id);GrantBadge(g,id);
    if(!BadgeOwned(g,id))return false;
    notice=std::string(kBadges[static_cast<int>(id)].name)+(already?" badge already owned":" badge obtained!");noticeFrames=180;
    RT_LOGF(RT_TAG_RUNTIME,"[lpbadges] pickup %02X -> %s; base HP %d ATK %d BP %d\n",item,kBadges[static_cast<int>(id)].name,Memory::Read32(g.PouchPtr()+Pouch::maxHp),Memory::Read32(g.PouchPtr()+Pouch::attack),Badges(g).bp);
    return true;
}
void BadgePickupTick(Guest& g) {
    if(noticeFrames>0)--noticeFrames;
    const auto pouch=g.PouchPtr();
    // Migrate consumables already held in an old save, including a completely full bag.
    for(unsigned i=0;i<Pouch::useItemCount;) {
        const auto item=Memory::Read16(pouch+Pouch::useItems+2*i);
        if(BadgeForPickup(item)==BadgeId::Count || !AcquireStatBadge(g,item)){++i;continue;}
        for(unsigned j=i;j+1<Pouch::useItemCount;++j)Memory::Write16(pouch+Pouch::useItems+2*j,Memory::Read16(pouch+Pouch::useItems+2*(j+1)));
        Memory::Write16(pouch+Pouch::useItems+2*(Pouch::useItemCount-1),0);
    }
    static bool fixture=false;
    if(!fixture)if(const auto list=std::getenv("SPM_LP_TEST_PICKUPS")) {
        fixture=true;
        for(const char* c=list;*c;) {char* end=nullptr;const auto id=std::strtoul(c,&end,0);if(end==c)break;g.Call(0x8014CB04,{static_cast<uint32_t>(id)});c=*end==','?end+1:end;}
    }
}
}
extern "C" void func_8014CB04(CpuContext*);
extern "C" void pouchAddItem_LostPagesBadges(CpuContext* ctx) {
    if(LostPages::BadgeForPickup(ctx->gpr[3])!=LostPages::BadgeId::Count){LostPages::Guest g(ctx);ctx->gpr[3]=LostPages::AcquireStatBadge(g,ctx->gpr[3])?1:0;return;}
    func_8014CB04(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8014CB04,pouchAddItem_LostPagesBadges,"pouchAddItem_LostPagesBadges");
extern "C" void func_8014D100(CpuContext*);
extern "C" void pouchCheckHaveItem_LostPagesBadges(CpuContext* ctx) {
    const auto id=LostPages::BadgeForPickup(ctx->gpr[3]);
    if(id!=LostPages::BadgeId::Count){LostPages::Guest g(ctx);ctx->gpr[3]=LostPages::BadgeOwned(g,id)?1:0;return;}
    func_8014D100(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8014D100,pouchCheckHaveItem_LostPagesBadges,"pouchCheckHaveItem_LostPagesBadges");

extern "C" void func_800325F0(CpuContext*);
extern "C" void msgSearch_LostPagesStatBadges(CpuContext* ctx) {
    static uint32_t text[4]{};
    const char* messages[]={"HP Plus badge","Power Plus badge","Equip HP Plus in Badges for +5 maximum HP. Costs 3 BP.","Equip Power Plus in Badges for +1 attack. Costs 6 BP."};
    int selected=-1;
    for(int i=0;i<2;++i) {
        const auto item=LostPages::kItemDataTable+(0x59+i)*LostPages::kItemDataSize;
        if(ctx->gpr[3]==Memory::Read32(item+0x10))selected=i;
        if(ctx->gpr[3]==Memory::Read32(item+0x14))selected=i+2;
    }
    if(selected>=0) {
        LostPages::Guest g(ctx);
        if(!text[selected]) {
            text[selected]=g.Call(0x801A5634,{0,128});
            if(Memory::Contains(text[selected],128))for(unsigned i=0;;++i){Memory::Write8(text[selected]+i,messages[selected][i]);if(!messages[selected][i])break;}
            else text[selected]=0;
        }
        if(text[selected]){ctx->gpr[3]=text[selected];return;}
    }
    func_800325F0(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x800325F0,msgSearch_LostPagesStatBadges,"msgSearch_LostPagesStatBadges");
