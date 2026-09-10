#pragma once

#include <string>

#include <glm/vec3.hpp>

#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	class ScriptRuntime;

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

	// The world-space eye position a GameCameraState resolves to - the same
	// point ViewportRenderer::cameraPosition() puts the camera at. Needed
	// because GameCameraState stores an ORBIT pose (yaw/pitch/distance around
	// a target) while a viewmodel needs the actual eye.
	//
	// yaw/pitch here are RADIANS (cameraLookingAt produces them via
	// asin/atan2 and setCamera consumes them unconverted), unlike
	// yawPitchForward above which takes degrees. That inconsistency is
	// pre-existing and load-bearing; this function exists partly so callers
	// stop having to know about it.
	[[nodiscard]] glm::vec3 gameCameraEye(const GameCameraState& camera);

	// Writes `camera`'s pose onto a SceneEntity's WORLD transform, so
	// anything parented to that entity (a weapon viewmodel, a crosshair, a
	// HUD panel) rides the player's view for free through the normal
	// parent-constraint solver - no separate viewmodel pass, no second
	// projection, no special-casing in the renderer.
	//
	// Sets position to the eye and rotation so that entityForward() equals the
	// VIEW direction (eye -> target), not the orbit direction (target -> eye).
	// Those are opposites, which is why the yaw comes out turned around by 180
	// degrees. Verified by testCameraPoseDrivesEntityForward rather than left
	// to a comment - the two conventions involved (entityForward's XYZ euler
	// on local +Z, and yawPitchForward's own pitch sign) are easy to get
	// subtly wrong in a way that only shows up as a weapon pointing backwards.
	void applyCameraPoseToEntity(SceneEntity& entity, const GameCameraState& camera);

	// Drives the scene's Main Camera entity from the live Play view, so
	// anything parented to it rides the player's eyes: a weapon viewmodel, a
	// held tool, a crosshair, a HUD panel.
	//
	// This IS the viewmodel mechanism. There is no separate viewmodel render
	// pass and no second projection - a gun in front of the player is just a
	// child of the Main Camera, placed by the same parent-constraint solver
	// that handles every other parent/child pair in the scene. The rule that
	// gives the user is worth stating plainly in the UI: parent something to
	// the Main Camera and it becomes part of the player's view.
	//
	// Does nothing when no script has claimed a camera, so the Main Camera
	// keeps its authored transform while editing and its gizmo stays put.
	//
	// Lives in Engine, called from BOTH hosts' tick loops, precisely so it
	// cannot drift the way this project's other Editor/Runtime seams have.
	void syncMainCameraToPlayView(
		EditorScene& scene,
		const ScriptRuntime& scriptRuntime,
		float lookYawDegrees,
		float lookPitchDegrees);
}
