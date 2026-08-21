#include "GameForger/Editor/ImGuiInputSource.hpp"

#include <imgui.h>

namespace gameforger::editor
{
	namespace
	{
		// Mirrors GlfwInputSource's own name table (Engine/src/Editor/GlfwInputSource.cpp)
		// key for key, just targeting ImGuiKey_* instead of GLFW_KEY_* - the two
		// must stay in sync so a script behaves identically under either host.
		ImGuiKey keyNameToImGuiKey(const std::string& name)
		{
			if (name.size() == 1)
			{
				const char letter = name[0];
				if (letter >= 'A' && letter <= 'Z')
				{
					return static_cast<ImGuiKey>(ImGuiKey_A + (letter - 'A'));
				}
				if (letter >= 'a' && letter <= 'z')
				{
					return static_cast<ImGuiKey>(ImGuiKey_A + (letter - 'a'));
				}
				if (letter >= '0' && letter <= '9')
				{
					return static_cast<ImGuiKey>(ImGuiKey_0 + (letter - '0'));
				}
			}
			if (name == "Space") return ImGuiKey_Space;
			if (name == "LeftShift") return ImGuiKey_LeftShift;
			if (name == "RightShift") return ImGuiKey_RightShift;
			if (name == "LeftCtrl") return ImGuiKey_LeftCtrl;
			if (name == "RightCtrl") return ImGuiKey_RightCtrl;
			if (name == "LeftAlt") return ImGuiKey_LeftAlt;
			if (name == "RightAlt") return ImGuiKey_RightAlt;
			if (name == "Escape") return ImGuiKey_Escape;
			if (name == "Tab") return ImGuiKey_Tab;
			if (name == "Enter") return ImGuiKey_Enter;
			if (name == "UpArrow") return ImGuiKey_UpArrow;
			if (name == "DownArrow") return ImGuiKey_DownArrow;
			if (name == "LeftArrow") return ImGuiKey_LeftArrow;
			if (name == "RightArrow") return ImGuiKey_RightArrow;
			return ImGuiKey_None;
		}
	}

	bool ImGuiInputSource::isKeyDown(const std::string& name) const
	{
		const ImGuiKey key = keyNameToImGuiKey(name);
		return key != ImGuiKey_None && ImGui::IsKeyDown(key);
	}

	bool ImGuiInputSource::isKeyPressed(const std::string& name) const
	{
		const ImGuiKey key = keyNameToImGuiKey(name);
		return key != ImGuiKey_None && ImGui::IsKeyPressed(key, false);
	}

	float ImGuiInputSource::getAxis(const std::string& positiveName, const std::string& negativeName) const
	{
		const ImGuiKey positiveKey = keyNameToImGuiKey(positiveName);
		const ImGuiKey negativeKey = keyNameToImGuiKey(negativeName);
		const float positive = (positiveKey != ImGuiKey_None && ImGui::IsKeyDown(positiveKey)) ? 1.0F : 0.0F;
		const float negative = (negativeKey != ImGuiKey_None && ImGui::IsKeyDown(negativeKey)) ? 1.0F : 0.0F;
		return positive - negative;
	}

	float ImGuiInputSource::getMouseDeltaX() const
	{
		return ImGui::GetIO().MouseDelta.x;
	}

	float ImGuiInputSource::getMouseDeltaY() const
	{
		return ImGui::GetIO().MouseDelta.y;
	}

	bool ImGuiInputSource::isMouseButtonDown(const std::string& name) const
	{
		// ImGui exposes mouse buttons as an indexed array of bools on
		// its IO. "Left" / "Right" / "Middle" map to indices 0/1/2 (the
		// standard GLFW / ImGui convention).
		int button = -1;
		if (name == "Left" || name == "left" || name == "LMB")
		{
			button = 0;
		}
		else if (name == "Right" || name == "right" || name == "RMB")
		{
			button = 1;
		}
		else if (name == "Middle" || name == "middle" || name == "MMB")
		{
			button = 2;
		}
		if (button < 0 || button >= 5)
		{
			return false;
		}
		return ImGui::GetIO().MouseDown[button];
	}

	float ImGuiInputSource::getScrollDelta() const
	{
		// ImGui's MouseWheel is the vertical scroll delta this frame,
		// positive for scroll-up. Mirrors getMouseDeltaX/Y in being
		// frame-local and reset by ImGui's NewFrame.
		return ImGui::GetIO().MouseWheel;
	}
}
