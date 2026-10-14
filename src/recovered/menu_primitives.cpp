#include "menu_primitives.h"

#include <limits>

namespace dokodemo::recovered {
namespace {
bool valid(MenuBytes b, std::size_t minimum) noexcept {
    return b.data != nullptr && b.size >= minimum;
}
bool valid(MenuConstBytes b, std::size_t minimum) noexcept {
    return b.data != nullptr && b.size >= minimum;
}
std::uint32_t load(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8u) |
        (std::uint32_t(p[2]) << 16u) | (std::uint32_t(p[3]) << 24u);
}
void store(std::uint8_t* p, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) p[i] = std::uint8_t(v >> (i * 8u));
}
std::int64_t signed_bits(std::uint32_t bits) noexcept {
    return bits < 0x80000000u ? std::int64_t(bits) : std::int64_t(bits) - 0x100000000LL;
}
bool bit_address(MenuBytes allocation, std::size_t base, std::uint32_t index,
    std::size_t& offset, std::uint32_t& mask) noexcept {
    if (!valid(allocation, 1) || base > allocation.size) return false;
    const std::int64_t value = signed_bits(index);
    const std::int64_t quotient = value / 8;
    const std::int64_t remainder = value - quotient * 8;
    if (quotient < 0) {
        const auto distance = std::uint64_t(-quotient);
        if (distance > base) return false;
        offset = base - std::size_t(distance);
    } else {
        const auto distance = std::uint64_t(quotient);
        if (distance > allocation.size - base) return false;
        offset = base + std::size_t(distance);
    }
    if (offset >= allocation.size) return false;
    mask = std::uint32_t(1u) << (std::uint32_t(remainder) & 31u);
    return true;
}
bool overlaps(MenuBytes a, MenuBytes b) noexcept {
    const auto left = reinterpret_cast<std::uintptr_t>(a.data);
    const auto right = reinterpret_cast<std::uintptr_t>(b.data);
    return left <= right ? right - left < a.size : left - right < b.size;
}
} // namespace

MenuStatus menu_repeat_94f90(std::uint32_t counter, std::uint32_t delay,
    std::uint32_t interval, std::uint32_t& result) noexcept {
    if (counter == 0) { result = 1; return MenuStatus::ok; }
    const std::uint32_t delta = counter - delay;
    if ((delta & 0x80000000u) != 0) { result = 0; return MenuStatus::ok; }
    if (interval == 0) return MenuStatus::division_zero_barrier;
    // Reached numerator is0..INT_MAX, hence INT_MIN/-1 is unreachable here.
    result = std::int64_t(delta) % signed_bits(interval) == 0 ? 2u : 0u;
    return MenuStatus::ok;
}

MenuStatus menu_commit_17b84_requests3_4(MenuBytes host) noexcept {
    if (!valid(host, 0x2C)) return MenuStatus::invalid_buffer;
    const std::uint32_t requested = load(host.data + 0x24);
    if (requested != 3 && requested != 4) return MenuStatus::unresolved_request;
    const std::uint32_t current = load(host.data + 0x20);
    store(host.data + 0x1C, current);
    store(host.data + 0x20, requested);
    store(host.data + 0x28, 6);
    return MenuStatus::ok;
}

MenuStatus menu_bitset_4184(MenuBytes allocation, std::size_t base,
    std::uint32_t index, std::uint32_t value) noexcept {
    std::size_t offset = 0; std::uint32_t mask = 0;
    if (!bit_address(allocation, base, index, offset, mask))
        return MenuStatus::unmapped_profile_index;
    const std::uint32_t before = allocation.data[offset];
    allocation.data[offset] = std::uint8_t((value & 0xFFu) != 0 ? before | mask : before & ~mask);
    return MenuStatus::ok;
}

MenuStatus menu_profile_bit_24ff4(MenuBytes allocation, std::size_t base,
    std::uint32_t index, std::uint32_t value) noexcept {
    return menu_bitset_4184(allocation, base, index, value);
}

MenuStatus menu_commit_17b84_request5(MenuBytes host, MenuBytes profile,
    std::size_t base) noexcept {
    if (!valid(host, 0x4B0)) return MenuStatus::invalid_buffer;
    if (load(host.data + 0x24) != 5) return MenuStatus::unresolved_request;
    if (!valid(profile, 1) || base > profile.size) return MenuStatus::unmapped_profile_index;
    if (overlaps(host, profile)) return MenuStatus::unsupported_alias;
    const std::uint32_t index = load(host.data + 0x58);
    const std::uint32_t indices[] = {index + 0x15u, 26, 29, 1, 2, index + 3u, 31};
    std::size_t offsets[7]{}; std::uint32_t masks[7]{};
    for (unsigned i = 0; i < 7; ++i)
        if (!bit_address(profile, base, indices[i], offsets[i], masks[i]))
            return MenuStatus::unmapped_profile_index;
    const std::uint32_t current = load(host.data + 0x20);
    store(host.data + 0x1C, current);
    store(host.data + 0x20, 5);
    for (unsigned i = 0; i < 6; ++i)
        profile.data[offsets[i]] = std::uint8_t(std::uint32_t(profile.data[offsets[i]]) | masks[i]);
    store(host.data + 0x4AC, 1);
    // BOOT17C84 is the seventh24FF4 call's delay slot: clear before bit31.
    store(host.data + 0x5C, 0);
    profile.data[offsets[6]] = std::uint8_t(std::uint32_t(profile.data[offsets[6]]) | masks[6]);
    store(host.data + 0x28, 3);
    return MenuStatus::ok;
}

MenuStatus menu_launch_arguments_23200(MenuConstBytes host, MenuLaunch23200& result) noexcept {
    if (!valid(host, 0x4C9)) return MenuStatus::invalid_buffer;
    result = {host.data[0x4C8] == 0 ? 1u : 0u, 0u, 0x67FD4u};
    return MenuStatus::ok;
}

MenuStatus menu_choice_getter_25058(MenuConstBytes slot, std::uint32_t& result) noexcept {
    if (!valid(slot, 4)) return MenuStatus::invalid_buffer;
    result = load(slot.data);
    return MenuStatus::ok;
}

MenuStatus menu_request_19aa8(MenuBytes host, std::uint32_t requested,
    std::uint32_t event, std::uint8_t* event_byte) noexcept {
    if (!valid(host, 0x2C) || event_byte == nullptr) return MenuStatus::invalid_buffer;
    store(host.data + 0x28, 2);
    *event_byte = std::uint8_t(event);
    store(host.data + 0x24, requested);
    return MenuStatus::ok;
}

MenuStatus menu_counters_32f4(MenuBytes input, std::uint32_t held) noexcept {
    if (!valid(input, 0x78)) return MenuStatus::invalid_buffer;
    store(input.data + 0x70, (held & 1u) != 0 ? load(input.data + 0x70) + 1u : 0xFFFFFFFFu);
    store(input.data + 0x74, (held & 2u) != 0 ? load(input.data + 0x74) + 1u : 0xFFFFFFFFu);
    return MenuStatus::ok;
}

MenuStatus menu_navigation_312e4(std::uint32_t choice, std::uint32_t positive,
    std::uint32_t negative, std::uint32_t& result) noexcept {
    if (choice < 1 || choice > 3 || positive > 2 || negative > 2)
        return MenuStatus::invalid_projection_domain;
    result = choice;
    if (positive != 0) {
        if (choice == 2) result = 3;
        else if (choice == 3 && positive == 1) result = 2;
    } else if (negative != 0) {
        if (choice == 3) result = 2;
        else if (choice == 2 && negative == 1) result = 3;
    }
    return MenuStatus::ok;
}

MenuStatus menu_pre_input_33fb8(MenuBytes title, std::uint32_t& result) noexcept {
    if (!valid(title, 0x12C)) return MenuStatus::invalid_buffer;
    const std::uint32_t state = load(title.data + 0x124);
    if (state == 0xFFFFFFFFu) { store(title.data + 0x128, 0); result = 0; }
    else if (state == 7) result = load(title.data + 0x128);
    else return MenuStatus::unresolved_request;
    return MenuStatus::ok;
}

} // namespace dokodemo::recovered
