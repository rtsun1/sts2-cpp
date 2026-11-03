#include "combat_payload.hpp"
#include "sts2/engine/combat.hpp"
#include <bit>
#include <new>
#include <stdexcept>

namespace sts2::engine {
namespace {
struct Writer {
    std::vector<std::byte> data;
    void u8(std::uint8_t v) { data.push_back(std::byte(v)); }
    void u16(std::uint16_t value) { for (unsigned i = 0; i < 2; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i))); }
    void u32(std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void u64(std::uint64_t v) { for (unsigned i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void i32(std::int32_t v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void text(std::string_view s) { for (auto c : s) u8(static_cast<std::uint8_t>(c)); }
    void handle(CardHandle h) { u32(h.slot); u32(h.generation); }
    void range(const RangeProgress& r) { u32(r.begin); u32(r.count); u32(r.cursor); }
    void frame(const ExecutionFrame& f) {
        u8(static_cast<std::uint8_t>(f.index()));
        if (const auto* value = std::get_if<CardProgramFrame>(&f)) { handle(value->source); u32(value->instruction); u32(value->play); i32(value->energy); u32(value->target.slot); u32(value->target.generation); }
        else if (const auto* value = std::get_if<DrawFrame>(&f)) {
            range(value->listeners); handle(value->current_card); i32(value->remaining); u8(value->handler.stage); u8(static_cast<std::uint8_t>(value->phase)); u8(value->from_hand_draw);
        } else if (const auto* value = std::get_if<ExhaustCardsFrame>(&f)) {
            range(value->cards); range(value->listeners); u8(value->handler.stage); u8(static_cast<std::uint8_t>(value->phase)); u8(static_cast<std::uint8_t>(value->source_zone)); u8(value->charge_card_step);
        } else if (const auto* value = std::get_if<ShuffleFrame>(&f)) {
            range(value->listeners); u32(value->expected_selection_count); u8(value->handler.stage); u8(static_cast<std::uint8_t>(value->phase)); u8(value->charge_draw_step_if_hooks);
        }
    }
    void card_extra(const CardExtraData& data) {
        u8(static_cast<std::uint8_t>(data.index()));
        const auto& scythe = std::get<TheScytheExtraData>(data);
        i32(scythe.current_damage); i32(scythe.increased_damage);
    }

};
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    std::uint8_t u8() { if (at == bytes.size()) throw std::invalid_argument("truncated checkpoint"); return std::to_integer<std::uint8_t>(bytes[at++]); }
    std::uint16_t u16() { const auto low = u8(); return static_cast<std::uint16_t>(low | (std::uint16_t(u8()) << 8)); }
    bool boolean() { const auto v = u8(); if (v > 1) throw std::invalid_argument("invalid boolean"); return v != 0; }
    std::uint32_t u32() { std::uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= std::uint32_t(u8()) << (8 * i); return v; }
    std::uint64_t u64() { std::uint64_t v = 0; for (unsigned i = 0; i < 8; ++i) v |= std::uint64_t(u8()) << (8 * i); return v; }
    std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
    CardExtraData card_extra() {
        switch (u8()) {
        case 0: return TheScytheExtraData{i32(), i32()};
        default: throw std::invalid_argument("Unknown card extra data");
        }
    }
    CardHandle handle() { const auto slot = u32(); return {slot, u32()}; }
    std::uint32_t count(std::size_t width, std::uint64_t limit) {
        const auto n = u32(); if (n > limit) throw std::length_error("Continuation capacity exceeded");
        if (n > (bytes.size() - at) / width) throw std::invalid_argument("Truncated continuation data");
        return n;
    }
    RangeProgress range() { return {u32(), u32(), u32()}; }
    ExecutionFrame frame() {
        switch (u8()) {
        case 0: return CardProgramFrame{handle(), u32(), u32(), i32(), {u32(), u32()}};
        case 1: {
            DrawFrame f; f.listeners = range(); f.current_card = handle(); f.remaining = i32();
            f.handler.stage = u8(); f.phase = static_cast<DrawPhase>(u8()); f.from_hand_draw = boolean(); return f;
        }
        case 2: {
            ExhaustCardsFrame f; f.cards = range(); f.listeners = range(); f.handler.stage = u8();
            f.phase = static_cast<ExhaustPhase>(u8()); f.source_zone = static_cast<Zone>(u8()); f.charge_card_step = boolean(); return f;
        }
        case 3: {
            ShuffleFrame f; f.listeners = range(); f.expected_selection_count = u32(); f.handler.stage = u8();
            f.phase = static_cast<ShufflePhase>(u8()); f.charge_draw_step_if_hooks = boolean(); return f;
        }
        default: throw std::invalid_argument("Unknown execution frame");
        }
    }

};
std::uint64_t digest(std::span<const std::byte> bytes) {
    return game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
}
std::uint64_t content_fingerprint() {
    Writer w;
    for (const auto& card : Ruleset::pinned().cards()) {
        w.u32(static_cast<std::uint32_t>(card.model_id.size())); w.text(card.model_id);
        w.u32(static_cast<std::uint32_t>(card.name.size())); w.text(card.name);
        w.i32(card.cost); w.u8(card.enemy_target); w.u8(static_cast<std::uint8_t>(card.rarity));
        w.u8(static_cast<std::uint8_t>(card.kind)); w.u8(card.keyword_mask);
        for (const auto program : card.programs) {
            w.u32(static_cast<std::uint32_t>(program.size()));
            for (const auto& instruction : program) { w.u8(static_cast<std::uint8_t>(instruction.opcode)); w.i32(instruction.base); }
        }
    }
    // Version exceptional enemy/power/relic handlers and clock/RNG semantics too.
    w.text("shrinker-a0:niche:38-40:late-victory:retained-death:duration-history:shrink70:chomp7:stomp13;vulnerable150;burning-blood6;survivor1;action_clock_v1;xoshiro256ss-v1");
    return digest(w.data);
}
Checkpoint persistence::encode_combat(const RunState& s) {
    if (!valid_combat(s)) throw std::invalid_argument("Cannot checkpoint invalid simulation");
    Writer w;
    w.text("STS2CB02"); w.u32(2); w.u32(s.combat().rules); w.text(game_digest); w.u64(content_fingerprint());
    w.u8(static_cast<std::uint8_t>(s.combat().phase)); w.i32(s.combat().energy); w.i32(s.combat().turn);
    w.u8(s.combat().enemy_side); w.u8(s.combat().burning_blood); w.u32(s.combat().next_creation_order);
    w.u8(s.combat().exhaust_relic_count); for (auto relic : s.combat().exhaust_relics) w.u8(static_cast<std::uint8_t>(relic));
    w.u32(s.cards().data.highest_generation);
    w.u32(static_cast<std::uint32_t>(s.cards().slots().size()));
    for (const auto& slot : s.cards().slots()) {
        w.u32(slot.generation); w.u8(slot.live); w.u16(static_cast<std::uint16_t>(slot.value.definition));
        w.u8(slot.value.upgraded); w.u32(slot.value.creation_order); w.u32(slot.value.origin.slot); w.u32(slot.value.origin.generation); w.i32(slot.value.last_finished_player_turn); w.u8(slot.value.removed);
        const auto& c = slot.value;
        w.u8(static_cast<std::uint8_t>(c.enchantment.definition)); w.i32(c.enchantment.amount);
        w.u8(static_cast<std::uint8_t>(c.affliction.definition)); w.i32(c.affliction.amount);
        w.u8(c.keyword_mask); w.u8(c.single_turn_retain); w.u8(c.single_turn_sly); w.u8(c.exhaust_on_next_play); w.i32(c.base_replay_count);
    }
    w.u32(static_cast<std::uint32_t>(s.enchantment_runtime().size()));
    for (const auto& entry : s.enchantment_runtime()) {
        w.handle(entry.card); w.u8(static_cast<std::uint8_t>(entry.data.index()));
        if (const auto* use = std::get_if<EnchantmentUseState>(&entry.data)) w.u8(use->consumed);
        else w.i32(std::get<MomentumState>(entry.data).extra_damage);
    }
    w.u32(static_cast<std::uint32_t>(s.card_extra().size()));
    for (const auto& entry : s.card_extra()) { w.handle(entry.card); w.card_extra(entry.data); }
    w.u32(static_cast<std::uint32_t>(s.cards().free_slots().size()));
    for (auto slot : s.cards().free_slots()) w.u32(slot);
    for (const auto& zone : s.zone_views()) { w.u32(static_cast<std::uint32_t>(zone.size())); for (auto h : zone) w.handle(h); }
    w.u32(static_cast<std::uint32_t>(s.cost_modifiers().size()));
    for (const auto& modifier : s.cost_modifiers()) { w.handle(modifier.card); w.u8(static_cast<std::uint8_t>(modifier.kind)); w.u8(static_cast<std::uint8_t>(modifier.expiry)); w.i32(modifier.amount); w.u8(modifier.reduce_only); }
    w.u32(static_cast<std::uint32_t>(s.captured_hook_listeners().size()));
    for (const auto& listener : s.captured_hook_listeners()) {
        w.u32(listener.owner.slot); w.u32(listener.owner.generation); w.u32(listener.power_instance);
        w.i32(listener.removed_power_amount); w.u8(static_cast<std::uint8_t>(listener.handler));
    }
    w.u32(static_cast<std::uint32_t>(s.continuation_cards().size())); for (auto h : s.continuation_cards()) w.handle(h);
    w.u32(static_cast<std::uint32_t>(s.execution().size())); for (const auto& frame : s.execution()) w.frame(frame);
    w.u32(s.combat().next_prompt.value); w.u32(s.combat().selection.id.value);
    w.u8(static_cast<std::uint8_t>(s.combat().selection.effect)); w.u8(static_cast<std::uint8_t>(s.combat().selection.zone));
    w.u32(s.combat().selection.minimum); w.u32(s.combat().selection.maximum); w.u8(s.combat().selection.automatic_at_maximum); w.u32(s.combat().selection.continuation_depth);
    w.u32(static_cast<std::uint32_t>(s.selection_candidates().size()));
    for (auto card : s.selection_candidates()) w.handle(card);
    w.u32(0); // Reserved legacy partial-selection count.
    w.u32(s.combat().counters.started_plays);
    w.u32(s.combat().counters.finished_plays);
    w.u32(s.combat().counters.started_this_turn);
    w.u32(s.combat().counters.attacks_this_turn);
    w.u32(s.combat().counters.zero_energy_attacks_this_turn);
    w.u32(s.combat().counters.non_hand_draws_this_turn);
    w.u32(s.combat().counters.finished_ethereal);
    w.u32(s.combat().next_power_instance);
    w.u64(digest(w.data));
    return {std::move(w.data)};
}
RestoreStatus persistence::decode_combat(RunState& s, std::span<const std::byte> bytes, Limits limits) noexcept {
    try {
        if (bytes.size() < 96 || bytes.size() > 64 * 1024 * 1024) return RestoreStatus::malformed;
        Reader trailer{bytes.last(8)};
        if (trailer.u64() != digest(bytes.first(bytes.size() - 8))) return RestoreStatus::malformed;
        Reader r{bytes.first(bytes.size() - 8)};
        for (char c : std::string_view("STS2CB02")) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u32() != 2 || r.u32() != rules_revision) return RestoreStatus::incompatible;
        for (char c : game_digest) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u64() != content_fingerprint()) return RestoreStatus::incompatible;
        s.data.combat.emplace();
        s.combat().phase = static_cast<CombatPhase>(r.u8());
        s.combat().energy = r.i32(); s.combat().turn = r.i32();
        s.combat().enemy_side = r.boolean(); s.combat().burning_blood = r.boolean(); s.combat().next_creation_order = r.u32();
        s.combat().exhaust_relic_count = r.u8(); for (auto& relic : s.combat().exhaust_relics) relic = static_cast<ExhaustRelic>(r.u8());
        const auto highest_generation = r.u32();
        const auto count = r.u32();
        if (count > limits.cards) return RestoreStatus::resource_limit;
        if (count > (r.bytes.size() - r.at) / 43) return RestoreStatus::malformed;
        std::vector<CardPool::Slot> slots(count);
        for (auto& slot : slots) {
            slot.generation = r.u32(); slot.live = r.boolean(); slot.value.definition = static_cast<CardId>(r.u16());
            slot.value.upgraded = r.boolean(); slot.value.creation_order = r.u32(); slot.value.origin.slot = r.u32(); slot.value.origin.generation = r.u32(); slot.value.last_finished_player_turn = r.i32(); slot.value.removed = r.boolean();
            auto& c = slot.value;
            c.enchantment = {static_cast<EnchantmentId>(r.u8()), r.i32()};
            c.affliction = {static_cast<AfflictionId>(r.u8()), r.i32()};
            c.keyword_mask = r.u8(); c.single_turn_retain = r.boolean(); c.single_turn_sly = r.boolean(); c.exhaust_on_next_play = r.boolean(); c.base_replay_count = r.i32();
        }
        const auto runtime_count = r.count(10, limits.cards);
        for (std::uint32_t i = 0; i < runtime_count; ++i) {
            const auto card = r.handle();
            EnchantmentRuntimeData data;
            switch (r.u8()) {
            case 0: data = EnchantmentUseState{r.boolean()}; break;
            case 1: data = MomentumState{r.i32()}; break;
            default: return RestoreStatus::malformed;
            }
            s.enchantment_runtime().push_back({card, data});
        }
        const auto extra_count = r.count(17, limits.cards);
        for (std::uint32_t i = 0; i < extra_count; ++i) {
            const auto card = r.handle(); auto data = r.card_extra();
            s.card_extra().push_back({card, data});
        }
        const auto free_count = r.u32(); if (free_count > count) return RestoreStatus::malformed;
        std::vector<std::uint32_t> free(free_count); for (auto& index : free) index = r.u32();
        if (!s.cards().restore(std::move(slots), std::move(free))) return RestoreStatus::malformed;
        if (s.cards().data.highest_generation > highest_generation) return RestoreStatus::malformed;
        s.cards().data.highest_generation = highest_generation;
        s.reserve_combat(count);
        std::uint64_t total = 0;
        for (auto& zone : s.zone_views()) {
            const auto n = r.u32(); total += n;
            if (total > count) return RestoreStatus::malformed;
            for (std::uint32_t i = 0; i < n; ++i) zone.push_back(r.handle());
        }
        const auto modifiers = r.u32(); if (modifiers > (r.bytes.size() - r.at) / 15) return RestoreStatus::malformed;
        for (std::uint32_t i = 0; i < modifiers; ++i) { CostModifier modifier; modifier.card = r.handle(); modifier.kind = static_cast<CostModifierKind>(r.u8()); modifier.expiry = static_cast<CostExpiry>(r.u8()); modifier.amount = r.i32(); modifier.reduce_only = r.boolean(); s.cost_modifiers().push_back(modifier); }
        const auto captured_count = r.count(17, std::uint64_t(limits.continuations) * 4096);
        for (std::uint32_t i = 0; i < captured_count; ++i) {
            HookListener listener; listener.owner = {r.u32(), r.u32()}; listener.power_instance = r.u32();
            listener.removed_power_amount = r.i32(); listener.handler = static_cast<HookHandler>(r.u8());
            s.captured_hook_listeners().push_back(listener);
        }
        const auto operand_count = r.count(8, std::uint64_t(limits.continuations) * 16); for (std::uint32_t i = 0; i < operand_count; ++i) s.continuation_cards().push_back(r.handle());
        const auto frames = r.count(20, limits.continuations);
        for (std::uint32_t i = 0; i < frames; ++i) s.execution().push_back(r.frame());
        s.combat().next_prompt.value = r.u32(); s.combat().selection.id.value = r.u32();
        s.combat().selection.effect = static_cast<SelectionEffect>(r.u8()); s.combat().selection.zone = static_cast<Zone>(r.u8());
        s.combat().selection.minimum = r.u32(); s.combat().selection.maximum = r.u32(); s.combat().selection.automatic_at_maximum = r.boolean(); s.combat().selection.continuation_depth = r.u32();
        const auto candidates = r.u32(); if (candidates > count) return RestoreStatus::malformed;
        for (std::uint32_t i = 0; i < candidates; ++i) s.selection_candidates().push_back(r.handle());
        if (r.u32() != 0) return RestoreStatus::malformed;
        s.combat().counters.started_plays = r.u32();
        s.combat().counters.finished_plays = r.u32();
        s.combat().counters.started_this_turn = r.u32();
        s.combat().counters.attacks_this_turn = r.u32();
        s.combat().counters.zero_energy_attacks_this_turn = r.u32();
        s.combat().counters.non_hand_draws_this_turn = r.u32();
        s.combat().counters.finished_ethereal = r.u32();
        s.combat().next_power_instance = r.u32();
        // Run restoration supplies its newly parsed canonical pool here. Borrow
        // only identity resolution; parsed records/overflow remain transactional.
        const RunState& validation = s;
        if (r.at != r.bytes.size() || !valid_combat(validation)) return RestoreStatus::malformed;

        return RestoreStatus::restored;
    } catch (const std::bad_alloc&) { return RestoreStatus::resource_limit; } catch (const std::length_error&) { return RestoreStatus::resource_limit; }
    catch (...) { return RestoreStatus::malformed; }
}
} // namespace sts2::engine
