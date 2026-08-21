#pragma once

#include <string>

#include <glm/vec3.hpp>

#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	// A resolved orbit-style camera pose (yaw/pitch/distance around target) -
	// exactly what ViewportRenderer::setCamera(yaw, pitch, distance, target)
	// expects. Deliberately leaner than the Editor's own EditorCameraState
	// (main.cpp) - no drag-gesture bookkeeping, since that's an Editor-
	// authoring-viewport-only concept with no Runtime equivalent.
	struct GameCameraState
	{
		float yaw = 0.7F;
		float pitch = 0.35F;
		float distance = 4.0F;
		glm::vec3 target{0.0F};
	};

	// Like a plain yaw-only forward vector, but tilted up/down by
	// pitchDegrees too - used for the scripted Game view camera's mouse-look.
	[[nodiscard]] glm::vec3 yawPitchForward(float yawDegrees, float pitchDegrees);

	// ViewportRenderer::cameraPosition() computes eye = target + distance *
	// dir(yaw, pitch). This inverts that relationship: given a desired eye
	// position and look-at point, it solves for the (yaw, pitch, distance,
	// target) that reproduces them exactly, so a scripted camera can still
	// be driven through the existing setCamera() orbit API.
	[[nodiscard]] GameCameraState cameraLookingAt(const glm::vec3& eye, const glm::vec3& aimPoint);

	// The Game/Play camera for whichever entity's script last called
	// self.camera:setMode(...) - first-person (at the entity's eyes) or
	// third-person (orbiting the entity). Offsets come from the entity's own
	// EntityCameraRig (Inspector's "Camera Rig" section); lookYaw/
	// PitchDegrees come from the player's mouse-look and are 0 until they
	// look around.
	//
	// In FPS mode the entity's own rotation already reflects mouse yaw (the
	// character's facing IS the view direction there), so only pitch is
	// taken from lookPitchDegrees. In third-person mode the camera orbits
	// the entity independently of its facing, using both.
	[[nodiscard]] GameCameraState scriptedPlayCamera(
		const SceneEntity& entity, const std::string& mode, float lookYawDegrees, float lookPitchDegrees);
}
