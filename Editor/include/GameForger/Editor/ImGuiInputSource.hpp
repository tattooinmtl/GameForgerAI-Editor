#pragma once

#include "GameForger/Editor/InputSource.hpp"

namespace gameforger::editor
{
	// Answers ScriptRuntime's self.input queries off ImGui's own per-frame
	// keyboard state (ImGui::IsKeyDown/IsKeyPressed) - used only by the
	// Editor's Play mode, which already runs inside an active ImGui frame
	// every tick. See GlfwInputSource for GameForgerRuntime's equivalent,
	// used where no ImGui context exists at all.
	class ImGuiInputSource final : public InputSource
	{
	public:
		[[nodiscard]] bool isKeyDown(const std::string& name) const override;
		[[nodiscard]] bool isKeyPressed(const std::string& name) const override;
		[[nodiscard]] float getAxis(const std::string& positiveName, const std::string& negativeName) const override;
		[[nodiscard]] float getMouseDeltaX() const override;
		[[nodiscard]] float getMouseDeltaY() const override;
		[[nodiscard]] bool isMouseButtonDown(const std::string& name) const override;
		[[nodiscard]] float getScrollDelta() const override;
	};
}
