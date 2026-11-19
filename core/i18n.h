#pragma once
#include <cstddef>
#include <string>
namespace assistant {
enum class Locale { ZhCN, EnUS };
enum class TextId {
#define X(id, zh, en) id,
#include "strings.def"
#undef X
    Count
};
const char* tr(TextId id, Locale locale);
const char* text_key(TextId id);
const char* locale_code(Locale locale);
Locale parse_locale(const std::string& value);
}
