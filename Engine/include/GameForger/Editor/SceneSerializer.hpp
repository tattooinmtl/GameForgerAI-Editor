#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	struct SceneSaveResult
	{
		bool success = false;
		std::string message;
	};

	struct SceneLoadResult
	{
		bool success = false;
		std::string message;
		std::vector<SceneEntity> entities; // valid only when success is true
	};

	// Writes every entity's full state (transform, tag, color, pivot, camera
	// rig, attached script paths, animation keyframes) to `filePath` as JSON.
	[[nodiscard]] SceneSaveResult saveScene(
		const std::filesystem::path& filePath, const std::vector<SceneEntity>& entities);

	// Reads a scene previously written by saveScene(). Loaded entities don't
	// carry ids (the save format identifies them by name) - the caller is
	// expected to assign fresh ids, e.g. via EditorScene::loadEntities().
	[[nodiscard]] SceneLoadResult loadScene(const std::filesystem::path& filePath);
}
