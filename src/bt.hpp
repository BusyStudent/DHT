/**
 * @file bt.hp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The Bittorrent peer protocol
 * @version 0.1
 * @date 2025-03-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once
#include "bencode.hpp"
#include "nodeid.hpp"
#include "net.hpp"
#include <format>
#include <array>

using PeerId = std::array<std::byte, 20>;

enum class BtMessageId : uint8_t {
    KeepAlive = 0,
    Choke = 1,
    Unchoke = 2,
    Interested = 3,
    NotInterested = 4,
    Have = 5,
    Bitfield = 6,
    Request = 7,
    Piece = 8,
    Cancel = 9,
    Port = 10,
    Extended = 20, // The message from extension
};

/**
 * @brief Bittorrent handshake messsage "Bittorrent protocol" + reserved bytes + infohash + peerid
 * 
 */
struct alignas (1) BtHandshakeMessage {
    std::byte pstrlen;
    std::array<std::byte, 19> pstr;
    std::array<std::byte, 8> reserved;
    InfoHash infoHash;
    PeerId peerId;
};

static_assert(sizeof(BtHandshakeMessage) == 68);


/**
 * @brief Using the Bittorrent peer protocol
 * 
 */
class BtClient {
public:
    BtClient(TcpClient client);

    static constexpr int MetadataExtId = 0x1; // The metadata extension id in our side we used
    static constexpr int PexExtId = 0x2; // The pex extension id in our side we used

    auto handshake(const InfoHash &hash, const PeerId &peerId) -> IoTask<void>;

    /**
     * @brief Send an message to the remote peer
     * 
     * @param id The message id
     * @param data The message data
     * @return IoTask<void> 
     */
    auto sendMessage(BtMessageId id, std::span<const std::byte> data) -> IoTask<void>;

    /**
     * @brief Send an extension message to the remote peer
     * 
     * @param extId The extension id (0 on handshake)
     * @param message 
     * @return IoTask<void> 
     */
    auto sendMessageExt(int extId, const BenObject &message) -> IoTask<void>;

    /**
     * @brief Receive a message header from the remote peer
     * 
     * @return IoTask<std::pair<BtMessageId, size_t> > 
     */
    auto recvMessage() -> IoTask<std::pair<BtMessageId, size_t> >;

    /**
     * @brief Receive a message payload from the remote peer (call it until the payload is fully received, 0 is returned)
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto recvMessagePayload(std::span<std::byte> buffer) -> IoTask<size_t>;
    
    /**
     * @brief Receive an extended message id, (use like recvMessage -> (id == Extended) -> recvMessageExtId -> recvMessagePayload)
     * 
     * @return IoTask<int> 
     */
    auto recvMessageExtId() -> IoTask<int>;

    /**
     * @brief Discard the current received message payload
     * 
     * @return IoTask<void> 
     */
    auto dropMessagePayload() -> IoTask<void>;

    /**
     * @brief Get the raw remote extension object
     * 
     * @return const BenObject& 
     */
    auto remoteExtensionObject() const -> const BenObject & {
        return mRemoteExtension;
    }

    auto hasExtension() const -> bool {
        return !mRemoteExtension.isNull() && mRemoteExtension["m"].isDict();
    }

    auto hasPex() const -> bool {
        if (!hasExtension()) {
            return false;
        }
        return !mRemoteExtension["m"]["ut_pex"].isNull();
    }

    auto hasMetadataExt() const -> bool {
        if (!hasExtension()) {
            return false;
        }
        return !mRemoteExtension["m"]["ut_metadata"].isNull();
    }

    auto peerId() const -> const PeerId & {
        return mRemotePeerId;
    }

    /**
     * @brief Get the id of the metadata extension
     * 
     * @return std::optional<int> 
     */
    auto metadataId() const -> std::optional<int> {
        if (!hasMetadataExt()) {
            return std::nullopt;
        }
        auto &val = mRemoteExtension["m"]["ut_metadata"];
        if (val.isInt()) {
            return val.toInt();
        }
        return std::nullopt;
    }

    /**
     * @brief Get the size of the metadata
     * 
     * @return std::optional<size_t> 
     */
    auto metadataSize() const -> std::optional<size_t> {
        if (!hasMetadataExt()) {
            return std::nullopt;
        }
        auto &metadataSize = mRemoteExtension["metadata_size"];
        if (metadataSize.isInt()) {
            return metadataSize.toInt();
        }
        return std::nullopt;
    }
private:
    TcpClient mClient;
    PeerId mRemotePeerId;
    BenObject mRemoteExtension;

    // State
    std::optional<size_t> mPayloadLeft; // The length of the current message
};

template <>
struct std::formatter<BtMessageId> {
    constexpr auto parse(std::format_parse_context &ctxt) const {
        return ctxt.begin();
    }
    auto format(const BtMessageId &id, std::format_context &ctxt) const {
        std::string_view sv;
        switch (id) {
            case BtMessageId::KeepAlive: sv = "KeepAlive"; break;
            case BtMessageId::Choke: sv = "Choke"; break;
            case BtMessageId::Unchoke: sv = "Unchoke"; break;
            case BtMessageId::Interested: sv = "Interested"; break;
            case BtMessageId::NotInterested: sv = "NotInterested"; break;
            case BtMessageId::Have: sv = "Have"; break;
            case BtMessageId::Bitfield: sv = "Bitfield"; break;
            case BtMessageId::Request: sv = "Request"; break;
            case BtMessageId::Piece: sv = "Piece"; break;
            case BtMessageId::Cancel: sv = "Cancel"; break;
            case BtMessageId::Port: sv = "Port"; break;
            case BtMessageId::Extended: sv = "Extended"; break;
            default: sv = "Unknown"; break;
        }
        return std::format_to(ctxt.out(), "{}", sv);
    }
};