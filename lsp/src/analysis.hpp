#pragma once

#include "index.hpp"
#include "protocol.hpp"

#include "parser.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace lsp {

struct AnalysisResult {
    std::vector<Diagnostic>    diags;
    std::vector<fusion::Token> tokens;  // always set, even on lex/parse failure
    FileIndex                  index;   // set when parse succeeds
    std::vector<fusion::CallSiteArgSpans> call_sites;  // set when parse succeeds; for call-site-only inlay hints
    /** Symbol name -> absolute path of .fusion file that defines it (from import lib blocks). */
    std::unordered_map<std::string, std::string> imported_symbol_path;
};

// Run lex → parse → (import resolution) → sema on source.
// Always returns tokens. Returns a FileIndex when parsing succeeds.
// file_path: absolute path to the source file; used to locate imports.
// Leave empty to skip import resolution.
AnalysisResult analyze(const std::string& source, const std::string& file_path = "");

} // namespace lsp
