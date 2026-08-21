#pragma once

#include <string_view>

namespace gameforger
{
    class Engine final
    {
    public:
        [[nodiscard]] static std::string_view name() noexcept;
        [[nodiscard]] static std::string_view version() noexcept;
    };
}
