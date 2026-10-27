#pragma once
#include "lp_badge_catalog.h"
#include <string>
namespace LostPages {
constexpr BadgeId BadgeForPickup(unsigned item) {
    return item==0x59?BadgeId::HpPlus:item==0x5A?BadgeId::PowerPlus:BadgeId::Count;
}
std::string BadgePickupNotice();
// Shows `text` in the field notice line for `frames` frames.
void ShowFieldNotice(const std::string& text, int frames);
}
