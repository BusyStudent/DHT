/**
 * @file torrent.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The torrent helper class
 * @version 0.1
 * @date 2025-03-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include "bencode.hpp"
#include "nodeid.hpp"

/**
 * @brief The Torrent class
 * 
 */
class Torrent {
public:
    struct File {
        size_t length;
        std::vector<std::string> paths;
    };

    Torrent() = default;
    Torrent(const Torrent &) = default;
    Torrent(Torrent &&) = default;

    /**
     * @brief Get the name of the torrent
     * 
     * @return std::string 
     */
    auto name() const -> std::string;
    auto length() const -> std::optional<size_t>;
    auto hasMultiFiles() const -> bool;
    auto files() const -> std::vector<File>;
    auto pieces() const -> std::span<const std::byte>;
    auto infoHash() const -> InfoHash;

    static auto fromObject(BenObject object) -> Torrent;
    static auto parse(std::span<const std::byte> buffer) -> Torrent;

    explicit operator bool() const noexcept {
        return mDict.isNull();
    }
private:
    BenObject mDict;
};

template <>
struct std::formatter<Torrent> {
    constexpr auto parse(std::format_parse_context &ctxt) const {
        return ctxt.begin();
    }
    auto format(const Torrent &torrent, std::format_context &ctxt) const {
        std::string text;
        if (torrent.hasMultiFiles()) {
            text = std::format("Torrent[name={}, files=[", torrent.name());
            for (const auto &file : torrent.files()) {
                // Format each file's length and path
                text += std::format("{{length={}, path=[", file.length);
                // Join path components
                for (const auto &path : file.paths) {
                    text += std::format("\"{}\", ", path);
                }
                if (!file.paths.empty()) {
                    text.erase(text.length() - 2);
                }
                text += "]}, ";
            }
            if (!torrent.files().empty()) {
                text.erase(text.length() - 2);
            }
            text += "]]";
        }
        else {
            auto length = torrent.length();
            text = std::format("Torrent[name={}, length={}]", 
                torrent.name(),
                length.value_or(0)
            );
        }
        return std::format_to(ctxt.out(), "{}", text);
    }
};