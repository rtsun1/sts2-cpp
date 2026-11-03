#pragma once
#include <iterator>
#include "sts2/content/catalog.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/state/map.hpp"
#include "sts2/state/rng.hpp"
#include "sts2/state/cards.hpp"
#include "sts2/state/combat.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <variant>
#include <stdexcept>

namespace sts2::engine {
struct RelicBagData {
    std::array<InlineSequence<ContentId, 64>, 8> tiers;
    InlineSequence<RelicTier, 8> order;
};
template<bool Const> struct BasicRelicBagView {
    using Data = std::conditional_t<Const, const RelicBagData, RelicBagData>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Data& data;
    Owner& overflow;
    std::array<SequenceView<ContentId, 64, Const>, 8> tiers;
    SequenceView<RelicTier, 8, Const> order;
    BasicRelicBagView(Data& r, Owner& o) noexcept : data(r), overflow(o),
        tiers{{{r.tiers[0], o}, {r.tiers[1], o}, {r.tiers[2], o}, {r.tiers[3], o}, {r.tiers[4], o}, {r.tiers[5], o}, {r.tiers[6], o}, {r.tiers[7], o}}}, order(r.order, o) {}
    template<bool Other> bool operator==(const BasicRelicBagView<Other>& other) const { return tiers == other.tiers && order == other.order; }
};
using RelicBag = BasicRelicBagView<false>;
using ConstRelicBag = BasicRelicBagView<true>;
struct ActRoomsData {
    ContentId act = no_content, ancient = no_content, boss = no_content, second_boss = no_content;
    InlineSequence<ContentId, 32> events, normal, elites, shared_ancients;
    std::uint32_t events_visited = 0, normal_visited = 0, elites_visited = 0, bosses_visited = 0;
};
template<bool Const> struct BasicActRoomsView {
    template<class T> using Ref = std::conditional_t<Const, const T&, T&>;
    using Data = std::conditional_t<Const, const ActRoomsData, ActRoomsData>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Data& data;
    Owner& overflow;
    Ref<ContentId> act = data.act;
    Ref<ContentId> ancient = data.ancient;
    Ref<ContentId> boss = data.boss;
    Ref<ContentId> second_boss = data.second_boss;
    SequenceView<ContentId, 32, Const> events{data.events, overflow};
    SequenceView<ContentId, 32, Const> normal{data.normal, overflow};
    SequenceView<ContentId, 32, Const> elites{data.elites, overflow};
    SequenceView<ContentId, 32, Const> shared_ancients{data.shared_ancients, overflow};
    Ref<std::uint32_t> events_visited = data.events_visited;
    Ref<std::uint32_t> normal_visited = data.normal_visited;
    Ref<std::uint32_t> elites_visited = data.elites_visited;
    Ref<std::uint32_t> bosses_visited = data.bosses_visited;
    BasicActRoomsView(Data& r, Owner& o) noexcept : data(r), overflow(o) {}
    template<bool Other> bool operator==(const BasicActRoomsView<Other>& other) const { return act == other.act && ancient == other.ancient && boss == other.boss && second_boss == other.second_boss && events == other.events && normal == other.normal && elites == other.elites && shared_ancients == other.shared_ancients && events_visited == other.events_visited && normal_visited == other.normal_visited && elites_visited == other.elites_visited && bosses_visited == other.bosses_visited; }
};
enum class RunPhase : std::uint8_t { uninitialized, neow, neow_done, map, combat, combat_done, rewards, room, complete, invalid };
enum class RewardReturn : std::uint8_t { map, parent_room };
enum class ParentRoomStage : std::uint8_t { child_combat, child_rewards, choosing };
struct ParentInteractionState {
    ParentRoomStage stage = ParentRoomStage::child_combat;
    std::int32_t pending_gold = 0;
    friend bool operator==(const ParentInteractionState&, const ParentInteractionState&) = default;
};
// Stateless rooms need no payload. An enclosing interaction can remain here
// while its child combat and generated offers live in their own records.
using RoomPayload = std::variant<std::monostate, ParentInteractionState>;
struct RewardOffers {
    std::int32_t gold = 0;
    ContentId potion = no_content;
    std::array<PersistentCard, 3> cards{};
    bool gold_taken : 1 = false;
    bool potion_taken : 1 = false;
    bool card_taken : 1 = false;
    bool selecting_card : 1 = false;
    bool resolved : 1 = false;
    bool skipped : 1 = false;
    std::uint8_t selected_card = UINT8_MAX;
    RewardReturn return_to = RewardReturn::map;
    friend bool operator==(const RewardOffers&, const RewardOffers&) = default;
};
using CombatRewards = RewardOffers;
using CreaturePool = PoolData<CreatureState, CreatureTag, 8, PoolAllocation::specified_slot>;
template<bool Const = false>
using CreaturePoolView = PoolView<CreatureState, CreatureTag, 8, Const, PoolAllocation::specified_slot>;
// A borrowed traversal of authoritative pool membership; no roster is stored.
struct EnemyPoolRange {
    CreaturePoolView<true> pool;
    struct Iterator {
        using value_type = CreatureHandle;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        const CreaturePool* data = nullptr;
        const OverflowStorage* owner = nullptr;
        std::size_t index = 0;
        void seek() noexcept {
            const auto slots = data->slots.values(*owner);
            while (index < slots.size() && (!slots[index].live || !slots[index].value.enemy_member)) ++index;
        }
        CreatureHandle operator*() const noexcept { return {static_cast<std::uint32_t>(index), data->slots.values(*owner)[index].generation}; }
        Iterator& operator++() noexcept { ++index; seek(); return *this; }
        Iterator operator++(int) noexcept { auto previous = *this; ++*this; return previous; }
        bool operator==(const Iterator& other) const noexcept { return data == other.data && index == other.index; }
    };
    Iterator begin() const noexcept { Iterator it{&pool.data, &pool.owner, 0}; it.seek(); return it; }
    Iterator end() const noexcept { return {&pool.data, &pool.owner, pool.slots().size()}; }
    bool empty() const noexcept { return begin() == end(); }
    std::size_t size() const noexcept { std::size_t count = 0; for (auto handle : *this) { (void)handle; ++count; } return count; }
    CreatureHandle front() const noexcept { return empty() ? CreatureHandle{} : *begin(); }
    CreatureHandle operator[](std::size_t index) const noexcept { for (auto handle : *this) if (index-- == 0) return handle; return {}; }
};
using PersistentCardPool = PoolData<PersistentCard, PersistentCardTag, 32>;
template<bool Const> struct BasicDeckView {
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    using Order = std::conditional_t<Const, const InlineSequence<PersistentCardHandle, 32>, InlineSequence<PersistentCardHandle, 32>>;
    PoolView<PersistentCard, PersistentCardTag, 32, Const> pool;
    Order& order;
    Owner& owner;
    SequenceView<PersistentCardExtra, 10, Const> extra;
    auto handles() const noexcept { return SequenceView<PersistentCardHandle, 32, Const>(order, owner); }
    std::size_t size() const noexcept { return handles().size(); }
    bool empty() const noexcept { return handles().empty(); }
    auto get(PersistentCardHandle h) const noexcept { return pool.get(h); }
    decltype(auto) operator[](std::size_t i) const noexcept { return *get(handles()[i]); }
    decltype(auto) front() const noexcept { return (*this)[0]; }
    decltype(auto) back() const noexcept { return (*this)[size() - 1]; }
    struct Iterator {
        BasicDeckView view;
        std::size_t index;
        using difference_type = std::ptrdiff_t;
        using value_type = PersistentCard;
        using iterator_category = std::forward_iterator_tag;
        decltype(auto) operator*() const { return view[index]; }
        Iterator& operator++() { ++index; return *this; }
        bool operator==(const Iterator& other) const { return index == other.index; }
    };
    Iterator begin() const noexcept { return {*this, 0}; }
    Iterator end() const noexcept { return {*this, size()}; }
    void reserve(std::size_t n) requires (!Const) { handles().reserve(n); pool.reserve(n); }
    void push_back(PersistentCard card) requires (!Const) {
        reserve(size() + 1);
        const auto handle = pool.create(card, UINT32_MAX);
        if (!handle.present()) throw std::length_error("persistent card pool exhausted");
        handles().push_back(handle);
    }
    bool erase(PersistentCardHandle handle) noexcept requires (!Const) {
        auto order_view = handles();
        const auto at = std::find(order_view.begin(), order_view.end(), handle);
        if (at == order_view.end() || !pool.erase(handle)) return false;
        order_view.erase(at);
        extra.erase(std::remove_if(extra.begin(), extra.end(), [&](const auto& entry) { return entry.card == handle; }), extra.end());
        return true;
    }
    void clear() noexcept requires (!Const) { while (!empty()) (void)erase(handles().back()); }
    template<bool Other> bool operator==(const BasicDeckView<Other>& other) const {
        return pool == other.pool && handles() == other.handles() && extra == other.extra;
    }
};
struct RunData {
    InlineSequence<char, 32> seed {};
    std::uint64_t numeric_seed = 0;
    ContentId character = no_content;
    std::uint8_t ascension = 0;
    CreaturePool creatures {};
    CreatureHandle player {};
    std::int32_t gold = 0;
    std::int32_t potion_slots = 3;
    std::int32_t orb_slots = 0;
    RngBank rng {};
    PlayerRngBank player_rng {};
    RelicBagData shared_relics {};
    RelicBagData player_relics {};
    std::array<ActRoomsData, 3> acts {};
    PersistentCardPool persistent_cards {};
    InlineSequence<PersistentCardExtra, 10> card_extra {};
    InlineSequence<PersistentCardHandle, 32> deck {};
    InlineSequence<ContentId, 16> relics {};
    std::uint32_t joss_paper_counter = 0;
    InlineSequence<ContentId, 4> potions {};
    float card_rarity_offset = -0.05f;
    float potion_reward_odds = 0.4f;
    std::optional<CombatData> combat;
    bool direct_combat = false;
    ContentId combat_encounter = no_content; // Retained through reward/map return.
    std::uint32_t combat_generation = 0;
    std::optional<CombatRewards> rewards;
    RunClock clock;
    BoundaryId boundary{};
    RunPhase phase = RunPhase::uninitialized;
    RoomPayload room {};
    std::uint8_t act_index = 0;
    std::array<std::optional<MapState>, 3> maps;
    MapNodeId current = no_node;
    InlineSequence<MapNodeId, 64> visited;
    GameRng event_rng;
    std::array<ContentId, 3> neow_options{};
    ContentId selected_neow = no_content;
    template<class Visit> void for_each_sequence(const OverflowStorage& owner, Visit visit) const noexcept {
        for_each_sequence(visit);
        if (creatures.slots.valid(owner)) for (const auto& slot : creatures.slots.values(owner)) visit(slot.value.powers);
    }
    template<class Visit> void for_each_sequence(Visit visit) const noexcept {
        visit(seed); visit(creatures.slots);
        for (const auto* bag : {&shared_relics, &player_relics}) {
            for (const auto& tier : bag->tiers) visit(tier);
            visit(bag->order);
        }
        for (const auto& act : acts) { visit(act.events); visit(act.normal); visit(act.elites); visit(act.shared_ancients); }
        visit(persistent_cards.slots); visit(persistent_cards.free); visit(card_extra); visit(deck); visit(relics); visit(potions);
        visit(visited);
    }
};
static_assert(std::is_trivially_copyable_v<RunData>);
inline bool valid_creature_storage(const RunData& data, const OverflowStorage& owner) noexcept {
    if (!valid_data_storage(data, owner)) return false;
    const CreaturePoolView<true> pool{data.creatures, owner};
    if (!pool.valid() || !pool.get(data.player)) return false;
    for (const auto& slot : pool.slots()) if (!slot.value.powers.valid(owner)) return false;
    const EnemyPoolRange enemies{pool};
    if (data.combat.has_value() != !enemies.empty() || data.player.slot != 0 || pool.get(data.player)->enemy_member) return false;
    for (const auto& slot : pool.slots()) {
        if (slot.value.enemy_type >= EnemyId::count || static_cast<unsigned>(slot.value.enemy_ai.selected) > static_cast<unsigned>(EnemyMove::third)) return false;
    }
    return true;
}
struct RunState;
CloneStatus clone_into(RunState&, const RunState&) noexcept;
struct RunState {
    RunData data;
    OverflowStorage overflow, combat_overflow;
    explicit RunState(StorageAllocator allocator = {}) : overflow(allocator), combat_overflow(allocator) {
        data.player = CreaturePoolView<>{data.creatures, overflow}.create_at(0, {}, UINT32_MAX);
    }
    RunState(const RunState& other) : RunState(other.overflow.allocator()) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; }
    RunState& operator=(const RunState& other) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; return *this; }
    RunState(RunState&& other) noexcept : overflow(std::move(other.overflow)), combat_overflow(std::move(other.combat_overflow)) {
        std::memcpy(&data, &other.data, sizeof(data));
    }
    RunState& operator=(RunState&& other) noexcept {
        if (this != &other) {
            overflow = std::move(other.overflow); combat_overflow = std::move(other.combat_overflow);
            std::memcpy(&data, &other.data, sizeof(data));
        }
        return *this;
    }
    void reset() noexcept {
        data = RunData{}; overflow.reset(); combat_overflow.reset();
        data.player = CreaturePoolView<>{data.creatures, overflow}.create_at(0, {}, UINT32_MAX);
    }
    bool has_combat() const noexcept { return data.combat.has_value(); }
    CombatData& combat() noexcept { return *data.combat; }
    const CombatData& combat() const noexcept { return *data.combat; }
    auto creatures() noexcept { return CreaturePoolView<>{data.creatures, overflow}; }
    auto creatures() const noexcept { return CreaturePoolView<true>{data.creatures, overflow}; }
    CreatureState* creature(CreatureHandle id) noexcept { return creatures().get(id); }
    const CreatureState* creature(CreatureHandle id) const noexcept { return creatures().get(id); }
    CreatureState& player() noexcept { return *creature(data.player); }
    const CreatureState& player() const noexcept { return *creature(data.player); }
    auto enemies() const noexcept { return EnemyPoolRange{creatures()}; }
    bool has_living_enemies() const noexcept {
        for (auto handle : enemies()) if (creature(handle)->hp > 0) return true;
        return false;
    }
    bool is_enemy(CreatureHandle handle) const noexcept {
        const auto* value = creature(handle); return value && value->enemy_member;
    }
    void set_enemy_membership(CreatureHandle handle, bool member) {
        auto* value = creature(handle);
        if (!value || handle == data.player) throw std::invalid_argument("Invalid enemy identity");
        value->enemy_member = member;
    }
    CreatureHandle create_enemy_at(std::uint32_t encounter_slot, CreatureState value, std::size_t limit = 4096) {
        if (encounter_slot == UINT32_MAX) return {};
        value.enemy_member = true;
        value.skip_current_enemy_turn = has_combat() && combat().enemy_side;
        return creatures().create_at(encounter_slot + 1, value, limit);
    }
    CreatureHandle creature_handle(const CreatureState& value) const noexcept {
        const auto slots = creatures().slots();
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].live && &slots[i].value == &value) return {static_cast<std::uint32_t>(i), slots[i].generation};
        return {};
    }
    CreatureHandle enemy_handle() const noexcept { return enemies().front(); }
    CreatureState& enemy() noexcept { return *creature(enemy_handle()); }
    const CreatureState& enemy() const noexcept { return *creature(enemy_handle()); }
    auto seed() noexcept { return TextView<32, false>{data.seed, overflow}; }
    auto seed() const noexcept { return TextView<32, true>{data.seed, overflow}; }
    auto cards() noexcept { return PoolView<CombatCard, CombatCardTag, 32>{combat().cards, combat_overflow}; }
    auto cards() const noexcept { return PoolView<CombatCard, CombatCardTag, 32, true>{combat().cards, combat_overflow}; }
    auto zone(Zone id) noexcept { return SequenceView(combat().zones[static_cast<std::size_t>(id)], combat_overflow); }
    auto zone(Zone id) const noexcept { return SequenceView(combat().zones[static_cast<std::size_t>(id)], combat_overflow); }
    auto zone_views() noexcept { return std::array{zone(Zone::hand), zone(Zone::draw), zone(Zone::discard), zone(Zone::exhaust), zone(Zone::play)}; }
    auto zone_views() const noexcept { return std::array{zone(Zone::hand), zone(Zone::draw), zone(Zone::discard), zone(Zone::exhaust), zone(Zone::play)}; }
    void reserve_combat(std::size_t count) { cards().reserve(count); for (auto zone : zone_views()) zone.reserve(count); }
    void reset_combat() noexcept {
        data.combat.reset(); combat_overflow.reset();
        for (auto enemy : enemies()) { if (auto* c = creature(enemy)) c->clear(); (void)creatures().erase(enemy); }
    }
    void emplace_combat() {
        reset_combat();
        data.combat.emplace();
        const auto enemy = create_enemy_at(0, {}, UINT32_MAX);
        if (!enemy.present()) throw std::length_error("Creature identity exhausted");
    }
    bool valid_storage() const noexcept { return valid_creature_storage(data, overflow); }
    bool valid_combat_storage() const noexcept { return has_combat() && valid_storage() && valid_data_storage(combat(), combat_overflow); }
    std::size_t retained_bytes() const noexcept { return sizeof(*this) + overflow.retained_bytes() + combat_overflow.retained_bytes(); }
    auto card_extra() noexcept { return SequenceView(combat().card_extra, combat_overflow); }
    auto card_extra() const noexcept { return SequenceView(combat().card_extra, combat_overflow); }
    auto enchantment_runtime() noexcept { return SequenceView(combat().enchantment_runtime, combat_overflow); }
    auto enchantment_runtime() const noexcept { return SequenceView(combat().enchantment_runtime, combat_overflow); }
    auto cost_modifiers() noexcept { return SequenceView(combat().cost_modifiers, combat_overflow); }
    auto cost_modifiers() const noexcept { return SequenceView(combat().cost_modifiers, combat_overflow); }
    auto execution() noexcept { return SequenceView(combat().execution, combat_overflow); }
    auto execution() const noexcept { return SequenceView(combat().execution, combat_overflow); }
    auto captured_hook_listeners() noexcept { return SequenceView(combat().captured_hook_listeners, combat_overflow); }
    auto captured_hook_listeners() const noexcept { return SequenceView(combat().captured_hook_listeners, combat_overflow); }
    auto continuation_cards() noexcept { return SequenceView(combat().continuation_cards, combat_overflow); }
    auto continuation_cards() const noexcept { return SequenceView(combat().continuation_cards, combat_overflow); }
    auto selection_candidates() noexcept { return SequenceView(combat().selection_candidates, combat_overflow); }
    auto selection_candidates() const noexcept { return SequenceView(combat().selection_candidates, combat_overflow); }
    auto deck_extra() noexcept { return SequenceView(data.card_extra, overflow); }
    auto deck_extra() const noexcept { return SequenceView(data.card_extra, overflow); }
    auto relics() noexcept { return SequenceView(data.relics, overflow); }
    auto relics() const noexcept { return SequenceView(data.relics, overflow); }
    auto potions() noexcept { return SequenceView(data.potions, overflow); }
    auto potions() const noexcept { return SequenceView(data.potions, overflow); }
    auto visited() noexcept { return SequenceView(data.visited, overflow); }
    auto visited() const noexcept { return SequenceView(data.visited, overflow); }
    auto deck() noexcept { return BasicDeckView<false>{{data.persistent_cards, overflow}, data.deck, overflow, deck_extra()}; }
    auto deck() const noexcept { return BasicDeckView<true>{{data.persistent_cards, overflow}, data.deck, overflow, deck_extra()}; }
    auto shared_relics() noexcept { return RelicBag{data.shared_relics, overflow}; }
    auto shared_relics() const noexcept { return ConstRelicBag{data.shared_relics, overflow}; }
    auto player_relics() noexcept { return RelicBag{data.player_relics, overflow}; }
    auto player_relics() const noexcept { return ConstRelicBag{data.player_relics, overflow}; }
    auto act(std::size_t index) noexcept { return BasicActRoomsView<false>{data.acts[index], overflow}; }
    auto act(std::size_t index) const noexcept { return BasicActRoomsView<true>{data.acts[index], overflow}; }
    bool combat_equals(const RunState& other) const;
    friend CloneStatus clone_into(RunState& destination, const RunState& source) noexcept try {
        if (&destination == &source) return CloneStatus::cloned;
        // Prepare all ownership before publishing any part of the complete run.
        OverflowStorage run(destination.overflow.allocator()), combat(destination.combat_overflow.allocator());
        run.clone_from(source.overflow); combat.clone_from(source.combat_overflow);
        destination.overflow.swap(run); destination.combat_overflow.swap(combat);
        std::memcpy(&destination.data, &source.data, sizeof(RunData));
        return CloneStatus::cloned;
    } catch (const std::bad_alloc&) { return CloneStatus::resource_limit; }
      catch (const std::length_error&) { return CloneStatus::resource_limit; }
    friend bool operator==(const RunState& a, const RunState& b);
};
inline bool RunState::combat_equals(const RunState& other) const {
    if (has_combat() != other.has_combat()) return false;
    if (!has_combat()) return true;
    return combat().rules == other.combat().rules
        && combat().phase == other.combat().phase
        && combat().energy == other.combat().energy
        && combat().turn == other.combat().turn
        && combat().enemy_side == other.combat().enemy_side
        && combat().burning_blood == other.combat().burning_blood
        && combat().counters == other.combat().counters
        && combat().exhaust_relics == other.combat().exhaust_relics
        && combat().exhaust_relic_count == other.combat().exhaust_relic_count
        && combat().selection == other.combat().selection
        && combat().next_prompt == other.combat().next_prompt
        && combat().next_power_instance == other.combat().next_power_instance
        && combat().next_creation_order == other.combat().next_creation_order
        && cards() == other.cards()
        && zone_views() == other.zone_views()
        && card_extra() == other.card_extra()
        && enchantment_runtime() == other.enchantment_runtime()
        && cost_modifiers() == other.cost_modifiers()
        && execution() == other.execution()
        && captured_hook_listeners() == other.captured_hook_listeners()
        && continuation_cards() == other.continuation_cards()
        && selection_candidates() == other.selection_candidates();
}
inline bool operator==(const RunState& a, const RunState& b) {
    for (std::size_t i = 0; i < a.data.acts.size(); ++i) if (!(a.act(i) == b.act(i))) return false;
    return a.seed() == b.seed() && a.data.numeric_seed == b.data.numeric_seed &&
        a.data.character == b.data.character && a.data.ascension == b.data.ascension &&
        a.data.player == b.data.player && a.creatures() == b.creatures() &&
        a.data.gold == b.data.gold &&
        a.data.potion_slots == b.data.potion_slots && a.data.orb_slots == b.data.orb_slots &&
        a.data.rng == b.data.rng && a.data.player_rng == b.data.player_rng &&
        a.shared_relics() == b.shared_relics() && a.player_relics() == b.player_relics() &&
        a.deck() == b.deck() && a.deck_extra() == b.deck_extra() && a.relics() == b.relics() && a.data.joss_paper_counter == b.data.joss_paper_counter &&
        a.potions() == b.potions() && a.data.card_rarity_offset == b.data.card_rarity_offset &&
        a.data.potion_reward_odds == b.data.potion_reward_odds && a.combat_equals(b) &&
        a.data.direct_combat == b.data.direct_combat && a.data.combat_encounter == b.data.combat_encounter && a.data.combat_generation == b.data.combat_generation &&
        a.data.rewards == b.data.rewards && a.data.clock == b.data.clock && a.data.boundary == b.data.boundary &&
        a.data.phase == b.data.phase && a.data.room == b.data.room && a.data.act_index == b.data.act_index && a.data.maps == b.data.maps &&
        a.data.current == b.data.current && a.visited() == b.visited() && a.data.event_rng == b.data.event_rng &&
        a.data.neow_options == b.data.neow_options && a.data.selected_neow == b.data.selected_neow;
}
} // namespace sts2::engine
