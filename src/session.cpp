#include <ilias/task.hpp>
#include "session.hpp"

using namespace std::literals;

DhtSession::DhtSession(IoContext &ctxt, const IPEndpoint &listen) : mCtxt(ctxt) {

}

auto DhtSession::run() -> Task<void> { 
    co_return;
}

auto DhtSession::sendKrpc(const BenObject &message, const IPEndpoint &endpoint)
    -> IoTask<std::pair<BenObject, IPEndpoint> > 
{
    static_assert(std::movable<Result<std::pair<BenObject, IPEndpoint>>>);
    auto content = message.encode();
    auto id = getMessageTransactionId(message);
    auto [sender, receiver] = oneshot::channel<std::pair<BenObject, IPEndpoint> >();
    auto [it, emplace] = mPendingQueries.try_emplace(id, std::move(sender));
    if (!emplace) {
        DHT_LOG("Exisiting id in queries ?, may bug");
    }
    auto res = co_await (receiver | setTimeout(1s));
    if (!res) { // Tiemout !, remove it in map
        mPendingQueries.erase(it);
    }
    co_return res;
}
