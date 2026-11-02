#include "combat_payload.hpp"
#include "sts2/engine/run.hpp"
#include <bit>
#include <new>
#include <stdexcept>

namespace sts2::engine {
namespace {
struct Capacity {};
struct Writer {
    std::vector<std::byte> bytes;
    void u8(std::uint8_t value) { bytes.push_back(std::byte(value)); }
    void u16(std::uint16_t value) { for (unsigned i = 0; i < 2; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i))); }
    void u32(std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i))); }
    void u64(std::uint64_t value) { for (unsigned i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i))); }
    void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void text(std::string_view value) { for (auto c : value) u8(static_cast<std::uint8_t>(c)); }
    void rng(const RngState& value) { for (auto word : value.words) u64(word); u32(value.counter); }
    void ids(std::span<const ContentId> values) { u32(static_cast<std::uint32_t>(values.size())); for (auto value : values) u32(value); }
    void card(const PersistentCard& value) { u32(value.definition); u32(value.instance); u32(value.upgrade); i32(value.floor_added); u8(static_cast<std::uint8_t>(value.enchantment.definition)); i32(value.enchantment.amount); }
    void handle(CardHandle h) { u32(h.slot); u32(h.generation); }
    void card_extra(const CardExtraData& data) {
        u8(static_cast<std::uint8_t>(data.index()));
        const auto& scythe = std::get<TheScytheExtraData>(data);
        i32(scythe.current_damage); i32(scythe.increased_damage);
    }
    void creature(const CreatureState& c, const OverflowStorage& owner) {
        i32(c.hp); i32(c.max_hp); i32(c.block); u64(c.positive_unblocked_hits); u8(static_cast<std::uint8_t>(c.enemy_ai.selected)); u8(c.enemy_member); u8(c.skip_current_enemy_turn);
        u32(static_cast<std::uint32_t>(c.powers.size()));
        for (const auto& p : c.power_instances(owner)) {
            const auto id = p.definition;
            u16(static_cast<std::uint16_t>(id)); u32(p.instance); i32(p.amount); i32(p.amount_on_turn_start);
            u8(p.enemy_owner); u8(p.skip_next_tick); u8(p.has_applier); u8(p.applier_enemy); handle(p.source); u32(p.applier.slot); u32(p.applier.generation); u8(static_cast<std::uint8_t>(p.stacking));
        }
    }
    void edges(const MapEdges& value) {
        for (auto node : value.slots) u32(node);
        for (auto slot : value.free) u8(slot);
        u8(value.used); u8(value.free_count);
    }
    void bag(ConstRelicBag value) {
        for (const auto& tier : value.tiers) ids(tier);
        u32(static_cast<std::uint32_t>(value.order.size())); for (auto tier : value.order) u8(static_cast<std::uint8_t>(tier));
    }
    void map(const MapState& value) {
        u32(value.count); i32(value.height); u32(value.start); u32(value.boss); u32(value.second_boss); edges(value.starts); rng(value.generator_state);
        for (auto node : value.grid) u32(node);
        for (const auto& node : value.nodes) {
            i32(node.col); i32(node.row); u8(static_cast<std::uint8_t>(node.kind)); u8(node.can_modify); u8(node.active); edges(node.children); edges(node.parents);
        }
    }
};
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    std::uint8_t u8() { if (at == bytes.size()) throw std::invalid_argument("Truncated run checkpoint"); return std::to_integer<std::uint8_t>(bytes[at++]); }
    std::uint16_t u16() { const auto low = u8(); return static_cast<std::uint16_t>(low | (std::uint16_t(u8()) << 8)); }
    bool boolean() { const auto value = u8(); if (value > 1) throw std::invalid_argument("Invalid boolean"); return value != 0; }
    std::uint32_t u32() { std::uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(u8()) << (8 * i); return value; }
    std::uint64_t u64() { std::uint64_t value = 0; for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(u8()) << (8 * i); return value; }
    std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
    std::uint16_t id() { const auto value = u32(); if (value > UINT16_MAX) throw std::invalid_argument("Invalid compact ID"); return static_cast<std::uint16_t>(value); }
    std::size_t count(std::size_t width, std::size_t maximum = 65536) {
        const auto n = u32(); if (n > maximum) throw Capacity{};
        if (n > (bytes.size() - at) / width) throw std::invalid_argument("Truncated run table");
        return n;
    }
    RngState rng() { RngState result; for (auto& word : result.words) word = u64(); result.counter = u32(); return result; }
    void random(GameRng& value) { if (!value.restore(rng())) throw std::invalid_argument("Zero random state"); }
    template<std::size_t N> void ids(SequenceView<ContentId, N> values) { const auto n = count(4); values.reserve(n); for (std::size_t i = 0; i < n; ++i) values.push_back(id()); }
    PersistentCard card() { PersistentCard result; result.definition = id(); result.instance = u32(); result.upgrade = u32(); result.floor_added = i32(); result.enchantment = {static_cast<EnchantmentId>(u8()), i32()}; return result; }
    CardExtraData card_extra() {
        switch (u8()) {
        case 0: return TheScytheExtraData{i32(), i32()};
        default: throw std::invalid_argument("Unknown card extra data");
        }
    }
    CardHandle handle() { const auto slot = u32(); return {slot, u32()}; }
    CreatureState creature(OverflowStorage& owner) {
        CreatureState c; c.hp = i32(); c.max_hp = i32(); c.block = i32(); c.positive_unblocked_hits = u64(); c.enemy_ai.selected = static_cast<BeetleMove>(u8()); c.enemy_member = boolean(); c.skip_current_enemy_turn = boolean();
        const auto count = u32();
        if (count > 65536) throw std::invalid_argument("Invalid power count");
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto id = static_cast<PowerId>(u16());
            if (id >= PowerId::count) throw std::invalid_argument("Invalid power slot");
            PowerInstance p; p.definition = id; p.instance = u32(); p.amount = i32(); p.amount_on_turn_start = i32();
            p.enemy_owner = boolean(); p.skip_next_tick = boolean(); p.active = true;
            p.has_applier = boolean(); p.applier_enemy = boolean(); p.source = handle(); p.applier = {u32(), u32()}; p.stacking = static_cast<PowerInstance::Stacking>(u8());
            c.power_instances(owner).push_back(p);
        }
        return c;
    }
    void edges(MapEdges& value) {
        for (auto& node : value.slots) node = id();
        for (auto& slot : value.free) slot = u8();
        value.used = u8(); value.free_count = u8();
    }
    void bag(RelicBag value) {
        for (auto& tier : value.tiers) ids(tier);
        const auto n = count(1, 8); value.order.reserve(n);
        for (std::size_t i = 0; i < n; ++i) value.order.push_back(static_cast<RelicTier>(u8()));
    }
    void map(MapState& value) {
        value.count = id(); value.height = i32(); value.start = id(); value.boss = id(); value.second_boss = id(); edges(value.starts); value.generator_state = rng();
        for (auto& node : value.grid) node = id();
        for (auto& node : value.nodes) {
            node.col = i32(); node.row = i32(); node.kind = static_cast<MapKind>(u8()); node.can_modify = boolean(); node.active = boolean(); edges(node.children); edges(node.parents);
        }
    }
};
std::uint64_t checksum(std::span<const std::byte> bytes) { return game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size()}); }
}
Checkpoint checkpoint(const RunState& run) {
    if (!valid_run(run)) throw std::invalid_argument("Cannot checkpoint invalid run");
    Writer w; w.text("STS2RUN1"); w.u32(19); w.u32(rules_revision); w.text(game_digest); w.text(SetupRules::fingerprint()); w.u64(content_fingerprint());
    w.u32(run.creatures().data.highest_generation);
    w.u32(static_cast<std::uint32_t>(run.creatures().slots().size()));
    for (const auto& slot : run.creatures().slots()) { w.u32(slot.generation); w.u8(slot.live); w.creature(slot.value, run.overflow); }
    w.u32(run.data.player.slot); w.u32(run.data.player.generation);
    w.u32(static_cast<std::uint32_t>(run.seed().size())); w.text(run.seed()); w.u64(run.data.numeric_seed); w.u32(run.data.character); w.u8(run.data.ascension);
    w.i32(run.data.gold); w.i32(run.data.potion_slots); w.i32(run.data.orb_slots);
    for (const auto& rng : run.data.rng.streams) w.rng(rng.state());
    for (const auto& rng : run.data.player_rng.streams) w.rng(rng.state());
    w.bag(run.shared_relics()); w.bag(run.player_relics());
    for (std::size_t act_index = 0; act_index < run.data.acts.size(); ++act_index) {
        const auto act = run.act(act_index);
        w.u32(act.act); w.u32(act.ancient); w.u32(act.boss); w.u32(act.second_boss);
        w.ids(act.events); w.ids(act.normal); w.ids(act.elites); w.ids(act.shared_ancients);
        w.u32(act.events_visited); w.u32(act.normal_visited); w.u32(act.elites_visited); w.u32(act.bosses_visited);
    }
    w.u32(run.deck().pool.data.highest_generation);
    w.u32(static_cast<std::uint32_t>(run.deck().pool.slots().size()));
    for (const auto& slot : run.deck().pool.slots()) { w.u32(slot.generation); w.u8(slot.live); w.card(slot.value); }
    w.u32(static_cast<std::uint32_t>(run.deck().pool.free_slots().size()));
    for (const auto slot : run.deck().pool.free_slots()) w.u32(slot);
    w.u32(static_cast<std::uint32_t>(run.deck().size()));
    for (const auto h : run.deck().handles()) { w.u32(h.slot); w.u32(h.generation); }
    w.u32(static_cast<std::uint32_t>(run.deck_extra().size()));
    for (const auto& entry : run.deck_extra()) { w.u32(entry.card.slot); w.u32(entry.card.generation); w.card_extra(entry.data); }
    w.ids(run.relics()); w.u32(run.data.joss_paper_counter); w.ids(run.potions()); w.u32(std::bit_cast<std::uint32_t>(run.data.card_rarity_offset)); w.u32(std::bit_cast<std::uint32_t>(run.data.potion_reward_odds));
    const auto clock = run.data.clock.config(); w.u64(static_cast<std::uint64_t>(clock.epoch_seconds)); w.u64(static_cast<std::uint64_t>(clock.seconds_per_action)); w.u64(static_cast<std::uint64_t>(run.data.clock.elapsed_seconds()));
    w.u32(run.data.boundary.value); w.u8(static_cast<std::uint8_t>(run.data.phase));
    w.u8(static_cast<std::uint8_t>(run.data.room.index()));
    if (const auto* parent = std::get_if<ParentInteractionState>(&run.data.room)) { w.u8(static_cast<std::uint8_t>(parent->stage)); w.i32(parent->pending_gold); }
    w.u8(run.data.act_index);
    for (const auto& map : run.data.maps) { w.u8(map.has_value()); if (map) w.map(*map); }
    w.u32(run.data.current); w.ids(run.visited()); w.rng(run.data.event_rng.state());
    for (auto option : run.data.neow_options) w.u32(option);
    w.u32(run.data.selected_neow); w.u8(run.data.direct_combat); w.u32(run.data.combat_generation);
    w.u8(run.has_combat());
    if (run.data.combat) { const auto combat = persistence::encode_combat(run); w.u32(static_cast<std::uint32_t>(combat.bytes.size())); w.bytes.insert(w.bytes.end(), combat.bytes.begin(), combat.bytes.end()); }
    w.u8(run.data.rewards.has_value());
    if (run.data.rewards) {
        const auto& rewards = *run.data.rewards; w.i32(rewards.gold); w.u32(rewards.potion); for (const auto& card : rewards.cards) w.card(card);
        w.u8(rewards.gold_taken); w.u8(rewards.potion_taken); w.u8(rewards.card_taken); w.u8(rewards.selecting_card); w.u8(rewards.resolved); w.u8(rewards.skipped); w.u8(rewards.selected_card); w.u8(static_cast<std::uint8_t>(rewards.return_to));
    }
    w.u64(checksum(w.bytes)); return {std::move(w.bytes)};
}
RestoreStatus restore(RunState& destination, std::span<const std::byte> bytes, Scratch& scratch, Limits limits) noexcept {
    try {
        if (bytes.size() < 160 || bytes.size() > 64 * 1024 * 1024) return RestoreStatus::malformed;
        Reader trailer{bytes.last(8)}; if (trailer.u64() != checksum(bytes.first(bytes.size() - 8))) return RestoreStatus::malformed;
        Reader r{bytes.first(bytes.size() - 8)};
        for (char c : std::string_view("STS2RUN1")) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u32() != 19 || r.u32() != rules_revision) return RestoreStatus::incompatible;
        for (const auto identity : {game_digest, SetupRules::fingerprint()}) for (char c : identity)
            if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u64() != content_fingerprint()) return RestoreStatus::incompatible;
        RunState run(destination.overflow.allocator());
        const auto highest = r.u32();
        const auto creature_slot_count = r.count(32, 4096);
        std::vector<CreaturePool::Slot> creature_slots; creature_slots.reserve(creature_slot_count);
        for (std::size_t i = 0; i < creature_slot_count; ++i) {
            CreaturePool::Slot slot; slot.generation = r.u32(); slot.live = r.boolean(); slot.value = r.creature(run.overflow); creature_slots.push_back(slot);
        }
        auto creatures = run.creatures();
        if (!creatures.restore(std::move(creature_slots)) || highest < run.data.creatures.highest_generation) return RestoreStatus::malformed;
        run.data.creatures.highest_generation = highest;
        run.data.player = {r.u32(), r.u32()};
        if (!creatures.get(run.data.player)) return RestoreStatus::malformed;
        const auto seed_size = r.count(1, 4096); for (std::size_t i = 0; i < seed_size; ++i) run.seed().push_back(static_cast<char>(r.u8()));
        run.data.numeric_seed = r.u64(); run.data.character = r.id(); run.data.ascension = r.u8();
        run.data.gold = r.i32(); run.data.potion_slots = r.i32(); run.data.orb_slots = r.i32();
        for (auto& rng : run.data.rng.streams) r.random(rng);
        for (auto& rng : run.data.player_rng.streams) r.random(rng);
        r.bag(run.shared_relics()); r.bag(run.player_relics());
        for (std::size_t act_index = 0; act_index < run.data.acts.size(); ++act_index) {
        auto act = run.act(act_index);
            act.act = r.id(); act.ancient = r.id(); act.boss = r.id(); act.second_boss = r.id();
            r.ids(act.events); r.ids(act.normal); r.ids(act.elites); r.ids(act.shared_ancients);
            act.events_visited = r.u32(); act.normal_visited = r.u32(); act.elites_visited = r.u32(); act.bosses_visited = r.u32();
        }
        const auto highest_generation = r.u32();
        const auto slot_count = r.count(26, limits.cards);
        std::vector<PersistentCardPool::Slot> slots(slot_count);
        for (auto& slot : slots) { slot.generation = r.u32(); slot.live = r.boolean(); slot.value = r.card(); }
        const auto free_count = r.count(4, slot_count);
        std::vector<std::uint32_t> free(free_count);
        for (auto& slot : free) slot = r.u32();
        if (!run.deck().pool.restore(std::move(slots), std::move(free)) || run.deck().pool.data.highest_generation > highest_generation) return RestoreStatus::malformed;
        run.deck().pool.data.highest_generation = highest_generation;
        const auto cards = r.count(8, limits.cards);
        run.deck().handles().reserve(cards);
        for (std::size_t i = 0; i < cards; ++i) run.deck().handles().push_back({r.u32(), r.u32()});
        const auto extra_count = r.count(17, limits.cards);
        for (std::uint32_t i = 0; i < extra_count; ++i) {
            const PersistentCardHandle card{r.u32(), r.u32()}; auto data = r.card_extra();
            run.deck_extra().push_back({card, data});
        }
        r.ids(run.relics()); run.data.joss_paper_counter = r.u32(); r.ids(run.potions()); run.data.card_rarity_offset = std::bit_cast<float>(r.u32()); run.data.potion_reward_odds = std::bit_cast<float>(r.u32());
        const auto epoch = r.u64(), step = r.u64(), elapsed = r.u64();
        if (epoch > RunClock::max_seconds || step > 86400 || elapsed > RunClock::max_seconds ||
            !run.data.clock.restore({static_cast<std::int64_t>(epoch), static_cast<std::int64_t>(step)}, static_cast<std::int64_t>(elapsed))) return RestoreStatus::malformed;
        run.data.boundary.value = r.u32(); run.data.phase = static_cast<RunPhase>(r.u8());
        switch (r.u8()) {
        case 0: break;
        case 1: run.data.room = ParentInteractionState{static_cast<ParentRoomStage>(r.u8()), r.i32()}; break;
        default: return RestoreStatus::malformed;
        }
        run.data.act_index = r.u8();
        for (auto& map : run.data.maps) if (r.boolean()) { map.emplace(); r.map(*map); }
        run.data.current = r.id(); r.ids(run.visited()); r.random(run.data.event_rng);
        for (auto& option : run.data.neow_options) option = r.id();
        run.data.selected_neow = r.id(); run.data.direct_combat = r.boolean(); run.data.combat_generation = r.u32();
        if (r.boolean()) {
            const auto size = r.count(1, 64 * 1024 * 1024);
            const auto status = persistence::decode_combat(run, r.bytes.subspan(r.at, size), limits);
            if (status != RestoreStatus::restored) return status;
            r.at += size;
        }
        if (r.boolean()) {
            run.data.rewards.emplace(); auto& rewards = *run.data.rewards; rewards.gold = r.i32(); rewards.potion = r.id();
            for (auto& card : rewards.cards) card = r.card();
            rewards.gold_taken = r.boolean(); rewards.potion_taken = r.boolean(); rewards.card_taken = r.boolean(); rewards.selecting_card = r.boolean();
            rewards.resolved = r.boolean(); rewards.skipped = r.boolean(); rewards.selected_card = r.u8(); rewards.return_to = static_cast<RewardReturn>(r.u8());
        }
        if (r.at != r.bytes.size() || !valid_run(run)) return RestoreStatus::malformed;
        destination = std::move(run); scratch.steps = 0; return RestoreStatus::restored;
    } catch (const Capacity&) { return RestoreStatus::resource_limit; }
    catch (const std::bad_alloc&) { return RestoreStatus::resource_limit; } catch (const std::length_error&) { return RestoreStatus::resource_limit; }
    catch (...) { return RestoreStatus::malformed; }
}
} // namespace sts2::engine
