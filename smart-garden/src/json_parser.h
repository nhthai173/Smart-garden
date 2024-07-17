//
// Created by Thái Nguyễn on 17/7/24.
//

#ifndef SMART_GARDEN_JSON_PARSER_H
#define SMART_GARDEN_JSON_PARSER_H

#define JSON_PARSER_VERSION "1.0.0"

//#define USE_ARRAY
#define USE_JSON_PARSER_CLASS

#include <Arduino.h>
#include "vector"

namespace JSON {

    struct index_t {
        int8_t depth;
        uint32_t index;
        char str;
    };

    /**
     * Get property value from JSON string
     * @param body JSON string
     * @param key property name
     * @return
     */
    String getProperty(const String &body, const String &key);


#ifdef USE_ARRAY
    /**
     * Get item value from JSON array string
     * @param arrayString
     * @param index start from 0
     * @return
     */
    String getItem(const String &arrayString, unsigned int index);
#endif

#ifdef USE_JSON_PARSER_CLASS
    class JsonParser;
#endif

} // namespace JSON


String JSON::getProperty(const String &body, const String &key) {
    if (body[0] != '{') {
        return ""; // invalid JSON
    }

    String _key = "\"" + key + "\"";
    int8_t _depth = 0;
    const char *str = body.c_str();
    std::vector<JSON::index_t> indexes;
    while (*str) {
        if (*str == '{' || *str == '[') {
            JSON::index_t index{};
            index.depth = _depth;
            index.index = str - body.c_str();
            index.str = *str;
            ++_depth;
        }
        if (*str == '}' || *str == ']') {
            JSON::index_t index{};
            index.depth = _depth;
            index.index = str - body.c_str();
            index.str = *str;
            --_depth;
        }
        if (*str == _key[0]) {
            uint8_t index = str - body.c_str();
            String k = body.substring(index, index + _key.length());
            if (strcmp(k.c_str(), _key.c_str()) == 0 && body[index + _key.length()] == ':') {
                JSON::index_t res{};
                res.depth = _depth;
                res.index = index;
                res.str = 1;
                indexes.push_back(res);
            }
        }
        str++;
    }

    for (auto &i: indexes) {
        if (i.str == 1 && i.depth == 1) {
            unsigned int start_index = i.index + _key.length() + 1; // start index of value

            // By pass space
            while (body[start_index] == ' ')
                ++start_index;

            // property is Object/Array
            if (body[start_index] == '{' || body[start_index] == '[') {
                char endStr = body[start_index] == '{' ? '}' : ']';
                for (auto &j: indexes) {
                    if (j.str == endStr && j.depth == i.depth + 1 && j.index > start_index) {
                        int end_index = j.index;
                        return body.substring(start_index, end_index + 1);
                    }
                }
                return ""; // invalid JSON
            }

            // Other type
            int end_index = body.indexOf(",", start_index);
            if (end_index < 0) {
                end_index = body.length() - 1;
            }
            String output = body.substring(start_index, end_index);
            output.trim();
            if (output[0] == '\"' && output[output.length() - 1] == '\"') {
                output = output.substring(1, output.length() - 1);
            }
            return output;
        }
    }

    return ""; // not found
}



#ifdef USE_ARRAY

String JSON::getItem(const String &arrayString, unsigned int index) {
    if (arrayString[0] != '[') {
        return ""; // invalid Array
    }

    int8_t _depth = 0;
    const char *str = arrayString.c_str();
    std::vector<JSON::index_t> indexes;
    while (*str) {
        if (*str == '{' || *str == '[') {
            JSON::index_t res{};
            res.depth = _depth;
            res.index = str - arrayString.c_str();
            res.str = *str;
            ++_depth;
        }
        if (*str == '}' || *str == ']') {
            JSON::index_t res{};
            res.depth = _depth;
            res.index = str - arrayString.c_str();
            res.str = *str;
            --_depth;
        }
        if (*str == ',') {
            JSON::index_t res{};
            res.depth = _depth;
            res.index = str - arrayString.c_str();
            res.str = -1;
            indexes.push_back(res);
        }
        str++;
    }

    unsigned int prevIndex = 0;
    for (auto &i : indexes) {
        if (i.str == -1 && i.depth == 1) {
            if (index == 0) {
                if (prevIndex == 0) {
                    prevIndex = 1;
                }
                String output = arrayString.substring(prevIndex, i.index);
                output.trim();
                if (output[0] == '{' || output[0] == '[') {
                    char endStr = output[0] == '{' ? '}' : ']';
                    for (auto &j : indexes) {
                        if (j.str == endStr && j.depth == i.depth + 1 && j.index > i.index) {
                            int end_index = j.index;
                            return arrayString.substring(i.index, end_index + 1);
                        }
                    }
                    return ""; // invalid JSON
                } else if (output[0] == '\"' && output[output.length() - 1] == '\"') {
                    output = output.substring(1, output.length() - 1);
                }
                return output;
            }
            prevIndex = i.index + 1;
            --index;
        }
    }

    return ""; // not found
}

#endif // USE_ARRAY




#ifdef USE_JSON_PARSER_CLASS

class JSON::JsonParser {

private:
    String _buf;

public:
    JsonParser(const String &body) {
        _buf = body;
    }

    ~JsonParser() {
        _buf = "";
    }

    String getString(const String &property) {
        return JSON::getProperty(_buf, property);
    }

    bool getBool(const String &property) {
        return getString(property) == "true";
    }

    uint32_t getInt(const String &property) {
        return getString(property).toInt();
    }

    float getFloat(const String &property) {
        return getString(property).toFloat();
    }

    JsonParser *getObject(const String &property) {
        return new JsonParser(JSON::getProperty(_buf, property));
    }

#ifdef USE_ARRAY

    String getString(const String &property, unsigned int index) {
        return JSON::getItem(JSON::getProperty(_buf, property), index);
    }

    bool getBool(const String &property, unsigned int index) {
        return getString(property, index) == "true";
    }

    uint32_t getInt(const String &property, unsigned int index) {
        return getString(property, index).toInt();
    }

    float getFloat(const String &property, unsigned int index) {
        return getString(property, index).toFloat();
    }

    JsonParser *getObject(const String &property, unsigned int index) {
        return new JsonParser(JSON::getItem(JSON::getProperty(_buf, property), index));
    }

    JsonParser *getArray(const String &property) {
        return getObject(property);
    }

    JsonParser *getArray(const String &property, unsigned int index) {
        return getObject(property, index);
    }
#endif // USE_ARRAY

}; // class JsonParser

#endif // USE_JSON_PARSER_CLASS


#endif //SMART_GARDEN_JSON_PARSER_H
