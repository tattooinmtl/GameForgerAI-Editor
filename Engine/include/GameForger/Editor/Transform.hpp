#pragma once

#include <optional>
#include <string>

#include <glm/mat4x4.hpp>

#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	// Model matrix used for rendering and picking: position/rotation/scale with
	// the entity's pivotOffset baked in, so the mesh visually rotates/scales
	// around that point (e.g. a door's hinge) instead of its geometric center.
	[[nodiscard]] glm::mat4 composeEntityTransform(const SceneEntity& entity);

	// Plain position/rotation/scale matrix, with NO pivot offset applied — the
	// pivot point's own world frame. Feed this (not composeEntityTransform) to
	// ImGuizmo::Manipulate so the gizmo handles sit at the pivot, matching what
	// the user is actually dragging.
	[[nodiscard]] glm::mat4 composeEntityPivotFrame(const SceneEntity& entity);

	// Maps preset names ("center", "left", "top-right", ...) to a pivotOffset in
	// the primitive's [-1,1] local space. Returns std::nullopt for unknown names.
	[[nodiscard]] std::optional<glm::vec3> resolvePivotPreset(const std::string& presetName);
	// Per-primitive preset variant. Some presets mean different things for
	// different primitives - "top" for a Cylinder is its top cap, for a
	// Plane is above the flat quad, for a Cube is the +Y face. The
	// primitive-aware overload returns offsets that match the visual
	// "where is the top of this thing?" rather than assuming every
	// primitive is cube-shaped. Falls back to the cube-shaped result if
	// the primitive is unrecognized.
	[[nodiscard]] glm::vec3 resolvePivotPreset(
		const std::string& presetName,
		PrimitiveType primitive);
}
