#pragma once

#include <string>

namespace gameforger::editor
{
	// Abstracts "what keys are currently pressed" so ScriptRuntime's self.input
	// Lua binding doesn't need to know whether it's running inside the Editor
	// (ImGui owns keyboard state via its own NewFrame-driven poll - see
	// ImGuiInputSource, Editor-only) or inside a standalone GameForgerRuntime
	// window, which never creates an ImGui context at all (see
	// GlfwInputSource, which reads GLFW's keyboard state directly).
	class InputSource
	{
	public:
		virtual ~InputSource() = default;

		[[nodiscard]] virtual bool isKeyDown(const std::string& name) const = 0;
		// True only on the frame `name` transitions from up to down (no
		// auto-repeat) - matches the semantics scripts already rely on via
		// the Editor's ImGui-backed implementation.
		[[nodiscard]] virtual bool isKeyPressed(const std::string& name) const = 0;
		[[nodiscard]] virtual float getAxis(const std::string& positiveName, const std::string& negativeName) const = 0;
		// Mouse movement since the previous frame, in pixels - for scripts
		// that need continuous aiming (e.g. catapult_controller.lua), not
		// just discrete key presses. Editor/ImGuiInputSource reads ImGui's
		// own already-reset-per-frame MouseDelta directly; Runtime/
		// GlfwInputSource has to track cursor position itself since GLFW
		// has no equivalent, updated once per frame in update() the same
		// way isKeyPressed's edge-detection state is.
		[[nodiscard]] virtual float getMouseDeltaX() const = 0;
		[[nodiscard]] virtual float getMouseDeltaY() const = 0;
		// Mouse button state. True while the button is held; no
		// edge-detection. Scripts that need "pressed this frame" can
		// track transitions themselves, since the same string may mean
		// different things to different control schemes.
		[[nodiscard]] virtual bool isMouseButtonDown(const std::string& name) const = 0;
		// Vertical scroll-wheel delta since the previous frame, in
		// ImGui/GLFW "lines" units. Positive values scroll up. Zero when
		// the wheel hasn't moved.
		[[nodiscard]] virtual float getScrollDelta() const = 0;
	};
}
