#pragma once

#include <imgui.h>

namespace gameforger::editor
{
	// The dock node `windowName` currently sits in, or 0 if it is not docked
	// (or does not exist yet this session).
	//
	// Used to place a NEW panel beside an existing one without rebuilding the
	// layout: the caller pairs this with
	// ImGui::SetNextWindowDockID(id, ImGuiCond_FirstUseEver), which applies
	// only to a window that has no entry in GameForgerEditorLayout.ini yet.
	// An existing saved arrangement is therefore never disturbed, and once the
	// user drags the panel somewhere else that choice sticks.
	//
	// Its own translation unit because the implementation needs
	// imgui_internal.h, whose ImGuiInputSource enum collides with this
	// project's class of the same name in main.cpp.
	[[nodiscard]] ImGuiID dockIdOfWindow(const char* windowName);
}
