#include "analysis.hpp"

#include "lexer.hpp"
#include "multifile.hpp"
#include "parser.hpp"
#include "sema.hpp"

#include <exception>
#include <string>

namespace lsp {

// ---- Import path resolution (matches compiler multifile logic) ----

static std::string get_dir(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

static std::string resolve_import(const std::string& dir, const std::string& name) {
    std::string p = dir + "/" + name;
    if (p.size() < 7 || p.substr(p.size() - 7) != ".fusion")
        p += ".fusion";
    return p;
}

// Convert a 1-indexed compiler (line, col) to a single-character LSP Range.
static Range make_range(size_t line, size_t col) {
    int l = (line > 0) ? static_cast<int>(line) - 1 : 0;
    int c = (col  > 0) ? static_cast<int>(col)  - 1 : 0;
    return Range{Position{l, c}, Position{l, c + 1}};
}

static Diagnostic err(Range r, std::string msg) {
    return Diagnostic{r, DiagnosticSeverity::Error, std::move(msg)};
}

AnalysisResult analyze(const std::string& source, const std::string& file_path) {
    AnalysisResult result;

    // ---- Lex ----
    try {
        result.tokens = fusion::lex(source);
    } catch (const std::exception& ex) {
        result.diags.push_back(err({}, std::string("Lexer error: ") + ex.what()));
        return result;
    } catch (...) {
        result.diags.push_back(err({}, "Unknown lexer error"));
        return result;
    }

    // ---- Parse ----
    fusion::ParseResult pr;
    try {
        pr = fusion::parse(result.tokens);
    } catch (const std::exception& ex) {
        result.diags.push_back(err({}, std::string("Parse error: ") + ex.what()));
        return result;
    } catch (...) {
        result.diags.push_back(err({}, "Unknown parse error"));
        return result;
    }

    if (!pr.ok()) {
        result.diags.push_back(
            err(make_range(pr.error.line, pr.error.column), pr.error.message));
        return result;  // no AST → no index
    }

    result.call_sites = std::move(pr.call_sites);

    // Build imported symbol -> path for go-to-definition (before merge; import_libs unchanged).
    std::string main_dir = file_path.empty() ? "." : get_dir(file_path);
    for (const auto& il : pr.program->import_libs) {
        std::string path = resolve_import(main_dir, il.name);
        for (const auto& f : il.fn_decls)
            result.imported_symbol_path[f.name] = path;
        for (const std::string& sname : il.struct_names)
            result.imported_symbol_path[sname] = path;
    }

    // ---- Import resolution ----
    // Attempt to load library files listed in `import lib` declarations and
    // merge their exported symbols into this program so sema can validate calls.
    // Errors are silently ignored: libraries may not exist relative to the LSP
    // working directory, and sema will surface "undefined function" errors anyway.
    if (!file_path.empty() && !pr.program->import_libs.empty()) {
        fusion::resolve_imports_and_merge(file_path, pr.program.get());
    }

    // ---- Sema ----
    // Run sema before building the index so it can annotate the AST
    // (e.g. fill load_field_struct), then build the index from the enriched tree.
    fusion::SemaResult sr;
    try {
        sr = fusion::check(pr.program.get());
    } catch (const std::exception& ex) {
        result.diags.push_back(err({}, std::string("Semantic error: ") + ex.what()));
    } catch (...) {
        result.diags.push_back(err({}, "Unknown semantic error"));
    }

    if (!sr.ok) {
        // Find the first-code-token range for errors without position info.
        Range first_code_range;
        for (const auto& t : result.tokens) {
            if (t.kind != fusion::TokenKind::Eof) {
                first_code_range = make_range(t.line, t.column);
                break;
            }
        }
        for (const auto& se : sr.errors) {
            Range r = (se.line > 0) ? make_range(se.line, se.column)
                                    : first_code_range;
            result.diags.push_back(err(r, se.message));
        }
    }

    // ---- Index ----
    // Always build even when sema reported an error — partial index is useful.
    result.index = build_index(result.tokens, *pr.program);

    return result;
}

} // namespace lsp
