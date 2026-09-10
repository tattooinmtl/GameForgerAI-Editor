#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/Storyboard.hpp"

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
		// Storyboard shots saved alongside the entities. Empty for a scene
		// written before shots were persisted, which loads fine - the field
		// is simply absent and the storyboard starts empty, as it always did.
		std::vector<CineShot> shots;
	};

	// Writes every entity's full state (transform, tag, color, pivot, camera
	// rig, attached script paths, animation keyframes) to `filePath` as JSON,
	// plus the storyboard shots and their audio cues.
	//
	// `shots` is defaulted so the existing call sites that only deal with
	// entities (Runtime, tests) need no change.
	[[nodiscard]] SceneSaveResult saveScene(
		const std::filesystem::path& filePath,
		const std::vector<SceneEntity>& entities,
		const std::vector<CineShot>& shots = {});

	// Reads a scene previously written by saveScene(). Loaded entities don't
	// carry ids (the save format identifies them by name) - the caller is
	// expected to assign fresh ids, e.g. via EditorScene::loadEntities().
	[[nodiscard]] SceneLoadResult loadScene(const std::filesystem::path& filePath);
}
