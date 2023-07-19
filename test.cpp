#include "illias.hpp"
#include "dht.hpp"
#include <iostream>
#include <format>

using namespace Illias;

int main() {
    Illias::InitSocket();
    DHTClient client;
    client.run();
}
