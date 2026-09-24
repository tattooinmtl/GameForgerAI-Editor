#pragma once

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/ScriptRuntime.hpp"
#include "GameForger/Runtime/GameplayLoop.hpp"

namespace gameforger::editor
{
	struct HudColor
	{
		float r = 1.0F;
		float g = 1.0F;
		float b = 1.0F;
		float a = 1.0F;
	};

	// The 2D drawing surface the gameplay HUD is drawn onto, in screen
	// pixels. The Editor implements it with ImGui's draw list (Game view);
	// GameForgerRuntime with its own small GL renderer (RuntimeHud) - so both
	// show exactly the same effects and HUD from ONE layout function below.
	class HudCanvas
	{
	public:
		virtual ~HudCanvas() = default;
		virtual void line(const glm::vec2& from, const glm::vec2& to, const HudColor& color, float thickness) = 0;
		virtual void rectFilled(const glm::vec2& min, const glm::vec2& max, const HudColor& color, float rounding) = 0;
		virtual void rect(const glm::vec2& min, const glm::vec2& max, const HudColor& color, float rounding,
			float thickness) = 0;
		virtual void circleFilled(const glm::vec2& center, float radius, const HudColor& color) = 0;
		// `scale` 1 = the canvas's normal UI text size.
		virtual void text(const glm::vec2& position, const HudColor& color, const std::string& text, float scale) = 0;
		[[nodiscard]] virtual glm::vec2 textSize(const std::string& text, float scale) = 0;
		// An image file relative to the project root (inventory icons). Draws
		// nothing if it can't be loaded.
		virtual void image(const std::string& projectRelativePath, const glm::vec2& min, const glm::vec2& max) = 0;
	};

	struct HudFrame
	{
		// The game view's rectangle on the canvas.
		glm::vec2 origin{0.0F};
		glm::vec2 size{0.0F};
		glm::mat4 view{1.0F};
		glm::mat4 projection{1.0F};
		float timeSeconds = 0.0F;
		// The entity whose camera is in use (the player), or null.
		const SceneEntity* player = nullptr;
		// Show the hotbar (the player has an inventory-providing script).
		bool showHotbar = false;
		// "[E] Pick up X" style hint, empty for none.
		std::string interactionHint;
		bool drawCrosshair = false;
	};

	// World -> canvas pixel inside `frame`; false if behind the camera.
	[[nodiscard]] bool hudWorldToScreen(const HudFrame& frame, const glm::vec3& world, glm::vec2& screen);

	// Everything gameplay draws over the 3D view: script effects (beams,
	// flashes, floating text), health bars over health.lua objects, the
	// hotbar, script HUD bars (health/XP/mana), the weapon line a script
	// publishes as self.hud_text, the message line, the interaction hint and
	// (optionally) a crosshair.
	void drawGameplayHud(
		HudCanvas& canvas, const HudFrame& frame, const EditorScene& scene, const ScriptRuntime& scriptRuntime,
		const GameplayState& gameplay);
}
