#pragma once

#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	// The weapons fps_player.lua knows how to use - also the `weapon` enum
	// options in items.lua. Order = display order.
	inline const std::vector<std::string>& fpsWeaponIds()
	{
		static const std::vector<std::string> ids{
			"sword", "axe", "hammer", "pickaxe", "gun", "ak47", "taser", "lightning", "fire", "frost", "heal"};
		return ids;
	}

	struct FpsRigOptions
	{
		// Where the player's feet go.
		glm::vec3 position{0.0F, 0.0F, 0.0F};
		// Also lay out one pickup of every starting weapon in front of the
		// player (items.lua objects), hidden pickups for the three casters
		// xp_system.lua unlocks (Fire/Frost/Life), training targets
		// (health.lua, two of them chasing and hitting back via
		// enemy_ai.lua), a rock to mine, and a Game Manager.
		bool includeDemoContent = false;
		// Also add a large ground plane (for an empty scene).
		bool includeGround = false;
	};

	struct FpsRigBuildResult
	{
		bool success = false;
		std::string message;
		std::string playerName;
		std::string rigName;
		int entitiesCreated = 0;
	};

	// Builds a complete first-person player through the command bus (so it's
	// one undoable, saveable set of ordinary entities - nothing hidden):
	//
	//   Player (capsule, fps_player.lua, tag Player, cursor lock on)
	//   FPSRig                      <- moved to the eyes every frame by fps_player.lua
	//     FPSRig.Pitch              <- tilted with the mouse-look pitch
	//       FPSRig.Sway             <- bob / sway / recoil offset
	//         FPSRig.HandR          <- right hand (palm, 4 fingers, thumb, sleeve)
	//           FPSRig.W.sword ... FPSRig.W.lightning   (8 weapon models, all hidden
	//                                                    until equipped; guns have a
	//                                                    .Muzzle marker)
	//         FPSRig.HandL          <- left hand, shown for two-handed weapons
	//
	// Every hand/weapon part is a plain primitive child - select any of them
	// in the Hierarchy to recolor/resize it, or parent your own imported model
	// under a FPSRig.W.<weapon> node instead. Group nodes carry the "Empty"
	// tag (transform only, not drawn); every rig entity carries "Viewmodel"
	// (skipped by raycasts and weapon hits).
	FpsRigBuildResult buildFpsPlayerRig(EditorScene& scene, AICommandBus& commandBus, const FpsRigOptions& options);

	// The FPS Opus preset: the scripts that go on the player, in attach order.
	inline const std::vector<std::string>& fpsOpusPlayerScripts()
	{
		static const std::vector<std::string> scripts{"Game/Scripts/fps_player.lua", "Game/Scripts/projectiles.lua",
			"Game/Scripts/effects.lua", "Game/Scripts/xp_system.lua", "Game/Scripts/health.lua"};
		return scripts;
	}

	// Creates a "Game Manager" object (transform-only, game_manager.lua) -
	// the built game's title/splash logo/intro settings, edited in the
	// Inspector. Returns its name, or empty on failure.
	std::string createGameManager(EditorScene& scene, AICommandBus& commandBus, const glm::vec3& position);
}
