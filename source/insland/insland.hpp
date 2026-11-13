#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - umbrella header
// ---------------------------------------------------------------------------
// Include the whole public engine with a single line:
//
//     #include "insland/insland.hpp"
//     using namespace insland;
//
// Everything here is stable, game-facing API.  Internal detail headers
// (model_data.hpp, renderer internals, ...) are deliberately not exposed.
// ---------------------------------------------------------------------------

// Core ----------------------------------------------------------------------
#include "core/types.hpp"
#include "core/filesystem.hpp"
#include "core/logger.hpp"
#include "core/memory.hpp"
#include "core/serializer.hpp"

// Engine --------------------------------------------------------------------
#include "engine/engine.hpp"
#include "engine/camera.hpp"
#include "engine/model.hpp"

// Application layer ---------------------------------------------------------
#include "app/application.hpp"

// Systems -------------------------------------------------------------------
#include "assets/asset_manager.hpp"
#include "graphics/sky.hpp"
#include "raycast/raycast.hpp"
#include "scene/scene.hpp"
#include "ui/ui.hpp"
#include "world/world.hpp"
