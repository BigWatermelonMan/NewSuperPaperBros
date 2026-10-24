// Lost Pages: small helpers for calling into the recompiled game and reading its structures (US0 addresses,
// offsets from the spm-decomp / spm-headers projects).
#pragma once

#include "abi_bridge.h"
#include "memory.h"
#include "ppc_runtime.h"

#include <cstdint>
#include <initializer_list>
#include <string>

namespace LostPages {

namespace Fn {
constexpr uint32_t marioGetPtr = 0x801214EC;
constexpr uint32_t marioKeyOff = 0x80122198;
constexpr uint32_t marioKeyOn = 0x801221D0;
constexpr uint32_t marioChgPose = 0x80125C88;
constexpr uint32_t marioGetScreenPos = 0x801262EC;
constexpr uint32_t marioTakeDamage = 0x80129C24;
constexpr uint32_t marioSetInvincibility = 0x8012A5BC;
constexpr uint32_t pouchGetPtr = 0x8014B594;
constexpr uint32_t pouchSetMaxHp = 0x8014C9C0;
constexpr uint32_t pouchSetHp = 0x8014C920;
constexpr uint32_t pouchSetAttack = 0x8014C8A0;
constexpr uint32_t seqGetSeq = 0x8017B3D8;
constexpr uint32_t npcGetEntries = 0x801C10A4;
constexpr uint32_t npcGetMaxEntries = 0x801C10B0;
constexpr uint32_t npcGetTribe = 0x801C88D4;
constexpr uint32_t npcSetAnim = 0x801BF0F8;
constexpr uint32_t npcTakeDamage = 0x801C5568;
constexpr uint32_t evtStopAll = 0x800DA024;
constexpr uint32_t evtStartAll = 0x800DA0A4;
constexpr uint32_t wpadGetButtonsHeld = 0x802350B8;
constexpr uint32_t wpadGetButtonsPressed = 0x802350E0;
constexpr uint32_t msgSearch = 0x800325F0;
constexpr uint32_t mapDispOff = 0x8007F0A4;
constexpr uint32_t mapDispOn = 0x8007F0B4;
constexpr uint32_t seqSetSeq = 0x8017B3C8;
}  // namespace Fn

constexpr uint32_t kItemDataTable = 0x803B6978;
constexpr uint32_t kItemDataSize = 0x2C;

// MarioWork
namespace Mario {
constexpr uint32_t flags = 0x00, curPose = 0x1C, motionId = 0x2C, keyOff = 0x35, position = 0x5C;
}
// NPCEntry
namespace Npc {
constexpr uint32_t flag8 = 0x008, name = 0x024, position = 0x2A0, tribeId = 0x49C, maxHp = 0x4EC, hp = 0x4F0,
                   parts = 0x714, size = 0x748;
}
// NPCTribe
namespace Tribe {
constexpr uint32_t animPose = 0x00, catchCard = 0x08, maxHp = 0x18, killXp = 0x38, attack = 0x64;
}
// MarioPouchWork
namespace Pouch {
constexpr uint32_t lostPages = 0x000, level = 0x004, attack = 0x008, hp = 0x00C, maxHp = 0x010, xp = 0x018,
                   useItems = 0x060, useItemCount = 10;
}

// Sideways Wii Remote buttons (the game's own layout): the D-pad is turned a quarter.
namespace Btn {
constexpr uint32_t screenDown = 0x0001, screenUp = 0x0002, screenRight = 0x0004, screenLeft = 0x0008,
                   plus = 0x0010, two = 0x0100, one = 0x0200, b = 0x0400, a = 0x0800, minus = 0x1000;
}

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 ReadVec3(uint32_t addr) {
    return {Memory::ReadFloat32(addr), Memory::ReadFloat32(addr + 4), Memory::ReadFloat32(addr + 8)};
}
inline void WriteVec3(uint32_t addr, const Vec3& v) {
    Memory::WriteFloat32(addr, v.x);
    Memory::WriteFloat32(addr + 4, v.y);
    Memory::WriteFloat32(addr + 8, v.z);
}

std::string GuestString(uint32_t addr, uint32_t max = 64);

// Calls guest functions from a native hook. Scratch memory comes from the guest stack below the caller's frame.
class Guest {
public:
    explicit Guest(CpuContext* ctx) : ctx_(ctx), scratch_((ctx->gpr[1] - 0x800u) & ~0xFu) {}
    uint32_t Call(uint32_t fn, std::initializer_list<uint32_t> args = {}, std::initializer_list<double> fargs = {});
    uint32_t Scratch(uint32_t offset) const { return scratch_ + offset; }  // 0x000-0x3FF usable
    // World position -> the game's 608x480 screen space (y down).
    bool ScreenPos(const Vec3& world, float& sx, float& sy);
    uint32_t MarioPtr() { return Call(Fn::marioGetPtr); }
    uint32_t PouchPtr() { return Call(Fn::pouchGetPtr); }
    uint32_t Sda() const { return ctx_->gpr[13]; }  // r13: the game's small-data base

private:
    CpuContext* ctx_;
    uint32_t scratch_;
};

// Mario's last height standing on the ground (the battle floor), tracked every frame in the field.
extern float g_lastGroundY;

// Address of a NUL-terminated copy of `text` in the game's own read-only data (found once, cached), so
// functions that keep the pointer (marioChgPose) never see it go away. 0 if the game has no such string.
uint32_t GameString(const char* text);

}  // namespace LostPages
