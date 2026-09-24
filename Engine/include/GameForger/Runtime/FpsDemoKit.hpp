#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace gameforger::editor
{
	// The FPS Demo kit: the full first-person demo set (player controller,
	// weapons, spell projectiles, particles, XP, health, items, enemy, game
	// manager). It lives in its own folders so it can be copied into any
	// project as a unit (File > Import FPS Demo Kit into This Project):
	//
	//   Game/Scripts/FPSDemo/   the scripts (see README.md there)
	//   Game/Icons/FPSDemo/     the weapon/item icons they reference
	//
	// The small scripts in Game/Scripts/ (fps_controller, enemy_ai, ...) are
	// separate starter scripts and never depend on the kit.
	namespace fpsdemo
	{
		inline constexpr const char* kScriptFolder = "Game/Scripts/FPSDemo";
		inline constexpr const char* kIconFolder = "Game/Icons/FPSDemo";

		inline constexpr const char* kPlayer = "Game/Scripts/FPSDemo/fps_player.lua";
		inline constexpr const char* kProjectiles = "Game/Scripts/FPSDemo/projectiles.lua";
		inline constexpr const char* kEffects = "Game/Scripts/FPSDemo/effects.lua";
		inline constexpr const char* kXpSystem = "Game/Scripts/FPSDemo/xp_system.lua";
		inline constexpr const char* kHealth = "Game/Scripts/FPSDemo/health.lua";
		inline constexpr const char* kItems = "Game/Scripts/FPSDemo/items.lua";
		inline constexpr const char* kEnemy = "Game/Scripts/FPSDemo/enemy.lua";
		inline constexpr const char* kGameManager = "Game/Scripts/FPSDemo/game_manager.lua";

		// What goes on the player object, in attach order.
		inline constexpr std::array<const char*, 5> kPlayerScripts{kPlayer, kProjectiles, kEffects, kXpSystem, kHealth};
	}

	struct FpsDemoKitImportResult
	{
		bool success = false;
		int filesCopied = 0;
		int filesSkipped = 0; // already in the project and overwrite == false
		std::string message;
	};

	// Copies the kit (Game/Scripts/FPSDemo/ + Game/Icons/FPSDemo/) from
	// `kitSourceRoot` - any folder laid out like a project (this engine's
	// repo, or the Kits/ folder built next to the editor) - into
	// `projectRoot`. Existing files are kept unless `overwrite`.
	FpsDemoKitImportResult importFpsDemoKit(
		const std::filesystem::path& kitSourceRoot, const std::filesystem::path& projectRoot, bool overwrite);

	// True if every kit script is present in the project.
	[[nodiscard]] bool fpsDemoKitInstalled(const std::filesystem::path& projectRoot);

	// Where to import the kit from: a Kits/ folder next to the editor
	// executable, else the nearest parent folder of it that has the kit
	// (the engine folder the editor was built in).
	[[nodiscard]] std::optional<std::filesystem::path> findFpsDemoKitSource(const std::filesystem::path& executableDirectory);
}
