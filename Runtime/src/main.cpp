#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
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

#include "GameMenu.hpp"

namespace
{
	void executeOrLog(gameforger::editor::AICommandBus& commandBus, const gameforger::editor::AIEditorCommand& command)
	{
		const gameforger::editor::AICommandResult result = commandBus.execute(command);
		if (!result.success)
		{
			std::fprintf(stderr, "Runtime command failed: %s\n", result.message.c_str());
		}
	}

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

	// Nearest isPickupItem entity to `origin` (the player's own position, not
	// a camera aim ray) within horizontalRange/heightTolerance - mirrors the
	// Editor's own findPickupCandidate (Editor/src/main.cpp) exactly; kept as
	// a small standalone copy here rather than relocated to Engine, since
	// it's the only piece of the Play-mode gameplay loop that isn't already
	// shared and isn't worth a new header for ~30 lines.
	const gameforger::editor::SceneEntity* findPickupCandidate(
		const gameforger::editor::EditorScene& scene,
		const glm::vec3& origin,
		const float horizontalRange,
		const float heightTolerance,
		const int excludeId)
	{
		const gameforger::editor::SceneEntity* best = nullptr;
		float bestDistanceSquared = horizontalRange * horizontalRange;
		for (const gameforger::editor::SceneEntity& other : scene.entities())
		{
			if (other.id == excludeId || !other.isPickupItem)
			{
				continue;
			}
			if (std::abs(other.position.y - origin.y) > heightTolerance)
			{
				continue;
			}
			const float deltaX = other.position.x - origin.x;
			const float deltaZ = other.position.z - origin.z;
			const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
			if (distanceSquared > bestDistanceSquared)
			{
				continue;
			}
			best = &other;
			bestDistanceSquared = distanceSquared;
		}
		return best;
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
	// MVP scope cut, stated plainly: only itemName/iconPath/count round-
	// trip - not scale/color/materialBlendWeight/materialLayers/
	// materialUvScale. A saved-then-reloaded item that's later dropped
	// (Editor-only feature today, see drawInventoryWindow) would drop with
	// default appearance instead of its original look. Worth extending if
	// Runtime ever gets its own drop-from-inventory action.
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
		std::string json = "{\n  \"inventoryItems\": [\n";
		for (std::size_t index = 0; index < items.size(); ++index)
		{
			const gameforger::editor::GameplayState::InventoryItem& item = items[index];
			json += "    { \"itemName\": \"" + escapeJsonString(item.itemName) + "\", \"iconPath\": \"" +
				escapeJsonString(item.iconPath) + "\", \"count\": " + std::to_string(item.count) + " }";
			json += (index + 1 < items.size()) ? ",\n" : "\n";
		}
		json += "  ]\n}\n";

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
		std::vector<gameforger::editor::GameplayState::InventoryItem> items;
		items.reserve(itemsField->arrayValue.size());
		for (const gameforger::editor::json::Value& itemValue : itemsField->arrayValue)
		{
			gameforger::editor::GameplayState::InventoryItem item;
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
			items.push_back(std::move(item));
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
	std::vector<SceneEntity> initialEntities;
	bool resumedFromSave = false;
	if (std::filesystem::exists(saveScenePath))
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
		const std::optional<std::filesystem::path> startupScenePath = readStartupScenePath(projectRoot);
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
	showSplashScreen(projectRoot / "Game" / "Branding" / "engine.png", 1.8);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(1280, 720, "GameForgerAI Runtime", nullptr, nullptr);
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
	if (!gameMenu.initialize(projectRoot / "Game" / "Fonts" / "Thuast Demo.otf"))
	{
		std::fprintf(stderr, "Failed to initialize the pause menu (shader/GL setup failed) - continuing without it.\n");
	}

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

	constexpr const char* kInventorySystemScriptPath = "Game/Scripts/inventory_system.lua";

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
		applyParentConstraints(scene, commandBus);
		tickPlayModeAnimations(scene, commandBus, gameplay, !menuOpen, deltaTime);
		tickScripts(scene, scriptRuntime, !menuOpen, deltaTime);
		tickProjectiles(scene, commandBus, gameplay, !menuOpen, deltaTime);

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
		const bool wantsCursorLock = followedEntity != nullptr && followedEntity->cameraRig.lockCursor && !menuOpen;
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
				executeOrLog(commandBus, SetPropertyCommand{followedEntity->name, "Transform", "rotation", newRotation});
				// Rotation just changed under the entity findEntity() found
				// earlier this frame - re-resolve so the pickup check below
				// (and this frame's camera framing) see the fresh facing.
				followedEntity = scene.findEntity(followedEntity->id);
			}
		}

		// Pickup (E key) - proximity-based, same as the Editor's Game view
		// (see findPickupCandidate's own comment for why). No visual "[E]
		// Pick up X" hint or inventory grid yet (both are ImGui UI chrome,
		// deferred to a later pass) - E still works, it just does its thing
		// silently for now. Suppressed while the menu is open so E doesn't
		// double as a gameplay action behind the pause overlay.
		if (followedEntity != nullptr && !menuOpen)
		{
			const bool hasInventorySystem = std::find(
				followedEntity->scripts.begin(), followedEntity->scripts.end(), kInventorySystemScriptPath) !=
				followedEntity->scripts.end();
			if (hasInventorySystem)
			{
				const float pickupRange = scriptRuntime.getScriptNumberField(
					followedEntity->id, kInventorySystemScriptPath, "pickup_range", 4.0F);
				const float pickupHeightTolerance = scriptRuntime.getScriptNumberField(
					followedEntity->id, kInventorySystemScriptPath, "pickup_height_tolerance", 2.5F);
				const SceneEntity* candidate = findPickupCandidate(
					scene, followedEntity->position, pickupRange, pickupHeightTolerance, followedEntity->id);
				if (candidate != nullptr && inputSource.isKeyPressed("E"))
				{
					const std::string itemName = candidate->pickupItem.itemName;
					const std::string iconPath = candidate->pickupItem.iconPath;
					const glm::vec3 itemScale = candidate->scale;
					const glm::vec3 itemColor = candidate->color;
					const glm::vec3 itemMaterialBlendWeight = candidate->materialBlendWeight;
					const std::array<TerrainLayerData, 3> itemMaterialLayers = candidate->materialLayers;
					const glm::vec2 itemMaterialUvScale = candidate->materialUvScale;
					executeOrLog(commandBus, DeleteEntityCommand{candidate->name});
					const auto existing = std::find_if(
						gameplay.inventoryItems.begin(),
						gameplay.inventoryItems.end(),
						[&itemName](const GameplayState::InventoryItem& item)
						{ return item.itemName == itemName; });
					if (existing != gameplay.inventoryItems.end())
					{
						existing->count += 1;
					}
					else
					{
						gameplay.inventoryItems.push_back(
							{itemName,
							 iconPath,
							 1,
							 itemScale,
							 itemColor,
							 itemMaterialBlendWeight,
							 itemMaterialLayers,
							 itemMaterialUvScale});
					}
					std::fprintf(stderr, "Picked up %s.\n", itemName.c_str());
				}
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

		if (!viewportRenderer.resize(width, height))
		{
			std::fprintf(stderr, "Failed to resize the game viewport.\n");
		}
		viewportRenderer.render(scene.entities(), {}, projectRoot, excludeEntityId);
		viewportRenderer.blitToCurrentFramebuffer(width, height);

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
	// Unconditional, not just on a slider release - guarantees the final
	// values persist regardless of how the window closed (X, Alt+F4, or
	// the menu's own Quit button).
	writeSettings(settingsPath, gameSettings);

	gameMenu.shutdown();
	scriptRuntime.shutdown();
	viewportRenderer.shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
