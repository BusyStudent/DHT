#include "torrent.hpp"
#include "log.hpp"
#include "sha1.h"

auto Torrent::name() const -> std::string {
    return mDict["info"]["name"].toString();
}

auto Torrent::length() const -> size_t {
    auto len = mDict["info"]["length"];
    if (len.isInt()) {
        return len.toInt();
    }
    // is Multi
    size_t num = 0;
    for (const auto &f : files()) {
        num += f.length;
    }
    return num;
}

auto Torrent::hasMultiFiles() const -> bool { 
    return mDict["info"]["files"].isList();
}

auto Torrent::files() const -> std::vector<File> { 
    std::vector<File> files;
    if (auto &fs = mDict["info"]["files"]; fs.isList()) {
        for (auto &f : fs.toList()) {
            std::vector<std::string> path;
            for (auto &s : f["path"].toList()) {
                path.emplace_back(s.toString());
            }
            files.emplace_back(
                f["length"].toInt(),
                std::move(path)
            );
        }
    }
    else {
        std::vector<std::string> path;
        path.emplace_back(name());
        files.emplace_back(
            length(),
            std::move(path)
        );
    }
    return files;
}

auto Torrent::infoHash() const -> InfoHash { 
    auto encoded = mDict["info"].encode();
    char sha1[20];
    ::SHA1(sha1, encoded.data(), encoded.length());
    return InfoHash::from(sha1, sizeof(sha1));
}

auto Torrent::encode() const -> std::string {
    return mDict.encode();
}

auto Torrent::fromObject(BenObject object) -> Torrent {
    if (object.hasKey("info") && object["info"].isDict()) {  // As same as raw torrent
        Torrent t;
        t.mDict = std::move(object);
        return t;
    }
    if (!object["pieces"].isString() ||
        !object["piece length"].isInt()) {  // Invalid object
        return Torrent();
    }
    Torrent t;
    t.mDict = BenObject::makeDict();
    t.mDict["info"] = std::move(object);
    return t;
}

auto Torrent::parse(std::span<const std::byte> buffer) -> Torrent {
    return fromObject(BenObject::decode(buffer));
}
