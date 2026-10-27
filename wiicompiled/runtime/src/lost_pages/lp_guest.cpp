#include "lp_guest.h"

#include <cstring>
#include <string_view>
#include <unordered_map>

namespace LostPages {

std::string GuestString(uint32_t addr, uint32_t max) {
    std::string s;
    for (uint32_t i = 0; i < max && Memory::Contains(addr + i, 1); ++i) {
        const char c = static_cast<char>(Memory::Read8(addr + i));
        if (!c) break;
        s += c;
    }
    return s;
}

uint32_t Guest::Call(uint32_t fn, std::initializer_list<uint32_t> args, std::initializer_list<double> fargs) {
    CpuContext call = *ctx_;
    call.gpr[1] = scratch_ - 0x100u;  // below our scratch block
    uint32_t r = 3;
    for (uint32_t a : args) call.gpr[r++] = a;
    uint32_t f = 1;
    for (double a : fargs) call.fpr[f++].d = a;
    InvokeIndirectCpu(fn, &call);
    return call.gpr[3];
}

bool Guest::ScreenPos(const Vec3& world, float& sx, float& sy) {
    const uint32_t in = Scratch(0x380), out = Scratch(0x3A0);
    if (!Memory::Contains(in, 0x40)) return false;
    WriteVec3(in, world);
    Call(Fn::marioGetScreenPos, {in, out, out + 4, out + 8});
    sx = Memory::ReadFloat32(out);
    sy = Memory::ReadFloat32(out + 4);
    return true;
}

uint32_t GameString(const char* text) {
    static std::unordered_map<std::string, uint32_t> cache;
    if (auto it = cache.find(text); it != cache.end()) return it->second;
    // main.dol's data (strings live in .data/.rodata) and the linked rel image.
    constexpr uint32_t kRanges[][2] = {{0x80004000u, 0x80580000u}, {0x80BF6840u, 0x80F12640u}};
    const size_t len = std::strlen(text) + 1;
    uint32_t found = 0;
    for (const auto& range : kRanges) {
        const uint8_t* host = Memory::GetPointer(range[0], range[1] - range[0]);
        if (!host) continue;
        const std::string_view hay(reinterpret_cast<const char*>(host), range[1] - range[0]);
        const std::string_view needle(text, len);  // includes the terminator
        // Must also start a string: the byte before is a terminator (or alignment padding).
        for (size_t pos = hay.find(needle); pos != std::string_view::npos; pos = hay.find(needle, pos + 1)) {
            if (pos == 0 || hay[pos - 1] == '\0') {
                found = range[0] + static_cast<uint32_t>(pos);
                break;
            }
        }
        if (found) break;
    }
    cache.emplace(text, found);
    return found;
}

uint32_t PersistentString(Guest& g, const char* text) {
    static std::unordered_map<std::string, uint32_t> copies;
    if (const uint32_t found = GameString(text)) return found;
    if (auto it = copies.find(text); it != copies.end()) return it->second;
    const uint32_t size = static_cast<uint32_t>(std::strlen(text) + 1);
    const uint32_t copy = g.Call(0x801A5634, {0, size});  // __memAlloc(heap 0)
    if (!Memory::Contains(copy, size)) return 0;
    for (uint32_t i = 0; i < size; ++i) Memory::Write8(copy + i, static_cast<uint8_t>(text[i]));
    copies.emplace(text, copy);
    return copy;
}

}  // namespace LostPages
