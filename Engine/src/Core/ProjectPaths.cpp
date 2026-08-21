#include "GameForger/Core/ProjectPaths.hpp"

#include <algorithm>
#include <cctype>

namespace gameforger::core
{
    namespace
    {
        std::string toLowerAscii(std::string text)
        {
            std::transform(
                text.begin(), text.end(), text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        // True if `outer` is `inner` or a lexical ancestor of it, compared
        // component-by-component and case-insensitively (Windows paths are
        // case-insensitive) - NOT a string prefix check, which would let
        // "Game/ScriptsEvil" pass a boundary meant for "Game/Scripts".
        bool contains(const std::filesystem::path& outer, const std::filesystem::path& inner)
        {
            auto outerIt = outer.begin();
            auto innerIt = inner.begin();
            for (; outerIt != outer.end(); ++outerIt, ++innerIt)
            {
                if (innerIt == inner.end() ||
                    toLowerAscii(outerIt->generic_string()) != toLowerAscii(innerIt->generic_string()))
                {
                    return false;
                }
            }
            return true;
        }
    }

    std::optional<std::filesystem::path> resolveProjectFile(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& relativePath,
        const std::filesystem::path& requiredSubdirectory,
        const std::vector<std::string>& allowedExtensions)
    {
        if (relativePath.empty() || relativePath.is_absolute())
        {
            return std::nullopt;
        }

        std::error_code error;
        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(projectRoot, error);
        if (error)
        {
            return std::nullopt;
        }

        const std::filesystem::path requiredDirectory =
            std::filesystem::weakly_canonical(canonicalRoot / requiredSubdirectory, error);
        if (error || !contains(canonicalRoot, requiredDirectory))
        {
            return std::nullopt;
        }

        const std::filesystem::path candidate = std::filesystem::weakly_canonical(canonicalRoot / relativePath, error);
        if (error || !contains(requiredDirectory, candidate))
        {
            return std::nullopt;
        }

        if (!allowedExtensions.empty())
        {
            const std::string candidateExtension = toLowerAscii(candidate.extension().string());
            const bool matchesAllowedExtension = std::any_of(
                allowedExtensions.begin(),
                allowedExtensions.end(),
                [&candidateExtension](const std::string& allowed) { return toLowerAscii(allowed) == candidateExtension; });
            if (!matchesAllowedExtension)
            {
                return std::nullopt;
            }
        }

        return candidate;
    }
}
