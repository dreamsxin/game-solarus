/*
 * Copyright (C) 2006-2019 Christopho, Solarus - http://www.solarus-games.org
 *
 * Solarus is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Solarus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "solarus/core/CommandsEffects.h"
#include "solarus/core/Equipment.h"
#include "solarus/entities/Block.h"
#include "solarus/entities/Jumper.h"
#include "solarus/hero/HeroState.h"
#include "solarus/hero/SwordSwingingState.h"
#include "solarus/lua/LuaContext.h"

namespace Solarus {

/**
 * \brief Constructor.
 * \param hero The hero to control with this state.
 * \param state_name A name describing this state.
 */
HeroState::HeroState(Hero& hero, const std::string& state_name):
  HeroState(state_name) {

  set_entity(hero);
}

/**
 * \brief Constructor.
 *
 * Call set_entity() later before starting the state.
 *
 * \param state_name A name describing this state.
 */
HeroState::HeroState(const std::string& state_name):
  State(state_name) {

}

/**
 * \brief Returns the hero of this state.
 * \return The hero.
 */
inline Hero& HeroState::get_entity() {
  return static_cast<Hero&>(Entity::State::get_entity());
}

/**
 * \brief Returns the hero of this state.
 * \return The hero.
 */
inline const Hero& HeroState::get_entity() const {
  return static_cast<const Hero&>(Entity::State::get_entity());
}

/**
 * \brief Returns the hero's sprites.
 * \return the sprites
 */
const HeroSprites& HeroState::get_sprites() const {
  return get_entity().get_hero_sprites();
}

/**
 * \overload Non-const version.
 */
HeroSprites& HeroState::get_sprites() {
  return get_entity().get_hero_sprites();
}

/**
 * \brief Draws this state.
 */
void HeroState::draw_on_map(Camera &camera) {

  get_sprites().draw_on_map(camera);
}

/**
 * \copydoc Entity::State::notify_attack_command_pressed
 */
void HeroState::notify_attack_command_pressed() {
  Hero& hero = get_entity();

  if (!hero.is_suspended()
      && get_commands_effects().get_sword_key_effect() == CommandsEffects::ATTACK_KEY_SWORD
      && hero.can_start_sword()) {

    hero.start_sword();
  }
}

/**
 * \brief Notifies this state that an item command was just pressed.
 * \param slot The slot activated (1 or 2).
 */
void HeroState::notify_item_command_pressed(int slot) {
  Hero& hero = get_entity();

  EquipmentItem* item = get_equipment().get_item_assigned(slot);

  if (item != nullptr && hero.can_start_item(*item)) {
    hero.start_item(*item);
  }
}

/**
 * \copydoc Entity::State::is_block_obstacle
 */
bool HeroState::is_block_obstacle(
    Block& block) {
  return block.is_hero_obstacle(get_entity());
}

/**
 * \copydoc Entity::State::is_raised_block_obstacle
 */
bool HeroState::is_raised_block_obstacle(CrystalBlock& /* raised_block */) {
  return !get_entity().is_on_raised_blocks();
}

/**
 * \copydoc Entity::State::is_jumper_obstacle
 */
bool HeroState::is_jumper_obstacle(
    Jumper& jumper, const Rectangle& candidate_position) {
  const Hero& hero = get_entity();

  if (jumper.overlaps_jumping_region(hero.get_bounding_box(), false)) {
    // The hero already overlaps the active part of the jumper.
    // This is authorized if he arrived from another direction
    // and thus did not activate it.
    // This can be used to leave water pools for example.
    return false;
  }

  if (!jumper.overlaps_jumping_region(candidate_position, false)) {
    // The candidate position is in the inactive region: always accept that.
    return false;
  }

  if (!get_can_take_jumper()) {
    // If jumpers cannot be used in this state, consider their active region
    // as obstacles and their inactive region as traversable.
    // The active region should be an obstacle.
    return true;
  }

  // At this point, we know that the jumper can be activated.

  const bool hero_in_jump_position =
      jumper.is_in_jump_position(hero, hero.get_bounding_box(), false);
  const bool candidate_in_jump_position =
      jumper.is_in_jump_position(hero, candidate_position, false);

  if (candidate_in_jump_position) {
    // Wants to move to a valid jump position: accept.
    return false;
  }

  if (hero_in_jump_position) {
    // If the hero is already correctly placed (ready to jump),
    // make the jumper obstacle so that the player has to move in the
    // jumper's direction during a small delay before jumping.
    // This also prevents the hero to get inside the jumper's active region.
    return true;
  }

  const bool candidate_in_extended_jump_position =
      jumper.is_in_jump_position(hero, candidate_position, true);

  if (candidate_in_extended_jump_position) {
    // Wants to get inside the active region from an end of the jumper:
    // don't accept this.
    return true;
  }

  if (!jumper.is_jump_diagonal() &&
      hero.is_moving_towards(jumper.get_direction() / 2)) {
    // Special case: make the jumper traversable so
    // that the smooth movement can slide to it.
    return false;
  }

  if (!jumper.is_jump_diagonal() &&
      get_name() == "swimming" &&  // TODO use inheritance instead
      hero.is_moving_towards(((jumper.get_direction() / 2) + 2) % 4)
  ) {
    // Other special case: trying to enter the jumper the reverse way while
    // swimming: we accept this to allow the hero to leave water pools.
    // TODO I'm not sure if this behavior is really a good idea.
    // This may change in a future version.
    return false;
  }

  return true;
}

/**
 * @brief Get the commands linked with the hero
 * @return the commands
 */
const Controls& HeroState::get_commands() const {
  return *get_entity().get_controls();
}

/**
 * @brief Gets the commands effects linked with the hero
 * @return the command effects
 */
CommandsEffects& HeroState::get_commands_effects() {
  return get_entity().get_commands_effects();
}

/**
 * @brief Const version
 * @return the commands effects
 */
const CommandsEffects& HeroState::get_commands_effects() const {
  return get_entity().get_commands_effects();
}

}

