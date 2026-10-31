#pragma once
#include "sts2/state/cards.hpp"
#include <nlohmann/json.hpp>

namespace sts2::engine {
inline nlohmann::json saved_enchantment(CardEnchantment value) {
    if (value.definition == EnchantmentId::none) return nullptr;
    return {{"id", model_id(value.definition)}, {"amount", value.amount}, {"props", nullptr}};
}
inline nlohmann::json enchantment_runtime(const CombatCard& card, const EnchantmentRuntimeData* runtime) {
    if (card.enchantment.definition == EnchantmentId::none) return nullptr;
    const auto* use = runtime ? std::get_if<EnchantmentUseState>(runtime) : nullptr;
    const auto* momentum = runtime ? std::get_if<MomentumState>(runtime) : nullptr;
    const bool consumed = use && use->consumed;
    auto fields = nlohmann::json::object();
    if (card.enchantment.definition == EnchantmentId::glam) fields["Glam._usedThisCombat"] = consumed;
    if (card.enchantment.definition == EnchantmentId::momentum) fields["Momentum._extraDamage"] = momentum ? momentum->extra_damage : 0;
    return {{"status", consumed ? "Disabled" : "Normal"}, {"model_fields", fields}};
}
inline nlohmann::json local_keywords(CardKeywordMask keywords) {
    auto result = nlohmann::json::array();
    for (const auto& [keyword, name] : {std::pair{CardKeyword::exhaust, "Exhaust"}, {CardKeyword::retain, "Retain"},
        {CardKeyword::sly, "Sly"}, {CardKeyword::ethereal, "Ethereal"}, {CardKeyword::innate, "Innate"},
        {CardKeyword::unplayable, "Unplayable"}, {CardKeyword::eternal, "Eternal"}})
        if (has_keyword(keywords, keyword)) result.push_back(name);
    return result;
}
} // namespace sts2::engine
