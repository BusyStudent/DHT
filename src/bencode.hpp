#pragma once

#include <algorithm>
#include <charconv>
#include <compare>
#include <variant>
#include <format>
#include <string>
#include <vector>
#include <span>
#include <map>

/**
 * @brief The Ben code Object
 * 
 */
class BenObject {
public:
    using List = std::vector<BenObject>;
    using Dict = std::map<std::string, BenObject, std::less<> >;

    /**
     * @brief Construct a new empty Ben Object object
     * 
     */
    BenObject() = default;

    /**
     * @brief Construct a new Ben Object object
     * 
     */
    BenObject(const BenObject &) = default;

    /**
     * @brief Construct a new Ben Object object
     * 
     */
    BenObject(BenObject &&) = default;

    /**
     * @brief Construct a new INT Ben Object object
     * 
     * @param num 
     */
    BenObject(int64_t num) : mData(num) { }

    /**
     * @brief Construct a new string Ben Object object
     * 
     * @param str 
     */
    BenObject(std::string_view str) : mData(std::string(str)) { }

    /**
     * @brief Construct a new string Ben Object object
     * 
     * @param str 
     */
    BenObject(const std::string &str) : mData(str) { }

    /**
     * @brief Construct a new string Ben Object object from binary data
     * 
     * @param buffer The binary 
     */
    BenObject(std::span<const std::byte> buffer) : mData(
        std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size_bytes())
    ) { }

    /**
     * @brief Construct a new string Ben Object object
     * 
     * @param str 
     */
    BenObject(const char *str) : mData(str) { }

    /**
     * @brief Construct a new List Ben Object object
     * 
     * @param list 
     */
    BenObject(List &&list) : mData(std::move(list)) { }

    /**
     * @brief Construct a new Dict Ben Object object
     * 
     * @param dict 
     */
    BenObject(Dict &&dict) : mData(std::move(dict)) { }

    /**
     * 
     * @brief Construct a new List Ben Object object
     * 
     * @param list 
     */
    BenObject(std::initializer_list<BenObject> list) : mData(List(list)) { }

    /**
     * @brief Destroy the Ben Object object
     * 
     */
    ~BenObject() = default;

    /**
     * @brief Check is list
     * 
     * @return true 
     * @return false 
     */
    auto isList() const -> bool { return std::holds_alternative<List>(mData); }

    /**
     * @brief Check is dict
     * 
     * @return true 
     * @return false 
     */
    auto isDict() const -> bool { return std::holds_alternative<Dict>(mData); }

    /**
     * @brief Check is int
     * 
     * @return true 
     * @return false 
     */
    auto isInt() const -> bool { return std::holds_alternative<int64_t>(mData); }

    /**
     * @brief Check is string
     * 
     * @return true 
     * @return false 
     */
    auto isString() const -> bool { return std::holds_alternative<std::string>(mData); }

    /**
     * @brief Check is null
     * 
     * @return true 
     * @return false 
     */
    auto isNull() const -> bool { return std::holds_alternative<std::monostate>(mData); }

    /**
     * @brief Cast to string
     * 
     * @return std::string 
     */
    auto toString() const -> const std::string & { return std::get<std::string>(mData); }

    /**
     * @brief Cast to int
     * 
     * @return int64_t 
     */
    auto toInt() const -> int64_t { return std::get<int64_t>(mData); }

    /**
     * @brief Get the List from the BenObject
     * 
     * @return List& 
     */
    auto toList() const -> const List & { return std::get<List>(mData); }

    /**
     * @brief Get the Dict from the BenObject
     * 
     * @return Dict& 
     */
    auto toDict() const -> const Dict & { return std::get<Dict>(mData); }

    /**
     * @brief Get the element size
     * 
     * @return size_t 
     */
    auto size() const -> size_t {
        if (isList()) return toList().size();
        if (isDict()) return toDict().size();
        return 0;
    }

    /**
     * @brief Encode the object to string
     * 
     * @return std::string 
     */
    auto encode() const -> std::string;

    /**
     * @brief Encode current object's data to the target string's end
     * 
     * @param str
     * @return true 
     * @return false 
     */
    auto encodeTo(std::string &str) const -> bool;

    /**
     * @brief Assign the ben object
     * 
     * @return BenObject & 
     */
    auto operator =(const BenObject &other) -> BenObject & = default;

    auto operator =(BenObject &&) -> BenObject& = default;

    /**
     * @brief Index or emplace the value if is a dict
     * 
     * @param key 
     * @return BenObject& 
     */
    auto operator [](const std::string &key) -> BenObject & {
        return std::get<Dict>(mData)[key];
    }

    /**
     * @brief Index the value if is a dict
     * 
     * @param key 
     * @return const BenObject& 
     */
    auto operator [](std::string_view key) const -> const BenObject & {
        static BenObject null;
        auto &dict = std::get<Dict>(mData);
        auto iter = dict.find(key);
        if (iter == dict.end()) return null;
        return iter->second;
    }

    /**
     * @brief Indexing the element
     * 
     * @param idx 
     * @return BenObject& 
     */

    auto operator [](size_t idx) -> BenObject & {
        return std::get<List>(mData).at(idx);
    }

    auto operator [](size_t idx) const -> const BenObject & {
        return std::get<List>(mData).at(idx);
    }

    /**
     * @brief Compare the number
     * 
     */
    auto operator <=>(const BenObject &) const = default;

    /**
     * @brief Get the BenObject from the buffer
     * 
     * @param str 
     * @return BenObject 
     */
    static auto decode(std::string_view str) -> BenObject;

    static auto decode(const void *buffer, size_t n) -> BenObject;

    static auto decodeIn(std::string_view &current) -> BenObject;

    /**
     * @brief Create a string ben object from a raw memory
     * 
     * @param buf 
     * @param n 
     * @return BenObject 
     */
    static auto fromRawAsString(const void *buf, size_t n) -> BenObject;

    /**
     * @brief Create a list
     * 
     * @return BenObject 
     */
    static auto makeList() -> BenObject;

    /**
     * @brief Create a dict
     * 
     * @return BenObject 
     */
    static auto makeDict() -> BenObject;
private:
    std::variant<
        std::monostate,
        int64_t,
        std::string,
        List,
        Dict
    > mData;
};

// --- BenObject Impl
inline auto BenObject::encodeTo(std::string &buffer) const -> bool {
    if (isNull()) {
        return false;
    }
    if (isInt()) { //i123e
        buffer.push_back('i');
        buffer.append(std::to_string(toInt()));
        buffer.push_back('e');
        return true;
    }
    if (isString()) { //4:spam
        buffer.append(std::to_string(toString().size()));
        buffer.push_back(':');
        buffer.append(toString());
        return true;
    }
    if (isList()) { //l4:spame
        buffer.push_back('l');
        for (auto &item : toList()) {
            if (!item.encodeTo(buffer)) {
                return false;
            }
        }
        buffer.push_back('e');
        return true;
    }
    if (isDict()) { //d3:spam4:eggse
        buffer.push_back('d');
        for (auto &[key, value] : toDict()) {
            buffer.append(std::to_string(key.size()));
            buffer.push_back(':');
            buffer.append(key);
            if (!value.encodeTo(buffer)) {
                return false;
            }
        }
        buffer.push_back('e');
        return true;
    }
    // Impossible
    return false;
}

inline auto BenObject::encode() const -> std::string {
    std::string str;
    if (!encodeTo(str)) {
        str.clear();
    }
    return str;
}

// --- BenObject decode
inline auto BenObject::decodeIn(std::string_view &view) -> BenObject {
    if (view.size() < 3) {
        return BenObject();
    }
    if (::isdigit(view[0])) { //4:spam
        // String
        size_t num = 0;
        auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), num);
        if (ec != std::errc()) {
            return BenObject();
        }
        // Check ptr is :
        if (*ptr != ':') {
            return BenObject();
        }
        // Get the string
        view = view.substr(ptr - view.data() + 1);
        auto str = view.substr(0, num);
        view = view.substr(num);
        return str;
    }
    if (view.starts_with("i")) { //< i123e
        // Interger
        int64_t num = 0;
        auto [ptr, ec] = std::from_chars(view.data() + 1, view.data() + view.size(), num);
        if (ec != std::errc()) {
            return BenObject();
        }
        // Check ptr is e
        if (*ptr != 'e') {
            return BenObject();
        }
        view = view.substr(ptr - view.data() + 1);
        return num;
    }
    if (view.starts_with("l")) {
        // List
        view = view.substr(1); // drop the l
        List list;
        while (!view.starts_with('e')) { //l4:spame
            list.emplace_back(decodeIn(view));
            if (list.back().isNull()) {
                return BenObject();
            }
        }
        // Drop the e
        view = view.substr(1);
        return list;
    }
    if (view.starts_with("d")) {
        // Dict
        Dict dict;
        view = view.substr(1); // drop the d
        while (!view.starts_with('e')) { //d3:spam4:eggse
            auto key = decodeIn(view);
            if (key.isNull()) {
                return BenObject();
            }
            dict[key.toString()] = decodeIn(view);
            if (dict.at(key.toString()).isNull()) {
                return BenObject();
            }
        }
        // Drop the e
        view = view.substr(1);
        return dict;
    }
    return BenObject();
}

inline auto BenObject::decode(std::string_view view) -> BenObject {
    return decodeIn(view);
}

inline auto BenObject::decode(const void *buffer, size_t n) -> BenObject {
    return decode(std::string_view(static_cast<const char *>(buffer), n));
}

inline auto BenObject::fromRawAsString(const void *mem, size_t n) -> BenObject {
    return std::string_view(
        static_cast<const char *>(mem),
        n
    );
}

inline auto BenObject::makeList() -> BenObject {
    return List { };
}

inline auto BenObject::makeDict() -> BenObject {
    return Dict { };
}

template <>
struct std::formatter<BenObject> {
    auto parse(std::format_parse_context &ctxt) const {
        return ctxt.begin();
    }

    auto format(const BenObject &object, std::format_context &ctxt) const {
        std::string output;
        formatTo(output, object, 0);
        return std::format_to(ctxt.out(), "{}", output);
    }

    auto formatTo(std::string &output, const BenObject &cur, int indent) const -> void {
        std::string indentation(indent, ' ');
        if (cur.isInt()) {
            output += indentation + std::to_string(cur.toInt());
        } 
        else if (cur.isString()) {
            const auto &str = cur.toString();
            if (std::all_of(str.begin(), str.end(), ::isascii)) {
                output += indentation + "\"" + str + "\"";
            }
            else {
                output += indentation + "\"";
                for (unsigned char c : str) {
                    output += std::format("\\x{:02x}", c);
                }
                output += "\"";
            }
        }
        else if (cur.isList()) {
            output += indentation + "[\n";
            for (const auto &item : cur.toList()) {
                formatTo(output, item, indent + 2);
                output += ",\n";
            }
            if (!cur.toList().empty()) {
                output.pop_back();
                output.pop_back();
            }
            output += "\n" + indentation + "]";
        }
        else if (cur.isDict()) {
            output += indentation + "{\n";
            for (const auto &[key, value] : cur.toDict()) {
                output += indentation + "  \"" + key + "\": ";
                formatTo(output, value, indent + 2);
                output += ",\n";
            }
            if (!cur.toDict().empty()) {
                output.pop_back();
                output.pop_back();
            }
            output += "\n" + indentation + "}";
        }
        else {
            output += indentation + "null";
        }
    }
};