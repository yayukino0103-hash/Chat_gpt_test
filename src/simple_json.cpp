#include "simple_json.h"

#include <cctype>

namespace {

void skipSpaces(const std::string& s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
}

bool readQuoted(const std::string& s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    out.clear();

    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (i >= s.size()) {
                return false;
            }
            const char e = s[i++];
            if (e == '"' || e == '\\' || e == '/') {
                out.push_back(e);
            } else if (e == 'n') {
                out.push_back('\n');
            } else if (e == 't') {
                out.push_back('\t');
            } else {
                return false;
            }
            continue;
        }
        out.push_back(c);
    }

    return false;
}

bool readUnquoted(const std::string& s, std::size_t& i, std::string& out) {
    out.clear();
    const std::size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}') {
        ++i;
    }
    if (i == start) {
        return false;
    }

    std::size_t end = i;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    out = s.substr(start, end - start);
    return !out.empty();
}

std::string escapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

bool parseJsonObject(const std::string& input, std::unordered_map<std::string, std::string>& out) {
    out.clear();
    std::size_t i = 0;

    skipSpaces(input, i);
    if (i >= input.size() || input[i] != '{') {
        return false;
    }
    ++i;

    skipSpaces(input, i);
    if (i < input.size() && input[i] == '}') {
        ++i;
        skipSpaces(input, i);
        return i == input.size();
    }

    while (i < input.size()) {
        skipSpaces(input, i);

        std::string key;
        if (!readQuoted(input, i, key)) {
            return false;
        }

        skipSpaces(input, i);
        if (i >= input.size() || input[i] != ':') {
            return false;
        }
        ++i;

        skipSpaces(input, i);
        std::string value;
        if (i < input.size() && input[i] == '"') {
            if (!readQuoted(input, i, value)) {
                return false;
            }
        } else {
            if (!readUnquoted(input, i, value)) {
                return false;
            }
        }

        out[key] = value;

        skipSpaces(input, i);
        if (i < input.size() && input[i] == ',') {
            ++i;
            continue;
        }
        if (i < input.size() && input[i] == '}') {
            ++i;
            skipSpaces(input, i);
            return i == input.size();
        }
        return false;
    }

    return false;
}

std::string buildJsonObject(const std::vector<JsonField>& fields) {
    std::string out = "{";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        out += '"' + escapeJsonString(f.key) + '"';
        out += ':';
        if (f.quoted) {
            out += '"' + escapeJsonString(f.value) + '"';
        } else {
            out += f.value;
        }
        if (i + 1 < fields.size()) {
            out += ',';
        }
    }
    out += "}";
    return out;
}
