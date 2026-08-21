#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace
{
	// Flip on load so OpenGL's glTexImage2D (which treats the first byte as the
	// bottom row) matches the source art. Without this every STB-loaded
	// texture - terrain splats, normal/height maps, splash screen, the ImGui
	// font atlas - samples upside-down versus its source image. The flag is
	// process-global on stb_image, so setting it once in this TU (which owns
	// the STB_IMAGE_IMPLEMENTATION definition) covers every consumer.
	struct StbImageVerticalFlip
	{
		StbImageVerticalFlip() noexcept
		{
			stbi_set_flip_vertically_on_load(1);
		}
	};
	[[maybe_unused]] const StbImageVerticalFlip g_stbImageVerticalFlip{};
}
