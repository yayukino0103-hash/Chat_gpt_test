#ifndef SIMPLE_JSON_H
#define SIMPLE_JSON_H

#include <string>
#include <unordered_map>
#include <vector>

struct JsonField {
    std::string key;
    std::string value;
    bool quoted = true;
};

bool parseJsonObject(const std::string& input, std::unordered_map<std::string, std::string>& out);
std::string buildJsonObject(const std::vector<JsonField>& fields);

#endif // SIMPLE_JSON_H
