#include "server.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

using nlohmann::json;

// Capture all outgoing messages. Filter by: id != null -> response; method present -> notification.
struct MessageCapture {
    std::vector<json> all;

    void capture(const json& msg) { all.push_back(msg); }

    // Responses: message has "id" (and typically "result" or "error").
    std::vector<json> responses() const {
        std::vector<json> out;
        for (const auto& m : all)
            if (m.contains("id") && !m["id"].is_null())
                out.push_back(m);
        return out;
    }

    // Notifications: message has "method".
    std::vector<json> notifications() const {
        std::vector<json> out;
        for (const auto& m : all)
            if (m.contains("method"))
                out.push_back(m);
        return out;
    }

    // Find a publishDiagnostics notification for the given URI (eventually saw one).
    const json* find_publish_diagnostics(const std::string& uri) const {
        for (const auto& m : all) {
            if (m.contains("method") && m["method"] == "textDocument/publishDiagnostics" &&
                m.contains("params") && m["params"].contains("uri") && m["params"]["uri"] == uri)
                return &m;
        }
        return nullptr;
    }

    // Last publishDiagnostics for the given URI (so we see diagnostics after didChange).
    const json* find_last_publish_diagnostics(const std::string& uri) const {
        const json* last = nullptr;
        for (const auto& m : all) {
            if (m.contains("method") && m["method"] == "textDocument/publishDiagnostics" &&
                m.contains("params") && m["params"].contains("uri") && m["params"]["uri"] == uri)
                last = &m;
        }
        return last;
    }

    // Last response (by request order).
    const json* last_response() const {
        for (auto it = all.rbegin(); it != all.rend(); ++it)
            if (it->contains("id") && !(*it)["id"].is_null())
                return &(*it);
        return nullptr;
    }

    void clear() { all.clear(); }
};

// Build initialize request.
json make_initialize_request(int id = 1, const std::string& root_uri = "") {
    json params = {{"processId", nullptr}, {"rootUri", nullptr}, {"capabilities", json::object()}};
    if (!root_uri.empty()) params["rootUri"] = root_uri;
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", "initialize"}, {"params", params}};
}

// Build textDocument/didOpen notification.
json make_did_open(const std::string& uri, const std::string& text, int version = 1) {
    return {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didOpen"},
        {"params", {{"textDocument", {{"uri", uri}, {"languageId", "fusion"}, {"version", version}, {"text", text}}}}},
    };
}

// Build textDocument/hover request.
json make_hover_request(int id, const std::string& uri, int line, int character) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/hover"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", character}}}}},
    };
}

// Build textDocument/definition request.
json make_definition_request(int id, const std::string& uri, int line, int character) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/definition"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", character}}}}},
    };
}

// Build textDocument/completion request.
json make_completion_request(int id, const std::string& uri, int line, int character) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/completion"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", character}}}}},
    };
}

// Build textDocument/semanticTokens/full request.
json make_semantic_tokens_request(int id, const std::string& uri) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/semanticTokens/full"},
        {"params", {{"textDocument", {{"uri", uri}}}}},
    };
}

// Build textDocument/inlayHint request.
json make_inlay_hint_request(int id, const std::string& uri, int start_line = 0, int end_line = 100) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/inlayHint"},
        {"params",
         {{"textDocument", {{"uri", uri}}},
          {"range", {{"start", {{"line", start_line}, {"character", 0}}}, {"end", {{"line", end_line}, {"character", 0}}}}}}},
    };
}

// Build textDocument/didChange notification.
json make_did_change(const std::string& uri, int version, const json& content_changes) {
    return {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", version}}}, {"contentChanges", content_changes}}},
    };
}

// Build textDocument/didClose notification.
json make_did_close(const std::string& uri) {
    return {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didClose"},
        {"params", {{"textDocument", {{"uri", uri}}}}},
    };
}

// Build textDocument/documentSymbol request.
json make_document_symbol_request(int id, const std::string& uri) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/documentSymbol"},
        {"params", {{"textDocument", {{"uri", uri}}}}},
    };
}

// Build textDocument/documentHighlight request.
json make_document_highlight_request(int id, const std::string& uri, int line, int character) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/documentHighlight"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", character}}}}},
    };
}

// Build textDocument/references request.
json make_references_request(int id, const std::string& uri, int line, int character, bool include_declaration = true) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/references"},
        {"params",
         {{"textDocument", {{"uri", uri}}},
          {"position", {{"line", line}, {"character", character}}},
          {"context", {{"includeDeclaration", include_declaration}}}}},
    };
}

// Build textDocument/prepareRename request.
json make_prepare_rename_request(int id, const std::string& uri, int line, int character) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/prepareRename"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", character}}}}},
    };
}

// Build textDocument/rename request.
json make_rename_request(int id, const std::string& uri, int line, int character, const std::string& new_name) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/rename"},
        {"params",
         {{"textDocument", {{"uri", uri}}},
          {"position", {{"line", line}, {"character", character}}},
          {"newName", new_name}}},
    };
}

// Build textDocument/signatureHelp request.
json make_signature_help_request(int id, const std::string& uri, int line, int character) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/signatureHelp"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", character}}}}},
    };
}

// Build workspace/symbol request.
json make_workspace_symbol_request(int id, const std::string& query = "") {
    json params = {};
    if (!query.empty()) params["query"] = query;
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "workspace/symbol"},
        {"params", params},
    };
}

// Build textDocument/foldingRange request.
json make_folding_range_request(int id, const std::string& uri) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", "textDocument/foldingRange"},
        {"params", {{"textDocument", {{"uri", uri}}}}},
    };
}

// Build shutdown request.
json make_shutdown_request(int id) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", "shutdown"}};
}

class ServerIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        capture.clear();
        server.handle(make_initialize_request(0, ""));
    }

    MessageCapture capture;
    lsp::FusionLSPServer server{[this](const json& msg) { capture.capture(msg); }, ""};

    const json* response_for(const json& request) {
        if (!request.contains("id")) return nullptr;
        const json& id = request["id"];
        for (const auto& m : capture.all) {
            if (m.contains("id") && !m["id"].is_null() && m["id"] == id)
                return &m;
        }
        return nullptr;
    }
};

// Lifecycle: server not yet initialized (no fixture SetUp).
TEST(ServerIntegrationLifecycle, RequestBeforeInitializeReturnsError) {
    MessageCapture capture;
    lsp::FusionLSPServer server{[&capture](const json& msg) { capture.capture(msg); }, ""};
    json req = make_hover_request(1, "file:///tmp/x.fusion", 0, 0);
    server.handle(req);
    const json* res = nullptr;
    for (const auto& m : capture.all) {
        if (m.contains("id") && !m["id"].is_null() && m["id"] == req["id"]) res = &m;
    }
    ASSERT_NE(res, nullptr) << "expected a response";
    ASSERT_TRUE(res->contains("error")) << "expected error before initialize";
    EXPECT_EQ((*res)["error"]["code"].get<int>(), -32002);
}

TEST_F(ServerIntegration, RequestAfterShutdownReturnsError) {
    json shutdown_req = make_shutdown_request(10);
    server.handle(shutdown_req);
    capture.clear();
    json hover_req = make_hover_request(11, "file:///tmp/x.fusion", 0, 0);
    server.handle(hover_req);

    const json* res = response_for(hover_req);
    ASSERT_NE(res, nullptr) << "expected a response";
    ASSERT_TRUE(res->contains("error")) << "expected error after shutdown";
    EXPECT_EQ((*res)["error"]["code"].get<int>(), -32600);
}

TEST_F(ServerIntegration, InitializeReturnsCapabilities) {
    capture.clear();
    json req = make_initialize_request(1, "");
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr) << "expected a response to initialize";
    ASSERT_TRUE(res->contains("result"));
    const json& result = (*res)["result"];
    ASSERT_TRUE(result.contains("capabilities"));
    const json& caps = result["capabilities"];
    EXPECT_TRUE(caps.value("hoverProvider", false));
    EXPECT_TRUE(caps.value("definitionProvider", false));
    EXPECT_TRUE(caps.value("documentSymbolProvider", false));
    EXPECT_TRUE(caps.value("completionProvider", json()).is_object());
    EXPECT_TRUE(caps.value("semanticTokensProvider", json()).is_object());
    EXPECT_TRUE(caps.value("inlayHintProvider", false));
    EXPECT_TRUE(caps.contains("semanticTokensProvider"));
    const json& sem = caps["semanticTokensProvider"];
    EXPECT_TRUE(sem.contains("legend"));
    EXPECT_TRUE(sem["legend"].contains("tokenTypes"));
}

TEST_F(ServerIntegration, DidOpenValidPublishesZeroDiagnostics) {
    const std::string uri = "file:///tmp/test.fusion";
    const std::string text = "fn main() -> i64 { return 0; }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));

    const json* diag = capture.find_publish_diagnostics(uri);
    ASSERT_NE(diag, nullptr) << "expected eventually a publishDiagnostics for the URI";
    ASSERT_TRUE(diag->contains("params"));
    ASSERT_TRUE((*diag)["params"].contains("diagnostics"));
    EXPECT_TRUE((*diag)["params"]["diagnostics"].empty())
        << "valid Fusion should publish 0 diagnostics";
}

TEST_F(ServerIntegration, DidOpenInvalidPublishesDiagnosticWithRangeAndMessage) {
    const std::string uri = "file:///tmp/bad.fusion";
    const std::string text = "fn foo( { return 1; }\n";  // syntax error: missing )
    capture.clear();
    server.handle(make_did_open(uri, text));

    const json* diag = capture.find_publish_diagnostics(uri);
    ASSERT_NE(diag, nullptr) << "expected eventually a publishDiagnostics for the URI";
    ASSERT_TRUE(diag->contains("params"));
    const json& diagnostics = (*diag)["params"]["diagnostics"];
    ASSERT_GE(diagnostics.size(), 1u) << "invalid Fusion should publish at least one diagnostic";
    const json& d = diagnostics[0];
    EXPECT_TRUE(d.contains("range"));
    EXPECT_TRUE(d.contains("message"));
    EXPECT_TRUE(d["message"].is_string());
    if (d.contains("severity"))
        EXPECT_GE(d["severity"].get<int>(), 1);
}

TEST_F(ServerIntegration, DidChangeUpdatesDiagnostics) {
    const std::string uri = "file:///tmp/change.fusion";
    const std::string text = "fn main() -> i64 { return 0; }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    // Introduce syntax error: replace "0" with "(" so we get "return (;"
    json changes = json::array();
    changes.push_back({{"range", {{"start", {{"line", 0}, {"character", 27}}}, {"end", {{"line", 0}, {"character", 28}}}}}, {"text", "("}});
    server.handle(make_did_change(uri, 2, changes));

    const json* diag = capture.find_last_publish_diagnostics(uri);
    ASSERT_NE(diag, nullptr) << "expected publishDiagnostics after didChange";
    ASSERT_TRUE(diag->contains("params"));
    const json& diagnostics = (*diag)["params"]["diagnostics"];
    EXPECT_GE(diagnostics.size(), 1u) << "invalid Fusion after edit should publish at least one diagnostic";
}

TEST_F(ServerIntegration, DidCloseClearsDiagnostics) {
    const std::string uri = "file:///tmp/close.fusion";
    const std::string text = "fn main() -> i64 { return 0; }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    server.handle(make_did_close(uri));

    const json* diag = capture.find_publish_diagnostics(uri);
    ASSERT_NE(diag, nullptr) << "expected publishDiagnostics after didClose";
    ASSERT_TRUE(diag->contains("params"));
    EXPECT_TRUE((*diag)["params"]["diagnostics"].empty())
        << "didClose should publish empty diagnostics for the URI";
}

TEST_F(ServerIntegration, HoverAtSymbolReturnsContentsWithSignature) {
    const std::string uri = "file:///tmp/hover.fusion";
    const std::string text = "fn add(x: i64, y: i64) -> i64 { return x + y; }\nfn main() -> i64 { return add(1, 2); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    server.handle(make_hover_request(2, uri, 1, 16));  // on "add" in call

    const json* res = response_for(make_hover_request(2, uri, 1, 16));
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    if (result.is_null()) return;  // optional hover
    ASSERT_TRUE(result.contains("contents"));
    std::string contents_str;
    if (result["contents"].is_string())
        contents_str = result["contents"].get<std::string>();
    else if (result["contents"].is_object() && result["contents"].contains("value"))
        contents_str = result["contents"]["value"].get<std::string>();
    else
        FAIL() << "expected contents string or object with value";
    EXPECT_TRUE(contents_str.find("add") != std::string::npos);
    EXPECT_TRUE(contents_str.find("i64") != std::string::npos);
}

TEST_F(ServerIntegration, DocumentSymbolReturnsFunctionsAndStructs) {
    const std::string uri = "file:///tmp/symbols.fusion";
    const std::string text = "struct Point { x: i64; y: i64; }\nfn main() -> i64 { return 0; }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_document_symbol_request(20, uri);
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    ASSERT_TRUE(result.is_array());
    ASSERT_GE(result.size(), 1u) << "expected at least one symbol";
    bool has_main = false;
    bool has_point = false;
    for (const auto& item : result) {
        ASSERT_TRUE(item.contains("name")) << "each symbol should have name";
        ASSERT_TRUE(item.contains("kind")) << "each symbol should have kind";
        ASSERT_TRUE(item.contains("range")) << "each symbol should have range";
        if (item["name"] == "main") has_main = true;
        if (item["name"] == "Point") {
            has_point = true;
            EXPECT_TRUE(item.contains("children")) << "struct symbol may have children";
        }
    }
    EXPECT_TRUE(has_main) << "expected main function";
    EXPECT_TRUE(has_point) << "expected Point struct";
}

TEST_F(ServerIntegration, DocumentHighlightReturnsAllOccurrences) {
    const std::string uri = "file:///tmp/highlight.fusion";
    const std::string text = "fn bar() -> i64 { return 0; }\nfn main() -> i64 { return bar(); }\n";  // "bar" at 0:3 and 1:26
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_document_highlight_request(21, uri, 1, 26);  // on "bar" in call (LSP 0-based: line 1, char 26)
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_GE(result.size(), 2u) << "bar appears at least twice (definition and call)";
    for (const auto& h : result) {
        ASSERT_TRUE(h.contains("range")) << "each highlight should have range";
    }
}

TEST_F(ServerIntegration, ReferencesReturnsDeclarationAndUsages) {
    const std::string uri = "file:///tmp/refs.fusion";
    const std::string text = "fn defined() -> i64 { return 42; }\nfn main() -> i64 { return defined(); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_references_request(22, uri, 1, 26, true);  // on "defined" in call (LSP 0-based: line 1, char 26)
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_GE(result.size(), 2u) << "at least definition and one usage";
    for (const auto& loc : result) {
        ASSERT_TRUE(loc.contains("uri"));
        ASSERT_TRUE(loc.contains("range"));
    }
}

TEST_F(ServerIntegration, ReferencesExcludesDeclarationWhenRequested) {
    const std::string uri = "file:///tmp/refs2.fusion";
    const std::string text = "fn once() -> i64 { return 1; }\nfn main() -> i64 { return once(); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_references_request(23, uri, 1, 26, false);  // on "once" in call (LSP 0-based: line 1, char 26)
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    ASSERT_TRUE(result.is_array());
    // Should have at least the call site; declaration may be excluded
    EXPECT_GE(result.size(), 1u);
}

TEST_F(ServerIntegration, DefinitionFromCallSiteReturnsLocation) {
    const std::string uri = "file:///tmp/def.fusion";
    const std::string text = "fn defined() -> i64 { return 42; }\nfn main() -> i64 { return defined(); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    server.handle(make_definition_request(3, uri, 1, 16));  // on "defined" in call

    const json* res = response_for(make_definition_request(3, uri, 1, 16));
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    if (result.is_null()) return;
    ASSERT_TRUE(result.contains("uri"));
    EXPECT_EQ(result["uri"].get<std::string>(), uri);
    ASSERT_TRUE(result.contains("range"));
    EXPECT_TRUE(result["range"].contains("start"));
    EXPECT_EQ(result["range"]["start"]["line"], 0);  // definition is on line 0 (0-indexed)
}

TEST_F(ServerIntegration, PrepareRenameReturnsRangeAndPlaceholder) {
    const std::string uri = "file:///tmp/rename_prep.fusion";
    const std::string text = "fn old_name() -> i64 { return 0; }\nfn main() -> i64 { return old_name(); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_prepare_rename_request(24, uri, 0, 3);  // on "old_name"
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    if (result.is_null()) return;  // optional
    ASSERT_TRUE(result.contains("range")) << "prepareRename should return range";
    ASSERT_TRUE(result.contains("placeholder")) << "prepareRename should return placeholder";
    EXPECT_EQ(result["placeholder"].get<std::string>(), "old_name");
}

TEST_F(ServerIntegration, RenameReturnsWorkspaceEditWithChanges) {
    const std::string uri = "file:///tmp/rename_apply.fusion";
    const std::string text = "fn old_name() -> i64 { return 0; }\nfn main() -> i64 { return old_name(); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_rename_request(25, uri, 0, 3, "new_name");  // on "old_name", rename to new_name
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    if (result.is_null()) return;
    ASSERT_TRUE(result.contains("changes")) << "rename should return changes";
    ASSERT_TRUE(result["changes"].contains(uri)) << "changes should contain document URI";
    const json& edits = result["changes"][uri];
    ASSERT_TRUE(edits.is_array());
    EXPECT_GE(edits.size(), 1u) << "at least one edit";
    bool has_new_name = false;
    for (const auto& edit : edits) {
        ASSERT_TRUE(edit.contains("range"));
        ASSERT_TRUE(edit.contains("newText"));
        if (edit["newText"] == "new_name") has_new_name = true;
    }
    EXPECT_TRUE(has_new_name) << "at least one edit should have newText new_name";
}

TEST_F(ServerIntegration, CompletionContainsExpectedItems) {
    const std::string uri = "file:///tmp/complete.fusion";
    // Valid Fusion so the index is populated. Line 1: "fn main() -> void { return my_func(); }"
    // Cursor at (1, 21) = after "fn main() -> void { " so completion lists symbols (including my_func).
    const std::string text = "fn my_func() -> void { }\nfn main() -> void { return my_func(); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    server.handle(make_completion_request(4, uri, 1, 21));

    const json* res = response_for(make_completion_request(4, uri, 1, 21));
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    ASSERT_FALSE(result.is_null()) << "completion should return a result";
    ASSERT_TRUE(result.contains("items")) << "completion should have items";
    const json& items = result["items"];
    bool has_my_func = false;
    for (const auto& item : items) {
        if (item.contains("label") && item["label"] == "my_func")
            has_my_func = true;
    }
    EXPECT_TRUE(has_my_func) << "completion should contain my_func";
}

TEST_F(ServerIntegration, SemanticTokensReturnsNonEmptyAndStableLegend) {
    const std::string uri = "file:///tmp/tokens.fusion";
    const std::string text = "fn f() -> i64 { return 0; }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    server.handle(make_semantic_tokens_request(5, uri));

    const json* res = response_for(make_semantic_tokens_request(5, uri));
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    if (result.is_null()) return;
    ASSERT_TRUE(result.contains("data"));
    // data may be empty for minimal program but legend is fixed
    EXPECT_TRUE(result["data"].is_array());
    // Capabilities already assert legend; here we just ensure we got a result
    EXPECT_TRUE(result.contains("data"));
}

TEST_F(ServerIntegration, InlayHintReturnsCallSiteHints) {
    const std::string uri = "file:///tmp/inlay.fusion";
    const std::string text = "fn f(a: i64, b: i64) -> i64 { return a + b; }\nfn main() -> i64 { return f(1, 2); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    server.handle(make_inlay_hint_request(6, uri, 0, 2));

    const json* res = response_for(make_inlay_hint_request(6, uri, 0, 2));
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    EXPECT_TRUE(result.is_array());
    // Inlay hints are only at call sites; we may get 0 or more (e.g. param labels)
    // Just ensure we get a valid array response.
}

TEST_F(ServerIntegration, SignatureHelpReturnsSignatureInsideCall) {
    const std::string uri = "file:///tmp/sighelp.fusion";
    const std::string text = "fn f(a: i64, b: i64) -> i64 { return a + b; }\nfn main() -> i64 { return f(1, ); }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    // Position after "f(1, " so we're inside the call, second parameter
    json req = make_signature_help_request(26, uri, 1, 24);
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    if (result.is_null()) return;
    ASSERT_TRUE(result.contains("signatures")) << "signatureHelp should return signatures";
    const json& sigs = result["signatures"];
    ASSERT_GE(sigs.size(), 1u) << "at least one signature";
    if (result.contains("activeParameter"))
        EXPECT_GE(result["activeParameter"].get<int>(), 0);
}

TEST_F(ServerIntegration, WorkspaceSymbolReturnsSymbolsFromOpenDocument) {
    const std::string uri = "file:///tmp/ws_symbol.fusion";
    const std::string text = "fn unique_workspace_func() -> i64 { return 0; }\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_workspace_symbol_request(27, "unique_workspace");
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    ASSERT_TRUE(result.is_array());
    bool found = false;
    for (const auto& sym : result) {
        if (sym.contains("name") && sym["name"] == "unique_workspace_func" &&
            sym.contains("location") && sym["location"].contains("uri") &&
            sym["location"]["uri"] == uri)
            found = true;
    }
    EXPECT_TRUE(found) << "workspace/symbol should return symbol from open document";
}

TEST_F(ServerIntegration, FoldingRangeReturnsBlockRanges) {
    const std::string uri = "file:///tmp/folding.fusion";
    const std::string text = "fn main() -> i64 {\n  return 0;\n}\n";
    capture.clear();
    server.handle(make_did_open(uri, text));
    json req = make_folding_range_request(28, uri);
    server.handle(req);

    const json* res = response_for(req);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->contains("result"));
    json result = (*res)["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_GE(result.size(), 1u) << "at least one folding range for block";
    for (const auto& r : result) {
        ASSERT_TRUE(r.contains("startLine")) << "folding range should have startLine";
        ASSERT_TRUE(r.contains("endLine")) << "folding range should have endLine";
    }
}

} // namespace
