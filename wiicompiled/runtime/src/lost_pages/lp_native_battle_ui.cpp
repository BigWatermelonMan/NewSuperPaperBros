#include "lp_native_battle_ui.h"

#include <cstdlib>

// This module owns the battle dialogue event. The HUD is drawn by the overlay
// in lp_battle_ui.cpp because the battle diorama is composited over the guest
// EFB, hiding guest-drawn HUD entries.

namespace LostPages {
namespace {
constexpr uint32_t kEvtEntry = 0x800D8D58, kEvtCheckID = 0x800D9D58, kEvtDeleteID = 0x800D9CD0;
constexpr uint32_t kEvtMsgPrint = 0x800FB668;
constexpr uint32_t kEvtWork = 0x804C9990; // evtEntry: count at +0, entries at +0x90
constexpr uint32_t kUserFunc = 0x0005005C, kReturn = 2, kEndScript = 1;
constexpr uint32_t kScriptBytes = 8 * 4;
struct DialogueState {
    uint32_t script = 0, entry = 0, id = 0;
    unsigned polls = 0;
    bool autoplay = false, closing = false;
} dialogue;

bool OwnedEvent(Guest& g) {
    return dialogue.entry && g.Call(kEvtCheckID, {dialogue.id}) &&
        Memory::Contains(dialogue.entry, 0x1A8) &&
        (Memory::Read8(dialogue.entry + 8) & 1) &&
        Memory::Read32(dialogue.entry + 0x158) == dialogue.id &&
        Memory::Read32(dialogue.entry + 0x198) == dialogue.script;
}

// Same close path as msgWindow_ForceClose, restricted to the window attached to our event.
// Checking its back pointer prevents stale event IDs/window slots from affecting story dialogue.
bool CloseOwnedWindow(Guest& g) {
    // Before USER_FUNC first runs, +0x170 is merely zero, not a window that we own.
    if (Memory::Read32(dialogue.entry + 20) != dialogue.script + 8) return false;
    const auto index = Memory::Read32(dialogue.entry + 0x170);
    if (index >= 7 || !Memory::Contains(g.Sda() - 30576, 4)) return false;
    const auto base = Memory::Read32(g.Sda() - 30576);
    if (!base || !Memory::Contains(base, 7 * 72)) return false;
    const auto window = base + index * 72;
    const auto handle = Memory::Read32(window + 40);
    if (!handle || !Memory::Contains(handle, 4)) return false;
    const auto data = Memory::Read32(handle);
    if (!data || !Memory::Contains(data, 0xF204) ||
        Memory::Read32(data + 0xF200) != dialogue.entry) return false;
    Memory::Write16(window, 7);
    g.Call(0x80032A14, {handle}); // msgSetStayPose, also used by native ForceClose
    return true;
}

bool EventSlotAvailable() {
    if (!Memory::Contains(kEvtWork, 0x94)) return false;
    const auto count = Memory::Read32(kEvtWork), entries = Memory::Read32(kEvtWork + 0x90);
    if (!entries || !count || count > 1024 || !Memory::Contains(entries, count * 0x1A8)) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!(Memory::Read8(entries + i * 0x1A8 + 8) & 1)) return true;
    return false; // evtEntry asserts when the event pool is full
}

bool NpcExistsImpl(Guest& g, const char* name) {
    const auto entries = g.Call(Fn::npcGetEntries), count = g.Call(Fn::npcGetMaxEntries);
    if (!entries || count > 1024 || !Memory::Contains(entries, count * Npc::size)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto npc = entries + i * Npc::size;
        if ((Memory::Read32(npc) & 1) && GuestString(npc + Npc::name, 32) == name) return true;
    }
    return false;
}
} // namespace

bool StartDialogue(Guest& g, const char* msgName, const char* speaker, bool literal) {
    if (!msgName || !*msgName || OwnedEvent(g) || !EventSlotAvailable()) {
        RT_LOGF(RT_TAG_RUNTIME,"[lpnative] dialogue rejected: event=%08X pool=%d\n",dialogue.entry,EventSlotAvailable());
        return false;
    }
    dialogue.entry = dialogue.id = 0;
    const auto message = PersistentString(g, msgName);
    if (!message || (!literal && !g.Call(0x800326C8, {message}))) return false; // msgSearchNoFallback
    uint32_t voice = 0;
    if (speaker && *speaker && NpcExistsImpl(g, speaker)) {
        voice = PersistentString(g, speaker);
        if (!voice) return false;
    }
    if (!dialogue.script || !Memory::Contains(dialogue.script, kScriptBytes) ||
        Memory::Read32(dialogue.script) != kUserFunc || Memory::Read32(dialogue.script + 4) != kEvtMsgPrint) {
        dialogue.script = g.Call(0x801A5634, {0, kScriptBytes}); // one reusable heap-0 script
        if (!dialogue.script || !Memory::Contains(dialogue.script, kScriptBytes)) { dialogue.script = 0; return false; }
    }
    // evtEntry's pre-parser stops at END_SCRIPT (1); RETURN (2) calls evtDelete when executed.
    // _evt_msg_print tests flags & 1 for DIRECT. flags & 2 selects an ITEM, so leave it clear for NPCs.
    const uint32_t script[] = {kUserFunc, kEvtMsgPrint, literal ? 1u : 0u, message, 0, voice, kReturn, kEndScript};
    for (unsigned i = 0; i < 8; ++i) Memory::Write32(dialogue.script + i * 4, script[i]);
    const auto entry = g.Call(kEvtEntry, {dialogue.script, 0xEF, 0});
    if (!entry || !Memory::Contains(entry, 0x1A8)) return false;
    dialogue.entry = entry;
    dialogue.id = Memory::Read32(entry + 0x158);
    dialogue.polls = 0;
    dialogue.autoplay = std::getenv("SPM_LP_AUTOPLAY") != nullptr;
    dialogue.closing = false;
    // A newly created event normally runs after evtStopAll. Also exempt only this event from
    // subsequent stop masks, including evtEntry's inherited global pause mode.
    Memory::Write8(entry + 12, 0);
    Memory::Write8(entry + 8, Memory::Read8(entry + 8) & ~2u);
    RT_LOGF(RT_TAG_RUNTIME,"[lpnative] dialogue started: id=%u entry=%08X literal=%d speaker=%s\n",dialogue.id,entry,literal,voice?speaker:"center");
    return true;
}

bool NpcActive(Guest& g, const char* name) {
    const auto entries = g.Call(Fn::npcGetEntries), count = g.Call(Fn::npcGetMaxEntries);
    if (!entries || count > 1024 || !Memory::Contains(entries, count * Npc::size)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto npc = entries + i * Npc::size;
        if ((Memory::Read32(npc + Npc::flag8) & 1) && GuestString(npc + Npc::name, 32) == name) return true;
    }
    return false;
}

// The game's seven message window slots (msgdrv, 72 bytes each, at r13-30576): a slot is in use while its first
// halfword (the window's state) is set and it holds a message handle.
bool MessageWindowOpen(Guest& g) {
    if (!Memory::Contains(g.Sda() - 30576, 4)) return false;
    const auto base = Memory::Read32(g.Sda() - 30576);
    if (!base || !Memory::Contains(base, 7 * 72)) return false;
    bool open = false;
    uint32_t states = 0, handles = 0;
    for (uint32_t i = 0; i < 7; ++i) {
        const auto window = base + i * 72;
        const uint32_t state = Memory::Read16(window), handle = Memory::Read32(window + 40);
        states = states * 16 + (state & 15);
        handles = handles * 2 + (handle != 0);
        if (state && handle) open = true;
    }
    static const bool log = std::getenv("SPM_LP_LOG_MSGWIN") != nullptr;
    static uint64_t lastStates = ~0ull;
    if (log && (uint64_t{states} << 8 | handles) != lastStates) {
        lastStates = uint64_t{states} << 8 | handles;
        RT_LOGF(RT_TAG_RUNTIME, "[lpnative] message windows %07X handles %02X open %d\n", states, handles, open);
    }
    return open;
}

bool EventRunningBetween(Guest& g, uint32_t lo, uint32_t hi) {
    (void)g;
    if (!Memory::Contains(kEvtWork, 0x94)) return false;
    const auto count = Memory::Read32(kEvtWork), entries = Memory::Read32(kEvtWork + 0x90);
    if (!entries || !count || count > 1024 || !Memory::Contains(entries, count * 0x1A8)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto entry = entries + i * 0x1A8;
        if (!(Memory::Read8(entry + 8) & 1)) continue;
        const auto args = Memory::Read32(entry + 20);  // the running command's arguments
        if (args >= lo && args <= hi) return true;
    }
    return false;
}

bool DialogueActive(Guest& g) {
    if (!OwnedEvent(g)) { dialogue.entry = dialogue.id = 0; return false; }
    if (dialogue.autoplay && !dialogue.closing && ++dialogue.polls >= 90) {
        dialogue.closing = CloseOwnedWindow(g);
    }
    return true; // wait for the native close animation and RETURN; never advance battle prematurely
}

void ReleaseDialogue(Guest& g) {
    if (OwnedEvent(g)) {
        CloseOwnedWindow(g);
        g.Call(kEvtDeleteID, {dialogue.id});
    }
    dialogue.entry = dialogue.id = 0;
    dialogue.polls = 0;
    dialogue.closing = false;
}
} // namespace LostPages
