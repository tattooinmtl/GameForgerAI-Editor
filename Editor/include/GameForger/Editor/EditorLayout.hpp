#pragma once

#include <imgui.h>

namespace gameforger::editor
{
	// The dock node `windowName` currently sits in, or 0 if it is not docked
	// (or has not been submitted yet this session).
	[[nodiscard]] ImGuiID dockIdOfWindow(const char* windowName);

	// Places `newWindow` as a tab BETWEEN `firstWindow` and `secondWindow`, in
	// the node those two already share. Returns true once the tab order is
	// `… firstWindow, newWindow, secondWindow …`.
	//
	// This does not move, split, or resize anything: the node is whatever
	// `firstWindow` is already docked in, and the only change is the tab
	// index of `newWindow`. If the user has docked `newWindow` into a
	// different node, the caller must not call this.
	//
	// Its own translation unit because the implementation needs
	// imgui_internal.h, whose ImGuiInputSource enum collides with this
	// project's class of the same name in main.cpp.
	bool dockWindowBetween(const char* newWindow, const char* firstWindow, const char* secondWindow);
}
