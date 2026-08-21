#include "GameForger/Core/Engine.hpp"

namespace gameforger
{
    std::string_view Engine::name() noexcept
    {
        return "GameForgerAI";
    }

    std::string_view Engine::version() noexcept
    {
        return "0.1.0";
    }
}
