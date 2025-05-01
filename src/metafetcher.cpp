#include "metafetcher.hpp"
#include "log.hpp"
#include "sha1.h"

auto MetadataFetcher::fetch() -> IoTask<std::vector<std::byte> > {
    PeerId id {};

    id[0] = std::byte {'-'};
    id[1] = std::byte {'I'};
    id[2] = std::byte {'L'};
    id[3] = std::byte {'0'};
    id[4] = std::byte {'0'};
    id[5] = std::byte {'0'};
    id[6] = std::byte {'0'};
    id[7] = std::byte {'0'};
    id[8] = std::byte {'-'};
    for (size_t i = 9; i < 20; i++) {
        id[i] = std::byte ( ::rand() % 256 );
    }

    if (auto res = co_await mClient.handshake(mHash, id); !res) {
        co_return unexpected(res.error());
    }
    // Check support metadata?
    if (!mClient.metadataSize()) {
        co_return unexpected(Error::Unknown);
    }
    const auto metadataSize = *mClient.metadataSize();
    const auto pieces = (metadataSize + 16383) / 16384; // 16KB per piece
    std::vector<std::byte> metadata;
    BT_LOG("Metadata size: {}, pieces: {}", metadataSize, pieces);
    for (size_t i = 0; i < pieces; i++) {
        auto message = BenObject::makeDict();
        message["msg_type"] = int64_t(0);
        message["piece"] = i;
        if (auto res = co_await mClient.sendMessageExt(*mClient.metadataId(), message); !res) {
            co_return unexpected(res.error());
        }
        while (true) {
            const auto msg = co_await mClient.recvMessage();
            if (!msg) {
                co_return unexpected(msg.error());
            }
            const auto &[id, len] = *msg;
            if (id != BtMessageId::Extended) {
                // The message is not we want, custom message
                if (auto res = co_await mClient.dropMessagePayload(); !res) {
                    co_return unexpected(res.error());
                }
                continue;
            }
            std::string ext(len, '\0');
            if (auto res = co_await mClient.recvMessagePayload(makeBuffer(ext)); res != ext.size()) {
                co_return unexpected(res.error());
            }
            // Check the ext first byte is the ext id
            if (ext[0] != BtClient::MetadataExtId) {
                continue;
            }
            std::string_view sv(ext.data() + 1, ext.size() - 1);
            const auto dict = BenObject::decodeIn(sv);
            if (!dict.isDict()) {
                co_return unexpected(Error::Unknown);
            }
            BT_LOG("Got metadata pieces {} msg {}", i, dict);
            // Parse it
            if (dict["msg_type"] != 1) { // 
                co_return unexpected(Error::Unknown);
            }
            const auto piece = dict["piece"].toInt();
            const auto totalSize = dict["total_size"].toInt();
            const auto lastPiece = i == pieces - 1;

            if (piece != i) {
                BT_LOG("Piece missmatch, expect {}, got {}", i, piece);
                co_return unexpected(Error::Unknown);
            }
            if (totalSize != metadataSize) {
                BT_LOG("Piece size is not equal to the metadata size, expect {}, got {}", metadataSize, totalSize);
                co_return unexpected(Error::Unknown);
            }
            // Check the current size we got
            if (sv.size() != 16384 && !lastPiece) {
                BT_LOG("Piece size is not equal to 16k, got {}, idx {}", sv.size(), i);
                co_return unexpected(Error::Unknown);
            }
            // Push back
            auto buf = makeBuffer(sv);
            metadata.insert(metadata.end(), buf.begin(), buf.end());
            break;
        }
    }
    // Check hash
    char sha1[20] = {0};
    ::SHA1(sha1, reinterpret_cast<const char *>(metadata.data()), metadata.size());
    if (::memcmp(sha1, &mHash, 20) != 0) {
        BT_LOG("Metadata hash is not equal to the infohash, expect {}, got {}", mHash, InfoHash::from(sha1, sizeof(sha1)));
        co_return unexpected(Error::Unknown);
    }

    // Try to decode the metadata
    // BT_LOG("Metadata: {}", BenObject::decode(metadata.data(), metadata.size()));
    co_return metadata;
}
