#pragma once

#include "../BattleEmulator.h"
#include "freecam_fast_runtime.hpp"

namespace dq9::freecam::bindings {

// Manual BattleEmulator common-ID -> DQ9 runtime-action bindings.
// The DQ9 value is the action ID consumed by the presentation/free-camera
// pipeline. Keep command-layer IDs in the reverse-engineering/reference data;
// the fast runtime does not need them.
using AttackAlly = fast::FreeCamera<1, BattleEmulator::ATTACK_ALLY>;
using DragonSlash = fast::FreeCamera<63, BattleEmulator::DRAGON_SLASH>;
using MiracleSlash = fast::FreeCamera<65, BattleEmulator::MIRACLE_SLASH>;
using ThunderThrust = fast::FreeCamera<72, BattleEmulator::THUNDER_THRUST>;
using MedicinalHerbs = fast::FreeCamera<255, BattleEmulator::MEDICINAL_HERBS>;

static_assert(AttackAlly::commonActionId == BattleEmulator::ATTACK_ALLY);
static_assert(AttackAlly::dq9ActionId == 1);
static_assert(MedicinalHerbs::commonActionId == BattleEmulator::MEDICINAL_HERBS);
static_assert(MedicinalHerbs::dq9ActionId == 255);

} // namespace dq9::freecam::bindings
