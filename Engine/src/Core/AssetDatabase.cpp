#include "GameForger/Core/AssetDatabase.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

#include "GameForger/Editor/Json.hpp"

#if defined(_WIN32)
#include <objbase.h>
#endif

namespace gameforger::core
{
	AssetDatabase::AssetDatabase(std::filesystem::path projectRoot)
		: projectRoot_(std::filesystem::weakly_canonical(std::move(projectRoot)))
	{
		refresh();
	}

	void AssetDatabase::setProjectRoot(std::filesystem::path projectRoot)
	{
		projectRoot_ = std::filesystem::weakly_canonical(std::move(projectRoot));
		refresh();
	}

	std::string AssetDatabase::generateGuid()
	{
#if defined(_WIN32)
		GUID guid;
		if (CoCreateGuid(&guid) == S_OK)
		{
			std::ostringstream ss;
			ss << std::hex << std::setfill('0');
			ss << std::setw(8) << guid.Data1;
			ss << std::setw(4) << guid.Data2;
			ss << std::setw(4) << guid.Data3;
			for (int i = 0; i < 8; ++i)
			{
				ss << std::setw(2) << static_cast<int>(guid.Data4[i]);
			}
			return ss.str();
		}
#endif
		// Fallback UUID generation
		static std::random_device rd;
		static std::mt19937_64 gen(rd());
		static std::uniform_int_distribution<std::uint64_t> dis;

		const std::uint64_t part1 = dis(gen);
		const std::uint64_t part2 = dis(gen);

		std::ostringstream ss;
		ss << std::hex << std::setfill('0');
		ss << std::setw(16) << part1;
		ss << std::setw(16) << part2;
		return ss.str();
	}

	std::optional<std::string> AssetDatabase::readGuidFromMeta(const std::filesystem::path& metaFilePath)
	{
		std::ifstream file(metaFilePath, std::ios::binary);
		if (!file)
		{
			return std::nullopt;
		}

		std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const auto parsed = editor::json::parse(content);
		if (!parsed.has_value() || parsed->type != editor::json::Value::Type::Object)
		{
			return std::nullopt;
		}

		const auto* guidVal = parsed->find("guid");
		if (guidVal != nullptr && guidVal->type == editor::json::Value::Type::String && !guidVal->stringValue.empty())
		{
			return guidVal->stringValue;
		}

		return std::nullopt;
	}

	bool AssetDatabase::writeMetaFile(
		const std::filesystem::path& metaFilePath, const std::string& guid, const std::string& assetType)
	{
		std::error_code ec;
		if (metaFilePath.has_parent_path() && !metaFilePath.parent_path().empty())
		{
			std::filesystem::create_directories(metaFilePath.parent_path(), ec);
		}

		std::ofstream file(metaFilePath, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return false;
		}

		file << "{\n";
		file << "  \"guid\": \"" << guid << "\",\n";
		file << "  \"assetType\": \"" << assetType << "\"\n";
		file << "}\n";

		return file.good();
	}

	void AssetDatabase::scanDirectory(const std::filesystem::path& directory)
	{
		std::error_code ec;
		if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec))
		{
			return;
		}

		for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec))
		{
			if (ec) break;
			if (!entry.is_regular_file(ec))
			{
				continue;
			}

			const std::filesystem::path path = entry.path();
			if (path.extension() == ".meta")
			{
				continue; // Skip meta sidecars themselves
			}

			const std::filesystem::path relativePath = std::filesystem::relative(path, projectRoot_, ec);
			if (ec) continue;

			const std::filesystem::path metaPath = path.string() + ".meta";
			std::optional<std::string> guid = readGuidFromMeta(metaPath);

			if (!guid.has_value())
			{
				guid = generateGuid();
				writeMetaFile(metaPath, *guid);
			}

			const std::string genericRelPath = relativePath.generic_string();
			AssetMetadata metadata;
			metadata.guid = *guid;
			metadata.path = relativePath;
			metadata.assetType = path.extension().string();

			assetsByGuid_[*guid] = metadata;
			guidByRelativePath_[genericRelPath] = *guid;
		}
	}

	void AssetDatabase::refresh()
	{
		assetsByGuid_.clear();
		guidByRelativePath_.clear();

		if (projectRoot_.empty())
		{
			return;
		}

		const std::filesystem::path gameDir = projectRoot_ / "Game";
		std::error_code ec;
		if (std::filesystem::exists(gameDir, ec))
		{
			scanDirectory(gameDir);
		}
		else
		{
			scanDirectory(projectRoot_);
		}
	}

	std::optional<std::string> AssetDatabase::getGuidFromPath(const std::filesystem::path& relativePath)
	{
		const std::string genericPath = relativePath.generic_string();
		const auto it = guidByRelativePath_.find(genericPath);
		if (it != guidByRelativePath_.end())
		{
			return it->second;
		}

		const std::filesystem::path fullPath = projectRoot_ / relativePath;
		std::error_code ec;
		if (!std::filesystem::exists(fullPath, ec) || !std::filesystem::is_regular_file(fullPath, ec))
		{
			return std::nullopt;
		}

		const std::filesystem::path metaPath = fullPath.string() + ".meta";
		std::optional<std::string> guid = readGuidFromMeta(metaPath);
		if (!guid.has_value())
		{
			guid = generateGuid();
			writeMetaFile(metaPath, *guid);
		}

		AssetMetadata metadata;
		metadata.guid = *guid;
		metadata.path = relativePath;
		metadata.assetType = fullPath.extension().string();

		assetsByGuid_[*guid] = metadata;
		guidByRelativePath_[genericPath] = *guid;

		return *guid;
	}

	std::optional<std::filesystem::path> AssetDatabase::getPathFromGuid(const std::string& guid) const
	{
		const auto it = assetsByGuid_.find(guid);
		if (it != assetsByGuid_.end())
		{
			return it->second.path;
		}
		return std::nullopt;
	}
}
