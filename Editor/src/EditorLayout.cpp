#include "GameForger/Editor/EditorLayout.hpp"

#include <initializer_list>

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

		// ImGui holds at most ONE queued reorder per tab bar, and asserts if a
		// second is queued before the first is consumed. Two panels placing
		// themselves into the same node on the same frame (Project Settings and
		// Timeline both live with Console/Storyboard) hit exactly that. Yield and
		// let the caller retry next frame - both helpers already loop until they
		// return true.
		if (tabBar->ReorderRequestTabId != 0)
		{
			return false;
		}
		ImGui::TabBarQueueReorder(tabBar, &tabBar->Tabs[newIndex], offset);
		return false;
	}

	bool dockWindowAfter(const char* newWindow, const char* anchorWindow)
	{
		const ImGuiID nodeId = dockIdOfWindow(anchorWindow);
		if (nodeId == 0)
		{
			// Anchor not submitted yet this session.
			return false;
		}

		// Step 1: get into the anchor's node. Docking appends, so this alone
		// usually lands the tab last - which is already "after" the anchor
		// when the anchor is the final tab. Step 2 handles the rest.
		if (dockIdOfWindow(newWindow) != nodeId)
		{
			ImGui::DockBuilderDockWindow(newWindow, nodeId);
			ImGui::DockBuilderFinish(nodeId);
			// The dock request lands at the start of the next frame, so there
			// is no tab bar to reorder yet.
			return false;
		}

		ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
		if (node == nullptr || node->TabBar == nullptr)
		{
			return false;
		}
		ImGuiTabBar* tabBar = node->TabBar;

		const ImGuiWindow* newWindowPtr = ImGui::FindWindowByName(newWindow);
		const ImGuiWindow* anchorPtr = ImGui::FindWindowByName(anchorWindow);
		if (newWindowPtr == nullptr || anchorPtr == nullptr)
		{
			return false;
		}

		int newIndex = -1;
		int anchorIndex = -1;
		for (int i = 0; i < tabBar->Tabs.Size; ++i)
		{
			if (tabBar->Tabs[i].Window == newWindowPtr) newIndex = i;
			if (tabBar->Tabs[i].Window == anchorPtr)    anchorIndex = i;
		}
		if (newIndex < 0 || anchorIndex < 0)
		{
			return false;
		}

		// Land directly after the anchor. Moving left, the slot is
		// anchorIndex + 1; moving right, removing this tab first shifts the
		// anchor down one, so the target is anchorIndex itself.
		const int targetIndex = (newIndex < anchorIndex) ? anchorIndex : (anchorIndex + 1);
		const int offset = targetIndex - newIndex;
		if (offset == 0)
		{
			return true;
		}
		// TabBarQueueReorder asserts on a zero offset, hence the early return.
		// ImGui holds at most ONE queued reorder per tab bar, and asserts if a
		// second is queued before the first is consumed. Two panels placing
		// themselves into the same node on the same frame (Project Settings and
		// Timeline both live with Console/Storyboard) hit exactly that. Yield and
		// let the caller retry next frame - both helpers already loop until they
		// return true.
		if (tabBar->ReorderRequestTabId != 0)
		{
			return false;
		}
		ImGui::TabBarQueueReorder(tabBar, &tabBar->Tabs[newIndex], offset);
		return false;
	}

	void buildDefaultDockLayout(const ImGuiID dockspaceId)
	{
		// Start from nothing. Without the remove/add the split calls operate
		// on whatever the previous session left behind, which is how a
		// "reset" ends up reproducing the mess it was meant to clear.
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

		// Split off the outer columns first, then the bottom strip from what
		// is left, so the side columns run full height like every editor this
		// is modelled on rather than stopping short above the log.
		ImGuiID centre = dockspaceId;
		const ImGuiID leftColumn = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.19F, nullptr, &centre);
		const ImGuiID rightColumn = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.24F, nullptr, &centre);
		const ImGuiID bottomStrip = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30F, nullptr, &centre);

		ImGuiID leftTop = leftColumn;
		const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(leftTop, ImGuiDir_Down, 0.42F, nullptr, &leftTop);
		ImGuiID rightTop = rightColumn;
		const ImGuiID rightBottom =
			ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, 0.38F, nullptr, &rightTop);

		// Left: what is in the scene, and what is on disk.
		ImGui::DockBuilderDockWindow("Hierarchy", leftTop);
		ImGui::DockBuilderDockWindow("Project", leftBottom);

		// Right: what the selection is, and what you can make.
		ImGui::DockBuilderDockWindow("Inspector", rightTop);
		ImGui::DockBuilderDockWindow("Toolbox", rightBottom);

		// Centre: every view of the world. Mind Graph belongs here and not in
		// a side strip - a node graph needs the big pane, which is exactly
		// where the plan puts it.
		ImGui::DockBuilderDockWindow("Viewport", centre);
		ImGui::DockBuilderDockWindow("Game", centre);
		ImGui::DockBuilderDockWindow("Mind Graph", centre);
		ImGui::DockBuilderDockWindow("Cine Camera Preview", centre);

		// Bottom: everything you read or scrub rather than look at. Ordered
		// so the two most-used (Console, AI Forge) are the leftmost tabs.
		for (const char* window : {"Console", "AI Forge", "Animation", "Timeline", "Audio",
								   "Storyboard", "Project Settings", "Performance", "Blender MCP",
								   "AI Cockpit"})
		{
			ImGui::DockBuilderDockWindow(window, bottomStrip);
		}

		ImGui::DockBuilderFinish(dockspaceId);
	}
}
