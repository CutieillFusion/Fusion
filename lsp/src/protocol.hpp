#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace lsp {

// ---- Core position types (all 0-indexed per LSP spec) ----

struct Position {
    int line = 0;
    int character = 0;
};

struct Range {
    Position start;
    Position end;
};

// ---- Diagnostics ----

enum class DiagnosticSeverity {
    Error       = 1,
    Warning     = 2,
    Information = 3,
    Hint        = 4,
};

struct Diagnostic {
    Range             range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string        message;
    std::string        source = "fusion";
};

// ---- JSON serialization helpers ----

inline nlohmann::json to_json(const Position& p) {
    return {{"line", p.line}, {"character", p.character}};
}

inline nlohmann::json to_json(const Range& r) {
    return {{"start", to_json(r.start)}, {"end", to_json(r.end)}};
}

inline nlohmann::json to_json(const Diagnostic& d) {
    return {
        {"range",    to_json(d.range)},
        {"severity", static_cast<int>(d.severity)},
        {"message",  d.message},
        {"source",   d.source},
    };
}

// ---- JSON-RPC message builders ----

// Successful response to a request.
inline nlohmann::json make_response(const nlohmann::json& id, nlohmann::json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

// Server-to-client push notification (no id field).
inline nlohmann::json make_notification(const std::string& method, nlohmann::json params) {
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}};
}

// Error response to a request.
inline nlohmann::json make_error(const nlohmann::json& id, int code, const std::string& msg) {
    return {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"error",   {{"code", code}, {"message", msg}}},
    };
}

} // namespace lsp
