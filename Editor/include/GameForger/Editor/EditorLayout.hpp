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

	// Places `newWindow` as the tab immediately AFTER `anchorWindow`, in
	// whatever node the anchor already occupies. Same contract as
	// dockWindowBetween: no splits, no resizes, only this window's tab index
	// changes, and the caller stops asking once it returns true.
	bool dockWindowAfter(const char* newWindow, const char* anchorWindow);

	// Builds the whole default arrangement into `dockspaceId`, docking every
	// panel the editor has.
	//
	// This did not exist before, and its absence was a real defect rather
	// than a missing nicety: a fresh clone has no layout ini, and
	// Edit > Reset Editor Layout only cleared the ini in memory, so in both
	// cases every panel floated loose and overlapping. The editor then SAVED
	// that arrangement, which made it repeat on the next launch - the only
	// escape being a Reset that produced the same mess again.
	//
	// The arrangement is the one a Unity or Godot user expects: Hierarchy and
	// Project down the left, Inspector and Toolbox down the right, the log and
	// authoring tabs across the bottom, and the 3D views filling the centre.
	void buildDefaultDockLayout(ImGuiID dockspaceId);
}
