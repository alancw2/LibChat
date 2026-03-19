#ifndef CONFIG_HPP
#define CONFIG_HPP
#include <regex>

namespace Config {
	static constexpr std:string_view APP_NICKNAME_PATTERN = R"(^[^\s](?:[a-zA-Z0-9 ._,@#\-]*[^\s])?$)";
}
