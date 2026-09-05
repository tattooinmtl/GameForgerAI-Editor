#include "GameForger/Editor/Json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

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
			else if (codepoint < 0x10000)
			{
				out += static_cast<char>(0xE0 | (codepoint >> 12));
				out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else if (codepoint <= 0x10FFFF)
			{
				out += static_cast<char>(0xF0 | (codepoint >> 18));
				out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codepoint & 0x3F));
			}
		}

		[[nodiscard]] bool isHighSurrogate(const unsigned int codepoint) noexcept
		{
			return codepoint >= 0xD800 && codepoint <= 0xDBFF;
		}

		[[nodiscard]] bool isLowSurrogate(const unsigned int codepoint) noexcept
		{
			return codepoint >= 0xDC00 && codepoint <= 0xDFFF;
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
			// parseValue -> parseObject/parseArray -> parseValue recurses once
			// per level of nesting, so an input like "[[[[[..." would grow the
			// stack without bound and hard-crash the process. This parser is
			// fed AI provider HTTP response bodies, Blender MCP replies and
			// scene files opened from disk - none of which are trusted input -
			// so the depth is capped rather than left to the stack size.
			//
			// 200 is far beyond anything this project's own formats reach: a
			// saved scene nests about six levels deep.
			static constexpr std::size_t kMaxDepth = 200;

			const std::string& text_;
			std::size_t pos_ = 0;
			std::size_t depth_ = 0;

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
					// The only two recursive cases. Everything else below is a
					// flat scalar and cannot grow the stack.
					case '{':
					case '[':
					{
						if (depth_ >= kMaxDepth)
						{
							return std::nullopt;
						}
						++depth_;
						std::optional<Value> nested =
							text_[pos_] == '{' ? parseObject() : parseArray();
						--depth_;
						return nested;
					}
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
				if (!atEnd() && text_[pos_] == '-')
				{
					++pos_;
				}
				else if (!atEnd() && text_[pos_] == '+')
				{
					pos_ = start;
					return std::nullopt;
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
				const std::string token = text_.substr(start, pos_ - start);
				char* end = nullptr;
				const double parsed = std::strtod(token.c_str(), &end);
				if (end != token.c_str() + token.size())
				{
					pos_ = start;
					return std::nullopt;
				}
				Value value;
				value.type = Value::Type::Number;
				value.numberValue = parsed;
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
									const unsigned int unit = static_cast<unsigned int>(
										std::strtoul(text_.substr(pos_ + 2, 4).c_str(), nullptr, 16));
									if (isHighSurrogate(unit) &&
										pos_ + 11 < text_.size() &&
										text_[pos_ + 6] == '\\' &&
										text_[pos_ + 7] == 'u')
									{
										const unsigned int low = static_cast<unsigned int>(
											std::strtoul(text_.substr(pos_ + 8, 4).c_str(), nullptr, 16));
										if (isLowSurrogate(low))
										{
											const unsigned int codepoint =
												0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
											appendUtf8(result, codepoint);
											pos_ += 12;
											break;
										}
									}
									if (isHighSurrogate(unit) || isLowSurrogate(unit))
									{
										return std::nullopt;
									}
									appendUtf8(result, unit);
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

	// ---- Phase C: serialisation + builder helpers -------------------------

	namespace
	{
		void appendEscapedString(std::string& out, const std::string& text)
		{
			out += '"';
			for (const char c : text)
			{
				const unsigned char b = static_cast<unsigned char>(c);
				switch (b)
				{
					case '\\': out += "\\\\"; break;
					case '"':  out += "\\\""; break;
					case '\b': out += "\\b"; break;
					case '\f': out += "\\f"; break;
					case '\n': out += "\\n"; break;
					case '\r': out += "\\r"; break;
					case '\t': out += "\\t"; break;
					default:
						if (b < 0x20)
						{
							static constexpr char hex[] = "0123456789abcdef";
							out += "\\u00";
							out += hex[(b >> 4) & 0x0F];
							out += hex[b & 0x0F];
						}
						else
						{
							out += c;
						}
						break;
				}
			}
			out += '"';
		}

		void serializeInto(const Value& value, std::string& out)
		{
			switch (value.type)
			{
				case Value::Type::Null:
					out += "null";
					break;
				case Value::Type::Boolean:
					out += value.boolValue ? "true" : "false";
					break;
				case Value::Type::Number:
				{
					// Print integers as integers, floats compactly. std::to_string
					// on a double would emit "1.000000" - noisy for tool arg logs.
					const double n = value.numberValue;
					if (n == static_cast<double>(static_cast<long long>(n)) &&
					    n >= -9.0e15 && n <= 9.0e15)
					{
						out += std::to_string(static_cast<long long>(n));
					}
					else
					{
						char buf[32];
						std::snprintf(buf, sizeof(buf), "%.17g", n);
						out += buf;
					}
					break;
				}
				case Value::Type::String:
					appendEscapedString(out, value.stringValue);
					break;
				case Value::Type::Array:
					out += '[';
					for (std::size_t i = 0; i < value.arrayValue.size(); ++i)
					{
						if (i > 0) out += ',';
						serializeInto(value.arrayValue[i], out);
					}
					out += ']';
					break;
				case Value::Type::Object:
					out += '{';
					for (std::size_t i = 0; i < value.objectValue.size(); ++i)
					{
						if (i > 0) out += ',';
						appendEscapedString(out, value.objectValue[i].first);
						out += ':';
						serializeInto(value.objectValue[i].second, out);
					}
					out += '}';
					break;
			}
		}
	}

	std::string serialize(const Value& value)
	{
		std::string out;
		out.reserve(64);
		serializeInto(value, out);
		return out;
	}

	Value makeString(std::string text)
	{
		Value v;
		v.type = Value::Type::String;
		v.stringValue = std::move(text);
		return v;
	}

	Value makeNumber(double n)
	{
		Value v;
		v.type = Value::Type::Number;
		v.numberValue = n;
		return v;
	}

	Value makeBool(bool b)
	{
		Value v;
		v.type = Value::Type::Boolean;
		v.boolValue = b;
		return v;
	}

	Value makeNull()
	{
		Value v;
		v.type = Value::Type::Null;
		return v;
	}

	Value makeArray(std::vector<Value> items)
	{
		Value v;
		v.type = Value::Type::Array;
		v.arrayValue = std::move(items);
		return v;
	}

	Value makeObject(std::vector<std::pair<std::string, Value>> entries)
	{
		Value v;
		v.type = Value::Type::Object;
		v.objectValue = std::move(entries);
		return v;
	}
}
