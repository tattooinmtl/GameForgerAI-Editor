#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <glm/geometric.hpp>
#include <glm/common.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/GlfwInputSource.hpp"
#include "GameForger/Editor/Json.hpp"
#include "GameForger/Editor/SceneSerializer.hpp"
#include "GameForger/Editor/ScriptRuntime.hpp"
#include "GameForger/Editor/SplashScreen.hpp"
#include "GameForger/Editor/ViewportRenderer.hpp"
#include "GameForger/Runtime/GameCamera.hpp"
#include "GameForger/Runtime/GameplayLoop.hpp"

#include "GameForger/Runtime/GameplayHud.hpp"
#include "GameForger/Runtime/FpsDemoKit.hpp"

#include "GameMenu.hpp"
#include "RuntimeHud.hpp"

namespace
{
	void glfwErrorCallback(const int error, const char* description)
	{
		std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
	}

	// Same convention as the Editor (main.cpp): every relative asset/project
	// path resolves against the process's current working directory, which
	// VS_DEBUGGER_WORKING_DIRECTORY (both targets' CMakeLists) and
	// Build-Project.cmd already set to the repo root.
	std::optional<std::string> readTextFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			return std::nullopt;
		}
		return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	}

	// Reads Game/Project.json's "startupScene" field (a path relative to
	// projectRoot, e.g. "Game/Scenes/Startup.gfai" - the Editor's own
	// "Build Game..." menu item is what produces a .gfai from a .gfprod;
	// see New-GameForgerAIProject.ps1/Project.json), the one piece of
	// project metadata this Runtime needs.
	std::optional<std::filesystem::path> readStartupScenePath(const std::filesystem::path& projectRoot)
	{
		const std::optional<std::string> text = readTextFile(projectRoot / "Game" / "Project.json");
		if (!text.has_value())
		{
			std::fprintf(stderr, "Could not open Game/Project.json.\n");
			return std::nullopt;
		}
		const std::optional<gameforger::editor::json::Value> parsed = gameforger::editor::json::parse(*text);
		if (!parsed.has_value() || parsed->type != gameforger::editor::json::Value::Type::Object)
		{
			std::fprintf(stderr, "Game/Project.json is not a valid JSON object.\n");
			return std::nullopt;
		}
		const gameforger::editor::json::Value* startupSceneField = parsed->find("startupScene");
		const std::optional<std::string> startupScene =
			startupSceneField != nullptr ? startupSceneField->asString() : std::nullopt;
		if (!startupScene.has_value() || startupScene->empty())
		{
			std::fprintf(stderr, "Game/Project.json has no \"startupScene\" field.\n");
			return std::nullopt;
		}
		return projectRoot / *startupScene;
	}

	// Save/resume: the world half is just the existing scene-file format
	// (saveScene/loadScene, SceneSerializer.hpp) written to a fixed path -
	// full entity state (positions, and critically, any isPickupItem entity
	// already removed by an earlier E-press stays removed) for free, no new
	// serialization code. The inventory half has no existing format to
	// reuse (GameplayState::inventoryItems is normally session-only, by
	// design - see GameplayLoop.hpp), so it gets a small hand-rolled JSON
	// writer/reader here, matching this project's existing convention of
	// building JSON text directly rather than through a generic serializer
	// (see SceneSerializer.cpp's own escapeJson/vec3ToJsonArray).
	//
	// Saved per non-empty slot: slot index, name, icon, count, and the
	// items.lua data (type/weapon/stacking + the hidden scene objects the
	// slot holds, which the world save keeps). Legacy pickups' captured
	// look (scale/color/material) is not saved - Runtime has no drop action
	// for them yet.
	std::string escapeJsonString(const std::string& text)
	{
		std::string escaped;
		escaped.reserve(text.size());
		for (const char character : text)
		{
			switch (character)
			{
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\n':
				escaped += "\\n";
				break;
			default:
				escaped += character;
				break;
			}
		}
		return escaped;
	}

	bool writeInventorySave(
		const std::filesystem::path& path, const std::vector<gameforger::editor::GameplayState::InventoryItem>& items)
	{
		std::string json = "{\n  \"inventoryItems\": [";
		bool first = true;
		for (std::size_t index = 0; index < items.size(); ++index)
		{
			const gameforger::editor::GameplayState::InventoryItem& item = items[index];
			if (item.empty())
			{
				continue;
			}
			std::string stored;
			for (std::size_t storedIndex = 0; storedIndex < item.storedEntityNames.size(); ++storedIndex)
			{
				stored += (storedIndex == 0 ? "\"" : ", \"") + escapeJsonString(item.storedEntityNames[storedIndex]) + "\"";
			}
			json += first ? "\n" : ",\n";
			first = false;
			json += "    { \"slot\": " + std::to_string(index) + ", \"itemName\": \"" +
				escapeJsonString(item.itemName) + "\", \"iconPath\": \"" + escapeJsonString(item.iconPath) +
				"\", \"count\": " + std::to_string(item.count) + ", \"itemType\": \"" +
				escapeJsonString(item.itemType) + "\", \"weapon\": \"" + escapeJsonString(item.weapon) +
				"\", \"stackable\": " + (item.stackable ? "true" : "false") + ", \"maxStack\": " +
				std::to_string(item.maxStack) + ", \"storedEntities\": [" + stored + "] }";
		}
		json += first ? "]\n}\n" : "\n  ]\n}\n";

		std::ofstream output(path, std::ios::binary);
		if (!output)
		{
			return false;
		}
		output << json;
		return true;
	}

	std::optional<std::vector<gameforger::editor::GameplayState::InventoryItem>> readInventorySave(
		const std::filesystem::path& path)
	{
		const std::optional<std::string> text = readTextFile(path);
		if (!text.has_value())
		{
			return std::nullopt;
		}
		const std::optional<gameforger::editor::json::Value> parsed = gameforger::editor::json::parse(*text);
		if (!parsed.has_value() || parsed->type != gameforger::editor::json::Value::Type::Object)
		{
			return std::nullopt;
		}
		const gameforger::editor::json::Value* itemsField = parsed->find("inventoryItems");
		if (itemsField == nullptr || itemsField->type != gameforger::editor::json::Value::Type::Array)
		{
			return std::nullopt;
		}
		std::vector<gameforger::editor::GameplayState::InventoryItem> items(
			static_cast<std::size_t>(gameforger::editor::kInventorySlotCount));
		for (const gameforger::editor::json::Value& itemValue : itemsField->arrayValue)
		{
			gameforger::editor::GameplayState::InventoryItem item;
			if (const gameforger::editor::json::Value* typeField = itemValue.find("itemType"))
			{
				item.itemType = typeField->asString().value_or("");
			}
			if (const gameforger::editor::json::Value* weaponField = itemValue.find("weapon"))
			{
				item.weapon = weaponField->asString().value_or("");
			}
			if (const gameforger::editor::json::Value* stackableField = itemValue.find("stackable"))
			{
				item.stackable = stackableField->type == gameforger::editor::json::Value::Type::Boolean
					? stackableField->boolValue
					: true;
			}
			if (const gameforger::editor::json::Value* maxStackField = itemValue.find("maxStack"))
			{
				item.maxStack = static_cast<int>(maxStackField->asNumber().value_or(99.0));
			}
			if (const gameforger::editor::json::Value* storedField = itemValue.find("storedEntities");
				storedField != nullptr && storedField->type == gameforger::editor::json::Value::Type::Array)
			{
				for (const gameforger::editor::json::Value& storedName : storedField->arrayValue)
				{
					if (const std::optional<std::string> name = storedName.asString())
					{
						item.storedEntityNames.push_back(*name);
					}
				}
			}
			if (const gameforger::editor::json::Value* nameField = itemValue.find("itemName"))
			{
				item.itemName = nameField->asString().value_or("");
			}
			if (const gameforger::editor::json::Value* iconField = itemValue.find("iconPath"))
			{
				item.iconPath = iconField->asString().value_or("");
			}
			if (const gameforger::editor::json::Value* countField = itemValue.find("count"))
			{
				item.count = static_cast<int>(countField->asNumber().value_or(0.0));
			}
			if (item.empty())
			{
				continue;
			}
			// Older saves have no slot index - put those in the first free slot.
			int slot = -1;
			if (const gameforger::editor::json::Value* slotField = itemValue.find("slot"))
			{
				slot = static_cast<int>(slotField->asNumber().value_or(-1.0));
			}
			if (slot < 0 || slot >= static_cast<int>(items.size()) || !items[static_cast<std::size_t>(slot)].empty())
			{
				slot = -1;
				for (int free = 0; free < static_cast<int>(items.size()); ++free)
				{
					if (items[static_cast<std::size_t>(free)].empty())
					{
						slot = free;
						break;
					}
				}
			}
			if (slot >= 0)
			{
				items[static_cast<std::size_t>(slot)] = std::move(item);
			}
		}
		return items;
	}

	// Player preferences (distinct from Game/Saves/ session state - not
	// wiped if a save is ever deleted) - mouse sensitivity and the FPS cap,
	// see GameMenu. Same small hand-rolled JSON convention as the
	// inventory save above.
	struct GameSettings
	{
		float mouseSensitivity = 0.15F;
		int targetFps = 60;
	};

	bool writeSettings(const std::filesystem::path& path, const GameSettings& settings)
	{
		std::string json = "{\n  \"mouseSensitivity\": " + std::to_string(settings.mouseSensitivity) +
			",\n  \"targetFps\": " + std::to_string(settings.targetFps) + "\n}\n";
		std::ofstream output(path, std::ios::binary);
		if (!output)
		{
			return false;
		}
		output << json;
		return true;
	}

	std::optional<GameSettings> readSettings(const std::filesystem::path& path)
	{
		const std::optional<std::string> text = readTextFile(path);
		if (!text.has_value())
		{
			return std::nullopt;
		}
		const std::optional<gameforger::editor::json::Value> parsed = gameforger::editor::json::parse(*text);
		if (!parsed.has_value() || parsed->type != gameforger::editor::json::Value::Type::Object)
		{
			return std::nullopt;
		}
		GameSettings settings;
		if (const gameforger::editor::json::Value* sensitivityField = parsed->find("mouseSensitivity"))
		{
			settings.mouseSensitivity = static_cast<float>(sensitivityField->asNumber().value_or(settings.mouseSensitivity));
		}
		if (const gameforger::editor::json::Value* fpsField = parsed->find("targetFps"))
		{
			settings.targetFps = static_cast<int>(fpsField->asNumber().value_or(settings.targetFps));
		}
		settings.targetFps = std::clamp(settings.targetFps, 30, 90);
		return settings;
	}
}

namespace
{
	constexpr const char* kGameManagerScript = gameforger::editor::fpsdemo::kGameManager;

	// The scene's Game Manager (an object with game_manager.lua) - its
	// Inspector values configure the built game: window title, boot splash
	// logo and how long it shows. Null if the scene has none.
	const gameforger::editor::SceneEntity* findGameManager(const std::vector<gameforger::editor::SceneEntity>& entities)
	{
		for (const gameforger::editor::SceneEntity& entity : entities)
		{
			if (gameforger::editor::hasScript(entity, kGameManagerScript))
			{
				return &entity;
			}
		}
		return nullptr;
	}

	// Which startup scene a save belongs to. A save made while playing a
	// different game/scene (e.g. before "Build Game" pointed Project.json at
	// a new scene) is ignored, not deleted, so it can't hijack the new game.
	std::string readSaveMetaScene(const std::filesystem::path& metaPath)
	{
		const std::optional<std::string> text = readTextFile(metaPath);
		if (!text.has_value())
		{
			return {};
		}
		const std::optional<gameforger::editor::json::Value> parsed = gameforger::editor::json::parse(*text);
		if (!parsed.has_value())
		{
			return {};
		}
		const gameforger::editor::json::Value* scene = parsed->find("startupScene");
		return scene != nullptr ? scene->asString().value_or("") : std::string();
	}

	void writeSaveMeta(const std::filesystem::path& metaPath, const std::string& startupScene)
	{
		std::ofstream output(metaPath, std::ios::binary | std::ios::trunc);
		output << "{\n  \"startupScene\": \"" << escapeJsonString(startupScene) << "\"\n}\n";
	}

	// Runtime's inventory grid (I): the shared hotbar shows slots 1-8 all
	// the time; this panel shows all 24. Click a slot to equip it,
	// right-click to drop one in front of the player.
	struct RuntimeInventoryPanel
	{
		bool leftWasDown = false;
		bool rightWasDown = false;
	};

	void drawRuntimeInventory(gameforger::editor::RuntimeHud& hud, GLFWwindow* window, const int width, const int height,
		gameforger::editor::GameplayState& gameplay, gameforger::editor::EditorScene& scene,
		gameforger::editor::AICommandBus& commandBus, const gameforger::editor::SceneEntity* player,
		RuntimeInventoryPanel& panel)
	{
		using namespace gameforger::editor;
		double mouseX = 0.0;
		double mouseY = 0.0;
		glfwGetCursorPos(window, &mouseX, &mouseY);
		const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		const bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
		const bool leftClicked = leftDown && !panel.leftWasDown;
		const bool rightClicked = rightDown && !panel.rightWasDown;
		panel.leftWasDown = leftDown;
		panel.rightWasDown = rightDown;

		constexpr int kColumns = kHotbarSlotCount;
		constexpr float kSlot = 62.0F;
		constexpr float kGap = 8.0F;
		const int rows = (kInventorySlotCount + kColumns - 1) / kColumns;
		const glm::vec2 gridSize(kColumns * kSlot + (kColumns - 1) * kGap, rows * kSlot + (rows - 1) * kGap);
		const glm::vec2 panelMin(
			(static_cast<float>(width) - gridSize.x) * 0.5F - 24.0F, (static_cast<float>(height) - gridSize.y) * 0.5F - 60.0F);
		const glm::vec2 panelMax = panelMin + gridSize + glm::vec2(48.0F, 110.0F);
		hud.rectFilled({0.0F, 0.0F}, {static_cast<float>(width), static_cast<float>(height)}, {0.0F, 0.0F, 0.0F, 0.35F}, 0.0F);
		hud.rectFilled(panelMin, panelMax, {0.06F, 0.06F, 0.07F, 0.94F}, 8.0F);
		hud.rect(panelMin, panelMax, {0.976F, 0.53F, 0.012F, 0.9F}, 8.0F, 2.0F);
		hud.text(panelMin + glm::vec2(24.0F, 14.0F), {1.0F, 0.9F, 0.7F, 1.0F}, "Inventory", 1.3F);
		hud.text(panelMin + glm::vec2(24.0F, 40.0F), {0.7F, 0.7F, 0.75F, 1.0F},
			"Click: equip    Right-click: drop    I: close", 0.85F);

		const glm::vec2 gridOrigin = panelMin + glm::vec2(24.0F, 70.0F);
		int hovered = -1;
		auto& slots = gameplay.inventoryItems;
		for (int index = 0; index < static_cast<int>(slots.size()); ++index)
		{
			const int column = index % kColumns;
			const int row = index / kColumns;
			const glm::vec2 min = gridOrigin + glm::vec2(column * (kSlot + kGap), row * (kSlot + kGap));
			const glm::vec2 max = min + glm::vec2(kSlot);
			const bool isHovered = mouseX >= min.x && mouseX <= max.x && mouseY >= min.y && mouseY <= max.y;
			if (isHovered)
			{
				hovered = index;
			}
			const GameplayState::InventoryItem& item = slots[static_cast<std::size_t>(index)];
			hud.rectFilled(min, max, row == 0 ? HudColor{0.13F, 0.11F, 0.08F, 1.0F} : HudColor{0.10F, 0.10F, 0.12F, 1.0F}, 5.0F);
			if (!item.empty())
			{
				if (!item.iconPath.empty())
				{
					hud.image(item.iconPath, min + glm::vec2(6.0F), max - glm::vec2(6.0F));
				}
				else
				{
					hud.text(min + glm::vec2(8.0F, 22.0F), {0.9F, 0.9F, 0.9F, 1.0F}, item.itemName.substr(0, 3), 1.0F);
				}
				if (item.count > 1)
				{
					const std::string count = "x" + std::to_string(item.count);
					hud.text(max - hud.textSize(count, 0.8F) - glm::vec2(4.0F, 2.0F), {1.0F, 1.0F, 1.0F, 1.0F}, count, 0.8F);
				}
			}
			if (row == 0)
			{
				hud.text(min + glm::vec2(4.0F, 2.0F), {0.8F, 0.75F, 0.65F, 0.8F}, std::to_string(index + 1), 0.7F);
			}
			const bool selected = gameplay.selectedSlot == index;
			hud.rect(min, max,
				selected ? HudColor{0.976F, 0.53F, 0.012F, 1.0F}
						 : (isHovered ? HudColor{0.9F, 0.9F, 0.95F, 0.9F} : HudColor{0.5F, 0.5F, 0.55F, 0.6F}),
				5.0F, selected ? 3.0F : 1.0F);
		}

		if (hovered >= 0 && !slots[static_cast<std::size_t>(hovered)].empty())
		{
			const GameplayState::InventoryItem& item = slots[static_cast<std::size_t>(hovered)];
			std::string label = item.itemName;
			if (!item.weapon.empty())
			{
				label += "  (" + item.weapon + ")";
			}
			hud.text({panelMin.x + 24.0F, panelMax.y - 30.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, label, 1.0F);
			if (leftClicked)
			{
				gameplay.selectedSlot = hovered;
			}
			else if (rightClicked && player != nullptr)
			{
				const float yaw = player->rotationEuler.y;
				const glm::vec3 forward(std::sin(glm::radians(yaw)), 0.0F, std::cos(glm::radians(yaw)));
				const glm::vec3 dropPosition = player->position + forward * 2.0F + glm::vec3(0.0F, 0.6F, 0.0F);
				(void)dropInventoryItem(scene, commandBus, gameplay, hovered, dropPosition, yaw);
			}
		}
	}
}

int main()
{
	using namespace gameforger::editor;

	const std::filesystem::path projectRoot = std::filesystem::current_path();
	const std::filesystem::path saveDirectory = projectRoot / "Game" / "Saves";
	const std::filesystem::path saveScenePath = saveDirectory / "save.gfai";
	const std::filesystem::path saveInventoryPath = saveDirectory / "save.inventory.gfai";
	// Player preferences (mouse sensitivity, FPS cap) - separate from
	// Game/Saves/, see GameSettings's own doc comment.
	const std::filesystem::path settingsPath = projectRoot / "Game" / "Settings.json";
	GameSettings gameSettings = readSettings(settingsPath).value_or(GameSettings{});
	std::fprintf(
		stderr, "Settings: mouseSensitivity=%.3f targetFps=%d\n", gameSettings.mouseSensitivity,
		gameSettings.targetFps);

	// Resume: if a previous session's save exists, it takes priority over
	// Project.json's authored startupScene - this is the whole mechanism,
	// no menu/prompt needed (see the README's Alpha 0.73 note for why).
	const std::filesystem::path saveMetaPath = saveDirectory / "save.meta.json";
	const std::optional<std::filesystem::path> startupScenePath = readStartupScenePath(projectRoot);
	std::string startupSceneKey;
	if (startupScenePath.has_value())
	{
		std::error_code relativeError;
		startupSceneKey = std::filesystem::relative(*startupScenePath, projectRoot, relativeError).generic_string();
	}
	std::vector<SceneEntity> initialEntities;
	bool resumedFromSave = false;
	const bool saveMatchesGame =
		!startupSceneKey.empty() && readSaveMetaScene(saveMetaPath) == startupSceneKey;
	if (std::filesystem::exists(saveScenePath) && !saveMatchesGame)
	{
		std::fprintf(stderr, "Ignoring %s - it was saved from a different startup scene.\n",
			saveScenePath.string().c_str());
	}
	if (std::filesystem::exists(saveScenePath) && saveMatchesGame)
	{
		const SceneLoadResult saveLoadResult = loadScene(saveScenePath);
		if (saveLoadResult.success)
		{
			initialEntities = saveLoadResult.entities;
			resumedFromSave = true;
			std::fprintf(stderr, "Resumed from save: %s\n", saveScenePath.string().c_str());
		}
		else
		{
			std::fprintf(
				stderr, "Save file exists but failed to load (%s) - starting fresh instead.\n",
				saveLoadResult.message.c_str());
		}
	}
	if (!resumedFromSave)
	{
		if (!startupScenePath.has_value())
		{
			return 1;
		}
		const SceneLoadResult loadResult = loadScene(*startupScenePath);
		if (!loadResult.success)
		{
			std::fprintf(stderr, "Could not load startup scene: %s\n", loadResult.message.c_str());
			return 1;
		}
		initialEntities = loadResult.entities;
		std::fprintf(
			stderr, "Loaded %zu entities from %s\n", initialEntities.size(), startupScenePath->string().c_str());
	}

	EditorScene scene(projectRoot);
	scene.loadEntities(initialEntities);

	glfwSetErrorCallback(glfwErrorCallback);
	if (glfwInit() != GLFW_TRUE)
	{
		return 1;
	}

	// The shipped game's own boot splash - distinct from the Editor's
	// logo.jpg, same technique (own borderless GLFW window + GL context,
	// closes itself after durationSeconds - see SplashScreen.hpp).
	std::string windowTitle = "GameForgerAI Runtime";
	{
		std::filesystem::path splashPath = projectRoot / "Game" / "Branding" / "engine.png";
		double splashSeconds = 1.8;
		if (const SceneEntity* manager = findGameManager(initialEntities))
		{
			const std::string title = ScriptRuntime::scriptPropertyText(*manager, kGameManagerScript, "game_title", projectRoot);
			if (!title.empty())
			{
				windowTitle = title;
			}
			// The logo must live inside the game's own folder (Game/...).
			const std::string logo = ScriptRuntime::scriptPropertyText(*manager, kGameManagerScript, "splash_logo", projectRoot);
			if (const std::optional<std::filesystem::path> resolved =
					gameforger::core::resolveProjectFile(projectRoot, logo, "Game", {".png", ".jpg", ".jpeg", ".bmp", ".tga"});
				resolved.has_value() && std::filesystem::exists(*resolved))
			{
				splashPath = *resolved;
			}
			else if (!logo.empty())
			{
				std::fprintf(stderr, "Game Manager splash_logo '%s' not found inside Game/ - using the engine logo.\n",
					logo.c_str());
			}
			const std::string seconds =
				ScriptRuntime::scriptPropertyText(*manager, kGameManagerScript, "splash_seconds", projectRoot);
			if (!seconds.empty())
			{
				splashSeconds = std::clamp(std::strtod(seconds.c_str(), nullptr), 0.0, 10.0);
			}
		}
		if (splashSeconds > 0.0)
		{
			showSplashScreen(splashPath, splashSeconds);
		}
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(1280, 720, windowTitle.c_str(), nullptr, nullptr);
	if (window == nullptr)
	{
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(window);
	// vsync off - the manual frame limiter (see the main loop, below) owns
	// pacing instead, so the configured 30-90 FPS cap is honored regardless
	// of the monitor's own refresh rate.
	glfwSwapInterval(0);

	// Title-bar/taskbar icon - same source and technique as the Editor
	// (main.cpp's WM_SETICON call), just without the tray-icon/minimize-to-
	// tray behavior, which is an Editor-only convenience with no meaning
	// for a shipped game.
	{
		const std::filesystem::path iconPath = projectRoot / "Game" / "Branding" / "icon.ico";
		const std::wstring iconPathWide = iconPath.wstring();
		const HICON iconLarge =
			static_cast<HICON>(LoadImageW(nullptr, iconPathWide.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
		const HICON iconSmall =
			static_cast<HICON>(LoadImageW(nullptr, iconPathWide.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
		if (iconLarge != nullptr && iconSmall != nullptr)
		{
			const HWND nativeWindowHandle = glfwGetWin32Window(window);
			SendMessageW(nativeWindowHandle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge));
			SendMessageW(nativeWindowHandle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
		}
		else
		{
			std::fprintf(stderr, "Application icon could not be loaded from: %s\n", iconPath.string().c_str());
		}
	}

	if (gladLoadGL(glfwGetProcAddress) == 0)
	{
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	// Same renderer the Editor's Game view uses (moved to Engine/ for
	// exactly this reason - see ViewportRenderer.hpp) - renders into its own
	// offscreen framebuffer, blitted into this window's real framebuffer
	// each frame via blitToCurrentFramebuffer() since there's no ImGui here
	// to present through via ImGui::Image().
	ViewportRenderer viewportRenderer;
	if (!viewportRenderer.initialize())
	{
		std::fprintf(stderr, "Failed to initialize the renderer.\n");
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	// Esc-to-pause menu (mouse sensitivity + FPS cap sliders, Resume/Quit) -
	// see GameMenu.hpp. A font-baking failure still returns true (menu
	// stays usable with blank labels); only a real GL/shader failure
	// returns false, which isn't fatal enough to abort the whole game over.
	GameMenu gameMenu;
	{
		// The bundled display font is CFF-flavored (stb_truetype can't bake
		// it) - fall back to a TrueType font so menu text isn't blank.
		std::vector<std::filesystem::path> menuFonts{projectRoot / "Game" / "Fonts" / "Thuast Demo.otf"};
		for (const std::filesystem::path& candidate : runtimeFontCandidates(projectRoot))
		{
			menuFonts.push_back(candidate);
		}
		for (const std::filesystem::path& fontPath : menuFonts)
		{
			gameMenu.shutdown();
			if (!gameMenu.initialize(fontPath))
			{
				std::fprintf(stderr, "Failed to initialize the pause menu (shader/GL setup failed) - continuing without it.\n");
				break;
			}
			if (gameMenu.hasFont())
			{
				break;
			}
		}
	}
	RuntimeHud hud;
	if (!hud.initialize(projectRoot))
	{
		std::fprintf(stderr, "Failed to initialize the HUD renderer - continuing without HUD.\n");
	}
	RuntimeInventoryPanel inventoryPanel;

	// No undo history needed here (that's Editor-only bookkeeping around
	// this same execute() call - see main.cpp's own commandBus.setHandler).
	AICommandBus commandBus;
	commandBus.setHandler([&scene](const AIEditorCommand& command) { return scene.execute(command); });

	GlfwInputSource inputSource(window);
	GameplayState gameplay;
	if (resumedFromSave)
	{
		if (const std::optional<std::vector<GameplayState::InventoryItem>> savedInventory =
				readInventorySave(saveInventoryPath))
		{
			gameplay.inventoryItems = *savedInventory;
		}
	}
	ensureInventorySlots(gameplay);
	ScriptRuntime scriptRuntime;
	// Re-run whenever the running entity list changes out from under the
	// script runtime - initial startup, and the pause menu's Load button
	// (which replaces scene's entities with a save's, getting fresh ids in
	// the process - see EditorScene::loadEntities - so every old script
	// instance needs shutting down and restarting against the new ids).
	const auto startAllScripts = [&]()
	{
		scriptRuntime.initialize(
			scene,
			commandBus,
			inputSource,
			[](const bool isError, const std::string& message)
			{
				std::fprintf(stderr, "%s%s\n", isError ? "[script error] " : "[script] ", message.c_str());
			},
			[&gameplay](
				const glm::vec3& from, const glm::vec3& to, const float speed, const std::string& hitTag)
			{
				const glm::vec3 direction = to - from;
				const float distance = glm::length(direction);
				const glm::vec3 velocity =
					distance > 0.0001F ? (direction / distance) * speed : glm::vec3(0.0F, 0.0F, speed);
				gameplay.projectiles.push_back(GameplayState::Projectile{from, velocity, hitTag, 4.0F});
			},
			// Runtime doesn't build the Editor's F-hold/E-aim-catapult
			// interactions (Editor-Play-only, see drawGameViewPanel in
			// Editor/src/main.cpp) - self.world:isHoldingItem()/
			// isAimingCatapult() simply always report false here, and
			// the catapult set callback / gravity projectile spawn
			// callback are no-ops (Runtime has no catapult UI of its
			// own, so nothing ever calls them).
			[]() { return false; },
			[]() { return false; },
			[](const bool) { /* no-op: Runtime has no catapult UI */ },
			[](const glm::vec3&, const glm::vec3&, const float, const std::string&)
			{
				/* no-op: Runtime does not spawn gravity projectiles */
			});
		// After initialize() (which resets it) - self.inventory,
		// self.camera:getPitch/getAim and spawnBeam/spawnFlash use it.
		scriptRuntime.setGameplayState(&gameplay);
		for (const SceneEntity& entity : scene.entities())
		{
			for (const std::string& scriptPath : entity.scripts)
			{
				scriptRuntime.startScript(entity.id, scriptPath, projectRoot);
			}
		}
	};
	startAllScripts();

	// Mouse-look state for the scripted Game camera - same fields/meaning as
	// the Editor's PlayModeState (main.cpp), just local here since Runtime
	// has no other Play-mode bookkeeping to bundle them with.
	float gameCameraLookYawDegrees = 0.0F;
	float gameCameraLookPitchDegrees = 0.0F;
	double lastMouseX = 0.0;
	double lastMouseY = 0.0;
	glfwGetCursorPos(window, &lastMouseX, &lastMouseY);

	double lastFrameTime = glfwGetTime();

	bool menuOpen = false;
	bool quitRequested = false;
	bool saveRequested = false;
	bool loadRequested = false;

	while (glfwWindowShouldClose(window) == GLFW_FALSE)
	{
		glfwPollEvents();

		const double currentFrameTime = glfwGetTime();
		const float deltaTime = static_cast<float>(currentFrameTime - lastFrameTime);
		lastFrameTime = currentFrameTime;

		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		if (width <= 0 || height <= 0)
		{
			continue;
		}

		// Identical Play-mode gameplay tick order to the Editor's own
		// drawEditorPanels (main.cpp) - `!menuOpen` in place of
		// playMode.isPlaying (Runtime has no Play/Stop toggle, it's
		// "playing" its entire lifetime except while paused for the menu).
		gameplay.lookPitchDegrees = gameCameraLookPitchDegrees;
		gameplay.lookYawDegrees = gameCameraLookYawDegrees;
		// gameplay.inventoryOpen is toggled with I below (runtime inventory panel).
		applyParentConstraints(scene, commandBus);
		tickPlayModeAnimations(scene, commandBus, gameplay, !menuOpen, deltaTime);
		tickScripts(scene, scriptRuntime, !menuOpen, deltaTime);
		if (!menuOpen)
		{
			// Again after scripts so the first-person hands rig follows the
			// camera this frame (same as the Editor, see drawEditorPanels).
			applyParentConstraints(scene, commandBus);
		}
		tickProjectiles(scene, commandBus, gameplay, !menuOpen, deltaTime);
		tickEffects(gameplay, !menuOpen, deltaTime);

		const SceneEntity* followedEntity =
			scriptRuntime.hasActiveCamera() ? scene.findEntity(scriptRuntime.activeCameraEntityId()) : nullptr;
		// tickScripts may have deleted the followed entity (or some other
		// mutation could have invalidated it). Re-resolve now so the rest
		// of this frame never sees a dangling pointer - the next frame
		// would have caught it on its own lookup, but the current frame's
		// mouse-look and pickup paths would dereference freed memory.
		if (followedEntity != nullptr && scene.findEntity(followedEntity->id) == nullptr)
		{
			followedEntity = nullptr;
		}

		double mouseX = 0.0;
		double mouseY = 0.0;
		glfwGetCursorPos(window, &mouseX, &mouseY);
		double mouseDeltaX = mouseX - lastMouseX;
		double mouseDeltaY = mouseY - lastMouseY;
		lastMouseX = mouseX;
		lastMouseY = mouseY;

		// Esc toggles the pause menu (standard convention) - opening it
		// implies releasing the cursor lock too, so wantsCursorLock below
		// just checks !menuOpen directly rather than a separate suppression
		// flag (unlike the Editor's drawGameViewPanel, which has no menu
		// and so still uses gameplay.cursorLockSuppressed for its own Esc
		// handling - that field is untouched by Runtime now).
		if (inputSource.isKeyPressed("Escape"))
		{
			menuOpen = !menuOpen;
		}
		if (!menuOpen && followedEntity != nullptr && inputSource.isKeyPressed("I") &&
			entityProvidesInventory(*followedEntity, scriptRuntime))
		{
			gameplay.inventoryOpen = !gameplay.inventoryOpen;
		}
		const bool wantsCursorLock = followedEntity != nullptr && followedEntity->cameraRig.lockCursor && !menuOpen &&
			!gameplay.inventoryOpen;
		if (wantsCursorLock && !gameplay.cursorCurrentlyLocked)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			gameplay.cursorCurrentlyLocked = true;
			// GLFW's disabled-cursor mode can report a large one-time jump
			// in glfwGetCursorPos the instant it's enabled (it switches to
			// an unbounded virtual position with no guaranteed continuity
			// with the last real screen position) - resync so THIS frame's
			// look math (below) doesn't treat that jump as a real gesture.
			glfwGetCursorPos(window, &mouseX, &mouseY);
			lastMouseX = mouseX;
			lastMouseY = mouseY;
			mouseDeltaX = 0.0;
			mouseDeltaY = 0.0;
		}
		else if (!wantsCursorLock && gameplay.cursorCurrentlyLocked)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			gameplay.cursorCurrentlyLocked = false;
		}

		if (followedEntity != nullptr && gameplay.cursorCurrentlyLocked)
		{
			const float mouseLookSensitivity = gameSettings.mouseSensitivity;
			gameCameraLookPitchDegrees = glm::clamp(
				gameCameraLookPitchDegrees - static_cast<float>(mouseDeltaY) * mouseLookSensitivity, -80.0F, 80.0F);
			if (scriptRuntime.activeCameraMode() == "third_person")
			{
				gameCameraLookYawDegrees -= static_cast<float>(mouseDeltaX) * mouseLookSensitivity;
			}
			else
			{
				const glm::vec3 newRotation(
					followedEntity->rotationEuler.x,
					followedEntity->rotationEuler.y - static_cast<float>(mouseDeltaX) * mouseLookSensitivity,
					followedEntity->rotationEuler.z);
				commandBus.execute(SetPropertyCommand{followedEntity->name, "Transform", "rotation", newRotation});
				// Rotation just changed under the entity findEntity() found
				// earlier this frame - re-resolve so the pickup check below
				// (and this frame's camera framing) see the fresh facing.
				followedEntity = scene.findEntity(followedEntity->id);
			}
		}

		// Pickup (E key) - the same shared logic as the Editor's Game view
		// (findPickupItemCandidate/pickUpItem, GameplayLoop.cpp): legacy
		// "Is Pickup Item" objects and items.lua objects alike. No on-screen
		// hint or inventory grid in Runtime yet (those are ImGui UI in the
		// Editor); the hotbar keys 1-8 still work since fps_player.lua
		// handles them itself. Suppressed while the pause menu is open.
		std::string interactionHint;
		if (followedEntity != nullptr && !menuOpen && entityProvidesInventory(*followedEntity, scriptRuntime))
		{
			float pickupRange = 4.0F;
			float pickupHeightTolerance = 2.5F;
			for (const std::string& scriptPath : followedEntity->scripts)
			{
				const float range =
					scriptRuntime.getScriptNumberField(followedEntity->id, scriptPath, "pickup_range", -1.0F);
				if (range >= 0.0F)
				{
					pickupRange = range;
					pickupHeightTolerance = scriptRuntime.getScriptNumberField(
						followedEntity->id, scriptPath, "pickup_height_tolerance", pickupHeightTolerance);
					break;
				}
			}
			const SceneEntity* candidate = findPickupItemCandidate(
				scene, followedEntity->position, pickupRange, pickupHeightTolerance, followedEntity->id);
			if (candidate != nullptr)
			{
				interactionHint = "[E] Pick up " + pickupDisplayName(*candidate, scriptRuntime);
			}
			if (candidate != nullptr && inputSource.isKeyPressed("E") && !gameplay.inventoryOpen)
			{
				const int followedId = followedEntity->id;
				const std::string itemName = pickupDisplayName(*candidate, scriptRuntime);
				const bool picked = pickUpItem(scene, commandBus, gameplay, scriptRuntime, *candidate);
				gameplay.messageText = picked ? "Picked up " + itemName : "Inventory full";
				gameplay.messageSecondsRemaining = 1.6F;
				std::fprintf(stderr, picked ? "Picked up %s.\n" : "Inventory full - could not pick up %s.\n",
					itemName.c_str());
				followedEntity = scene.findEntity(followedId);
			}
		}

		// Camera: whichever entity's script last called self.camera:setMode
		// (fps/third_person), same framing math as the Editor's Game view -
		// or a fixed fallback pose if nothing has claimed the camera yet
		// (no free-look orbit control exists here, unlike the Editor's own
		// separate "gameCamera" - Runtime has no UI to drive one with).
		const GameCameraState cameraState = followedEntity != nullptr
			? scriptedPlayCamera(
				  *followedEntity, scriptRuntime.activeCameraMode(), gameCameraLookYawDegrees,
				  gameCameraLookPitchDegrees)
			: GameCameraState{};
		viewportRenderer.setCamera(cameraState.yaw, cameraState.pitch, cameraState.distance, cameraState.target);

		// Standard FPS convention: don't render the player's own body mesh
		// from inside its own head - same policy as the Editor's Game view.
		const int excludeEntityId = (followedEntity != nullptr && scriptRuntime.activeCameraMode() == "fps")
			? followedEntity->id
			: -1;

		viewportRenderer.resize(width, height);
		viewportRenderer.render(scene.entities(), {}, projectRoot, excludeEntityId);
		viewportRenderer.blitToCurrentFramebuffer(width, height);

		// Gameplay HUD - the same layout as the Editor's Game view
		// (drawGameplayHud), drawn by RuntimeHud.
		{
			hud.begin(width, height);
			HudFrame frame;
			frame.origin = glm::vec2(0.0F);
			frame.size = glm::vec2(static_cast<float>(width), static_cast<float>(height));
			frame.view = viewportRenderer.view();
			frame.projection = viewportRenderer.projection();
			frame.timeSeconds = static_cast<float>(glfwGetTime());
			frame.player = followedEntity;
			frame.showHotbar = followedEntity != nullptr && entityProvidesInventory(*followedEntity, scriptRuntime);
			frame.interactionHint = gameplay.inventoryOpen ? std::string() : interactionHint;
			frame.drawCrosshair = followedEntity != nullptr && scriptRuntime.activeCameraMode() == "fps" &&
				!gameplay.inventoryOpen && !menuOpen;
			drawGameplayHud(hud, frame, scene, scriptRuntime, gameplay);
			if (gameplay.inventoryOpen && !menuOpen)
			{
				drawRuntimeInventory(hud, window, width, height, gameplay, scene, commandBus, followedEntity, inventoryPanel);
				followedEntity = scriptRuntime.hasActiveCamera() ? scene.findEntity(scriptRuntime.activeCameraEntityId()) : nullptr;
			}
			hud.end();
		}

		// Win/lose banner. gameplay.gameOverMessage is set by
		// tickProjectiles() the first time a castle's HP hits 0; the
		// Editor's Game view already drew an ImGui overlay for it, but
		// Runtime had no consumer - a player would otherwise see the
		// projectiles stop without any feedback that the round ended.
		if (!gameplay.gameOverMessage.empty())
		{
			gameMenu.drawCenteredBanner(width, height, gameplay.gameOverMessage);
		}

		// Drawn on top of the already-blitted game view, into the same
		// real window framebuffer - a no-op when menuOpen is false.
		if (gameMenu.render(
				window, width, height, menuOpen, gameSettings.mouseSensitivity, gameSettings.targetFps,
				quitRequested, saveRequested, loadRequested))
		{
			writeSettings(settingsPath, gameSettings);
		}

		// Save/Load only signal intent from GameMenu (it has no access to
		// scene/scriptRuntime) - this is where they're actually performed.
		if (saveRequested)
		{
			std::error_code saveDirectoryError;
			std::filesystem::create_directories(saveDirectory, saveDirectoryError);
			const SceneSaveResult manualSaveResult = saveScene(saveScenePath, scene.entities());
			const bool inventorySaveOk = writeInventorySave(saveInventoryPath, gameplay.inventoryItems);
			writeSaveMeta(saveMetaPath, startupSceneKey);
			writeSettings(settingsPath, gameSettings);
			std::fprintf(
				stderr, "%s\n",
				(manualSaveResult.success && inventorySaveOk) ? "Saved." : "Save failed - see prior error above.");
			saveRequested = false;
		}
		if (loadRequested)
		{
			if (std::filesystem::exists(saveScenePath))
			{
				const SceneLoadResult manualLoadResult = loadScene(saveScenePath);
				if (manualLoadResult.success)
				{
					scene.loadEntities(manualLoadResult.entities);
					gameplay.projectiles.clear();
					gameplay.inventoryItems =
						readInventorySave(saveInventoryPath).value_or(std::vector<GameplayState::InventoryItem>{});
					ensureInventorySlots(gameplay);
					gameplay.beams.clear();
					gameplay.flashes.clear();
					gameplay.floatingTexts.clear();
					gameplay.hudBars.clear();
					gameplay.inventoryOpen = false;
					// Reset every other piece of session state that was
					// previously surviving a Load - otherwise the new
					// game's scripts see stale callbacks (held item,
					// operating catapult), a stale win/lose banner keeps
					// being drawn, and the camera looks the same direction
					// the previous game had it looking.
					gameplay.heldItemEntityName.clear();
					gameplay.playerOperatingCatapult = false;
					gameplay.gameOverMessage.clear();
					gameCameraLookYawDegrees = 0.0F;
					gameCameraLookPitchDegrees = 0.0F;
					// Loaded entities got fresh ids (see EditorScene::
					// loadEntities) - every old script instance is now
					// pointing at stale ids, so the whole runtime restarts
					// against the new ones rather than trying to remap.
					scriptRuntime.shutdown();
					startAllScripts();
					std::fprintf(stderr, "Loaded.\n");
				}
				else
				{
					std::fprintf(stderr, "Load failed: %s\n", manualLoadResult.message.c_str());
				}
			}
			else
			{
				std::fprintf(stderr, "No save file to load yet.\n");
			}
			loadRequested = false;
		}
		if (quitRequested)
		{
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		}

		inputSource.update();
		glfwSwapBuffers(window);

		// Manual FPS cap (glfwSwapInterval is 0, see below) - pads the
		// frame out to gameSettings.targetFps regardless of the monitor's
		// own refresh rate, honoring the full requested 30-90 range even
		// on a 60Hz/144Hz display. Accepts Windows' default ~15ms sleep-
		// resolution jitter rather than adding timeBeginPeriod complexity.
		const double frameBudgetSeconds = 1.0 / static_cast<double>(std::clamp(gameSettings.targetFps, 30, 90));
		const double elapsedThisFrame = glfwGetTime() - currentFrameTime;
		if (elapsedThisFrame < frameBudgetSeconds)
		{
			std::this_thread::sleep_for(std::chrono::duration<double>(frameBudgetSeconds - elapsedThisFrame));
		}
	}

	// Save on exit - the whole "resume where you left off" mechanism is
	// just this snapshot existing (or not) the next time the game launches.
	std::error_code createDirectoriesError;
	std::filesystem::create_directories(saveDirectory, createDirectoriesError);
	const SceneSaveResult sceneSaveResult = saveScene(saveScenePath, scene.entities());
	if (!sceneSaveResult.success)
	{
		std::fprintf(stderr, "Failed to save scene state: %s\n", sceneSaveResult.message.c_str());
	}
	if (!writeInventorySave(saveInventoryPath, gameplay.inventoryItems))
	{
		std::fprintf(stderr, "Failed to save inventory state.\n");
	}
	writeSaveMeta(saveMetaPath, startupSceneKey);
	// Unconditional, not just on a slider release - guarantees the final
	// values persist regardless of how the window closed (X, Alt+F4, or
	// the menu's own Quit button).
	writeSettings(settingsPath, gameSettings);

	gameMenu.shutdown();
	hud.shutdown();
	scriptRuntime.shutdown();
	viewportRenderer.shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
