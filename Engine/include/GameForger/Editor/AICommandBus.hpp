#pragma once

#include <functional>
#include <string>

#include "GameForger/Editor/AICommand.hpp"

namespace gameforger::editor
{
	struct AICommandResult
	{
		bool success = false;
		bool preview = false;
		std::string message;
	};

	class AICommandValidator final
	{
	public:
		[[nodiscard]] static AICommandResult validate(const AIEditorCommand& command);
	};

	class AICommandBus final
	{
	public:
		using Handler = std::function<AICommandResult(const AIEditorCommand&)>;

		void setHandler(Handler handler);
		[[nodiscard]] AICommandResult preview(const AIEditorCommand& command) const;
		[[nodiscard]] AICommandResult execute(const AIEditorCommand& command) const;

	private:
		Handler handler_;
	};
}