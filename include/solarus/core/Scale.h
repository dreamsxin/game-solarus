#ifndef SOLARUS_SCALE_H
#define SOLARUS_SCALE_H

#include "solarus/core/Size.h"
#include "solarus/core/Point.h"

namespace Solarus {

/**
 * @brief Represent a 2D scaling
 *
 * Two components scaling datas
 */
class Scale
{
public:
  constexpr Scale();
  constexpr Scale(float s);
  constexpr Scale(float x,float y);
  Scale& operator*=(const Scale& other);
  Scale& operator*=(float factor);
  float x = 1,y = 1;
};

constexpr Size operator*(const Size& size, const Scale& scale);
constexpr Point operator*(const Point& size, const Scale& scale);
constexpr Scale operator*(const Scale& a, const Scale& b);
constexpr Scale operator*(const Scale& a, float b);

}
#include "Scale.inl"

#endif // SCALE_H