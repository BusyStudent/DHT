#pragma once

#include <variant>
#include <vector>
#include <string>
#include <map>

class BenObject {
    public:
        using Dictionary = std::map<std::string, BenObject>;
        using List       = std::vector<BenObject>;
        using String     = std::string;
        using Null       = std::monostate;
        using Int        = int;

        BenObject() : data(Null{ }) { }
        BenObject(Int value) : data(value) { }
        BenObject(const char *value) : data(value) { }
        BenObject(const String &value) : data(value) { }
        BenObject(const List &list) : data(list) { }
        BenObject(const Dictionary &d) : data(d) { }
        BenObject(const BenObject &) = default;
        BenObject(BenObject &&) = default;
        BenObject(String &&value) : data(value) { }
        BenObject(List   &&list) : data(list) { }
        BenObject(Dictionary &&d) : data(d) { }
        ~BenObject() = default;
    public:
        template <typename T>
        bool is() const noexcept {
            return std::holds_alternative<T>(data);
        }
        bool is_null() const noexcept {
            return is<Null>();
        }
        bool is_integer() const noexcept {
            return is<Int>();
        }
        bool is_string() const noexcept {
            return is<String>();
        }
        bool is_list() const noexcept {
            return is<List>();
        }
        bool is_dictionary() const noexcept {
            return is<Dictionary>();
        }
        const Int &to_integer() const {
            return std::get<Int>(data);
        }
        const String &to_string() const {
            return std::get<String>(data);
        }
        const List   &to_list() const {
            return std::get<List>(data);
        }
        const Dictionary &to_dictionary() const {
            return std::get<Dictionary>(data);
        }
        Int &to_integer() {
            return std::get<Int>(data);
        }
        String &to_string() {
            return std::get<String>(data);
        }
        List   &to_list() {
            return std::get<List>(data);
        }
        Dictionary &to_dictionary() {
            return std::get<Dictionary>(data);
        }
    public: //< Helper parts
        template <typename T>
        void push_back(T &&v) {
            to_list().push_back(std::forward<T>(v));
        }
        bool empty() const {
            if (is_list()) {
                return to_list().empty();
            }
            if (is_dictionary()) {
                return to_dictionary().empty();
            }
            return true;
        }
        bool contains(const String &s) const {
            return to_dictionary().contains(s);
        }
        
        const BenObject &operator [](int idx) const {
            return to_list().at(idx);
        }
        const BenObject &operator [](const String &idx) const {
            return to_dictionary().at(idx);
        }

        BenObject &operator [](int idx) {
            return to_list().at(idx);
        }
        BenObject &operator [](const String &idx) {
            return to_dictionary()[idx];
        }
        BenObject &operator =(const BenObject &) = default;
        BenObject &operator =(BenObject      &&) = default;

        String     encode() const;
    public:
        static BenObject NewInt(Int v = { }) {
            return BenObject(v);
        }
        static BenObject NewString(const String &s = { }) {
            return BenObject(s);
        }
        static BenObject NewList() {
            return BenObject(List());
        }
        static BenObject NewDictionary() {
            return BenObject(Dictionary());
        }
        static BenObject Parse(std::string_view text);
    private:
        static BenObject ParseImpl(std::string_view &current);
        bool      encode_impl(String &buffer) const;
        void      dump_impl(std::ostream &os, int depth) const;

        using Storage = std::variant<Null, Dictionary, List, String, Int>;
        Storage data;
    friend std::ostream &operator <<(std::ostream &, const BenObject &object);
};
