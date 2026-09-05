#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gameforger::editor::json
{
	class Value
	{
	public:
		enum class Type
		{
			Null,
			Boolean,
			Number,
			String,
			Array,
			Object
		};

		Type type = Type::Null;
		bool boolValue = false;
		double numberValue = 0.0;
		std::string stringValue;
		std::vector<Value> arrayValue;
		std::vector<std::pair<std::string, Value>> objectValue;

		[[nodiscard]] const Value* find(const std::string& key) const noexcept;
		[[nodiscard]] std::optional<std::string> asString() const;
		[[nodiscard]] std::optional<double> asNumber() const;
		[[nodiscard]] std::optional<bool> asBool() const;
		[[nodiscard]] std::optional<std::vector<double>> asNumberArray() const;
	};

	// Parses a single JSON value from `text`. Returns std::nullopt on malformed input.
	[[nodiscard]] std::optional<Value> parse(const std::string& text);

	// Phase C: serialise a Value tree back to a compact JSON string. No
	// pretty-printing; the AI Cockpit only needs to send tool arguments,
	// which stay in-memory strings only briefly. Escaping matches parse().
	[[nodiscard]] std::string serialize(const Value& value);

	// Small builder helpers so callsites don't have to fiddle with Value's
	// tagged-union fields directly.
	[[nodiscard]] Value makeString(std::string text);
	[[nodiscard]] Value makeNumber(double n);
	[[nodiscard]] Value makeBool(bool b);
	[[nodiscard]] Value makeNull();
	[[nodiscard]] Value makeArray(std::vector<Value> items = {});
	[[nodiscard]] Value makeObject(std::vector<std::pair<std::string, Value>> entries = {});
}
