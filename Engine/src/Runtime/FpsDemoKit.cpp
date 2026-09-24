#include "GameForger/Runtime/FpsDemoKit.hpp"

#include <system_error>

namespace gameforger::editor
{
	namespace
	{
		bool hasKit(const std::filesystem::path& root)
		{
			std::error_code error;
			return std::filesystem::exists(root / fpsdemo::kPlayer, error) &&
				std::filesystem::exists(root / fpsdemo::kIconFolder, error);
		}
	}

	FpsDemoKitImportResult importFpsDemoKit(
		const std::filesystem::path& kitSourceRoot, const std::filesystem::path& projectRoot, const bool overwrite)
	{
		FpsDemoKitImportResult result;
		if (!hasKit(kitSourceRoot))
		{
			result.message = "No FPS Demo kit found in " + kitSourceRoot.string();
			return result;
		}
		std::error_code error;
		if (std::filesystem::equivalent(kitSourceRoot, projectRoot, error))
		{
			result.success = true;
			result.message = "This project is where the FPS Demo kit comes from - nothing to import.";
			return result;
		}
		for (const char* folder : {fpsdemo::kScriptFolder, fpsdemo::kIconFolder})
		{
			const std::filesystem::path source = kitSourceRoot / folder;
			const std::filesystem::path destination = projectRoot / folder;
			std::filesystem::create_directories(destination, error);
			if (error)
			{
				result.message = "Could not create " + destination.string() + ": " + error.message();
				return result;
			}
			for (const auto& entry : std::filesystem::recursive_directory_iterator(source, error))
			{
				if (!entry.is_regular_file())
				{
					continue;
				}
				const std::filesystem::path target = destination / std::filesystem::relative(entry.path(), source, error);
				std::filesystem::create_directories(target.parent_path(), error);
				if (!overwrite && std::filesystem::exists(target, error))
				{
					++result.filesSkipped;
					continue;
				}
				std::filesystem::copy_file(
					entry.path(), target, std::filesystem::copy_options::overwrite_existing, error);
				if (error)
				{
					result.message = "Could not copy " + entry.path().filename().string() + ": " + error.message();
					return result;
				}
				++result.filesCopied;
			}
		}
		result.success = true;
		result.message = "FPS Demo kit imported: " + std::to_string(result.filesCopied) + " file(s) copied" +
			(result.filesSkipped > 0 ? ", " + std::to_string(result.filesSkipped) + " already there (kept)" : "") + ".";
		return result;
	}

	bool fpsDemoKitInstalled(const std::filesystem::path& projectRoot)
	{
		std::error_code error;
		for (const char* script : {fpsdemo::kPlayer, fpsdemo::kProjectiles, fpsdemo::kEffects, fpsdemo::kXpSystem,
				 fpsdemo::kHealth, fpsdemo::kItems, fpsdemo::kEnemy, fpsdemo::kGameManager})
		{
			if (!std::filesystem::exists(projectRoot / script, error))
			{
				return false;
			}
		}
		return true;
	}

	std::optional<std::filesystem::path> findFpsDemoKitSource(const std::filesystem::path& executableDirectory)
	{
		if (hasKit(executableDirectory / "Kits"))
		{
			return executableDirectory / "Kits";
		}
		for (std::filesystem::path folder = executableDirectory; !folder.empty(); folder = folder.parent_path())
		{
			if (hasKit(folder))
			{
				return folder;
			}
			if (folder == folder.parent_path())
			{
				break;
			}
		}
		return std::nullopt;
	}
}
