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
#include "solarus/core/CurrentQuest.h"
#include "solarus/graphics/Surface.h"
#include "solarus/lua/ExportableToLuaPtr.h"
#include "solarus/lua/LuaContext.h"
#include "solarus/lua/LuaTools.h"
#include <list>
#include <lua.hpp>

namespace Solarus {

/**
 * Name of the Lua table representing the menu module.
 */
const std::string LuaContext::menu_module_name = "sol.menu";

/**
 * \brief Initializes the menu features provided to Lua.
 */
void LuaContext::register_menu_module() {

  // Functions of sol.menu.
  std::vector<luaL_Reg> functions = {
      { "start", menu_api_start },
      { "stop", menu_api_stop },
      { "stop_all", menu_api_stop_all },
      { "is_started", menu_api_is_started },
  };
  if (CurrentQuest::is_format_at_least({ 1, 6 })) {
    functions.insert(functions.end(), {
        { "bring_to_front", menu_api_bring_to_front },
        { "bring_to_back", menu_api_bring_to_back },
    });
  }

  register_functions(menu_module_name, functions);
}

/**
 * \brief Registers a menu into a context (table or a userdata).
 *
 * This function can be called safely even while iterating on the menus list.
 *
 * \param menu_ref Lua ref of the menu to add.
 * \param context_index Index of the table or userdata in the stack.
 * \param on_top \c true to place this menu on top of existing one in the
 * same context, \c false to place it behind.
 */
void LuaContext::add_menu(
    const ScopedLuaRef& menu_ref,
    int context_index,
    bool on_top
) {

  ScopedLuaRef context = LuaTools::create_ref(current_l,context_index);
  SOLARUS_ASSERT(!context.is_empty(), "creating context with empty context");

  if(std::count_if(menus.begin(), menus.end(),[&](const LuaMenuData& menu){
                   return menu.ref == menu_ref;
  })){
    LuaTools::error(current_l, "Cannot start an already started menu");
  }

  run_on_main([this, on_top, context, menu_ref](lua_State*) {
    if (on_top) {
      menus.emplace_back(menu_ref, context);
    }
    else {
      menus.emplace_front(menu_ref, context);
    }
    menu_on_started(menu_ref);
  });
}

/**
 * \brief Unregisters all menus associated to a context.
 *
 * This function can be called safely even while iterating on the menus list.
 *
 * \param context_index Index of a table or userdata containing menus.
 */
void LuaContext::remove_menus(int context_index) {
  // Some menu:on_finished() callbacks may create menus themselves,
  // and we don't want those new menus to get removed.
  for (LuaMenuData& menu: menus) {
    menu.recently_added = false;
  }

  for (LuaMenuData& menu: menus) {
    ScopedLuaRef menu_ref = menu.ref;
    if (menu.context.equals(current_l,context_index) && !menu.recently_added) {
      menu.ref.clear();
      menu.context.clear();
      menu_on_finished(menu_ref);
    }
  }
}

/**
 * \brief Unregisters all existing menus.
 *
 * This function can be called safely even while iterating on the menus list.
 */
void LuaContext::remove_menus() {

  // Some menu:on_finished() callbacks may create menus themselves,
  // and we don't want those new menus to get removed.
  for (LuaMenuData& menu: menus) {
    menu.recently_added = false;
  }

  for (LuaMenuData& menu: menus) {

    if (!menu.recently_added) {
      ScopedLuaRef menu_ref = menu.ref;
      if (!menu_ref.is_empty()) {
        menu.ref.clear();
        menu.context.clear();
        menu_on_finished(menu_ref);
      }
    }
  }
}

/**
 * \brief Destroys immediately all existing menus.
 */
void LuaContext::destroy_menus() {

  menus.clear();
}

/**
 * \brief Checks all menus and removes the ones that have to be removed.
 *
 * Note that the on_update() is called by the context of each menu, not
 * by this function.
 */
void LuaContext::update_menus() {

  // Destroy the ones that should be removed.
  for (auto it = menus.begin();
      it != menus.end();
      // No ++it (elements may be removed while traversing).
  ) {
    it->recently_added = false;
    if (it->ref.is_empty()) {
      // Empty ref on a menu means that we should remove.
      // In this case, context must also be nullptr.
      SOLARUS_ASSERT(it->context.is_empty(), "Menu with context and no ref");
      it = menus.erase(it);
    }
    else {
      ++it;
    }
  }
}

/**
 * \brief Returns whether a value is a menu.
 * \param l A Lua context.
 * \param index An index in the stack.
 * \return \c true if the value at this index is a menu.
 */
bool LuaContext::is_menu(lua_State* l, int index) {

  index = LuaTools::get_positive_index(l, index);

  if (!lua_istable(l, index)) {
    return false;
  }

  LuaContext& lua_context = get();
  for (LuaMenuData& menu: lua_context.menus) {
    if (menu.ref.is_empty()) {
      continue;
    }

    if(menu.ref.equals(l,index)) {
      return true;
    }
  }
  return false;
}

/**
 * \brief Implementation of sol.menu.start().
 * \param l The Lua context that is calling this function.
 * \return Number of values to return to Lua.
 */
int LuaContext::menu_api_start(lua_State *l) {

  return state_boundary_handle(l, [&] {
    // Parameters: context table.
    if (lua_type(l, 1) != LUA_TTABLE
        && lua_type(l, 1) != LUA_TUSERDATA) {
      LuaTools::type_error(l, 1, "table or userdata");
    }
    LuaTools::check_type(l, 2, LUA_TTABLE);
    bool on_top = LuaTools::opt_boolean(l, 3, true);
    lua_settop(l, 2);

    LuaContext& lua_context = get();
    const ScopedLuaRef& menu_ref = lua_context.create_ref();
    lua_context.add_menu(menu_ref, 1, on_top);

    return 0;
  });
}

/**
 * \brief Implementation of sol.menu.stop().
 * \param l the Lua context that is calling this function
 * \return number of values to return to Lua
 */
int LuaContext::menu_api_stop(lua_State* l) {

  return state_boundary_handle(l, [&] {
    //LuaContext& lua_context = get();

    LuaTools::check_type(l, 1, LUA_TTABLE);
    ScopedLuaRef ref = LuaTools::create_ref(l,1);
    run_on_main([ref](lua_State*){
      LuaContext& lua_context = get();
      std::list<LuaMenuData>& menus = lua_context.menus;
      for (LuaMenuData& menu: menus) {
        if (menu.ref == ref) {
          ScopedLuaRef menu_ref = menu.ref;  // Don't erase it immediately since we may be iterating over menus.
          menu.ref.clear();
          menu.context.clear();
          lua_context.menu_on_finished(menu_ref);
          break;
        }
      }
    });
    return 0;
  });
}

/**
 * \brief Implementation of sol.menu.stop_all().
 * \param l the Lua context that is calling this function
 * \return number of values to return to Lua
 */
int LuaContext::menu_api_stop_all(lua_State* l) {

  return state_boundary_handle(l, [&] {
    if (lua_type(l, 1) != LUA_TTABLE
        && lua_type(l, 1) != LUA_TUSERDATA) {
      LuaTools::type_error(l, 1, "table, game or map");
    }

    get().remove_menus(1);

    return 0;
  });
}

/**
 * \brief Implementation of sol.menu.is_started().
 * \param l the Lua context that is calling this function
 * \return number of values to return to Lua
 */
int LuaContext::menu_api_is_started(lua_State* l) {

  return state_boundary_handle(l, [&] {
    LuaContext& lua_context = get();

    LuaTools::check_type(l, 1, LUA_TTABLE);

    bool found = false;
    std::list<LuaMenuData>& menus = lua_context.menus;
    for (LuaMenuData& menu: menus) {
      push_ref(l, menu.ref);
      found = lua_equal(l, 1, -1);
      lua_pop(l, 1);

      if (found) {
        break;
      }
    }

    lua_pushboolean(l, found);

    return 1;
  });
}

/**
 * \brief Implementation of sol.menu.bring_to_front().
 * \param l The Lua context that is calling this function.
 * \return Number of values to return to Lua.
 */
int LuaContext::menu_api_bring_to_front(lua_State* l) {

  return state_boundary_handle(l, [&] {
    LuaContext& lua_context = get();

    LuaTools::check_type(l, 1, LUA_TTABLE);

    std::list<LuaMenuData>& menus = lua_context.menus;
    for (auto it = menus.begin();
         it != menus.end();
         ++it
    ) {
      LuaMenuData menu = *it;
      push_ref(l, menu.ref);
      if (lua_equal(l, 1, -1)) {
        menus.erase(it);
        menus.push_back(menu);
        lua_pop(l, 1);
        break;
      }
      lua_pop(l, 1);
    }

    return 0;
  });
}

/**
 * \brief Implementation of sol.menu.bring_to_back().
 * \param l The Lua context that is calling this function.
 * \return Number of values to return to Lua.
 */
int LuaContext::menu_api_bring_to_back(lua_State* l) {

  return state_boundary_handle(l, [&] {
    LuaContext& lua_context = get();

    LuaTools::check_type(l, 1, LUA_TTABLE);

    std::list<LuaMenuData>& menus = lua_context.menus;
    for (auto it = menus.begin();
         it != menus.end();
         ++it
    ) {
      LuaMenuData menu = *it;
      push_ref(l, menu.ref);
      if (lua_equal(l, 1, -1)) {
        menus.erase(it);
        menus.push_front(menu);
        lua_pop(l, 1);
        break;
      }
      lua_pop(l, 1);
    }

    return 0;
  });
}

/**
 * \brief Calls the on_started() method of a Lua menu.
 * \param menu_ref A reference to the menu object.
 */
void LuaContext::menu_on_started(const ScopedLuaRef& menu_ref) {
  check_callback_thread();
  push_ref(current_l, menu_ref);
  on_started();
  lua_pop(current_l, 1);
}

/**
 * \brief Calls the on_finished() method of a Lua menu.
 * \param menu_ref A reference to the menu object.
 */
void LuaContext::menu_on_finished(const ScopedLuaRef& menu_ref) {
  check_callback_thread(); //TODO verify if needed
  push_ref(current_l, menu_ref);
  remove_menus(-1);  // First, stop children menus if any.
  on_finished();
  remove_timers(-1);  // Stop timers associated to this menu.
  lua_pop(current_l, 1);
}

/**
 * \brief Calls the on_update() method of a Lua menu.
 * \param menu_ref A reference to the menu object.
 */
void LuaContext::menu_on_update(const ScopedLuaRef& menu_ref) {
  check_callback_thread();
  push_ref(current_l, menu_ref);
  on_update();
  menus_on_update(-1);  // Update children menus if any.
  lua_pop(current_l, 1);
}

/**
 * \brief Calls the on_draw() method of a Lua menu.
 * \param menu_ref A reference to the menu object.
 * \param dst_surface The destination surface.
 */
void LuaContext::menu_on_draw(
    const ScopedLuaRef& menu_ref,
    const SurfacePtr& dst_surface
) {
  push_ref(current_l, menu_ref);
  on_draw(dst_surface);
  menus_on_draw(-1, dst_surface);  // Draw children menus if any.
  lua_pop(current_l, 1);
}

/**
 * \brief Calls an input callback method of a Lua menu.
 * \param menu_ref A reference to the menu object.
 * \param event The input event to forward.
 * \return \c true if the event was handled and should stop being propagated.
 */
bool LuaContext::menu_on_input(
    const ScopedLuaRef& menu_ref,
    const InputEvent& event
) {
  // Get the Lua menu.
  push_ref(current_l, menu_ref);

  // Send the event to children menus first.
  bool handled = menus_on_input(-1, event);

  if (!handled) {
    // Sent the event to this menu.
    handled = on_input(event);
  }

  // Remove the menu from the stack.
  lua_pop(current_l, 1);

  return handled;
}

/**
 * \brief Calls the on_command_pressed/released() method of a Lua menu.
 * \param menu_ref A reference to the menu object.
 * \param command The game command just pressed.
 * \return \c true if the event was handled and should stop being propagated.
 */
bool LuaContext::menu_on_command(
    const ScopedLuaRef& menu_ref,
    const ControlEvent& command
) {
  push_ref(current_l, menu_ref);

  // Send the event to children menus first.
  bool handled = menus_on_command(-1, command);

  if (!handled) {
    // Sent the event to this menu.
    handled = on_command(command);
  }

  lua_pop(current_l, 1);

  return handled;
}

/**
 * \brief Calls the on_update() method of the menus associated to a context.
 * \param context_index Index of an object with menus.
 */
void LuaContext::menus_on_update(int context_index) {
  for (LuaMenuData& menu: menus) {
    if (menu.context.equals(current_l,context_index)) {
      menu_on_update(menu.ref);
    }
  }
}

/**
 * \brief Calls the on_draw() method of the menus associated to a context.
 * \param context_index Index of an object with menus.
 * \param dst_surface The destination surface to draw.
 */
void LuaContext::menus_on_draw(int context_index, const SurfacePtr& dst_surface) {
  for (LuaMenuData& menu: menus) {
    if (menu.context.equals(current_l,context_index)) {
      menu_on_draw(menu.ref, dst_surface);
    }
  }
}

/**
 * \brief Calls the on_input() method of the menus associated to a context.
 * \param context_index Index of an object with menus.
 * \param event The input event to handle.
 * \return \c true if the event was handled and should stop being propagated.
 */
bool LuaContext::menus_on_input(int context_index, const InputEvent& event) {
  bool handled = false;
  std::list<LuaMenuData>::reverse_iterator it;
  for (it = menus.rbegin(); it != menus.rend() && !handled; ++it) {
    const ScopedLuaRef& menu_ref = it->ref;
    if (it->context.equals(current_l,context_index)) {
      handled = menu_on_input(menu_ref, event);
    }
  }

  return handled;
}

bool LuaContext::menus_on_command(int context_index, const ControlEvent& event) {
  bool handled = false;
  std::list<LuaMenuData>::reverse_iterator it;
  for (it = menus.rbegin(); it != menus.rend() && !handled; ++it) {
    const ScopedLuaRef& menu_ref = it->ref;
    if (it->context.equals(current_l,context_index)) {
      handled = menu_on_command(menu_ref, event);
    }
  }

  return handled;
}

}

