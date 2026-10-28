#pragma once
// A point or a direction in the world, in centimetres, as the engine measures it.

namespace da::features::world
{
    struct Vec
    {
        double x = 0.0, y = 0.0, z = 0.0;
    };
}
