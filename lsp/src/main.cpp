#include "server.hpp"
#include "transport.hpp"

#include <iostream>

int main() {
    // Disable stdio sync — stdin/stdout are used exclusively for JSON-RPC.
    // stderr remains available for debug logging.
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    lsp::FusionLSPServer server([](const nlohmann::json& msg) { lsp::write_message(msg); });

    while (!server.should_exit()) {
        auto msg = lsp::read_message();
        if (!msg) {
            break; // EOF: client disconnected
        }
        server.handle(*msg);
    }

    return 0;
}
