#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gameforger::core
{
	struct AssetMetadata
	{
		std::string guid;
		std::filesystem::path path;
		std::string assetType;
		std::uint64_t lastWriteTime = 0;
	};

	class AssetDatabase final
	{
	public:
		AssetDatabase() = default;
		explicit AssetDatabase(std::filesystem::path projectRoot);

		// Scans the project directory (e.g. Game/), creates missing .meta files,
		// and rebuilds the two-way GUID <-> relative path cache.
		void refresh();
		void setProjectRoot(std::filesystem::path projectRoot);

		[[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }

		// Path to GUID resolution (creates .meta if missing)
		[[nodiscard]] std::optional<std::string> getGuidFromPath(const std::filesystem::path& relativePath);

		// GUID to relative path resolution
		[[nodiscard]] std::optional<std::filesystem::path> getPathFromGuid(const std::string& guid) const;

		// Returns all currently tracked asset metadatas
		[[nodiscard]] const std::unordered_map<std::string, AssetMetadata>& allAssets() const noexcept
		{
			return assetsByGuid_;
		}

		// Helper to generate a new 128-bit UUID/GUID hex string
		[[nodiscard]] static std::string generateGuid();

		// Reads or writes a sidecar .meta file
		[[nodiscard]] static std::optional<std::string> readGuidFromMeta(const std::filesystem::path& metaFilePath);
		static bool writeMetaFile(const std::filesystem::path& metaFilePath, const std::string& guid, const std::string& assetType = "Asset");

	private:
		std::filesystem::path projectRoot_;
		std::unordered_map<std::string, AssetMetadata> assetsByGuid_;
		std::unordered_map<std::string, std::string> guidByRelativePath_;

		void scanDirectory(const std::filesystem::path& directory);
	};
}
