#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gameforger::core
{
    // Resolves `relativePath` against `projectRoot`, requiring the result to
    // land inside `projectRoot / requiredSubdirectory` and, if
    // `allowedExtensions` is non-empty, have one of those extensions
    // (case-insensitive, each entry including its leading dot, e.g. ".lua").
    // Returns the resolved absolute path on success, or std::nullopt if
    // `relativePath` is empty, absolute, or escapes the required directory
    // via `..` or any other traversal.
    //
    // This is the single project-boundary check every asset/script path
    // lookup in this codebase should go through, rather than each call site
    // inventing its own (a prior AttachScriptCommand bug let scripts attach
    // from anywhere on disk because it only checked file existence, never
    // confinement).
    [[nodiscard]] std::optional<std::filesystem::path> resolveProjectFile(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& relativePath,
        const std::filesystem::path& requiredSubdirectory,
        const std::vector<std::string>& allowedExtensions = {});
}
