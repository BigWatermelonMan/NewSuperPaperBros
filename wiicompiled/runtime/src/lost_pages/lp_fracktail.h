#pragma once
#include "lp_guest.h"
#include "lp_fracktail_rules.h"
#include <array>
namespace LostPages {
namespace FracktailLayout {
// Keep Mario at normal scale: the dragon, rather than the entire camera, grows.
inline constexpr float Scale=3.15f, RestX=245.f;
inline constexpr float RaisedRootY=-320.f, LoweredRootY=-395.f;
inline constexpr float AntennaOffsetY=170.f*Scale;
}
struct FracktailVisual {
    bool active=false,lowered=false,throwing=false;
    bool breathing=false,charging=false;
    std::array<bool,2> minionHurt{};
    Vec3 root{},partner{},thrown{},floor{};
    std::array<Vec3,2> minions{};
    std::array<bool,2> minionAlive{};
    float facing=0,scale=FracktailLayout::Scale;
};
void UpdateFracktailVisual(Guest& g,const FracktailVisual& view);
void ReleaseFracktailVisual(Guest& g);
bool FracktailAntenna(Guest& g,Vec3& point);
void RequestFracktailBattle();
}
