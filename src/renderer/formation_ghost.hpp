#pragma once

#include "sim/entity.hpp" // Vector3

#include <string>

namespace osc::renderer {

/// A draw-only formation slot: the input layer plans it, the renderer
/// shows its mesh or strategic icon without creating a simulated unit.
struct FormationGhost {
    std::string blueprint_id;
    sim::Vector3 position;
    f32 heading = 0;
};

} // namespace osc::renderer
