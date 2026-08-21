#include "GameForger/Editor/TerrainTexture.hpp"

#include <algorithm>
#include <cmath>

#include <stb_image.h>

namespace gameforger::editor
{
	namespace
	{
		float luminanceAt(const LoadedTexture& image, const int x, const int y)
		{
			const int clampedX = std::clamp(x, 0, image.width - 1);
			const int clampedY = std::clamp(y, 0, image.height - 1);
			const std::size_t index =
				(static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(image.width) +
					static_cast<std::size_t>(clampedX)) *
				4;
			const float r = static_cast<float>(image.rgba[index]) / 255.0F;
			const float g = static_cast<float>(image.rgba[index + 1]) / 255.0F;
			const float b = static_cast<float>(image.rgba[index + 2]) / 255.0F;
			return 0.299F * r + 0.587F * g + 0.114F * b;
		}

		// Nearest-neighbor downsample (same resampling technique
		// loadHeightmapImage, main.cpp, already uses for heightmap import) -
		// a no-op (returns `source` as-is) if it's already within bounds.
		LoadedTexture downsampleToMax(const LoadedTexture& source, const int maxDimension)
		{
			if (!source.success || (source.width <= maxDimension && source.height <= maxDimension))
			{
				return source;
			}
			const float scale =
				static_cast<float>(maxDimension) / static_cast<float>(std::max(source.width, source.height));
			const int targetWidth = std::max(1, static_cast<int>(static_cast<float>(source.width) * scale));
			const int targetHeight = std::max(1, static_cast<int>(static_cast<float>(source.height) * scale));

			LoadedTexture result;
			result.success = true;
			result.width = targetWidth;
			result.height = targetHeight;
			result.rgba.resize(static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 4);
			for (int y = 0; y < targetHeight; ++y)
			{
				const int sourceY = std::min(source.height - 1, y * source.height / targetHeight);
				for (int x = 0; x < targetWidth; ++x)
				{
					const int sourceX = std::min(source.width - 1, x * source.width / targetWidth);
					const std::size_t sourceIndex =
						(static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(source.width) +
							static_cast<std::size_t>(sourceX)) *
						4;
					const std::size_t destIndex =
						(static_cast<std::size_t>(y) * static_cast<std::size_t>(targetWidth) +
							static_cast<std::size_t>(x)) *
						4;
					result.rgba[destIndex] = source.rgba[sourceIndex];
					result.rgba[destIndex + 1] = source.rgba[sourceIndex + 1];
					result.rgba[destIndex + 2] = source.rgba[sourceIndex + 2];
					result.rgba[destIndex + 3] = source.rgba[sourceIndex + 3];
				}
			}
			return result;
		}

		// A bump/height map doesn't need full source resolution - capping
		// what the (unoptimized, single-threaded) per-pixel generation
		// loops below actually process avoids blocking the main thread for
		// a long time on a large real-world ground texture (many are
		// 2K-8K). Reported live as the editor "crashing"/"memory
		// saturating" while a 3048x3048 texture loaded - it was neither;
		// it was this function's O(width*height) loop running twice
		// (normal + height) at full resolution on the main thread, far
		// worse in an unoptimized Debug build (no inlining, bounds-checked
		// iterators) than it would be in Release.
		constexpr int kMaxGenerationDimension = 1024;
	}

	LoadedTexture loadTextureImage(const std::filesystem::path& filePath)
	{
		int width = 0;
		int height = 0;
		int channelsInFile = 0;
		unsigned char* pixels =
			stbi_load(filePath.string().c_str(), &width, &height, &channelsInFile, 4);
		if (pixels == nullptr || width <= 0 || height <= 0)
		{
			return {};
		}

		LoadedTexture result;
		result.success = true;
		result.width = width;
		result.height = height;
		result.rgba.assign(pixels, pixels + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4));
		stbi_image_free(pixels);
		return result;
	}

	LoadedTexture generateNormalMapFromDiffuse(const LoadedTexture& sourceDiffuse, const float strength)
	{
		if (!sourceDiffuse.success)
		{
			return {};
		}
		const LoadedTexture diffuse = downsampleToMax(sourceDiffuse, kMaxGenerationDimension);

		LoadedTexture result;
		result.success = true;
		result.width = diffuse.width;
		result.height = diffuse.height;
		result.rgba.resize(static_cast<std::size_t>(diffuse.width) * static_cast<std::size_t>(diffuse.height) * 4);

		for (int y = 0; y < diffuse.height; ++y)
		{
			for (int x = 0; x < diffuse.width; ++x)
			{
				// Central-difference slope in each axis, from the diffuse
				// image's own brightness treated as a height field - the
				// same "grayscale as height" idea the terrain sculpt tool's
				// Perlin/heightmap-import paths already use.
				const float left = luminanceAt(diffuse, x - 1, y);
				const float right = luminanceAt(diffuse, x + 1, y);
				const float down = luminanceAt(diffuse, x, y + 1);
				const float up = luminanceAt(diffuse, x, y - 1);
				const float dx = (right - left) * strength;
				const float dy = (down - up) * strength;

				float nx = -dx;
				float ny = -dy;
				float nz = 1.0F;
				const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
				if (length > 0.0F)
				{
					nx /= length;
					ny /= length;
					nz /= length;
				}

				// Standard tangent-space normal map encoding (RGB = xyz*0.5+0.5)
				// - matches how a real authored normal map would be packed, so
				// swapping this auto-generated fallback for a real uploaded
				// texture later needs no shader changes.
				const std::size_t index =
					(static_cast<std::size_t>(y) * static_cast<std::size_t>(diffuse.width) +
						static_cast<std::size_t>(x)) *
					4;
				result.rgba[index] = static_cast<unsigned char>(std::clamp((nx * 0.5F + 0.5F) * 255.0F, 0.0F, 255.0F));
				result.rgba[index + 1] =
					static_cast<unsigned char>(std::clamp((ny * 0.5F + 0.5F) * 255.0F, 0.0F, 255.0F));
				result.rgba[index + 2] =
					static_cast<unsigned char>(std::clamp((nz * 0.5F + 0.5F) * 255.0F, 0.0F, 255.0F));
				result.rgba[index + 3] = 255;
			}
		}
		return result;
	}

	LoadedTexture generateHeightMapFromDiffuse(const LoadedTexture& sourceDiffuse)
	{
		if (!sourceDiffuse.success)
		{
			return {};
		}
		const LoadedTexture diffuse = downsampleToMax(sourceDiffuse, kMaxGenerationDimension);

		LoadedTexture result;
		result.success = true;
		result.width = diffuse.width;
		result.height = diffuse.height;
		result.rgba.resize(static_cast<std::size_t>(diffuse.width) * static_cast<std::size_t>(diffuse.height) * 4);

		for (int y = 0; y < diffuse.height; ++y)
		{
			for (int x = 0; x < diffuse.width; ++x)
			{
				const float height = luminanceAt(diffuse, x, y);
				const auto channel = static_cast<unsigned char>(std::clamp(height * 255.0F, 0.0F, 255.0F));
				const std::size_t index =
					(static_cast<std::size_t>(y) * static_cast<std::size_t>(diffuse.width) +
						static_cast<std::size_t>(x)) *
					4;
				result.rgba[index] = channel;
				result.rgba[index + 1] = channel;
				result.rgba[index + 2] = channel;
				result.rgba[index + 3] = 255;
			}
		}
		return result;
	}
}
