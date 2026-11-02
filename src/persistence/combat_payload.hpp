#pragma once
#include "sts2/persistence/checkpoint.hpp"
namespace sts2::engine::persistence {
Checkpoint encode_combat(const RunState&);
RestoreStatus decode_combat(RunState&, std::span<const std::byte>, Limits) noexcept;
}
