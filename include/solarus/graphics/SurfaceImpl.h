/*
 * Copyright (C) 2018-2020 std::gregwar, Solarus - http://www.solarus-games.org
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
#pragma once

#include "solarus/core/Rectangle.h"
#include "solarus/core/Point.h"
#include "solarus/graphics/SoftwarePixelFilter.h"
#include "solarus/graphics/View.h"

#include <SDL_render.h>
#include <memory>
#include <string>

namespace Solarus {

class Texture;
class RenderTexture;
class Surface;

/**
 * @brief Abstract class for internal surface pixel representation and manipulation
 */
class SurfaceImpl
{
public:

  SurfaceImpl(const Size& size);

  /**
     * @brief get the synchronised SDL_Surface
     *
     * The SDL_Texture alone is not convenient to access, for pixel
     * perfect collisions and pixel access, we keep a surface as a
     * cache.
     *
     * @return a valid SDL_Surface
     */
  virtual SDL_Surface* get_surface() const = 0;

  /**
   * @brief upload_surface back to the accelerated storage
   */
  virtual void upload_surface() = 0;

  int get_width() const;

  int get_height() const;

  const Size& get_size() const;


  /**
   * @brief get the pixel buffer for this surface
   * @return
   */
  std::string get_pixels() const;

  /**
   * @brief set the pixels of this surface from a buffer
   * @param buffer
   */
  void set_pixels(const std::string& buffer);

  /**
   * @brief Apply a pixel filter to this surface
   * @param pixel_filter the pixel filter
   * @param dst_surface surface to draw this pixel filter onto
   */
  void apply_pixel_filter(const SoftwarePixelFilter& pixel_filter, SurfaceImpl& dst_surface) const;

  /**
   * @brief Test wheter a pixel is transparent
   * @param index the pixel index (row major)
   * @return true if pixel is transparent
   */
  bool is_pixel_transparent(int index) const;

  /**
     * @brief ~SurfaceImpl
     */
  virtual ~SurfaceImpl();

  /**
   * @brief is this surface premultiplied
   * @return
   */
  bool is_premultiplied() const;

  /**
   * @brief Set if this surface is premultiplied
   * @param
   */
  void set_premultiplied(bool a_premultiplied);

  /**
   * Cast this surface to another (child type, no verification)
   */
  template<class T> T& as() {
    return *static_cast<T*>(this);
  }

  /**
   * Const version of the conversion
   */
  template<class T> const T& as() const {
    return *static_cast<const T*>(this);
  }

  void set_view(const View& view);
  const View& get_view() const;
  View& get_view();
private:
  Size size;
  bool premultiplied = false;
  View view;
};

using SurfaceImplPtr = std::shared_ptr<SurfaceImpl>;

}
