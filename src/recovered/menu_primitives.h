#pragma once

#include <cstddef>
#include <cstdint>

// Original BOOT SHA-256:
// 2b361beb4c55e08af2828713861429e768a1140f284a11f606fba5f580cd483c
// Portable bounded helpers, not a title/menu lifecycle or game host service.
namespace dokodemo::recovered {

struct MenuBytes { std::uint8_t* data; std::size_t size; };
struct MenuConstBytes { const std::uint8_t* data; std::size_t size; };

// All errors are protective native barriers. They do not reproduce PSP faults
// or unknown callee effects. On error, output references and memory are unchanged.
// Output references represent source registers and must be separate from byte
// views. Native byte views allow unaligned host storage; correspondence to original
// LW/SW assumes aligned source addresses, not the native buffer's address.
enum class MenuStatus : std::uint32_t {
    ok = 0, invalid_buffer = 1, division_zero_barrier = 2,
    unresolved_request = 3, invalid_projection_domain = 4,
    unmapped_profile_index = 5, unsupported_alias = 6,
};

// VA94F90..94FCC: counter is the explicitly loaded word, all arguments are bits.
// counter0 returns1; negative signed wrapping(counter-delay) returns0. Only a
// reached division with interval0 is a barrier; otherwise returns0 or2.
MenuStatus menu_repeat_94f90(std::uint32_t counter, std::uint32_t delay,
    std::uint32_t interval, std::uint32_t& result) noexcept;

// VA17B84: requested3/4 only, fields+1C=old+20,+20=+24,+28=6.
// Caller provides an explicitly owned host field span of at least0x2C bytes.
// Scheduling and the original host allocation/lifecycle are unproved.
MenuStatus menu_commit_17b84_requests3_4(MenuBytes host) noexcept;

// VA4184..41D4: base_offset represents original a0 within an explicit allocation.
// Signed index/8 truncates toward0; MIPS variable shift uses remainder&31.
// Negative indices are allowed only when their target byte is mapped here.
MenuStatus menu_bitset_4184(MenuBytes allocation, std::size_t base_offset,
    std::uint32_t index_bits, std::uint32_t value_bits) noexcept;

// VA24FF4..25020: caller explicitly binds seg1 BSS raw2A9DC/canonical10E55C.
// The slot has no original file bytes. No default/initial profile is supplied.
MenuStatus menu_profile_bit_24ff4(MenuBytes allocation, std::size_t base_offset,
    std::uint32_t index_bits, std::uint32_t value_bits) noexcept;

// Requested5 composition through all seven24FF4 calls. Requires host>=0x4B0,
// explicit profile allocation disjoint from host, and all target bytes mapped.
// Copies+1C/+20, then sets the first six profile bits (+58+15,26,29,1,2,+58+3),
// writes host+4AC=1 and +5C=0 (delay slot17C84), sets the seventh bit31,
// then writes host+28=3. Does not manufacture profile/host contents.
MenuStatus menu_commit_17b84_request5(MenuBytes host, MenuBytes profile,
    std::size_t profile_base_offset) noexcept;

struct MenuLaunch23200 { std::uint32_t a2; std::uint32_t a3; std::uint32_t callee_va; };
// VA23200..23210 stops BEFORE executing67FD4, after its argument delay slot.
MenuStatus menu_launch_arguments_23200(MenuConstBytes host,
    MenuLaunch23200& result) noexcept;

// VA25058..25064 readonly load: caller binds four bytes at seg1 raw2A8D8/
// canonical10E458, a BSS slot with unknown initial value and writer provenance.
MenuStatus menu_choice_getter_25058(MenuConstBytes bound_slot,
    std::uint32_t& result) noexcept;

// VA19AA8..19AC0: +28=2, bound event byte=low8(event), +24=requested, in that
// order. Event may alias host: final+24 write can overwrite it. Original-word
// correspondence requires aligned a0 and the fixed global's aligned address;
// tested aliases are offsets1C,20,24,28,4C8, not arbitrary PSP byte addresses.
// Caller explicitly binds BSS raw10EA0/canonicalF4A20; no default event slot.
MenuStatus menu_request_19aa8(MenuBytes host, std::uint32_t requested,
    std::uint32_t event_bits, std::uint8_t* bound_event_byte) noexcept;

// Post-call slice32F4..3330 only, counters+70/+74; excludes preceding32F0
// unrelated write and upstream input polling. Held bits0/1 increment with wrap,
// released counters becomeFFFFFFFF. Does not synthesize controller events.
MenuStatus menu_counters_32f4(MenuBytes input, std::uint32_t held_bits) noexcept;

// Post94F90 branch slices312E4/316A0/31654; caller supplies repeat returns.
// Choice1..3, repeat0..2 only. Positive has priority. No4AC38 effect call.
// Scalar projection requires original title+F4/+130 to be mapped and word-
// aligned, with the source entry registers described in semantic-evidence.
// These are caller-side source-memory premises, not native pointer addresses.
MenuStatus menu_navigation_312e4(std::uint32_t choice, std::uint32_t positive,
    std::uint32_t negative, std::uint32_t& result) noexcept;

// VA33FB8 call-free table branches state+124=FFFFFFFF or7 only.
// First clears+128 then returns0; second returns+128. Other states are barriers.
MenuStatus menu_pre_input_33fb8(MenuBytes title, std::uint32_t& result) noexcept;

} // namespace dokodemo::recovered
