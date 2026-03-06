#pragma once

#include <nlohmann/json.hpp>
#include <optional>

namespace lsp {

// Read one JSON-RPC message from stdin (Content-Length framing). Blocking.
// Returns nullopt on EOF or unrecoverable I/O error.
std::optional<nlohmann::json> read_message();

// Write one JSON-RPC message to stdout (Content-Length framing) and flush.
void write_message(const nlohmann::json& msg);

} // namespace lsp
