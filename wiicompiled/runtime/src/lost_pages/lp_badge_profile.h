#pragma once
#include "lp_badge_catalog.h"
#include <algorithm>
#include <array>
#include <istream>
#include <ostream>
#include <string>
namespace LostPages {
struct BadgeProfile {
    std::array<bool,kBadgeCount> owned{}, equipped{};
    int fp=10,energy=0,bp=3;
};
inline void WriteBadgeProfile(std::ostream& out,const BadgeProfile& p) {
    out<<"LPBADGES 3\n"<<p.fp<<' '<<p.energy<<' '<<p.bp<<'\n';
    for(int i=0;i<kBadgeCount;++i)out<<p.owned[i]<<p.equipped[i]<<'\n';
}
inline BadgeProfile ReadBadgeProfile(std::istream& in) {
    BadgeProfile p;std::string magic;int version=0;
    if(!(in>>magic>>version) || magic!="LPBADGES" || (version!=2 && version!=3))return p;
    if(!(in>>p.fp>>p.energy))return BadgeProfile{};
    if(version==3) {if(!(in>>p.bp))return BadgeProfile{};p.bp=std::clamp(p.bp,3,99);}
    else p.bp=-1; // migrate the preceding version's automatic BP budget in the game layer
    for(int i=0;i<kBadgeCount;++i) {
        std::string flags;
        if(!(in>>flags) || flags.size()!=2 || flags.find_first_not_of("01")!=std::string::npos)return BadgeProfile{};
        p.owned[i]=flags[0]=='1';p.equipped[i]=p.owned[i] && flags[1]=='1';
    }
    p.fp=std::clamp(p.fp,0,200);p.energy=std::clamp(p.energy,0,100);return p;
}
} // namespace LostPages
