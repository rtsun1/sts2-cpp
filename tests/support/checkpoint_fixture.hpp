#pragma once
#include "sts2/persistence/checkpoint.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::test {
inline std::uint32_t checkpoint_u32(std::span<const std::byte> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::logic_error("Missing checkpoint field");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(std::to_integer<unsigned>(bytes[at + i])) << (8 * i);
    return value;
}
inline void checkpoint_u32(std::span<std::byte> bytes, std::size_t at, std::uint32_t value) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::logic_error("Missing checkpoint field");
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = std::byte(value >> (8 * i));
}
inline void checksum_checkpoint(std::span<std::byte> bytes) {
    if (bytes.size() < 8) throw std::logic_error("Missing checkpoint checksum");
    const auto hash = game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size() - 8});
    for (unsigned i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = std::byte(hash >> (8 * i));
}
struct CombatPayloadRange {
    std::size_t begin, size;
    std::size_t end() const noexcept { return begin + size; }
};
inline CombatPayloadRange combat_payload_range(std::span<const std::byte> bytes) {
    constexpr std::string_view magic = "STS2CB02";
    const auto at = std::search(bytes.begin(), bytes.end(), magic.begin(), magic.end(),
        [](std::byte a, char b) { return std::to_integer<unsigned char>(a) == static_cast<unsigned char>(b); });
    const auto begin = static_cast<std::size_t>(at - bytes.begin());
    if (at == bytes.end() || begin < 4) throw std::logic_error("Missing combat payload");
    const auto size = checkpoint_u32(bytes, begin - 4);
    if (size < 8 || size > bytes.size() - begin) throw std::logic_error("Invalid combat payload range");
    return {begin, size};
}
inline void checksum_combat_and_run(std::vector<std::byte>& bytes) {
    const auto range = combat_payload_range(bytes);
    checksum_checkpoint(std::span(bytes).subspan(range.begin, range.size));
    checksum_checkpoint(bytes);
}
inline std::size_t player_identity_offset(std::span<const std::byte> bytes) {
    std::size_t at = 8 + 4 + 4 + game_digest.size() + SetupRules::fingerprint().size() + 8;
    at += 4; // Highest creature generation.
    const auto slots = checkpoint_u32(bytes, at); at += 4;
    for (std::uint32_t i = 0; i < slots; ++i) {
        at += 4 + 1 + 12 + 8 + 4; // Generation, live, HP/max HP/block, hit count.
        const auto powers = checkpoint_u32(bytes, at); at += 4;
        at += std::size_t(powers) * 35;
    }
    return at;
}
} // namespace sts2::engine::test
