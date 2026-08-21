#include "GameForger/Editor/Json.hpp"

#include <cctype>
#include <cstdlib>
#include <string_view>

namespace gameforger::editor::json
{
	namespace
	{
		void appendUtf8(std::string& out, const unsigned int codepoint)
		{
			if (codepoint < 0x80)
			{
				out += static_cast<char>(codepoint);
			}
			else if (codepoint < 0x800)
			{
				out += static_cast<char>(0xC0 | (codepoint >> 6));
				out += static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xE0 | (codepoint >> 12));
				out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codepoint & 0x3F));
			}
		}

		class Parser
		{
		public:
			explicit Parser(const std::string& text) : text_(text)
			{
			}

			[[nodiscard]] std::optional<Value> parseDocument()
			{
				skipWhitespace();
				std::optional<Value> value = parseValue();
				if (!value.has_value())
				{
					return std::nullopt;
				}
				skipWhitespace();
				if (!atEnd())
				{
					return std::nullopt;
				}
				return value;
			}

		private:
			const std::string& text_;
			std::size_t pos_ = 0;

			[[nodiscard]] bool atEnd() const noexcept
			{
				return pos_ >= text_.size();
			}

			void skipWhitespace()
			{
				while (!atEnd() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0)
				{
					++pos_;
				}
			}

			[[nodiscard]] std::optional<Value> parseValue()
			{
				skipWhitespace();
				if (atEnd())
				{
					return std::nullopt;
				}
				switch (text_[pos_])
				{
					case '{': return parseObject();
					case '[': return parseArray();
					case '"': return parseStringValue();
					case 't':
					case 'f': return parseBool();
					case 'n': return parseNull();
					default: return parseNumber();
				}
			}

			[[nodiscard]] bool consumeLiteral(const char* literal)
			{
				const std::size_t length = std::char_traits<char>::length(literal);
				if (text_.compare(pos_, length, literal) != 0)
				{
					return false;
				}
				pos_ += length;
				return true;
			}

			[[nodiscard]] std::optional<Value> parseBool()
			{
				if (consumeLiteral("true"))
				{
					Value value;
					value.type = Value::Type::Boolean;
					value.boolValue = true;
					return value;
				}
				if (consumeLiteral("false"))
				{
					Value value;
					value.type = Value::Type::Boolean;
					value.boolValue = false;
					return value;
				}
				return std::nullopt;
			}

			[[nodiscard]] std::optional<Value> parseNull()
			{
				if (consumeLiteral("null"))
				{
					return Value{};
				}
				return std::nullopt;
			}

			[[nodiscard]] std::optional<Value> parseNumber()
			{
				const std::size_t start = pos_;
				if (!atEnd() && (text_[pos_] == '-' || text_[pos_] == '+'))
				{
					++pos_;
				}
				bool sawDigit = false;
				while (!atEnd() &&
					(std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0 ||
						text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' ||
						text_[pos_] == '+' || text_[pos_] == '-'))
				{
					if (std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0)
					{
						sawDigit = true;
					}
					++pos_;
				}
				if (!sawDigit)
				{
					pos_ = start;
					return std::nullopt;
				}
				Value value;
				value.type = Value::Type::Number;
				value.numberValue = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
				return value;
			}

			[[nodiscard]] std::optional<std::string> parseRawString()
			{
				if (atEnd() || text_[pos_] != '"')
				{
					return std::nullopt;
				}
				++pos_;
				std::string result;
				while (!atEnd())
				{
					const char current = text_[pos_];
					if (current == '"')
					{
						++pos_;
						return result;
					}
					if (current == '\\' && pos_ + 1 < text_.size())
					{
						const char next = text_[pos_ + 1];
						switch (next)
						{
							case '"': result += '"'; pos_ += 2; break;
							case '\\': result += '\\'; pos_ += 2; break;
							case '/': result += '/'; pos_ += 2; break;
							case 'n': result += '\n'; pos_ += 2; break;
							case 't': result += '\t'; pos_ += 2; break;
							case 'r': result += '\r'; pos_ += 2; break;
							case 'b': result += '\b'; pos_ += 2; break;
							case 'f': result += '\f'; pos_ += 2; break;
							case 'u':
								if (pos_ + 5 < text_.size())
								{
									const unsigned int codepoint = static_cast<unsigned int>(
										std::strtoul(text_.substr(pos_ + 2, 4).c_str(), nullptr, 16));
									appendUtf8(result, codepoint);
									pos_ += 6;
								}
								else
								{
									pos_ += 2;
								}
								break;
							default:
								result += next;
								pos_ += 2;
								break;
						}
					}
					else
					{
						result += current;
						++pos_;
					}
				}
				return std::nullopt;
			}

			[[nodiscard]] std::optional<Value> parseStringValue()
			{
				std::optional<std::string> raw = parseRawString();
				if (!raw.has_value())
				{
					return std::nullopt;
				}
				Value value;
				value.type = Value::Type::String;
				value.stringValue = std::move(*raw);
				return value;
			}

			[[nodiscard]] std::optional<Value> parseArray()
			{
				if (atEnd() || text_[pos_] != '[')
				{
					return std::nullopt;
				}
				++pos_;
				Value value;
				value.type = Value::Type::Array;
				skipWhitespace();
				if (!atEnd() && text_[pos_] == ']')
				{
					++pos_;
					return value;
				}
				while (true)
				{
					std::optional<Value> element = parseValue();
					if (!element.has_value())
					{
						return std::nullopt;
					}
					value.arrayValue.push_back(std::move(*element));
					skipWhitespace();
					if (atEnd())
					{
						return std::nullopt;
					}
					if (text_[pos_] == ',')
					{
						++pos_;
						continue;
					}
					if (text_[pos_] == ']')
					{
						++pos_;
						return value;
					}
					return std::nullopt;
				}
			}

			[[nodiscard]] std::optional<Value> parseObject()
			{
				if (atEnd() || text_[pos_] != '{')
				{
					return std::nullopt;
				}
				++pos_;
				Value value;
				value.type = Value::Type::Object;
				skipWhitespace();
				if (!atEnd() && text_[pos_] == '}')
				{
					++pos_;
					return value;
				}
				while (true)
				{
					skipWhitespace();
					std::optional<std::string> key = parseRawString();
					if (!key.has_value())
					{
						return std::nullopt;
					}
					skipWhitespace();
					if (atEnd() || text_[pos_] != ':')
					{
						return std::nullopt;
					}
					++pos_;
					std::optional<Value> element = parseValue();
					if (!element.has_value())
					{
						return std::nullopt;
					}
					value.objectValue.emplace_back(std::move(*key), std::move(*element));
					skipWhitespace();
					if (atEnd())
					{
						return std::nullopt;
					}
					if (text_[pos_] == ',')
					{
						++pos_;
						continue;
					}
					if (text_[pos_] == '}')
					{
						++pos_;
						return value;
					}
					return std::nullopt;
				}
			}
		};
	}

	const Value* Value::find(const std::string& key) const noexcept
	{
		if (type != Type::Object)
		{
			return nullptr;
		}
		for (const auto& [entryKey, entryValue] : objectValue)
		{
			if (entryKey == key)
			{
				return &entryValue;
			}
		}
		return nullptr;
	}

	std::optional<std::string> Value::asString() const
	{
		if (type != Type::String)
		{
			return std::nullopt;
		}
		return stringValue;
	}

	std::optional<double> Value::asNumber() const
	{
		if (type != Type::Number)
		{
			return std::nullopt;
		}
		return numberValue;
	}

	std::optional<bool> Value::asBool() const
	{
		if (type != Type::Boolean)
		{
			return std::nullopt;
		}
		return boolValue;
	}

	std::optional<std::vector<double>> Value::asNumberArray() const
	{
		if (type != Type::Array)
		{
			return std::nullopt;
		}
		std::vector<double> result;
		result.reserve(arrayValue.size());
		for (const Value& element : arrayValue)
		{
			const std::optional<double> number = element.asNumber();
			if (!number.has_value())
			{
				return std::nullopt;
			}
			result.push_back(*number);
		}
		return result;
	}

	std::optional<Value> parse(const std::string& text)
	{
		// Strip a leading UTF-8 BOM (EF BB BF) if present - not part of the
		// JSON grammar, but common in files written by tools that default to
		// "UTF-8 with BOM" (e.g. PowerShell's Set-Content/Out-File, and by
		// extension New-GameForgerAIProject.ps1's generated Project.json).
		// Every other JSON producer in this codebase (SceneSerializer's own
		// saveScene) writes plain UTF-8 with no BOM, which is why this went
		// unnoticed until something tried to parse a PowerShell-authored file.
		constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
		const std::string_view body =
			text.size() >= utf8Bom.size() && text.compare(0, utf8Bom.size(), utf8Bom) == 0
			? std::string_view(text).substr(utf8Bom.size())
			: std::string_view(text);
		const std::string bodyText(body);
		Parser parser(bodyText);
		return parser.parseDocument();
	}
}
