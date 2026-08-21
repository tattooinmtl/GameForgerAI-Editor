#include "GameForger/Editor/GlfwInputSource.hpp"

#include <GLFW/glfw3.h>

namespace gameforger::editor
{
	namespace
	{
		// Mirrors ImGuiInputSource's own name table (Editor/src/ImGuiInputSource.cpp)
		// key for key, just targeting GLFW_KEY_* instead of ImGuiKey_* - the two
		// must stay in sync so a script behaves identically under either host.
		int keyNameToGlfwKey(const std::string& name)
		{
			if (name.size() == 1)
			{
				const char letter = name[0];
				if (letter >= 'A' && letter <= 'Z')
				{
					return GLFW_KEY_A + (letter - 'A');
				}
				if (letter >= 'a' && letter <= 'z')
				{
					return GLFW_KEY_A + (letter - 'a');
				}
				if (letter >= '0' && letter <= '9')
				{
					return GLFW_KEY_0 + (letter - '0');
				}
			}
			if (name == "Space") return GLFW_KEY_SPACE;
			if (name == "LeftShift") return GLFW_KEY_LEFT_SHIFT;
			if (name == "RightShift") return GLFW_KEY_RIGHT_SHIFT;
			if (name == "LeftCtrl") return GLFW_KEY_LEFT_CONTROL;
			if (name == "RightCtrl") return GLFW_KEY_RIGHT_CONTROL;
			if (name == "LeftAlt") return GLFW_KEY_LEFT_ALT;
			if (name == "RightAlt") return GLFW_KEY_RIGHT_ALT;
			if (name == "Escape") return GLFW_KEY_ESCAPE;
			if (name == "Tab") return GLFW_KEY_TAB;
			if (name == "Enter") return GLFW_KEY_ENTER;
			if (name == "UpArrow") return GLFW_KEY_UP;
			if (name == "DownArrow") return GLFW_KEY_DOWN;
			if (name == "LeftArrow") return GLFW_KEY_LEFT;
			if (name == "RightArrow") return GLFW_KEY_RIGHT;
			return GLFW_KEY_UNKNOWN;
		}
	}

	GlfwInputSource::GlfwInputSource(GLFWwindow* window) : window_(window)
	{
	}

	bool GlfwInputSource::isKeyDown(const std::string& name) const
	{
		const int key = keyNameToGlfwKey(name);
		return key != GLFW_KEY_UNKNOWN && glfwGetKey(window_, key) == GLFW_PRESS;
	}

	bool GlfwInputSource::isKeyPressed(const std::string& name) const
	{
		const bool down = isKeyDown(name);
		queriedThisFrame_[name] = down;
		const auto previous = previousDown_.find(name);
		const bool wasDown = previous != previousDown_.end() && previous->second;
		return down && !wasDown;
	}

	float GlfwInputSource::getAxis(const std::string& positiveName, const std::string& negativeName) const
	{
		return (isKeyDown(positiveName) ? 1.0F : 0.0F) - (isKeyDown(negativeName) ? 1.0F : 0.0F);
	}

	float GlfwInputSource::getMouseDeltaX() const
	{
		return static_cast<float>(mouseDeltaX_);
	}

	float GlfwInputSource::getMouseDeltaY() const
	{
		return static_cast<float>(mouseDeltaY_);
	}

	bool GlfwInputSource::isMouseButtonDown(const std::string& name) const
	{
		// Accept common aliases. GLFW has no string key for mouse
		// buttons, so map "Left" / "Right" / "Middle" to the matching
		// glfwGetMouseButton index.
		int button = GLFW_MOUSE_BUTTON_LEFT;
		if (name == "Left" || name == "left" || name == "LMB")
		{
			button = GLFW_MOUSE_BUTTON_LEFT;
		}
		else if (name == "Right" || name == "right" || name == "RMB")
		{
			button = GLFW_MOUSE_BUTTON_RIGHT;
		}
		else if (name == "Middle" || name == "middle" || name == "MMB")
		{
			button = GLFW_MOUSE_BUTTON_MIDDLE;
		}
		else
		{
			return false;
		}
		return window_ != nullptr && glfwGetMouseButton(window_, button) == GLFW_PRESS;
	}

	float GlfwInputSource::getScrollDelta() const
	{
		return static_cast<float>(scrollDelta_);
	}

	void GlfwInputSource::update()
	{
		for (const auto& [name, down] : queriedThisFrame_)
		{
			previousDown_[name] = down;
		}

		double mouseX = 0.0;
		double mouseY = 0.0;
		glfwGetCursorPos(window_, &mouseX, &mouseY);
		if (haveLastMouse_)
		{
			mouseDeltaX_ = mouseX - lastMouseX_;
			mouseDeltaY_ = mouseY - lastMouseY_;
		}
		else
		{
			mouseDeltaX_ = 0.0;
			mouseDeltaY_ = 0.0;
			haveLastMouse_ = true;
		}
		lastMouseX_ = mouseX;
		lastMouseY_ = mouseY;

		// Consume the wheel delta accumulated since the last frame.
		// We don't have a polling channel for it - the Runtime main
		// loop pushes scroll events in via accumulateScroll, but if it
		// doesn't, getScrollDelta() simply reports the last seen value.
		// Editor / GlfwInputSource readers that haven't wired
		// accumulateScroll yet will see 0 here, which is the safe
		// default for scripts that read it.
		scrollDelta_ = 0.0;
	}

	void GlfwInputSource::accumulateScroll(const double delta)
	{
		scrollDelta_ += delta;
	}
}
