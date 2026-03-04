#pragma once

#include "index.hpp"
#include "protocol.hpp"

#include "parser.hpp"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lsp {

class FusionLSPServer {
public:
    // send: called for every outgoing message (responses and notifications). Required.
    // workspace_root_override: if set, used as workspace root and initialize params do not override it (for tests).
    FusionLSPServer(std::function<void(const nlohmann::json&)> send,
                    std::optional<std::string> workspace_root_override = std::nullopt);

    // Dispatch one JSON-RPC message. Sends any response via the send callback; returns nullopt.
    std::optional<nlohmann::json> handle(const nlohmann::json& msg);

    bool should_exit() const { return exit_; }

private:
    void send_message(const nlohmann::json& msg);

    std::function<void(const nlohmann::json&)> send_callback_;
    std::optional<std::string> workspace_root_override_;
    bool initialized_ = false;
    bool shutdown_    = false;
    bool exit_        = false;

    struct Document {
        std::string uri;
        int         version = 0;
        std::string text;
        // Updated by analyze_and_update() on every open/change.
        std::vector<fusion::Token> tokens;
        FileIndex                  index;
        std::vector<fusion::CallSiteArgSpans> call_sites;  // call-site arg positions for inlay hints
        std::unordered_map<std::string, std::string> imported_symbol_path;  // symbol -> lib .fusion path for go-to-def
        std::string file_path;  // absolute path (uri_to_path(uri))
    };
    std::unordered_map<std::string, Document> docs_;

    std::string workspace_root_;
    // Lazily-populated analysis for .fusion files on disk that aren't open in the editor.
    struct CachedAnalysis {
        FileIndex index;
        std::vector<fusion::Token> tokens;
    };
    std::unordered_map<std::string, CachedAnalysis> ws_cache_;

    // Re-analyse a document, store tokens+index, and push diagnostics.
    void analyze_and_update(Document& doc);

    // Return index for path: open doc, ws_cache_, or load from disk and cache. nullptr if not found.
    const FileIndex* get_index_for_path(const std::string& path);

    // Return tokens for path (open doc or cache); load and cache if needed. nullptr if not found.
    const std::vector<fusion::Token>* get_tokens_for_path(const std::string& path);

    // ---- Request/notification handlers ----
    nlohmann::json on_initialize(const nlohmann::json& params);

    void on_did_open(const nlohmann::json& params);
    void on_did_change(const nlohmann::json& params);
    void on_did_close(const nlohmann::json& params);

    std::optional<nlohmann::json> on_hover(const nlohmann::json& params);
    std::optional<nlohmann::json> on_definition(const nlohmann::json& params);
    nlohmann::json                on_document_symbols(const nlohmann::json& params);
    nlohmann::json                on_completion(const nlohmann::json& params);
    std::optional<nlohmann::json> on_signature_help(const nlohmann::json& params);
    nlohmann::json                on_document_highlight(const nlohmann::json& params);
    nlohmann::json                on_references(const nlohmann::json& params);
    std::optional<nlohmann::json> on_prepare_rename(const nlohmann::json& params);
    std::optional<nlohmann::json> on_rename(const nlohmann::json& params);
    nlohmann::json                on_semantic_tokens_full(const nlohmann::json& params);
    nlohmann::json                on_workspace_symbols(const nlohmann::json& params);
    nlohmann::json                on_folding_range(const nlohmann::json& params);
    nlohmann::json                on_inlay_hint(const nlohmann::json& params);
};

} // namespace lsp
