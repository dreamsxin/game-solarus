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
#ifndef SOLARUS_SYSTEM_H
#define SOLARUS_SYSTEM_H

#include "solarus/core/Common.h"
#include <cstdint>
#include <string>
#include <chrono>

namespace Solarus {

class Arguments;

/**
 * \brief Provides low-level functions and initialization.
 *
 * This class initializes all low-level features.
 */
class SOLARUS_API System {
  public:

    static void initialize(const Arguments& args);
    static void quit();
    static void update(uint64_t timestep);

    static std::string get_os();

    static uint64_t now_ns();
    static uint64_t get_real_time_ns();
    static uint32_t now_ms();
    static uint32_t get_real_time_ms();
    static void sleep(uint32_t duration);

    static constexpr uint64_t fixed_timestep_ns = 10000000;  /**< Timestep added to the simulated time at each update. */

  private:
    using Clock = std::chrono::steady_clock;
    static Clock::time_point initial_time;         /**< Initial real time. */
    static uint64_t ticks;                         /**< Simulated time in nanoseconds. */

};

}

#endif

