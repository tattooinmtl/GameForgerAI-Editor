#include "GameForger/Core/Material.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "GameForger/Editor/Json.hpp"

namespace gameforger::core
{
	namespace
	{
		std::string blendModeToString(const MaterialBlendMode mode)
		{
			switch (mode)
			{
				case MaterialBlendMode::Cutout: return "Cutout";
				case MaterialBlendMode::Transparent: return "Transparent";
				case MaterialBlendMode::Opaque:
				default: return "Opaque";
			}
		}

		MaterialBlendMode blendModeFromString(const std::string& str)
		{
			if (str == "Cutout") return MaterialBlendMode::Cutout;
			if (str == "Transparent") return MaterialBlendMode::Transparent;
			return MaterialBlendMode::Opaque;
		}

		glm::vec4 readVec4(const editor::json::Value* obj, const std::string& key, const glm::vec4& fallback)
		{
			if (obj == nullptr) return fallback;
			const auto* field = obj->find(key);
			if (field == nullptr || field->type != editor::json::Value::Type::Array || field->arrayValue.size() < 4)
			{
				return fallback;
			}
			return glm::vec4(
				static_cast<float>(field->arrayValue[0].numberValue),
				static_cast<float>(field->arrayValue[1].numberValue),
				static_cast<float>(field->arrayValue[2].numberValue),
				static_cast<float>(field->arrayValue[3].numberValue));
		}

		glm::vec3 readVec3(const editor::json::Value* obj, const std::string& key, const glm::vec3& fallback)
		{
			if (obj == nullptr) return fallback;
			const auto* field = obj->find(key);
			if (field == nullptr || field->type != editor::json::Value::Type::Array || field->arrayValue.size() < 3)
			{
				return fallback;
			}
			return glm::vec3(
				static_cast<float>(field->arrayValue[0].numberValue),
				static_cast<float>(field->arrayValue[1].numberValue),
				static_cast<float>(field->arrayValue[2].numberValue));
		}

		glm::vec2 readVec2(const editor::json::Value* obj, const std::string& key, const glm::vec2& fallback)
		{
			if (obj == nullptr) return fallback;
			const auto* field = obj->find(key);
			if (field == nullptr || field->type != editor::json::Value::Type::Array || field->arrayValue.size() < 2)
			{
				return fallback;
			}
			return glm::vec2(
				static_cast<float>(field->arrayValue[0].numberValue),
				static_cast<float>(field->arrayValue[1].numberValue));
		}

		float readFloat(const editor::json::Value* obj, const std::string& key, const float fallback)
		{
			if (obj == nullptr) return fallback;
			const auto* field = obj->find(key);
			if (field == nullptr || field->type != editor::json::Value::Type::Number)
			{
				return fallback;
			}
			return static_cast<float>(field->numberValue);
		}

		std::string readString(const editor::json::Value* obj, const std::string& key, const std::string& fallback = "")
		{
			if (obj == nullptr) return fallback;
			const auto* field = obj->find(key);
			if (field == nullptr || field->type != editor::json::Value::Type::String)
			{
				return fallback;
			}
			return field->stringValue;
		}
	}

	std::string serializeMaterial(const Material& material)
	{
		std::ostringstream ss;
		ss << std::fixed << std::setprecision(4);
		ss << "{\n";
		ss << "  \"format\": \"GameForgerMaterial\",\n";
		ss << "  \"version\": 1,\n";
		ss << "  \"name\": \"" << material.name << "\",\n";
		ss << "  \"shader\": \"" << material.shader << "\",\n";
		ss << "  \"blendMode\": \"" << blendModeToString(material.blendMode) << "\",\n";
		ss << "  \"albedoColor\": [" << material.albedoColor.r << ", " << material.albedoColor.g << ", "
		   << material.albedoColor.b << ", " << material.albedoColor.a << "],\n";
		ss << "  \"albedoMapGuid\": \"" << material.albedoMapGuid << "\",\n";
		ss << "  \"normalMapGuid\": \"" << material.normalMapGuid << "\",\n";
		ss << "  \"normalScale\": " << material.normalScale << ",\n";
		ss << "  \"metallic\": " << material.metallic << ",\n";
		ss << "  \"roughness\": " << material.roughness << ",\n";
		ss << "  \"metallicRoughnessMapGuid\": \"" << material.metallicRoughnessMapGuid << "\",\n";
		ss << "  \"emissionColor\": [" << material.emissionColor.r << ", " << material.emissionColor.g << ", "
		   << material.emissionColor.b << "],\n";
		ss << "  \"emissionIntensity\": " << material.emissionIntensity << ",\n";
		ss << "  \"uvScale\": [" << material.uvScale.x << ", " << material.uvScale.y << "],\n";
		ss << "  \"uvOffset\": [" << material.uvOffset.x << ", " << material.uvOffset.y << "],\n";
		ss << "  \"triplanarBlendWeight\": [" << material.triplanarBlendWeight.x << ", "
		   << material.triplanarBlendWeight.y << ", " << material.triplanarBlendWeight.z << "]\n";
		ss << "}\n";

		return ss.str();
	}

	std::optional<Material> deserializeMaterial(const std::string& jsonText)
	{
		const auto parsed = editor::json::parse(jsonText);
		if (!parsed.has_value() || parsed->type != editor::json::Value::Type::Object)
		{
			return std::nullopt;
		}

		const auto* formatField = parsed->find("format");
		if (formatField == nullptr || formatField->type != editor::json::Value::Type::String ||
			formatField->stringValue != "GameForgerMaterial")
		{
			return std::nullopt;
		}

		Material mat;
		mat.name = readString(&*parsed, "name", "New Material");
		mat.shader = readString(&*parsed, "shader", "StandardPBR");
		mat.blendMode = blendModeFromString(readString(&*parsed, "blendMode", "Opaque"));

		mat.albedoColor = readVec4(&*parsed, "albedoColor", glm::vec4(1.0F));
		mat.albedoMapGuid = readString(&*parsed, "albedoMapGuid");

		mat.normalMapGuid = readString(&*parsed, "normalMapGuid");
		mat.normalScale = readFloat(&*parsed, "normalScale", 1.0F);

		mat.metallic = readFloat(&*parsed, "metallic", 0.0F);
		mat.roughness = readFloat(&*parsed, "roughness", 0.5F);
		mat.metallicRoughnessMapGuid = readString(&*parsed, "metallicRoughnessMapGuid");

		mat.emissionColor = readVec3(&*parsed, "emissionColor", glm::vec3(0.0F));
		mat.emissionIntensity = readFloat(&*parsed, "emissionIntensity", 1.0F);

		mat.uvScale = readVec2(&*parsed, "uvScale", glm::vec2(1.0F));
		mat.uvOffset = readVec2(&*parsed, "uvOffset", glm::vec2(0.0F));

		mat.triplanarBlendWeight = readVec3(&*parsed, "triplanarBlendWeight", glm::vec3(0.0F));

		return mat;
	}

	bool saveMaterialFile(const std::filesystem::path& filePath, const Material& material)
	{
		std::error_code ec;
		if (filePath.has_parent_path() && !filePath.parent_path().empty())
		{
			std::filesystem::create_directories(filePath.parent_path(), ec);
		}

		std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return false;
		}

		file << serializeMaterial(material);
		return file.good();
	}

	std::optional<Material> loadMaterialFile(const std::filesystem::path& filePath)
	{
		std::ifstream file(filePath, std::ios::binary);
		if (!file)
		{
			return std::nullopt;
		}

		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return deserializeMaterial(text);
	}
}
