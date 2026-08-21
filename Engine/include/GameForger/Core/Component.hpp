#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include "GameForger/Editor/AICommand.hpp"

namespace gameforger::core
{
	class GameObject;

	enum class ComponentType
	{
		Transform,
		MeshFilter,
		MeshRenderer,
		Collider,
		Light,
		Camera,
		AudioSource,
		Script,
		Terrain,
		Custom
	};

	class Component
	{
	public:
		virtual ~Component() = default;

		[[nodiscard]] virtual ComponentType componentType() const noexcept = 0;
		[[nodiscard]] virtual std::string_view typeName() const noexcept = 0;

		[[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
		void setEnabled(const bool enabled) noexcept { enabled_ = enabled; }

		[[nodiscard]] GameObject* gameObject() const noexcept { return owner_; }
		void setGameObject(GameObject* owner) noexcept { owner_ = owner; }

	private:
		GameObject* owner_ = nullptr;
		bool enabled_ = true;
	};

	// ------------------------------------------------------------------------
	// TransformComponent
	// ------------------------------------------------------------------------
	class TransformComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::Transform; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "Transform"; }

		glm::vec3 position{0.0F};
		glm::vec3 rotationEuler{0.0F};
		glm::vec3 scale{1.0F};
		glm::vec3 pivotOffset{0.0F};

		std::string parentName;
		glm::vec3 localPosition{0.0F};
		glm::vec3 localRotationEuler{0.0F};
		glm::vec3 localScale{1.0F};
	};

	// ------------------------------------------------------------------------
	// MeshFilterComponent
	// ------------------------------------------------------------------------
	class MeshFilterComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::MeshFilter; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "MeshFilter"; }

		editor::PrimitiveType primitive = editor::PrimitiveType::Cube;
		std::string meshAssetPath;
		bool isImportedMesh = false;
	};

	// ------------------------------------------------------------------------
	// MeshRendererComponent
	// ------------------------------------------------------------------------
	class MeshRendererComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::MeshRenderer; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "MeshRenderer"; }

		glm::vec3 color{0.8F, 0.8F, 0.8F};
		std::string materialAssetPath; // Reference to .gfmat
		std::string diffuseTexturePath;
		std::string normalMapPath;
		std::string roughnessMapPath;
		glm::vec2 uvScale{1.0F, 1.0F};
		glm::vec3 materialBlendWeight{0.0F};
		bool castShadows = true;
		bool receiveShadows = true;
	};

	// ------------------------------------------------------------------------
	// ColliderComponent
	// ------------------------------------------------------------------------
	enum class ColliderShape
	{
		Box,
		Sphere,
		Capsule,
		Mesh
	};

	class ColliderComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::Collider; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "Collider"; }

		ColliderShape shape = ColliderShape::Box;
		glm::vec3 center{0.0F};
		glm::vec3 size{1.0F};
		float radius = 0.5F;
		float height = 2.0F;
		bool isTrigger = false;
		bool autoGroundCheck = false;
	};

	// ------------------------------------------------------------------------
	// LightComponent
	// ------------------------------------------------------------------------
	enum class LightType
	{
		Directional,
		Point,
		Spot
	};

	class LightComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::Light; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "Light"; }

		LightType lightType = LightType::Directional;
		glm::vec3 color{1.0F, 1.0F, 1.0F};
		float intensity = 1.0F;
		float range = 10.0F;
		float spotAngle = 45.0F;
		bool castShadows = true;
		float shadowBias = 0.005F;
	};

	// ------------------------------------------------------------------------
	// CameraComponent
	// ------------------------------------------------------------------------
	class CameraComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::Camera; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "Camera"; }

		float fieldOfView = 60.0F;
		float nearClipPlane = 0.1F;
		float farClipPlane = 1000.0F;
		bool isOrthographic = false;
		float orthographicSize = 5.0F;
		glm::vec4 clearColor{0.15F, 0.15F, 0.18F, 1.0F};
		int priority = 0;
		bool isMain = false;
	};

	// ------------------------------------------------------------------------
	// AudioSourceComponent
	// ------------------------------------------------------------------------
	class AudioSourceComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::AudioSource; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "AudioSource"; }

		std::string clipAssetPath;
		float volume = 1.0F;
		float pitch = 1.0F;
		bool loop = false;
		bool playOnAwake = true;
		bool is3D = true;
		float minDistance = 1.0F;
		float maxDistance = 50.0F;
	};

	// ------------------------------------------------------------------------
	// ScriptComponent
	// ------------------------------------------------------------------------
	class ScriptComponent final : public Component
	{
	public:
		[[nodiscard]] ComponentType componentType() const noexcept override { return ComponentType::Script; }
		[[nodiscard]] std::string_view typeName() const noexcept override { return "Script"; }

		std::string scriptPath;
		std::unordered_map<std::string, float> numberProperties;
		std::unordered_map<std::string, std::string> stringProperties;
		std::unordered_map<std::string, bool> boolProperties;
	};
}
