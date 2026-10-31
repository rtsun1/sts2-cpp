#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace sts2::engine {
// Audited identities; execution support is checked separately.
enum class EnchantmentId : std::uint8_t {
    none, adroit, clone, corrupted, glam, goopy, imbued, inky, instinct,
    momentum, nimble, perfect_fit, royally_approved, sharp, slither,
    slumbering_essence, souls_power, sown, spiral, steady, swift,
    tezcataras_ember, vigorous, count
};
inline constexpr std::string_view model_id(EnchantmentId id) noexcept {
    constexpr std::array<std::string_view, static_cast<std::size_t>(EnchantmentId::count)> names = {
        "",
        "ENCHANTMENT.ADROIT",
        "ENCHANTMENT.CLONE",
        "ENCHANTMENT.CORRUPTED",
        "ENCHANTMENT.GLAM",
        "ENCHANTMENT.GOOPY",
        "ENCHANTMENT.IMBUED",
        "ENCHANTMENT.INKY",
        "ENCHANTMENT.INSTINCT",
        "ENCHANTMENT.MOMENTUM",
        "ENCHANTMENT.NIMBLE",
        "ENCHANTMENT.PERFECT_FIT",
        "ENCHANTMENT.ROYALLY_APPROVED",
        "ENCHANTMENT.SHARP",
        "ENCHANTMENT.SLITHER",
        "ENCHANTMENT.SLUMBERING_ESSENCE",
        "ENCHANTMENT.SOULS_POWER",
        "ENCHANTMENT.SOWN",
        "ENCHANTMENT.SPIRAL",
        "ENCHANTMENT.STEADY",
        "ENCHANTMENT.SWIFT",
        "ENCHANTMENT.TEZCATARAS_EMBER",
        "ENCHANTMENT.VIGOROUS"
    };
    const auto index = static_cast<std::size_t>(id);
    return index < names.size() ? names[index] : std::string_view{};
}
enum class AfflictionId : std::uint8_t { none, bound, entangled, galvanized, hexed, ringing, smog, tainted, count };
inline constexpr std::string_view model_id(AfflictionId id) noexcept {
    constexpr std::array<std::string_view, static_cast<std::size_t>(AfflictionId::count)> names = {
        "",
        "AFFLICTION.BOUND",
        "AFFLICTION.ENTANGLED",
        "AFFLICTION.GALVANIZED",
        "AFFLICTION.HEXED",
        "AFFLICTION.RINGING",
        "AFFLICTION.SMOG",
        "AFFLICTION.TAINTED"
    };
    const auto index = static_cast<std::size_t>(id);
    return index < names.size() ? names[index] : std::string_view{};
}
} // namespace sts2::engine
