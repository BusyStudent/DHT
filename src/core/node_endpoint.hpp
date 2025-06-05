#include "node_id.hpp"
#include <ilias/net.hpp>

using ilias::IPEndpoint;

struct NodeEndpoint {
    NodeId id;
    IPEndpoint ip;

    auto operator <=>(const NodeEndpoint &) const noexcept = default;
};


template <>
struct std::formatter<NodeEndpoint> {
    constexpr auto parse(std::format_parse_context &ctxt) const {
        return ctxt.begin();
    }

    auto format(const NodeEndpoint &endpoint, std::format_context &ctxt) const {
        return std::format_to(ctxt.out(), "{} :{}", endpoint.id, endpoint.ip);
    }
};