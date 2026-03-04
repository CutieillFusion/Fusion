#include "server.hpp"

#include "analysis.hpp"
#include "features.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace lsp {

// ---- URI / path helpers ----

// Convert a "file:///..." URI to an absolute filesystem path.
// Decodes percent-encoded characters (%20 etc.).
static std::string uri_to_path(const std::string& uri) {
    // file:///path  → /path  (authority is empty, path starts with '/')
    if (uri.size() < 8 || uri.substr(0, 8) != "file:///") return "";
    std::string raw = uri.substr(7);  // keep the leading '/'
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(raw[i+1]), lo = hex(raw[i+2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += raw[i];
    }
    return out;
}

// Convert an absolute filesystem path to a "file:///..." URI.
static std::string path_to_uri(const std::string& path) {
    return "file://" + path;
}

// Recursively find all .fusion files under root.
static std::vector<std::string> find_fusion_files(const std::string& root) {
    std::vector<std::string> result;
    if (root.empty()) return result;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() == ".fusion")
            result.push_back(entry.path().string());
    }
    return result;
}

// ---- Incremental text-edit helper ----

// Convert a 0-indexed LSP (line, character) to a byte offset in text.
static size_t line_char_to_offset(const std::string& text, int line, int ch) {
    int cur = 0;
    size_t i = 0;
    while (i < text.size() && cur < line) {
        if (text[i++] == '\n') cur++;
    }
    for (int c = 0; c < ch && i < text.size() && text[i] != '\n'; c++, i++);
    return i;
}

static void apply_text_edit(std::string& text, const nlohmann::json& change) {
    const std::string new_text = change["text"].get<std::string>();
    if (!change.contains("range")) {
        text = new_text;
        return;
    }
    const auto& r = change["range"];
    size_t start = line_char_to_offset(text,
        r["start"]["line"].get<int>(), r["start"]["character"].get<int>());
    size_t end   = line_char_to_offset(text,
        r["end"]["line"].get<int>(),   r["end"]["character"].get<int>());
    if (start > text.size()) start = text.size();
    if (end   > text.size()) end   = text.size();
    if (start > end)         start = end;
    text.replace(start, end - start, new_text);
}

// ---- Send seam (single point for all outgoing JSON) ----

void FusionLSPServer::send_message(const nlohmann::json& msg) {
    send_callback_(msg);
}

// ---- Core dispatch ----

std::optional<nlohmann::json> FusionLSPServer::handle(const nlohmann::json& msg) {
    if (!msg.contains("method")) return std::nullopt;  // stray response, ignore

    const std::string    method = msg["method"];
    const bool           is_req = msg.contains("id");
    const nlohmann::json id     = is_req ? msg["id"] : nlohmann::json(nullptr);
    const nlohmann::json params = msg.value("params", nlohmann::json::object());

    if (!initialized_ && method != "initialize" && method != "exit") {
        if (is_req) { send_message(make_error(id, -32002, "Server not yet initialized")); }
        return std::nullopt;
    }
    if (shutdown_ && method != "exit") {
        if (is_req) { send_message(make_error(id, -32600, "Invalid request after shutdown")); }
        return std::nullopt;
    }

    // ---- Lifecycle ----
    if (method == "initialize") {
        auto result = on_initialize(params);
        initialized_ = true;
        send_message(make_response(id, std::move(result)));
        return std::nullopt;
    }
    if (method == "initialized")        { return std::nullopt; }
    if (method == "shutdown")           { shutdown_ = true; send_message(make_response(id, nullptr)); return std::nullopt; }
    if (method == "exit")               { exit_ = true;     return std::nullopt; }
    if (method == "$/cancelRequest")    { return std::nullopt; }

    // ---- Document sync (notifications) ----
    if (method == "textDocument/didOpen")   { on_did_open(params);   return std::nullopt; }
    if (method == "textDocument/didChange") { on_did_change(params); return std::nullopt; }
    if (method == "textDocument/didClose")  { on_did_close(params);  return std::nullopt; }

    // ---- Navigation & intelligence (requests) ----
    if (method == "textDocument/hover") {
        auto r = on_hover(params);
        send_message(make_response(id, r ? *r : nlohmann::json(nullptr)));
        return std::nullopt;
    }
    if (method == "textDocument/definition") {
        auto r = on_definition(params);
        send_message(make_response(id, r ? *r : nlohmann::json(nullptr)));
        return std::nullopt;
    }
    if (method == "textDocument/documentSymbol") {
        send_message(make_response(id, on_document_symbols(params)));
        return std::nullopt;
    }
    if (method == "textDocument/completion") {
        send_message(make_response(id, on_completion(params)));
        return std::nullopt;
    }
    if (method == "textDocument/signatureHelp") {
        auto r = on_signature_help(params);
        send_message(make_response(id, r ? *r : nlohmann::json(nullptr)));
        return std::nullopt;
    }
    if (method == "textDocument/documentHighlight") {
        send_message(make_response(id, on_document_highlight(params)));
        return std::nullopt;
    }
    if (method == "textDocument/references") {
        send_message(make_response(id, on_references(params)));
        return std::nullopt;
    }
    if (method == "textDocument/prepareRename") {
        auto r = on_prepare_rename(params);
        send_message(make_response(id, r ? *r : nlohmann::json(nullptr)));
        return std::nullopt;
    }
    if (method == "textDocument/rename") {
        auto r = on_rename(params);
        send_message(make_response(id, r ? *r : nlohmann::json(nullptr)));
        return std::nullopt;
    }
    if (method == "textDocument/semanticTokens/full") {
        send_message(make_response(id, on_semantic_tokens_full(params)));
        return std::nullopt;
    }
    if (method == "workspace/symbol") {
        send_message(make_response(id, on_workspace_symbols(params)));
        return std::nullopt;
    }
    if (method == "textDocument/foldingRange") {
        send_message(make_response(id, on_folding_range(params)));
        return std::nullopt;
    }
    if (method == "textDocument/inlayHint") {
        send_message(make_response(id, on_inlay_hint(params)));
        return std::nullopt;
    }

    if (is_req) { send_message(make_error(id, -32601, "Method not found: " + method)); }
    return std::nullopt;
}

// ---- Initialize ----

FusionLSPServer::FusionLSPServer(std::function<void(const nlohmann::json&)> send,
                                 std::optional<std::string> workspace_root_override)
    : send_callback_(std::move(send)), workspace_root_override_(std::move(workspace_root_override)) {
    if (workspace_root_override_.has_value()) {
        workspace_root_ = workspace_root_override_.value();
    }
}

nlohmann::json FusionLSPServer::on_initialize(const nlohmann::json& params) {
    // Extract workspace root for disk-based workspace/symbol search (unless overridden for tests).
    if (!workspace_root_override_.has_value()) {
        if (params.contains("rootUri") && params["rootUri"].is_string()) {
            workspace_root_ = uri_to_path(params["rootUri"].get<std::string>());
        } else if (params.contains("rootPath") && params["rootPath"].is_string()) {
            workspace_root_ = params["rootPath"].get<std::string>();
        }
    }

    return {
        {"capabilities", {
            {"textDocumentSync",          2},   // Incremental
            {"hoverProvider",             true},
            {"definitionProvider",        true},
            {"documentSymbolProvider",    true},
            {"referencesProvider",        true},
            {"documentHighlightProvider", true},
            {"renameProvider", {{"prepareProvider", true}}},
            {"completionProvider", {
                {"triggerCharacters", {".", "(", ","}},
                {"resolveProvider",   false},
            }},
            {"signatureHelpProvider", {
                {"triggerCharacters",   {"(", ","}},
                {"retriggerCharacters", {")"}},
            }},
            {"semanticTokensProvider", {
                {"legend", {
                    {"tokenTypes",     {"keyword","type","function","variable","parameter","struct","comment","module"}},
                    {"tokenModifiers", nlohmann::json::array()},
                }},
                {"full", true},
            }},
            {"workspaceSymbolProvider", true},
            {"foldingRangeProvider",    true},
            {"inlayHintProvider",       true},
        }},
        {"serverInfo", {{"name", "fusion-lsp"}, {"version", "0.4.0"}}},
    };
}

// ---- Document sync ----

void FusionLSPServer::analyze_and_update(Document& doc) {
    doc.file_path = uri_to_path(doc.uri);
    auto result = analyze(doc.text, doc.file_path);
    doc.tokens                = std::move(result.tokens);
    doc.index                 = std::move(result.index);
    doc.call_sites            = std::move(result.call_sites);
    doc.imported_symbol_path  = std::move(result.imported_symbol_path);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : result.diags) arr.push_back(to_json(d));
    send_message(make_notification("textDocument/publishDiagnostics", {
        {"uri",         doc.uri},
        {"version",     doc.version},
        {"diagnostics", arr},
    }));
}

void FusionLSPServer::on_did_open(const nlohmann::json& params) {
    const auto& td = params["textDocument"];
    Document doc;
    doc.uri     = td["uri"];
    doc.version = td.value("version", 0);
    doc.text    = td["text"];
    docs_[doc.uri] = std::move(doc);
    analyze_and_update(docs_[params["textDocument"]["uri"]]);
}

void FusionLSPServer::on_did_change(const nlohmann::json& params) {
    const auto& td  = params["textDocument"];
    const std::string uri = td["uri"];
    auto it = docs_.find(uri);
    if (it == docs_.end()) return;
    Document& doc = it->second;
    doc.version   = td.value("version", doc.version);
    const auto& changes = params.value("contentChanges", nlohmann::json::array());
    // Apply each change in order (may be incremental range edits or a full replacement).
    for (const auto& change : changes)
        apply_text_edit(doc.text, change);
    analyze_and_update(doc);
}

void FusionLSPServer::on_did_close(const nlohmann::json& params) {
    const std::string uri = params["textDocument"]["uri"];
    docs_.erase(uri);
    send_message(make_notification("textDocument/publishDiagnostics", {
        {"uri",         uri},
        {"diagnostics", nlohmann::json::array()},
    }));
}

// ---- Feature helpers ----

// Extract (uri, line, character) from a TextDocumentPositionParams.
static std::tuple<std::string, int, int> cursor(const nlohmann::json& params) {
    return {
        params["textDocument"]["uri"],
        params["position"]["line"],
        params["position"]["character"],
    };
}

// ---- Hover ----

std::optional<nlohmann::json> FusionLSPServer::on_hover(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    auto it = docs_.find(uri);
    if (it == docs_.end() || it->second.index.empty()) return std::nullopt;
    return hover(it->second.tokens, it->second.index, line, ch);
}

// ---- Go-to-definition ----

const FileIndex* FusionLSPServer::get_index_for_path(const std::string& path) {
    for (const auto& [u, doc] : docs_) {
        if (uri_to_path(u) == path) return &doc.index;
    }
    auto it = ws_cache_.find(path);
    if (it != ws_cache_.end()) return &it->second.index;
    std::ifstream f(path);
    if (!f.is_open()) return nullptr;
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto result = analyze(src, path);
    ws_cache_[path].index = std::move(result.index);
    ws_cache_[path].tokens = std::move(result.tokens);
    return &ws_cache_[path].index;
}

const std::vector<fusion::Token>* FusionLSPServer::get_tokens_for_path(const std::string& path) {
    for (const auto& [u, doc] : docs_) {
        if (uri_to_path(u) == path) return &doc.tokens;
    }
    auto it = ws_cache_.find(path);
    if (it != ws_cache_.end()) return &it->second.tokens;
    std::ifstream f(path);
    if (!f.is_open()) return nullptr;
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto result = analyze(src, path);
    ws_cache_[path].index = std::move(result.index);
    ws_cache_[path].tokens = std::move(result.tokens);
    return &ws_cache_[path].tokens;
}

std::optional<nlohmann::json> FusionLSPServer::on_definition(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    auto it = docs_.find(uri);
    if (it == docs_.end() || it->second.index.empty()) return std::nullopt;
    const Document& doc = it->second;
    auto get_index = [this](const std::string& p) { return get_index_for_path(p); };
    return definition(doc.tokens, doc.index, uri, line, ch,
                      doc.imported_symbol_path, doc.file_path,
                      get_index, path_to_uri);
}

// ---- Document symbols ----

nlohmann::json FusionLSPServer::on_document_symbols(const nlohmann::json& params) {
    const std::string uri = params["textDocument"]["uri"];
    auto it = docs_.find(uri);
    if (it == docs_.end() || it->second.index.empty()) return nlohmann::json::array();
    return document_symbols(it->second.index);
}

// ---- Completion ----

nlohmann::json FusionLSPServer::on_completion(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    auto it = docs_.find(uri);
    if (it == docs_.end()) return {{"isIncomplete", false}, {"items", nlohmann::json::array()}};
    return completion(it->second.tokens, it->second.index, line, ch);
}

// ---- Signature help ----

std::optional<nlohmann::json> FusionLSPServer::on_signature_help(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    auto it = docs_.find(uri);
    if (it == docs_.end() || it->second.index.empty()) return std::nullopt;
    return signature_help(it->second.tokens, it->second.index, line, ch);
}

// ---- Document highlight ----

nlohmann::json FusionLSPServer::on_document_highlight(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    auto it = docs_.find(uri);
    if (it == docs_.end()) return nlohmann::json::array();
    return document_highlight(it->second.tokens, it->second.index, line, ch);
}

// ---- References ----

nlohmann::json FusionLSPServer::on_references(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    auto it = docs_.find(uri);
    if (it == docs_.end()) return nlohmann::json::array();
    const Document& doc = it->second;
    bool inc_decl = params.value("context", nlohmann::json::object())
                          .value("includeDeclaration", true);

    const fusion::Token* tok = token_at(doc.tokens, line, ch);
    if (!tok) return nlohmann::json::array();

    // Cross-file references for structs and global functions (e.g. make_leaf, create_moons).
    bool is_struct = doc.index.struct_by_name.count(tok->ident) != 0;
    bool is_global_function = false;
    if (!is_struct) {
        auto range = doc.index.by_name.equal_range(tok->ident);
        for (auto it = range.first; it != range.second; ++it) {
            const SymInfo& s = doc.index.syms[it->second];
            if (s.kind == SymKind::Function && s.scope.empty()) {
                is_global_function = true;
                break;
            }
        }
    }
    bool do_cross_file = is_struct || is_global_function;

    if (do_cross_file) {
        nlohmann::json arr = nlohmann::json::array();
        std::string current_path = doc.file_path.empty() ? uri_to_path(uri) : doc.file_path;

        // Collect paths: open docs + all .fusion files under workspace.
        std::vector<std::string> paths;
        for (const auto& [u, d] : docs_) {
            std::string p = d.file_path.empty() ? uri_to_path(u) : d.file_path;
            if (!p.empty()) paths.push_back(p);
        }
        for (const std::string& path : find_fusion_files(workspace_root_)) {
            if (std::find(paths.begin(), paths.end(), path) == paths.end())
                paths.push_back(path);
        }

        for (const std::string& path : paths) {
            const std::vector<fusion::Token>* toks = get_tokens_for_path(path);
            if (!toks) continue;
            nlohmann::json file_refs = reference_locations_in_file(*toks, tok->ident, path_to_uri(path));
            for (auto& loc : file_refs)
                arr.push_back(std::move(loc));
        }
        return arr;
    }

    return references(doc.tokens, doc.index, uri, line, ch, inc_decl);
}

// ---- Prepare rename ----

std::optional<nlohmann::json> FusionLSPServer::on_prepare_rename(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    auto it = docs_.find(uri);
    if (it == docs_.end()) return std::nullopt;
    return prepare_rename(it->second.tokens, it->second.index, line, ch);
}

// ---- Rename ----

std::optional<nlohmann::json> FusionLSPServer::on_rename(const nlohmann::json& params) {
    auto [uri, line, ch] = cursor(params);
    const std::string new_name = params.value("newName", "");
    if (new_name.empty()) return std::nullopt;
    auto it = docs_.find(uri);
    if (it == docs_.end()) return std::nullopt;
    return rename_symbol(it->second.tokens, it->second.index, uri, line, ch, new_name);
}

// ---- Semantic tokens ----

nlohmann::json FusionLSPServer::on_semantic_tokens_full(const nlohmann::json& params) {
    const std::string uri = params["textDocument"]["uri"];
    auto it = docs_.find(uri);
    if (it == docs_.end()) return {{"data", nlohmann::json::array()}};
    return semantic_tokens_full(it->second.tokens, it->second.index, it->second.text);
}

// ---- Workspace symbols ----

// Append all matching symbols from an index to the result array.
static void collect_symbols(const FileIndex& index,
                             const std::string& uri,
                             const std::string& query,
                             nlohmann::json& arr) {
    for (const auto& s : index.syms) {
        if (!s.scope.empty() || s.line == 0) continue;  // skip locals
        if (!query.empty()) {
            std::string lo_name = s.name, lo_q = query;
            for (auto& c : lo_name) c = static_cast<char>(std::tolower(c));
            for (auto& c : lo_q)   c = static_cast<char>(std::tolower(c));
            if (lo_name.find(lo_q) == std::string::npos) continue;
        }
        int l = (s.line > 0) ? static_cast<int>(s.line) - 1 : 0;
        int c = (s.col  > 0) ? static_cast<int>(s.col)  - 1 : 0;
        Range r{Position{l,c}, Position{l, c + static_cast<int>(s.name.size())}};
        arr.push_back({
            {"name",     s.name},
            {"kind",     static_cast<int>(s.kind)},
            {"location", {{"uri", uri}, {"range", to_json(r)}}},
        });
    }
}

nlohmann::json FusionLSPServer::on_workspace_symbols(const nlohmann::json& params) {
    const std::string query = params.value("query", "");
    nlohmann::json arr = nlohmann::json::array();

    // Search open documents (live in-memory versions).
    for (const auto& [uri, doc] : docs_) {
        if (doc.index.empty()) continue;
        collect_symbols(doc.index, uri, query, arr);
    }

    // Search disk files not currently open in the editor.
    for (const auto& path : find_fusion_files(workspace_root_)) {
        const std::string uri = path_to_uri(path);
        if (docs_.count(uri)) continue;  // already covered above

        // Lazily build and cache the analysis (index + tokens).
        if (!ws_cache_.count(path)) {
            std::ifstream f(path);
            if (!f.is_open()) continue;
            std::string src((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
            auto result = analyze(src, path);
            ws_cache_[path].index = std::move(result.index);
            ws_cache_[path].tokens = std::move(result.tokens);
        }

        const FileIndex& fidx = ws_cache_[path].index;
        if (!fidx.empty()) collect_symbols(fidx, uri, query, arr);
    }

    return arr;
}

// ---- Folding ranges ----

nlohmann::json FusionLSPServer::on_folding_range(const nlohmann::json& params) {
    const std::string uri = params["textDocument"]["uri"];
    auto it = docs_.find(uri);
    if (it == docs_.end()) return nlohmann::json::array();
    return folding_ranges(it->second.tokens);
}

// ---- Inlay hints ----

nlohmann::json FusionLSPServer::on_inlay_hint(const nlohmann::json& params) {
    const std::string uri = params["textDocument"]["uri"];
    auto it = docs_.find(uri);
    if (it == docs_.end()) return nlohmann::json::array();

    // LSP passes a visible range; hints outside it can be omitted for performance.
    const auto& r = params.value("range", nlohmann::json::object());
    int sl = r.value("start", nlohmann::json::object()).value("line", 0);
    int el = r.value("end",   nlohmann::json::object()).value("line", 1 << 20);

    return inlay_hints(it->second.tokens, it->second.index, it->second.call_sites, sl, el);
}

} // namespace lsp
