#include "transport.hpp"

#include <iostream>
#include <string>

namespace lsp {

std::optional<nlohmann::json> read_message() {
    size_t content_length = 0;
    std::string line;

    // Read HTTP-style headers until a blank line.
    while (std::getline(std::cin, line)) {
        // Strip trailing \r (headers are \r\n separated on the wire).
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break; // blank line ends the header block
        }
        const std::string prefix = "Content-Length: ";
        if (line.rfind(prefix, 0) == 0) {
            try {
                content_length = std::stoull(line.substr(prefix.size()));
            } catch (...) {
                // ignore malformed header
            }
        }
        // Content-Type is accepted but ignored; we always parse as UTF-8 JSON.
    }

    if (!std::cin || content_length == 0) {
        return std::nullopt;
    }

    std::string body(content_length, '\0');
    if (!std::cin.read(body.data(), static_cast<std::streamsize>(content_length))) {
        return std::nullopt;
    }

    try {
        return nlohmann::json::parse(body);
    } catch (...) {
        return std::nullopt;
    }
}

void write_message(const nlohmann::json& msg) {
    std::string body = msg.dump();
    std::cout
        << "Content-Length: " << body.size() << "\r\n"
        << "\r\n"
        << body;
    std::cout.flush();
}

} // namespace lsp
