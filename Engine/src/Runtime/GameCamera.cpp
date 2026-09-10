#include "GameForger/Runtime/GameCamera.hpp"

#include "GameForger/Editor/ScriptRuntime.hpp"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

namespace gameforger::editor
{
	glm::vec3 yawPitchForward(const float yawDegrees, const float pitchDegrees)
	{
		const float yawRadians = glm::radians(yawDegrees);
		const float pitchRadians = glm::radians(pitchDegrees);
		return glm::vec3(
			std::cos(pitchRadians) * std::sin(yawRadians),
			std::sin(pitchRadians),
			std::cos(pitchRadians) * std::cos(yawRadians));
	}

	GameCameraState cameraLookingAt(const glm::vec3& eye, const glm::vec3& aimPoint)
	{
		const glm::vec3 toEye = eye - aimPoint;
		const float distance = glm::max(glm::length(toEye), 0.01F);
		const glm::vec3 direction = toEye / distance;
		GameCameraState result;
		result.pitch = std::asin(glm::clamp(direction.y, -1.0F, 1.0F));
		result.yaw = std::atan2(direction.x, direction.z);
		result.distance = distance;
		result.target = aimPoint;
		return result;
	}

	GameCameraState scriptedPlayCamera(
		const SceneEntity& entity, const std::string& mode, const float lookYawDegrees,
		const float lookPitchDegrees)
	{
		const EntityCameraRig& rig = entity.cameraRig;
		if (mode == "third_person")
		{
			// The rig's height/distance ratio is treated as the default
			// orbit pitch, so existing rig values keep framing the camera
			// the same way they always did until the player looks around.
			const float basePitchDegrees =
				glm::degrees(std::atan2(rig.thirdPersonHeight, glm::max(rig.thirdPersonDistance, 0.01F)));
			const float orbitYawDegrees =
				entity.rotationEuler.y + 180.0F + rig.thirdPersonYawOffsetDegrees + lookYawDegrees;
			const float orbitPitchDegrees = glm::clamp(basePitchDegrees + lookPitchDegrees, -80.0F, 80.0F);
			const float orbitDistance = glm::length(glm::vec2(rig.thirdPersonDistance, rig.thirdPersonHeight));

			const glm::vec3 aimPoint = entity.position + glm::vec3(0.0F, rig.thirdPersonAimHeight, 0.0F);
			const glm::vec3 eye = aimPoint + yawPitchForward(orbitYawDegrees, orbitPitchDegrees) * orbitDistance;
			return cameraLookingAt(eye, aimPoint);
		}

		const glm::vec3 forward = yawPitchForward(entity.rotationEuler.y, lookPitchDegrees);
		const glm::vec3 eye = entity.position + glm::vec3(0.0F, rig.fpsEyeHeight, 0.0F);
		const glm::vec3 aimPoint = eye + forward * 10.0F;
		return cameraLookingAt(eye, aimPoint);
	}

	glm::vec3 gameCameraEye(const GameCameraState& camera)
	{
		// Mirrors ViewportRenderer::cameraPosition() exactly. Radians, not
		// degrees - see the header note.
		const glm::vec3 direction(
			std::cos(camera.pitch) * std::sin(camera.yaw),
			std::sin(camera.pitch),
			std::cos(camera.pitch) * std::cos(camera.yaw));
		return camera.target + direction * camera.distance;
	}

	void applyCameraPoseToEntity(SceneEntity& entity, const GameCameraState& camera)
	{
		entity.position = gameCameraEye(camera);
		// The view looks from the eye BACK toward the target, so the view
		// direction is the negation of the orbit direction - which is why the
		// yaw is turned around by 180 degrees rather than used as-is.
		entity.rotationEuler = glm::vec3(
			glm::degrees(camera.pitch), glm::degrees(camera.yaw) + 180.0F, 0.0F);
	}

	void syncMainCameraToPlayView(
		EditorScene& scene,
		const ScriptRuntime& scriptRuntime,
		const float lookYawDegrees,
		const float lookPitchDegrees)
	{
		if (!scriptRuntime.hasActiveCamera())
		{
			return;
		}
		const SceneEntity* followed = scene.findEntity(scriptRuntime.activeCameraEntityId());
		if (followed == nullptr)
		{
			return;
		}
		int mainCameraId = -1;
		for (const SceneEntity& candidate : scene.entities())
		{
			if (candidate.isCamera && candidate.camera.isMainCamera && candidate.active)
			{
				mainCameraId = candidate.id;
				break;
			}
		}
		if (mainCameraId < 0)
		{
			return;
		}
		const GameCameraState pose = scriptedPlayCamera(
			*followed, scriptRuntime.activeCameraMode(), lookYawDegrees, lookPitchDegrees);

		// Written directly rather than through the command bus, on purpose:
		// this is per-frame Play bookkeeping, not a user edit - the same
		// reasoning terrain sculpting and keyframe recording already use.
		// Routing 60 camera poses a second through a validated, undoable
		// command would bury the undo stack in steps nobody can act on.
		if (SceneEntity* mainCamera = scene.findEntityMutable(mainCameraId))
		{
			applyCameraPoseToEntity(*mainCamera, pose);
		}
	}
}
