#include "bencode.hpp"
#include <optional>
#include <format>

static std::optional<int> parse_int(std::string_view text) {
    int value;
    auto [ptr, err] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (err == std::errc{ }) {
        return value;
    }
    return std::nullopt;
}

std::string BenObject::encode() const {
    std::string ret;
    if (!encode_impl(ret)) {
        ret.clear();
    }
    return ret;
}
bool        BenObject::encode_impl(String &buf) const {
    if (is_null()) {
        return false;
    }
    if (is_integer()) {
        auto target = std::back_inserter(buf);
        std::format_to(target, "i{}e", to_integer());
        return true;
    }
    if (is_string()) {
        const auto &str = to_string();
        if (str.empty()) {
            return false;
        }

        auto target = std::back_inserter(buf);
        std::format_to(target, "{}:{}", str.size(), str);
        return true;
    }
    if (is_list()) {
        buf += 'l';
        const auto &list = to_list();
        if (list.empty()) {
            return false;
        }
        for (const auto &item : list) {
            if (!item.encode_impl(buf)) {
                return false;
            }
        }
        buf += 'e';
        return true;
    }
    if (is_dictionary()) {
        buf += 'd';
        const auto &dict = to_dictionary();
        if (dict.empty()) {
            return false;
        }
        for (const auto &[key, item] : dict) {
            auto target = std::back_inserter(buf);
            if (key.empty()) {
                return false;
            }
            std::format_to(target, "{}:{}", key.size(), key);
            if (!item.encode_impl(buf)) {
                return false;
            }
        }
        buf += 'e';
        return true;
    }
    ::abort();
}
BenObject BenObject::ParseImpl(std::string_view &text) {
    if (text.empty() || text.length() <= 2) {
        return BenObject();
    }
    else if (auto spos = text.find(':'); std::isdigit(text[0]) && spos != text.npos) {
        // String n:data
        auto val = parse_int(text.substr(0, spos));
        if (!val) {
            return BenObject();
        }
        auto n = val.value_or(0);
        if (text.length() < n + spos + 1) {
            return BenObject();
        }
        auto object = BenObject(String(text.substr(spos + 1, n)));
        // Move forward
        text = text.substr(n + spos + 1);
        return object;
    }
    else if (auto epos = text.find('e'); text.starts_with('i') && epos != text.npos) {
        // Int i123e
        auto num = text.substr(1, epos - 1);
        auto val = parse_int(num);
        if (!val) {
            return BenObject();
        }
        auto object = BenObject(val.value_or(0));
        // Move
        text = text.substr(epos + 1);
        return object;
    }
    else if (text.starts_with('l')) {
        // List lxxxe
        auto list = BenObject::NewList();
        // Move 1 for drop l
        text = text.substr(1);

        // Decode each elems
        while (!text.empty() && text[0] != 'e') {
            auto ret = ParseImpl(text);
            if (ret.is_null()) {
                // Error
                return BenObject();
            }
            list.push_back(std::move(ret));
        }
        if (list.empty()) {
            // Bencode didnot allow empty
            return BenObject();
        }
        // Move 1 for drop e
        text = text.substr(1);
        return list;
    }
    else if (text.starts_with('d')) {
        // Dict dxxxe
        auto dict = BenObject::NewDictionary();
        // Move 1 for drop d
        text = text.substr(1);

        // Decode each elems [str, value]
        while (!text.empty() && text[0] != 'e') {
            auto key = ParseImpl(text);
            if (!key.is_string()) {
                // Error
                return BenObject();
            }
            auto value = ParseImpl(text);
            if (key.is_null()) {
                // Error
                return BenObject();
            }
            dict[key.to_string()] = std::move(value);
        }
        if (dict.empty()) {
            // Bencode didnot allow empty
            return BenObject();
        }
        // Move 1 for drop e
        text = text.substr(1);
        return dict;
    }
    return BenObject();
}
BenObject BenObject::Parse(std::string_view text) {
    return ParseImpl(text);
}
void      BenObject::dump_impl(std::ostream &os, int depth) const {
    // TODO :
}

