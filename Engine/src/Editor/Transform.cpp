#include "GameForger/Editor/Transform.hpp"

#include <cctype>
#include <cmath>

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace gameforger::editor
{
	namespace
	{
		std::string normalizePresetName(const std::string& text)
		{
			std::string result;
			result.reserve(text.size());
			for (const char character : text)
			{
				if (std::isalpha(static_cast<unsigned char>(character)) != 0)
				{
					result += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
				}
			}
			return result;
		}
	}

	glm::mat4 composeEntityPivotFrame(const SceneEntity& entity)
	{
		const float translation[3] = {entity.position.x, entity.position.y, entity.position.z};
		const float rotation[3] = {entity.rotationEuler.x, entity.rotationEuler.y, entity.rotationEuler.z};
		const float scale[3] = {entity.scale.x, entity.scale.y, entity.scale.z};

		float matrix[16];
		ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, matrix);
		return glm::make_mat4(matrix);
	}

	glm::mat4 composeEntityTransform(const SceneEntity& entity)
	{
		const glm::mat4 trs = composeEntityPivotFrame(entity);
		if (entity.pivotOffset == glm::vec3(0.0F))
		{
			return trs;
		}
		return trs * glm::translate(glm::mat4(1.0F), -entity.pivotOffset);
	}

	OrientedBox colliderBox(const SceneEntity& entity)
	{
		const glm::mat4 model = composeEntityTransform(entity);
		OrientedBox box;
		box.center = glm::vec3(model[3]);
		for (int axis = 0; axis < 3; ++axis)
		{
			const glm::vec3 column(model[axis]);
			const float length = glm::length(column);
			box.halfExtents[axis] = length;
			box.axes[static_cast<std::size_t>(axis)] =
				length > 1e-6F ? column / length : box.axes[static_cast<std::size_t>(axis)];
		}
		box.axisAligned = std::abs(entity.rotationEuler.x) < 1e-3F && std::abs(entity.rotationEuler.y) < 1e-3F &&
			std::abs(entity.rotationEuler.z) < 1e-3F;
		if (box.axisAligned)
		{
			box.axes = {glm::vec3(1.0F, 0.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F), glm::vec3(0.0F, 0.0F, 1.0F)};
		}
		return box;
	}

	bool OrientedBox::contains(const glm::vec3& point) const noexcept
	{
		const glm::vec3 d = point - center;
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			if (std::abs(glm::dot(d, axes[axis])) > halfExtents[static_cast<int>(axis)])
			{
				return false;
			}
		}
		return true;
	}

	glm::vec3 OrientedBox::worldHalfSize() const noexcept
	{
		glm::vec3 size(0.0F);
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			size += glm::abs(axes[axis]) * halfExtents[static_cast<int>(axis)];
		}
		return size;
	}

	std::optional<glm::vec3> resolvePivotPreset(const std::string& presetName)
	{
		const std::string key = normalizePresetName(presetName);
		if (key == "center" || key == "middle" || key == "centre")
		{
			return glm::vec3(0.0F, 0.0F, 0.0F);
		}
		if (key == "left")
		{
			return glm::vec3(-1.0F, 0.0F, 0.0F);
		}
		if (key == "right")
		{
			return glm::vec3(1.0F, 0.0F, 0.0F);
		}
		if (key == "top")
		{
			return glm::vec3(0.0F, 1.0F, 0.0F);
		}
		if (key == "bottom")
		{
			return glm::vec3(0.0F, -1.0F, 0.0F);
		}
		if (key == "topleft")
		{
			return glm::vec3(-1.0F, 1.0F, 0.0F);
		}
		if (key == "topright")
		{
			return glm::vec3(1.0F, 1.0F, 0.0F);
		}
		if (key == "bottomleft")
		{
			return glm::vec3(-1.0F, -1.0F, 0.0F);
		}
		if (key == "bottomright")
		{
			return glm::vec3(1.0F, -1.0F, 0.0F);
		}
		return std::nullopt;
	}

	glm::vec3 resolvePivotPreset(const std::string& presetName, const PrimitiveType primitive)
	{
		const std::optional<glm::vec3> cubeOffset = resolvePivotPreset(presetName);
		if (!cubeOffset.has_value())
		{
			return glm::vec3(0.0F);
		}
		const glm::vec3 cube = *cubeOffset;
		const std::string key = normalizePresetName(presetName);

		// Per-primitive remapping. The cube offsets assume a local space
		// spanning [-1, +1] on every axis. Each primitive below has its
		// own bounding shape; we return the closest equivalent offset.
		switch (primitive)
		{
			case PrimitiveType::Plane:
				// Flat XZ quad at y=0. "top" doesn't have a natural face
				// on the plane itself; place the pivot above the plane
				// (the most common "raise the pivot above the visible
				// surface" intent). All other directions are unchanged
				// from the cube case (the plane still spans [-1, +1]
				// on X and Z).
				if (key == "top") return glm::vec3(0.0F, 1.0F, 0.0F);
				if (key == "bottom") return glm::vec3(0.0F, -1.0F, 0.0F);
				return cube;
			case PrimitiveType::Cylinder:
			case PrimitiveType::Cone:
			case PrimitiveType::Capsule:
				// Symmetric around Y; left/right are X-axis offsets
				// (still correct - cylinder radius is 1). top/bottom
				// are also correct (these primitives do span [-1, +1]
				// on Y). The diagonal "top-left" etc. are still
				// meaningful (top of the cap + leftmost extent).
				return cube;
			case PrimitiveType::Sphere:
				// Perfectly symmetric - the cube offsets are already
				// correct in every direction.
				return cube;
			case PrimitiveType::Cube:
			default:
				return cube;
		}
	}
}
