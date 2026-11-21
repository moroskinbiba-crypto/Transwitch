#include "PersistenceStore.hpp"
#include "../projects/common/include/desknx/SafeReplace.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace desknx::persistence {
namespace {

struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Kind kind{Kind::Null};
    std::string scalar;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }
    bool isString() const { return kind == Kind::String; }
    bool isNumber() const { return kind == Kind::Number; }

    const JsonValue* get(const char* key) const {
        if (!isObject() || key == nullptr)
            return nullptr;
        for (auto it = object.rbegin(); it != object.rend(); ++it) {
            if (it->first == key)
                return &it->second;
        }
        return nullptr;
    }

    std::string stringValue(const std::string& fallback = {}) const {
        return isString() ? scalar : fallback;
    }

    long long signedValue(long long fallback) const {
        if (!isNumber() || scalar.empty())
            return fallback;
        errno = 0;
        char* end = nullptr;
        const long long value = std::strtoll(scalar.c_str(), &end, 10);
        if (errno == ERANGE || end == scalar.c_str() || *end != '\0')
            return fallback;
        return value;
    }

    unsigned long long unsignedValue(unsigned long long fallback) const {
        if (!isNumber() || scalar.empty() || scalar[0] == '-')
            return fallback;
        errno = 0;
        char* end = nullptr;
        const unsigned long long value = std::strtoull(scalar.c_str(), &end, 10);
        if (errno == ERANGE || end == scalar.c_str() || *end != '\0')
            return fallback;
        return value;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& source) : m_source(source) {}

    bool parse(JsonValue& value) {
        skipWhitespace();
        if (!parseValue(value, 0))
            return false;
        skipWhitespace();
        return m_position == m_source.size();
    }

private:
    static constexpr unsigned MaxDepth = 48;

    void skipWhitespace() {
        while (m_position < m_source.size()) {
            const char ch = m_source[m_position];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                break;
            ++m_position;
        }
    }

    bool consume(char expected) {
        if (m_position >= m_source.size() || m_source[m_position] != expected)
            return false;
        ++m_position;
        return true;
    }

    bool parseLiteral(const char* literal) {
        for (const char* p = literal; *p != '\0'; ++p) {
            if (m_position >= m_source.size() || m_source[m_position] != *p)
                return false;
            ++m_position;
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned codepoint) {
        if (codepoint <= 0x7Fu) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        } else if (codepoint <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
    }

    bool readHex4(unsigned& value) {
        if (m_position + 4 > m_source.size())
            return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char ch = m_source[m_position++];
            unsigned digit = 0;
            if (ch >= '0' && ch <= '9')
                digit = static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f')
                digit = static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F')
                digit = static_cast<unsigned>(ch - 'A' + 10);
            else
                return false;
            value = (value << 4) | digit;
        }
        return true;
    }

    bool parseString(std::string& value) {
        if (!consume('"'))
            return false;

        value.clear();
        while (m_position < m_source.size()) {
            const unsigned char ch = static_cast<unsigned char>(m_source[m_position++]);
            if (ch == '"')
                return true;
            if (ch < 0x20u)
                return false;
            if (ch != '\\') {
                value.push_back(static_cast<char>(ch));
                continue;
            }

            if (m_position >= m_source.size())
                return false;
            const char escaped = m_source[m_position++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u': {
                    unsigned codepoint = 0;
                    if (!readHex4(codepoint))
                        return false;
                    if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) {
                        if (m_position + 2 > m_source.size() ||
                            m_source[m_position] != '\\' ||
                            m_source[m_position + 1] != 'u')
                            return false;
                        m_position += 2;
                        unsigned low = 0;
                        if (!readHex4(low) || low < 0xDC00u || low > 0xDFFFu)
                            return false;
                        codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) +
                                    (low - 0xDC00u);
                    } else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) {
                        return false;
                    }
                    appendUtf8(value, codepoint);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool parseNumber(JsonValue& value) {
        const std::size_t start = m_position;
        if (m_position < m_source.size() && m_source[m_position] == '-')
            ++m_position;
        if (m_position >= m_source.size())
            return false;

        if (m_source[m_position] == '0') {
            ++m_position;
            if (m_position < m_source.size() &&
                m_source[m_position] >= '0' && m_source[m_position] <= '9')
                return false;
        } else {
            if (m_source[m_position] < '1' || m_source[m_position] > '9')
                return false;
            while (m_position < m_source.size() &&
                   m_source[m_position] >= '0' && m_source[m_position] <= '9')
                ++m_position;
        }

        if (m_position < m_source.size() && m_source[m_position] == '.') {
            ++m_position;
            const std::size_t fractionStart = m_position;
            while (m_position < m_source.size() &&
                   m_source[m_position] >= '0' && m_source[m_position] <= '9')
                ++m_position;
            if (fractionStart == m_position)
                return false;
        }

        if (m_position < m_source.size() &&
            (m_source[m_position] == 'e' || m_source[m_position] == 'E')) {
            ++m_position;
            if (m_position < m_source.size() &&
                (m_source[m_position] == '+' || m_source[m_position] == '-'))
                ++m_position;
            const std::size_t exponentStart = m_position;
            while (m_position < m_source.size() &&
                   m_source[m_position] >= '0' && m_source[m_position] <= '9')
                ++m_position;
            if (exponentStart == m_position)
                return false;
        }

        value.kind = JsonValue::Kind::Number;
        value.scalar.assign(m_source, start, m_position - start);
        return true;
    }

    bool parseArray(JsonValue& value, unsigned depth) {
        if (!consume('['))
            return false;
        value.kind = JsonValue::Kind::Array;
        value.array.clear();
        skipWhitespace();
        if (consume(']'))
            return true;

        while (true) {
            JsonValue item;
            if (!parseValue(item, depth + 1))
                return false;
            value.array.push_back(std::move(item));
            skipWhitespace();
            if (consume(']'))
                return true;
            if (!consume(','))
                return false;
            skipWhitespace();
        }
    }

    bool parseObject(JsonValue& value, unsigned depth) {
        if (!consume('{'))
            return false;
        value.kind = JsonValue::Kind::Object;
        value.object.clear();
        skipWhitespace();
        if (consume('}'))
            return true;

        while (true) {
            std::string key;
            if (!parseString(key))
                return false;
            skipWhitespace();
            if (!consume(':'))
                return false;
            skipWhitespace();
            JsonValue item;
            if (!parseValue(item, depth + 1))
                return false;
            value.object.emplace_back(std::move(key), std::move(item));
            skipWhitespace();
            if (consume('}'))
                return true;
            if (!consume(','))
                return false;
            skipWhitespace();
        }
    }

    bool parseValue(JsonValue& value, unsigned depth) {
        if (depth > MaxDepth || m_position >= m_source.size())
            return false;
        skipWhitespace();
        if (m_position >= m_source.size())
            return false;

        const char ch = m_source[m_position];
        if (ch == '{')
            return parseObject(value, depth);
        if (ch == '[')
            return parseArray(value, depth);
        if (ch == '"') {
            value.kind = JsonValue::Kind::String;
            return parseString(value.scalar);
        }
        if (ch == 't' && parseLiteral("true")) {
            value.kind = JsonValue::Kind::Boolean;
            value.scalar = "true";
            return true;
        }
        if (ch == 'f' && parseLiteral("false")) {
            value.kind = JsonValue::Kind::Boolean;
            value.scalar = "false";
            return true;
        }
        if (ch == 'n' && parseLiteral("null")) {
            value.kind = JsonValue::Kind::Null;
            return true;
        }
        return parseNumber(value);
    }

    const std::string& m_source;
    std::size_t m_position{0};
};

bool readJsonFile(const char* path, JsonValue& root) {
    if (!path || !path[0]) return false;
    std::error_code sizeError;
    const auto bytes=std::filesystem::file_size(path,sizeError);
    constexpr std::uintmax_t MaxJsonBytes=2u*1024u*1024u;
    if (sizeError || bytes==0 || bytes>MaxJsonBytes) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    const std::string input((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    if (file.bad() || input.size()!=bytes)
        return false;
    JsonParser parser(input);
    return parser.parse(root) && root.isObject();
}

bool readJsonFileWithBackup(const char* path, JsonValue& root) {
    if (readJsonFile(path,root)) return true;
    const std::string backup=std::string(path)+".bak";
    if (!readJsonFile(backup.c_str(),root)) return false;
    fprintf(stderr,"[persistence] restored %s from backup\n",path);
    return true;
}

void appendQuoted(std::string& output, const std::string& value) {
    static const char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (ch < 0x20u) {
                    output += "\\u00";
                    output.push_back(hex[(ch >> 4) & 0x0Fu]);
                    output.push_back(hex[ch & 0x0Fu]);
                } else {
                    output.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    output.push_back('"');
}

bool writeJsonFileAtomically(const char* path, const std::string& content) {
    const std::string temporary = std::string(path) + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.put('\n');
        file.flush();
        if (!file.good()) {
            file.close();
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }
    }

    const std::string backup=std::string(path)+".bak";
    const auto status=desknx::safeio::replaceFilePreservingOnFailure(
        temporary.c_str(),path,backup.c_str(),true);
    if(status!=desknx::safeio::ReplaceStatus::Success) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary,cleanupError);
        return false;
    }
    return true;
}

std::uint32_t readU32(const JsonValue* value, std::uint32_t fallback = 0) {
    if (!value || !value->isNumber())
        return fallback;
    const auto number = value->unsignedValue(fallback);
    return number > 0xFFFFFFFFull ? fallback : static_cast<std::uint32_t>(number);
}

int readInt(const JsonValue* value, int fallback) {
    if (!value || !value->isNumber())
        return fallback;
    const long long number = value->signedValue(fallback);
    if (number < static_cast<long long>(-2147483647 - 1) ||
        number > static_cast<long long>(2147483647))
        return fallback;
    return static_cast<int>(number);
}

} // namespace

bool PersistenceStore::load() {
    const bool configOk = loadConfig();
    const bool desktopsOk = loadDesktops();
    const bool widgetsOk = loadWidgets();

    if (!configOk)
        fprintf(stderr, "[persistence] config.json not found or invalid\n");
    if (!desktopsOk)
        fprintf(stderr, "[persistence] workspaces.json not found or invalid\n");
    if (!widgetsOk)
        fprintf(stderr, "[persistence] widgets.json not found or invalid\n");

    return configOk || desktopsOk || widgetsOk;
}

bool PersistenceStore::loadConfig() {
    JsonValue root;
    if (!readJsonFileWithBackup(kConfigPath, root))
        return false;

    if (const JsonValue* value = root.get("theme"))
        m_appearance.theme = value->stringValue(m_appearance.theme);
    if (const JsonValue* value = root.get("brightness"))
        m_appearance.brightness = std::clamp(readInt(value, m_appearance.brightness), 0, 100);
    if (const JsonValue* value = root.get("language"))
        m_appearance.language = value->stringValue(m_appearance.language);
    if (const JsonValue* value = root.get("steamGridDbApiKey"))
        m_steamGridDbApiKey = value->stringValue(m_steamGridDbApiKey);

    return true;
}

bool PersistenceStore::loadDesktops() {
    JsonValue root;
    if (!readJsonFileWithBackup(kDesktopsPath, root))
        return false;

    m_desktops.clear();
    const JsonValue* desktops = root.get("desktops");
    if (desktops && desktops->isArray()) {
        for (const JsonValue& item : desktops->array) {
            if (!item.isObject())
                continue;

            DesktopLayout layout;
            layout.id = readU32(item.get("id"));
            if (const JsonValue* name = item.get("name"))
                layout.name = name->stringValue();

            const JsonValue* tiles = item.get("tiles");
            if (tiles && tiles->isArray()) {
                for (const JsonValue& tile : tiles->array) {
                    if (tile.isNumber()) {
                        const auto id = tile.unsignedValue(0);
                        if (id != 0)
                            layout.tileIds.push_back(static_cast<std::uint64_t>(id));
                    }
                }
            }
            if (layout.id > 0)
                m_desktops.push_back(std::move(layout));
        }
    }
    return !m_desktops.empty();
}

bool PersistenceStore::loadWidgets() {
    JsonValue root;
    if (!readJsonFileWithBackup(kWidgetsPath, root))
        return false;

    m_widgets.clear();
    const JsonValue* widgets = root.get("widgets");
    if (widgets && widgets->isArray()) {
        for (const JsonValue& item : widgets->array) {
            if (!item.isObject())
                continue;

            WidgetState widget;
            widget.id = readU32(item.get("id"));
            if (const JsonValue* type = item.get("type"))
                widget.type = type->stringValue();
            widget.columns = readInt(item.get("columns"), 1);
            widget.rows = readInt(item.get("rows"), 1);
            widget.positionX = readInt(item.get("positionX"), 0);
            widget.positionY = readInt(item.get("positionY"), 0);
            if (const JsonValue* asset = item.get("asset"))
                widget.assetRef = asset->stringValue();
            if (widget.id > 0)
                m_widgets.push_back(std::move(widget));
        }
    }
    return !m_widgets.empty();
}

bool PersistenceStore::save() {
    std::error_code ec;
    std::filesystem::create_directories(kDataDir, ec);
    if (ec) {
        fprintf(stderr, "[persistence] failed to create data directory: %s\n",
                ec.message().c_str());
        return false;
    }

    const bool configOk = saveConfig();
    const bool desktopsOk = saveDesktops();
    const bool widgetsOk = saveWidgets();

    if (!configOk)
        fprintf(stderr, "[persistence] failed to save config.json\n");
    if (!desktopsOk)
        fprintf(stderr, "[persistence] failed to save workspaces.json\n");
    if (!widgetsOk)
        fprintf(stderr, "[persistence] failed to save widgets.json\n");

    return configOk && desktopsOk && widgetsOk;
}

bool PersistenceStore::saveConfig() const {
    std::string json = "{\n  \"version\": 1,\n  \"theme\": ";
    appendQuoted(json, m_appearance.theme.empty() ? "Default Light" : m_appearance.theme);
    json += ",\n  \"brightness\": " + std::to_string(m_appearance.brightness);
    json += ",\n  \"language\": ";
    appendQuoted(json, m_appearance.language.empty() ? "auto" : m_appearance.language);
    if (!m_steamGridDbApiKey.empty()) {
        json += ",\n  \"steamGridDbApiKey\": ";
        appendQuoted(json, m_steamGridDbApiKey);
    }
    json += "\n}";
    return writeJsonFileAtomically(kConfigPath, json);
}

bool PersistenceStore::saveDesktops() const {
    std::string json = "{\n  \"version\": 1,\n  \"desktops\": [";
    for (std::size_t i = 0; i < m_desktops.size(); ++i) {
        const DesktopLayout& layout = m_desktops[i];
        json += i == 0 ? "\n    {" : ",\n    {";
        json += "\n      \"id\": " + std::to_string(layout.id) + ",\n      \"name\": ";
        appendQuoted(json, layout.name);
        json += ",\n      \"tiles\": [";
        for (std::size_t tile = 0; tile < layout.tileIds.size(); ++tile) {
            if (tile != 0)
                json += ", ";
            json += std::to_string(layout.tileIds[tile]);
        }
        json += "]\n    }";
    }
    if (!m_desktops.empty())
        json += "\n  ";
    json += "]\n}";
    return writeJsonFileAtomically(kDesktopsPath, json);
}

bool PersistenceStore::saveWidgets() const {
    std::string json = "{\n  \"version\": 1,\n  \"widgets\": [";
    for (std::size_t i = 0; i < m_widgets.size(); ++i) {
        const WidgetState& widget = m_widgets[i];
        json += i == 0 ? "\n    {" : ",\n    {";
        json += "\n      \"id\": " + std::to_string(widget.id) + ",\n      \"type\": ";
        appendQuoted(json, widget.type);
        json += ",\n      \"columns\": " + std::to_string(widget.columns);
        json += ",\n      \"rows\": " + std::to_string(widget.rows);
        json += ",\n      \"positionX\": " + std::to_string(widget.positionX);
        json += ",\n      \"positionY\": " + std::to_string(widget.positionY);
        json += ",\n      \"asset\": ";
        appendQuoted(json, widget.assetRef);
        json += "\n    }";
    }
    if (!m_widgets.empty())
        json += "\n  ";
    json += "]\n}";
    return writeJsonFileAtomically(kWidgetsPath, json);
}

} // namespace desknx::persistence
