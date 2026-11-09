#include "encounters.hpp"

namespace sts2::engine::gameplay {
bool supported_encounter(std::string_view id) noexcept {
    return id == "ENCOUNTER.SHRINKER_BEETLE_WEAK" || id == "ENCOUNTER.FUZZY_WURM_CRAWLER_WEAK" || id == "ENCOUNTER.OVERGROWTH_CRAWLERS";
}
EncounterComposition generate_encounter(std::string_view id, const RunState&) noexcept {
    if (id == "ENCOUNTER.SHRINKER_BEETLE_WEAK") return {{{EnemyId::shrinker_beetle}}, 1};
    if (id == "ENCOUNTER.FUZZY_WURM_CRAWLER_WEAK") return {{{EnemyId::fuzzy_wurm_crawler}}, 1};
    if (id == "ENCOUNTER.OVERGROWTH_CRAWLERS") return {{{EnemyId::shrinker_beetle, EnemyId::fuzzy_wurm_crawler}}, 2};
    return {};
}
}
