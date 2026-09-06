#include "GameForger/Editor/EditorLayout.hpp"

#include <imgui.h>
// FindWindowByName / DockBuilder* are not part of the public API. See the
// header for why this is its own translation unit.
#include <imgui_internal.h>

namespace gameforger::editor
{
	ImGuiID dockIdOfWindow(const char* windowName)
	{
		if (windowName == nullptr)
		{
			return 0;
		}
		// Null until the target window has been submitted once, which is why
		// callers retry across frames rather than assuming frame 1 works.
		const ImGuiWindow* window = ImGui::FindWindowByName(windowName);
		return window != nullptr ? window->DockId : 0;
	}

	bool dockWindowBetween(const char* newWindow, const char* firstWindow, const char* secondWindow)
	{
		const ImGuiID nodeId = dockIdOfWindow(firstWindow);
		if (nodeId == 0 || nodeId != dockIdOfWindow(secondWindow))
		{
			// Either the anchors have not been submitted yet, or the user has
			// since dragged them into different nodes - in which case there is
			// no "between" to honour and we leave their arrangement alone.
			return false;
		}

		// Step 1: get into the node at all. A window that is already there is
		// unaffected by this.
		if (dockIdOfWindow(newWindow) != nodeId)
		{
			ImGui::DockBuilderDockWindow(newWindow, nodeId);
			ImGui::DockBuilderFinish(nodeId);
			// The dock request is processed at the start of the next frame, so
			// there is no tab bar to reorder yet - report "not done" and let
			// the caller try again.
			return false;
		}

		// Step 2: reorder within the tab bar. Docking always APPENDS, so
		// without this the panel lands after both anchors rather than between
		// them. TabBarQueueReorder is the only mechanism that moves an
		// existing tab; DockBuilderDockWindow will not re-sequence a window
		// that is already in the node.
		ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
		if (node == nullptr || node->TabBar == nullptr)
		{
			return false;
		}
		ImGuiTabBar* tabBar = node->TabBar;

		const ImGuiWindow* newWindowPtr = ImGui::FindWindowByName(newWindow);
		const ImGuiWindow* secondWindowPtr = ImGui::FindWindowByName(secondWindow);
		if (newWindowPtr == nullptr || secondWindowPtr == nullptr)
		{
			return false;
		}

		int newIndex = -1;
		int secondIndex = -1;
		for (int i = 0; i < tabBar->Tabs.Size; ++i)
		{
			if (tabBar->Tabs[i].Window == newWindowPtr)    newIndex = i;
			if (tabBar->Tabs[i].Window == secondWindowPtr) secondIndex = i;
		}
		if (newIndex < 0 || secondIndex < 0)
		{
			return false;
		}

		// Land immediately before `secondWindow`. The previous formula
		// (`secondIndex - newIndex - 1`) is only correct when moving RIGHT;
		// if the new panel was already AFTER the second anchor (the saved
		// ini case: Console, Storyboard, Project Settings) it overshot and
		// TabBarQueueReorder's IM_ASSERT(offset != 0) also forbids a no-op.
		const int targetIndex = (newIndex < secondIndex) ? (secondIndex - 1) : secondIndex;
		const int offset = targetIndex - newIndex;
		if (offset == 0)
		{
			return true;
		}

		ImGui::TabBarQueueReorder(tabBar, &tabBar->Tabs[newIndex], offset);
		return false;
	}
}
