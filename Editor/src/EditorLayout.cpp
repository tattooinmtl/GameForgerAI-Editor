#include "GameForger/Editor/EditorLayout.hpp"

#include <imgui.h>
// FindWindowByName is not part of the public API. See the header for why this
// is its own translation unit.
#include <imgui_internal.h>

namespace gameforger::editor
{
	ImGuiID dockIdOfWindow(const char* windowName)
	{
		if (windowName == nullptr)
		{
			return 0;
		}
		// Null on the very first frame, before the target window has been
		// submitted once. Returning 0 is correct: SetNextWindowDockID(0) is a
		// no-op, so the panel simply floats for one frame and docks on the
		// next, rather than being pinned to a bogus node.
		const ImGuiWindow* window = ImGui::FindWindowByName(windowName);
		return window != nullptr ? window->DockId : 0;
	}
}
