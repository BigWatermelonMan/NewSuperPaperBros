#pragma once
#include "lp_badge_catalog.h"
#include "lp_badge_profile.h"
#include "lp_guest.h"
#include <array>
#include <string>

namespace LostPages {
bool BadgeTesting();
BadgeProfile& Badges(Guest& g, bool create=false);
bool BadgeOwned(Guest& g, BadgeId id);
bool BadgeEquipped(Guest& g, BadgeId id);
int BadgeCount(Guest& g, BadgeId family);
bool ToggleBadge(Guest& g, BadgeId id, std::string& reason);
void GrantBadge(Guest& g, BadgeId id);
void SaveBadges(Guest& g);
int BadgeBpUsed(Guest& g);
int BadgeBpMax(Guest& g);
int BattleMaxHp(Guest& g);
int BattleMaxFp(Guest& g);
int BattleFp(Guest& g);
bool SpendFp(Guest& g, int amount);
void RecoverFp(Guest& g, int amount);
int MoveFpCost(Guest& g, BadgeId id);
void AddStarEnergy(Guest& g, int amount);
void BadgeFieldTick(Guest& g);
} // namespace LostPages
