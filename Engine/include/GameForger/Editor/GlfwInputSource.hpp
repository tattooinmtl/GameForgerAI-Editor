#pragma once

#include <string>
#include <unordered_map>

#include "GameForger/Editor/InputSource.hpp"

struct GLFWwindow;

namespace gameforger::editor
{
	// Answers ScriptRuntime's self.input queries directly off GLFW's own
	// keyboard state - used by GameForgerRuntime (the standalone player),
	// which never creates an ImGui context (see ImGuiInputSource for the
	// Editor's equivalent, which reads ImGui's key state instead).
	//
	// isKeyPressed needs "true only on the frame the key went down" edge
	// semantics, which glfwGetKey alone doesn't provide (it's a live
	// down/up poll, no history) - update() must be called exactly once per
	// frame, after every script has had a chance to query this frame's
	// state, so the NEXT frame's isKeyPressed calls compare against the
	// right "previous" snapshot.
	class GlfwInputSource final : public InputSource
	{
	public:
		explicit GlfwInputSource(GLFWwindow* window);

		[[nodiscard]] bool isKeyDown(const std::string& name) const override;
		[[nodiscard]] bool isKeyPressed(const std::string& name) const override;
		[[nodiscard]] float getAxis(const std::string& positiveName, const std::string& negativeName) const override;
		// Valid starting the frame AFTER the first update() call (0 before
		// then, same "nothing to diff against yet" convention as
		// isKeyPressed's very first frame never reporting a press).
		[[nodiscard]] float getMouseDeltaX() const override;
		[[nodiscard]] float getMouseDeltaY() const override;
		[[nodiscard]] bool isMouseButtonDown(const std::string& name) const override;
		[[nodiscard]] float getScrollDelta() const override;

		void update();
		// Accumulate a scroll-wheel delta since the last update().
		// Called by the main loop's GLFW scroll callback (Runtime).
		void accumulateScroll(double delta);

	private:
		GLFWwindow* window_ = nullptr;
		mutable std::unordered_map<std::string, bool> previousDown_;
		mutable std::unordered_map<std::string, bool> queriedThisFrame_;
		double lastMouseX_ = 0.0;
		double lastMouseY_ = 0.0;
		double mouseDeltaX_ = 0.0;
		double mouseDeltaY_ = 0.0;
		double scrollDelta_ = 0.0;
		bool haveLastMouse_ = false;
	};
}
