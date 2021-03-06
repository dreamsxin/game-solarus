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
#include "solarus/audio/Music.h"
#include "solarus/core/Arguments.h"
#include "solarus/core/CurrentQuest.h"
#include "solarus/core/Debug.h"
#include "solarus/core/Game.h"
#include "solarus/core/Logger.h"
#include "solarus/core/MainLoop.h"
#include "solarus/core/QuestFiles.h"
#include "solarus/core/QuestProperties.h"
#include "solarus/core/Savegame.h"
#include "solarus/core/Settings.h"
#include "solarus/core/String.h"
#include "solarus/core/System.h"
#include "solarus/entities/TilePattern.h"
#include "solarus/graphics/Color.h"
#include "solarus/graphics/Surface.h"
#include "solarus/graphics/Video.h"
#include "solarus/graphics/quest_icon.h"

#include "solarus/lua/LuaContext.h"
#include "solarus/lua/LuaTools.h"
#include "solarus/core/Profiler.h"

#include <lua.hpp>
#include <clocale>
#include <sstream>
#include <string>
#include <thread>

namespace Solarus {

namespace {

/**
 * \brief Checks that the quest is compatible with the current version of
 * Solarus.
 * \param quest_version Solarus sersion of the quest (major and minor number).
 */
void check_version_compatibility(const std::pair<int, int>& quest_version) {

  const int quest_major_version = quest_version.first;
  const int quest_minor_version = quest_version.second;

  if (quest_version.first == 0) {
    Debug::die("No Solarus version is specified in your quest.dat file!");
  }

  // The third digit of the version (patch) is ignored because compatibility
  // is not broken by patches.

  bool compatible = true;
  if (quest_major_version != SOLARUS_MAJOR_VERSION) {
    // Assume that changes of major versions break compatibility.
    compatible = false;
  }
  else {
    if (quest_minor_version > SOLARUS_MINOR_VERSION) {
      // The quest is too recent for this engine.
      compatible = false;
    }
    else {
      // 1.5 quests can be run by Solarus 1.5, 1.6 and 1.7.
      // 1.6 quests can be run by Solarus 1.6 and 1.7.
      if (quest_minor_version < SOLARUS_MINOR_VERSION &&
          quest_major_version == 1 &&
          quest_minor_version < 5
      ) {
        compatible = false;
      }
    }
  }

  if (!compatible) {
    std::ostringstream oss;
    oss << "This quest is made for Solarus " << quest_major_version << "."
        << quest_minor_version << ".x but you are running Solarus "
        << SOLARUS_VERSION;
    Debug::die(oss.str());
  }
}

/**
 * \brief Returns path of the quest to run.
 *
 * It may be the path defined as command-line argument,
 * the path defined during the build process, or the current directory
 * if nothing was specified.
 *
 * \param args Command-line arguments in system 8-bit encoding.
 * \return The quest path in UTF-8 encoding.
 */
std::string get_quest_path(const Arguments& args) {

  // If a quest command-line argument was specified, use it instead.
  const std::vector<std::string>& options = args.get_arguments();
  if (!options.empty()
      && !options.back().empty()
      && options.back()[0] != '-') {
    // The last parameter is not an option: it is the quest path.
    return options.back();
  }

  // The default quest path is defined during the build process.
  return SOLARUS_DEFAULT_QUEST;
}

}  // Anonymous namespace.

/**
 * \brief Initializes the game engine.
 * \param args Command-line arguments.
 */
MainLoop::MainLoop(const Arguments& args):
  lua_context(nullptr),
  resource_provider(),
  root_surface(nullptr),
  game(nullptr),
  next_game(nullptr),
  exiting(false),
  debug_lag(0),
  lua_console_enabled(true),
  suspend_unfocused(true),
  suspended(false),
  turbo(false),
  lua_commands(),
  lua_commands_mutex(),
  num_lua_commands_pushed(0),
  num_lua_commands_done(0),
  commands_dispatcher(*this)
{

#ifdef SOLARUS_GIT_REVISION
  Logger::info("Solarus " SOLARUS_VERSION " (" SOLARUS_GIT_REVISION ")");
#else
  Logger::info("Solarus " SOLARUS_VERSION);
#endif

  // Main loop settings.
  const std::string lag_arg = args.get_argument_value("-lag");
  if (!lag_arg.empty()) {
    std::istringstream iss(lag_arg);
    iss >> debug_lag;
  }
  const std::string& turbo_arg = args.get_argument_value("-turbo");
  turbo = (turbo_arg == "yes");
  const std::string& suspend_unfocused_arg = args.get_argument_value("-suspend-unfocused");
  suspend_unfocused = suspend_unfocused_arg.empty() || suspend_unfocused_arg == "yes";
  const std::string& lua_console_arg = args.get_argument_value("-lua-console");
  lua_console_enabled = lua_console_arg.empty() || lua_console_arg == "yes";

  // Try to open the quest.
  const std::string& quest_path = get_quest_path(args);
  Logger::info("Opening quest '" + quest_path + "'");
  if (!QuestFiles::open_quest(args.get_program_name(), quest_path)) {
    Debug::error("No quest was found in the directory '" + quest_path + "'");
    return;
  }

  // Initialize engine features (audio, video...).
  System::initialize(args);

  // Read the quest resource list from data.
  CurrentQuest::initialize();

  // Read the quest general properties.
  load_quest_properties();

  make_root_surface();

  // Run the Lua world.
  // Do this after the creation of the window, but before showing the window,
  // because Lua might change the video mode initially.
  lua_context = std::unique_ptr<LuaContext>(new LuaContext(*this));

  if(Video::get_renderer().needs_window_workaround()) {
    Video::show_window();
    lua_context->initialize(args);
    Video::hide_window();
  } else {
    lua_context->initialize(args);
  }

  if (lua_console_enabled) {
    Logger::info("Lua console: yes");
    initialize_lua_console();
  }
  else {
    Logger::info("Lua console: no");
  }

  if (turbo) {
    Logger::info("Turbo mode: yes");
  }
  else {
    Logger::info("Turbo mode: no");
  }

  // Start loading resources in background.
  resource_provider.start_preloading_resources();

  // Display the game icon as window icon (if any)
  setup_game_icon();

  // Show the window.
  Video::show_window();

  // Set the fullscreen mode if requested.
  const std::string& fullscreen_arg = args.get_argument_value("-fullscreen");
  if (!fullscreen_arg.empty()) {
    Video::set_fullscreen(fullscreen_arg == "yes");
  }

  // Set the mouse cursor visibility if requested.
  const std::string& cursor_visible_arg = args.get_argument_value("-cursor-visible");
  if (!cursor_visible_arg.empty()) {
    Video::set_cursor_visible(cursor_visible_arg == "yes");
  }
}

/**
 * \brief Cleans everything.
 */
MainLoop::~MainLoop() {

  if (game != nullptr) {
    game->stop();
    game.reset();  // While deleting the game, the Lua world must still exist.
  }

  resource_provider.clear();

  // Clear the surface while Lua still exists,
  // because it may point to other surfaces that have Lua movements.
  root_surface = nullptr;

  if (lua_context != nullptr) {
    lua_context->exit();
  }
  CurrentQuest::quit();
  QuestFiles::close_quest();
  System::quit();
  quit_lua_console();
}

/**
 * \brief Returns the shared Lua context.
 * \return The Lua context where all scripts are run.
 */
LuaContext& MainLoop::get_lua_context() {
  return *lua_context;
}

/**
 * \brief Returns the resource provider of this quest.
 * \return The resource provider.
 */
ResourceProvider& MainLoop::get_resource_provider() {
  return resource_provider;
}

/**
 * \brief Returns whether the simulation is suspended.
 *
 * \return true if the simulation is suspended
 */
bool MainLoop::is_suspended() {
  return suspended;
}

/**
 * \brief Suspends or resumes the simulation.
 *
 * \param suspended true to suspend the simulation, false to resume it
 */
void MainLoop::set_suspended(bool suspended) {

  if (suspended != this->suspended) {
    this->suspended = suspended;
  }
}

/**
 * \brief Returns whether the user just closed the window.
 *
 * When this function returns true, you should stop immediately
 * whatever you are doing, free your memory and let the program quit.
 *
 * \return true if the user wants to exit the program
 */
bool MainLoop::is_exiting() {
  return exiting;
}

/**
 * \brief Sets whether the user wants to quit the program.
 */
void MainLoop::set_exiting() {

  // Stop the program.
  exiting = true;
}

/**
 * \brief Returns whether the program is being reset.
 */
bool MainLoop::is_resetting() {

  return game != nullptr && next_game == nullptr;
}

/**
 * \brief Marks the current game as finished and sets the initial screen
 * to be started at the next cycle.
 */
void MainLoop::set_resetting() {

  // Reset the program.
  if (game != nullptr) {
    game->stop();
  }
  set_game(nullptr);
}

/**
 * \brief Returns the current game if any.
 * \return The game currently running or nullptr.
 */
Game* MainLoop::get_game() {
  return game.get();
}

/**
 * \brief Changes the game.
 * \param game The new game to start, or nullptr to start no game.
 */
void MainLoop::set_game(Game* game) {
  this->next_game = game;
}

/**
 * \brief Schedules a Lua command to be executed at the next cycle.
 *
 * This function is thread safe, it can be called from a separate thread
 * while the main loop is running.
 *
 * \param command The Lua string to execute.
 * \return A number identifying your command.
 */
int MainLoop::push_lua_command(const std::string& command) {

  std::lock_guard<std::mutex> lock(lua_commands_mutex);
  lua_commands.push_back(command);
  return num_lua_commands_pushed++;
}

/**
 * \brief Runs the main loop until the user requests to stop the program.
 *
 * The main loop controls simulated time and repeatedly updates the world and
 * redraws the screen.
 *
 * Does nothing if the quest is missing.
 */
void MainLoop::run() {
  SOL_MAIN_THREAD;
#ifdef SOLARUS_PROFILING
  profiler::startListen();
#endif

  if (!QuestFiles::quest_exists()) {
    return;
  }

  // Main loop.
  Logger::info("Simulation started");

  if(CurrentQuest::get_properties().is_dynamic_timestep()) {
    dynamic_run();
  } else {
    fixed_run();
  }

  Logger::info("Simulation finished");
}

void MainLoop::dynamic_run() {
  uint64_t last_frame_date = System::get_real_time_ns();
  int64_t delta_buffer = 0; //Delta buffer is used to store difference between perfect timing and realtime

  auto delta_buffer_spill = 0.01f; //The lower it is the smoother we are but the less efficiently we catch up time
  // The main loop basically repeats
  // check_input(), update(), draw() and sleep().
  // Each call to update() makes the simulated time advance until next frame.

  while (!is_exiting()) {
    SOL_PBLOCK("Solarus::MainLoop::Frame");
    // 1. Detect and handle input events.
    check_input();

    if (!is_exiting() && !is_suspended()) {
      // Measure the time of the last iteration.
      uint64_t now = System::get_real_time_ns();
      int64_t last_frame_duration = now - last_frame_date;
      int64_t used_last_frame_duration = last_frame_duration;
      last_frame_date = now;

      int64_t perfect_frame_duration = Video::get_display_period_ns();

      if (last_frame_duration > perfect_frame_duration * 5) {
        //Do not try to catch up more than 10 frames, pretend everything is fine
        used_last_frame_duration = perfect_frame_duration;
      }

      //Compute how much we spill from delta_buffer
      int64_t spill = delta_buffer * delta_buffer_spill;

      //Transfer time to smoothed duration
      int64_t smoothed_duration = perfect_frame_duration + spill;

      //Put current frame error in buffer
      delta_buffer += used_last_frame_duration - smoothed_duration;

      // 2. Update the world once
      step(smoothed_duration);

      // 3. Draw
      draw();

      //Debug
      //std::cout << perfect_frame_duration << " " << last_frame_duration << " " << smoothed_duration << " " << delta_buffer << " " << spill << std::endl;
    }
    else
    {
      System::sleep(System::fixed_timestep_ns / 1000000);
    }
  }
}

void MainLoop::fixed_run() {
  uint64_t last_frame_date = System::get_real_time_ns();
  int64_t lag = 0;  // Lose time of the simulation to catch up.
  int64_t time_dropped = 0;  // Time that won't be caught up.

  // The main loop basically repeats
  // check_input(), update(), draw() and sleep().
  // Each call to update() makes the simulated time advance one fixed step.

  while (!is_exiting()) {
    SOL_PBLOCK("Solarus::MainLoop::Frame");
    // Measure the time of the last iteration.
    uint64_t now = System::get_real_time_ns() - time_dropped;
    uint64_t last_frame_duration = now - last_frame_date;
    last_frame_date = now;
    lag += last_frame_duration;
    // At this point, lag represents how much late the simulated time with
    // compared to the real time.

    if (lag >= 200000000) {
      // Huge lag: don't try to catch up.
      // Maybe we have just made a one-time heavy operation like loading a
      // big file, or the process was just unsuspended.
      // Let's fake the real time instead.
      time_dropped += lag - System::fixed_timestep_ns;
      lag = System::fixed_timestep_ns;
      last_frame_date = System::get_real_time_ns() - time_dropped;
    }

    // 1. Detect and handle input events.
    check_input();

    // 2. Update the world once, or several times (skipping some draws)
    // to catch up if the system is slow.
    int num_updates = 0;
    if (turbo && !is_suspended()) {
      // Turbo mode: always update at least once.
      step(System::fixed_timestep_ns);
      lag -= System::fixed_timestep_ns;
      ++num_updates;
    }

    while (lag >= static_cast<int64_t>(System::fixed_timestep_ns) &&
           num_updates < 10 && // To draw sometimes anyway on very slow systems.
           !is_exiting() && !is_suspended()
    ) {
      step(System::fixed_timestep_ns);
      lag -= System::fixed_timestep_ns;
      ++num_updates;
    }

    // 3. Redraw the screen.
    if (num_updates > 0 && !is_suspended()) {
      draw();
    }

    // 4. Sleep if we have time, to save CPU and GPU cycles.
    if (debug_lag > 0 && !turbo && !is_suspended()) {
      SOL_PBLOCK("Debug lag")
      // Extra sleep time for debugging, useful to simulate slower systems.
      System::sleep(debug_lag / 1000000);
    }

    last_frame_duration = (System::get_real_time_ms() - time_dropped) - last_frame_date;
    if (last_frame_duration < System::fixed_timestep_ns && !turbo) {
      SOL_PBLOCK("Timestep sleep");
      System::sleep((System::fixed_timestep_ns - last_frame_duration) / 1000000);
    }
  }
}

/**
 * \brief Advances the simulation of one tick.
 *
 * You can use this function if you want to simulate step by step.
 * Otherwise, use run() to execute the standard main loop.
 */
void MainLoop::step(uint64_t timestep_ns) {
  SOL_PFUN();
  if (game != nullptr) {
    game->update();
  }
  lua_context->update();
  System::update(timestep_ns);

  // Go to another game?
  if (next_game != game.get()) {

    game = std::unique_ptr<Game>(next_game);

    if (game != nullptr) {
      game->start();
    }
    else {
      lua_context->exit();
      lua_context->initialize(Arguments());
      Music::stop_playing();
    }
  }
}

/**
 * \brief Detects whether there were input events and if yes, handles them.
 */
void MainLoop::check_input() {
  SOL_PFUN();
  // Check SDL events.
  for(std::unique_ptr<InputEvent> event =  InputEvent::get_event();
      event != nullptr;
      event = InputEvent::get_event()) {
     notify_input(*event);
  }

  // Check Lua requests.
  if (lua_console_enabled && !lua_commands.empty()) {
    std::lock_guard<std::mutex> lock(lua_commands_mutex);
    for (const std::string& command : lua_commands) {
      std::cout << "\n";  // To make sure that the command delimiter starts on a new line.
      Logger::info("====== Begin Lua command #" + String::to_string(num_lua_commands_done) + " ======");
      const bool success = get_lua_context().do_string_with_easy_env(command, "Lua command");
      if (success) {
        std::cout << "\n";
        Logger::info("====== End Lua command #" + String::to_string(num_lua_commands_done) + ": success ======");
      }
      else {
        std::cout << "\n";
        Logger::info("====== End Lua command #" + String::to_string(num_lua_commands_done) + ": error ======");
      }
      ++num_lua_commands_done;
    }
    lua_commands.clear();
  }
}

void MainLoop::setup_game_icon() {
  static const std::vector<std::string> file_names = {
    "logos/icon_1024.png",
    "logos/icon_512.png",
    "logos/icon_256.png",
    "logos/icon_128.png",
    "logos/icon_64.png",
    "logos/icon_48.png",
    "logos/icon_32.png",
    "logos/icon_24.png",
    "logos/icon_16.png"
  };

  for(const auto& file : file_names) {
    SDL_Surface_UniquePtr surface = Surface::create_sdl_surface_from_file(file);
    if(surface) {
      Video::set_window_icon(surface.get());
      return;
    }
  }

  //else try to use default icon
  SDL_Surface_UniquePtr surface = Surface::create_sdl_surface_from_memory(quest_icon_data, quest_icon_data_len);
  SOLARUS_REQUIRE(bool(surface), "Could not load built-in icon");

  Video::set_window_icon(surface.get());
}

/**
 * \brief This function is called when there is an input event.
 *
 * It forwards the event to Lua, and then to the game if Lua did not
 * handle it.
 */
void MainLoop::notify_input(const InputEvent& event) {

  bool handled = false;
  if (event.is_window_closing()) {
    set_exiting();
  }
  else if (event.is_window_resizing()) {
    // Let video module resize it's geometry
    Video::on_window_resized(event.get_window_size());
    //Notify game if any
    make_root_surface();
    if(game) {
      game->notify_window_size_changed(event.get_window_size());
    }
  }
  else if (suspend_unfocused && event.is_window_focus_lost()) {
    if (!is_suspended()) {
      Logger::info("Simulation suspended");
      set_suspended(true);
      Sound::pause_all();
      Music::pause_playing();
    }
  }
  else if (suspend_unfocused && event.is_window_focus_gained()) {
    if (is_suspended()) {
      Logger::info("Simulation resumed");
      set_suspended(false);
      Music::resume_playing();
      Sound::resume_all();
    }
  }
  else if (event.is_keyboard_key_pressed()) {
    // A key was pressed.
#if defined(PANDORA)
    // TODO make a clean flag
    if (event.get_keyboard_key() == InputEvent::KEY_ESCAPE) {
      exiting = true;
    }
#endif
  } else if (event.is_controller_event()) {
    // First check if main joypad disconnected
    if(InputEvent::is_legacy_joypad_enabled() &&
       event.is_joypad_removed() &&
       game &&
       (game->get_controls().get_joypad() == event.get_joypad())) {
      // Main controls joypad is removed, try to fallback on another joypad
      auto new_joy = InputEvent::other_joypad(event.get_joypad());
      if(new_joy) {
        Logger::info("Using joystick: \"" + new_joy->get_name() + "\"");
      }
      game->get_controls().set_joypad(new_joy); //Could set joypad to nullptr, leaving it without joy
    }

    handled = event.notify_joypad(*lua_context);

    if(InputEvent::is_legacy_joypad_enabled() && event.is_joypad_added() && game && !game->get_controls().get_joypad()) {
      // A joypad was connected and main commands did not had a joypad
      auto new_joy = event.get_joypad();
      if(new_joy) {
        Logger::info("Using joystick: \"" + new_joy->get_name() + "\"");
      }
      game->get_controls().set_joypad(new_joy);
    }
  }

  // Send the event to Lua and to the current screen.
  if(!handled) {
    handled = lua_context->notify_input(event);
  }

  if (!handled && game != nullptr) {
    handled = game->notify_input(event);
  }

  if(!handled) {
    commands_dispatcher.notify_input(event);
  }
}

void MainLoop::notify_control(const ControlEvent& event) {
  if(game != nullptr) {
    game->notify_control(event);
  }
}

/**
 * \brief Redraws the current screen.
 *
 * This function is called repeatedly by the main loop.
 */
void MainLoop::draw() {
  SOL_PFUN();
  root_surface->clear();
  Video::clear_screen_surface();

  if (game != nullptr) {
    game->draw(root_surface, Video::get_screen_surface());
  }
  lua_context->main_on_draw(root_surface);
  Video::render(root_surface);
  lua_context->video_on_draw(Video::get_screen_surface());
  Video::finish();
}

/**
 * \brief Reads the quest properties file quest.dat and applies its settings.
 */
void MainLoop::load_quest_properties() {

  const QuestProperties& properties = CurrentQuest::get_properties();

  check_version_compatibility(properties.get_solarus_version_major_minor());

  Logger::info("Quest format: " + properties.get_solarus_version());

  const std::string& title = properties.get_title();
  const std::string& quest_version = properties.get_quest_version();

  if (!title.empty()) {
    std::string window_title = title;
    if (!quest_version.empty()) {
      window_title += " " + quest_version;
    }
#if defined(SOLARUS_GIT_REVISION) && !defined(NDEBUG)
    window_title += " - Solarus " SOLARUS_VERSION " (" SOLARUS_GIT_REVISION ")";
#else
    window_title += " - Solarus " SOLARUS_VERSION;
#endif
    Video::set_window_title(window_title);
  }

  Video::set_quest_size_range(
      properties.get_normal_quest_size(),
      properties.get_min_quest_size(),
      properties.get_max_quest_size()
  );
}

/**
 * \brief Enables accepting standard input lines as Lua commands.
 */
void MainLoop::initialize_lua_console() {

  // Watch stdin in a separate thread.
  stdin_thread = std::thread([this]() {

    std::string line;
    while (!is_exiting()) {

      if (std::getline(std::cin, line)) {

        while (!line.empty() && std::isspace(line.at(line.size() - 1))) {
          line.erase(line.size() - 1);
        }

        if (!line.empty()) {
          push_lua_command(line);
        }
      }
    }
  });
  stdin_thread.detach();
}

/**
 * \brief Cleans resources started by initialize_lua_console().
 */
void MainLoop::quit_lua_console() {

  exiting = true;
  if (!stdin_thread.joinable()) {
      return;
  }

  stdin_thread.join();
}

/**
 * @brief create the root surface
 */
void MainLoop::make_root_surface() {
  Size s = Video::get_quest_size();
  switch (Video::get_geometry_mode()) {
  case Video::GeometryMode::DYNAMIC_ABSOLUTE:
  case Video::GeometryMode::DYNAMIC_QUEST_SIZE:
    s = Video::get_window_size();
    break;
  default:
    break;
  }
  if(!root_surface or root_surface->get_size() != s) {
    root_surface = Surface::create(s);
  }
}

}
