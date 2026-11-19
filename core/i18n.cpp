#include "i18n.h"
namespace assistant {
static const char* const texts[][3] = {
#define X(id, zh, en) {#id, zh, en},
#include "strings.def"
#undef X
};
static_assert(sizeof(texts) / sizeof(texts[0]) == static_cast<size_t>(TextId::Count));
const char* tr(TextId id, Locale locale) {
    const auto n = static_cast<size_t>(id);
    return n < static_cast<size_t>(TextId::Count) ? texts[n][locale == Locale::EnUS ? 2 : 1] : "?";
}
const char* text_key(TextId id) { return texts[static_cast<size_t>(id)][0]; }
const char* locale_code(Locale locale) { return locale == Locale::EnUS ? "en-US" : "zh-CN"; }
Locale parse_locale(const std::string& value) { return value == "en-US" ? Locale::EnUS : Locale::ZhCN; }
}
