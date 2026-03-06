#pragma once

#include "index.hpp"
#include "parser.hpp"
#include "protocol.hpp"

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace lsp {

// textDocument/hover
std::optional<nlohmann::json> hover(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch);

// textDocument/definition
// get_index_for_path: (path) -> FileIndex* or nullptr; path_to_uri: (path) -> uri string.
std::optional<nlohmann::json> definition(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& uri,
    int line, int ch,
    const std::unordered_map<std::string, std::string>& imported_symbol_path,
    const std::string& file_path,
    std::function<const FileIndex*(const std::string&)> get_index_for_path,
    std::function<std::string(const std::string&)> path_to_uri);

// textDocument/documentSymbol  (returns DocumentSymbol[])
nlohmann::json document_symbols(const FileIndex& idx);

// textDocument/completion
nlohmann::json completion(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch);

// textDocument/signatureHelp
std::optional<nlohmann::json> signature_help(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch);

// textDocument/documentHighlight  (returns DocumentHighlight[])
nlohmann::json document_highlight(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch);

// All reference locations for an identifier in a single file (for cross-file references).
nlohmann::json reference_locations_in_file(
    const std::vector<fusion::Token>& toks,
    const std::string& name,
    const std::string& uri);

// textDocument/references  (returns Location[])
nlohmann::json references(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& uri,
    int line, int ch,
    bool include_declaration);

// textDocument/prepareRename  (returns {range,placeholder} or null)
std::optional<nlohmann::json> prepare_rename(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch);

// textDocument/rename  (returns WorkspaceEdit or null)
std::optional<nlohmann::json> rename_symbol(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& uri,
    int line, int ch,
    const std::string& new_name);

// textDocument/semanticTokens/full  (returns {data:[...]})
// source: full document text for comment scanning (# to EOL).
nlohmann::json semantic_tokens_full(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& source);

// textDocument/foldingRange  (returns FoldingRange[])
nlohmann::json folding_ranges(const std::vector<fusion::Token>& toks);

// textDocument/inlayHint  (returns InlayHint[])
// call_sites: from parser; only call expressions (no FnDecl/import/extern).
// range: 0-indexed LSP line range to scan (start_line, end_line).
nlohmann::json inlay_hints(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::vector<fusion::CallSiteArgSpans>& call_sites,
    int start_line, int end_line);

} // namespace lsp
