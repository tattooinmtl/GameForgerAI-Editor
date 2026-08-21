#pragma once

#include <filesystem>

namespace gameforger::editor
{
	// Shows a borderless, centered splash window with the given image for
	// `durationSeconds`, then closes it. Requires glfwInit() to already have
	// been called. Fails silently (returns immediately) if the image can't be
	// loaded, so a missing asset never blocks startup.
	void showSplashScreen(const std::filesystem::path& imagePath, double durationSeconds);
}
