#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace gameforger::core
{
	enum class MaterialBlendMode
	{
		Opaque,
		Cutout,
		Transparent
	};

	struct Material
	{
		std::string name = "New Material";
		std::string shader = "StandardPBR";
		MaterialBlendMode blendMode = MaterialBlendMode::Opaque;

		glm::vec4 albedoColor{1.0F, 1.0F, 1.0F, 1.0F};
		std::string albedoMapGuid;

		std::string normalMapGuid;
		float normalScale = 1.0F;

		float metallic = 0.0F;
		float roughness = 0.5F;
		std::string metallicRoughnessMapGuid;

		glm::vec3 emissionColor{0.0F, 0.0F, 0.0F};
		float emissionIntensity = 1.0F;

		glm::vec2 uvScale{1.0F, 1.0F};
		glm::vec2 uvOffset{0.0F, 0.0F};

		// Triplanar specific
		glm::vec3 triplanarBlendWeight{0.0F, 0.0F, 0.0F};
	};

	// Serializes a Material to .gfmat JSON format
	[[nodiscard]] std::string serializeMaterial(const Material& material);

	// Deserializes a Material from .gfmat JSON string
	[[nodiscard]] std::optional<Material> deserializeMaterial(const std::string& jsonText);

	// File I/O helpers
	[[nodiscard]] bool saveMaterialFile(const std::filesystem::path& filePath, const Material& material);
	[[nodiscard]] std::optional<Material> loadMaterialFile(const std::filesystem::path& filePath);
}
