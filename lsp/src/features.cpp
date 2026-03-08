#include "features.hpp"

#include <algorithm>
#include <tuple>
#include <vector>

namespace lsp {

// ---- Utility ----

// One-token Range from a SymInfo (1-indexed line/col → 0-indexed LSP).
static Range sym_range(const SymInfo& s) {
    int l = (s.line > 0) ? static_cast<int>(s.line) - 1 : 0;
    int c = (s.col  > 0) ? static_cast<int>(s.col)  - 1 : 0;
    return Range{Position{l, c}, Position{l, c + static_cast<int>(s.name.size())}};
}

static Range tok_range(const fusion::Token& tok) {
    int l = static_cast<int>(tok.line)   - 1;
    int c = static_cast<int>(tok.column) - 1;
    return Range{Position{l, c}, Position{l, c + static_cast<int>(tok.ident.size())}};
}

// Find the best (most global) match for a name in the index.
static const SymInfo* best_sym(const FileIndex& idx, const std::string& name) {
    auto range = idx.by_name.equal_range(name);
    const SymInfo* best = nullptr;
    for (auto it = range.first; it != range.second; ++it) {
        const SymInfo& s = idx.syms[it->second];
        if (s.scope.empty()) return &s;   // global — can't do better
        if (!best) best = &s;
    }
    return best;
}

// ---- Hover ----

std::optional<nlohmann::json> hover(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch)
{
    const fusion::Token* tok = token_at(toks, line, ch);
    if (!tok) return std::nullopt;

    const SymInfo* s = best_sym(idx, tok->ident);
    if (!s || s->sig.empty()) return std::nullopt;

    return nlohmann::json{
        {"contents", {
            {"kind",  "markdown"},
            {"value", "```fusion\n" + s->sig + "\n```"},
        }},
        {"range", to_json(tok_range(*tok))},
    };
}

// ---- Go-to-definition ----

std::optional<nlohmann::json> definition(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& uri,
    int line, int ch,
    const std::unordered_map<std::string, std::string>& imported_symbol_path,
    const std::string& file_path,
    std::function<const FileIndex*(const std::string&)> get_index_for_path,
    std::function<std::string(const std::string&)> path_to_uri)
{
    const fusion::Token* tok = token_at(toks, line, ch);
    if (!tok) return std::nullopt;

    const SymInfo* s = best_sym(idx, tok->ident);
    if (!s || s->line == 0) return std::nullopt;

    auto it = imported_symbol_path.find(tok->ident);
    if (it != imported_symbol_path.end()) {
        const std::string& lib_path = it->second;
        const FileIndex* lib_idx = get_index_for_path(lib_path);
        if (lib_idx) {
            const SymInfo* lib_s = best_sym(*lib_idx, tok->ident);
            if (lib_s && lib_s->line != 0)
                return nlohmann::json{
                    {"uri",   path_to_uri(lib_path)},
                    {"range", to_json(sym_range(*lib_s))},
                };
        }
    }

    return nlohmann::json{
        {"uri",   uri},
        {"range", to_json(sym_range(*s))},
    };
}

// ---- Document symbols ----

nlohmann::json document_symbols(const FileIndex& idx) {
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& s : idx.syms) {
        if (!s.scope.empty() || s.line == 0) continue;  // skip locals

        Range r = sym_range(s);
        nlohmann::json item = {
            {"name",           s.name},
            {"kind",           static_cast<int>(s.kind)},
            {"range",          to_json(r)},
            {"selectionRange", to_json(r)},
            {"detail",         s.sig},
        };

        // Attach struct fields as children so the outline is expandable.
        if (s.kind == SymKind::Struct) {
            auto it = idx.struct_by_name.find(s.name);
            if (it != idx.struct_by_name.end()) {
                nlohmann::json children = nlohmann::json::array();
                for (const auto& f : idx.structs[it->second].fields) {
                    children.push_back({
                        {"name",           f.name},
                        {"kind",           static_cast<int>(SymKind::Field)},
                        {"detail",         f.type},
                        {"range",          to_json(r)},  // approximate to struct line
                        {"selectionRange", to_json(r)},
                    });
                }
                item["children"] = std::move(children);
            }
        }

        arr.push_back(std::move(item));
    }
    return arr;
}

// ---- Completion ----

// LSP CompletionItemKind values used here.
static constexpr int CK_Function = 3;
static constexpr int CK_Variable = 6;
static constexpr int CK_Keyword  = 14;
static constexpr int CK_Struct   = 22;
static constexpr int CK_Field    = 5;
static constexpr int CK_Type     = 25;

static const char* const KEYWORDS[] = {
    "fn", "let", "if", "elif", "else", "for", "return", "as",
    "struct", "extern", "opaque",
};

static const char* const TYPE_NAMES[] = {
    "i32", "i64", "f32", "f64", "ptr", "void",
};

static const char* const BUILTINS[] = {
    "print", "store", "load",
    "load_f64", "load_i32", "load_ptr",
    "store_field", "load_field", "addr_of",
    "len", "read_line", "to_str", "from_str",
    "open", "close", "read_line_file", "write_file",
    "eof_file", "line_count_file",
    "stack", "heap", "stack_array", "heap_array",
    "free", "free_array", "as_heap", "as_array",
    "get_func_ptr", "call",
    "read_key", "chr", "flush",
    "write_bytes", "read_bytes",
    "http_request", "http_status",
};

static bool is_builtin(const std::string& name) {
    for (const char* b : BUILTINS)
        if (name == b) return true;
    return false;
}

static nlohmann::json make_item(const std::string& label, int kind,
                                 const std::string& detail = "") {
    nlohmann::json item = {{"label", label}, {"kind", kind}};
    if (!detail.empty()) item["detail"] = detail;
    return item;
}

nlohmann::json completion(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch)
{
    // Determine the last token before the cursor.
    int tgt_line = line + 1;
    int tgt_col  = ch   + 1;
    int last_idx = -1;
    for (int i = static_cast<int>(toks.size()) - 1; i >= 0; i--) {
        int tl = static_cast<int>(toks[i].line);
        int tc = static_cast<int>(toks[i].column);
        if (tl < tgt_line || (tl == tgt_line && tc < tgt_col)) {
            last_idx = i;
            break;
        }
    }

    nlohmann::json items = nlohmann::json::array();

    // After a dot: offer struct fields, filtered by the variable's type if known.
    if (last_idx >= 0 && toks[last_idx].kind == fusion::TokenKind::Dot) {
        // Try to resolve the struct type of the preceding identifier.
        std::string resolved_struct;
        if (last_idx >= 1 && toks[last_idx - 1].kind == fusion::TokenKind::Ident) {
            const std::string& var_name = toks[last_idx - 1].ident;
            auto range = idx.by_name.equal_range(var_name);
            for (auto it = range.first; it != range.second; ++it) {
                const SymInfo& s = idx.syms[it->second];
                if (!s.struct_name.empty()) {
                    resolved_struct = s.struct_name;
                    break;
                }
            }
        } else if (last_idx >= 1 && toks[last_idx - 1].kind == fusion::TokenKind::RBracket) {
            // Handle varName[index]. — walk back to find the array identifier.
            int depth = 0;
            for (int j = last_idx - 1; j >= 0; j--) {
                if (toks[j].kind == fusion::TokenKind::RBracket) depth++;
                else if (toks[j].kind == fusion::TokenKind::LBracket) {
                    if (--depth == 0 && j > 0 &&
                        toks[j-1].kind == fusion::TokenKind::Ident) {
                        const std::string& arr = toks[j-1].ident;
                        auto range = idx.by_name.equal_range(arr);
                        for (auto it = range.first; it != range.second; ++it)
                            if (!idx.syms[it->second].struct_name.empty()) {
                                resolved_struct = idx.syms[it->second].struct_name;
                                break;
                            }
                    }
                    break;
                }
            }
        }

        if (!resolved_struct.empty()) {
            // Show only fields of the resolved struct.
            auto sit = idx.struct_by_name.find(resolved_struct);
            if (sit != idx.struct_by_name.end()) {
                for (const auto& f : idx.structs[sit->second].fields)
                    items.push_back(make_item(f.name, CK_Field, f.type));
            }
        } else {
            // Fall back: show all fields from all structs.
            for (const auto& si : idx.structs) {
                for (const auto& f : si.fields) {
                    items.push_back(make_item(f.name, CK_Field,
                                              f.type + "  [" + si.name + "]"));
                }
            }
        }
        return {{"isIncomplete", false}, {"items", items}};
    }

    // General: user-defined symbols.
    for (const auto& s : idx.syms) {
        if (!s.scope.empty()) continue;  // skip parameters/locals to avoid noise
        int kind = CK_Variable;
        if (s.kind == SymKind::Function) kind = CK_Function;
        else if (s.kind == SymKind::Struct) kind = CK_Struct;
        items.push_back(make_item(s.name, kind, s.sig));
    }

    // Built-in functions.
    for (const auto* b : BUILTINS) {
        items.push_back(make_item(b, CK_Function));
    }

    // Language keywords.
    for (const auto* kw : KEYWORDS) {
        items.push_back(make_item(kw, CK_Keyword));
    }

    // Primitive types (handy after ':' in a declaration).
    for (const auto* ty : TYPE_NAMES) {
        items.push_back(make_item(ty, CK_Type));
    }

    return {{"isIncomplete", false}, {"items", items}};
}

// ---- Signature help ----

std::optional<nlohmann::json> signature_help(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch)
{
    int active_param = 0;
    std::string fn_name = call_context(toks, line, ch, active_param);
    if (fn_name.empty()) return std::nullopt;

    // Find the function signature in the index.
    const SymInfo* fn_sym = nullptr;
    auto range = idx.by_name.equal_range(fn_name);
    for (auto it = range.first; it != range.second; ++it) {
        if (idx.syms[it->second].kind == SymKind::Function) {
            fn_sym = &idx.syms[it->second];
            break;
        }
    }
    if (!fn_sym) return std::nullopt;

    // Build the parameter array by extracting the param list from the signature string.
    // Signature format: "fn name(p1: t1, p2: t2) -> ret"  or  "extern fn name(...) -> ret"
    const std::string& sig = fn_sym->sig;
    nlohmann::json params_arr = nlohmann::json::array();
    size_t lp = sig.find('(');
    size_t rp = sig.rfind(')');
    if (lp != std::string::npos && rp != std::string::npos && lp < rp) {
        std::string plist = sig.substr(lp + 1, rp - lp - 1);
        size_t start = 0;
        while (start <= plist.size()) {
            size_t comma = plist.find(", ", start);
            if (comma == std::string::npos) comma = plist.size();
            std::string p = plist.substr(start, comma - start);
            if (!p.empty()) {
                params_arr.push_back({{"label", p}});
            }
            if (comma >= plist.size()) break;
            start = comma + 2;
        }
    }

    // Derive a shorter label that omits the parameter list to avoid duplication
    // in the editor UI. Keep the function name and return type, e.g.:
    //   "fn add(x: i64, y: i64) -> i64" → "fn add() -> i64"
    std::string label = sig;
    if (lp != std::string::npos && rp != std::string::npos && lp < rp) {
        // Keep text up to and including the opening '('.
        std::string head = sig.substr(0, lp + 1);
        // Append an empty parameter list and preserve any trailing return type.
        if (rp + 1 < sig.size()) {
            label = head + ")" + sig.substr(rp + 1);  // ") -> ret"
        } else {
            label = head + ")";
        }
    }

    int n = static_cast<int>(params_arr.size());
    int clamped = (n > 0) ? std::min(active_param, n - 1) : 0;

    return nlohmann::json{
        {"signatures", nlohmann::json::array({{
            {"label",      label},
            {"parameters", params_arr},
        }})},
        {"activeSignature", 0},
        {"activeParameter", clamped},
    };
}

// ---- Shared helper: all token ranges for a given identifier name ----

static std::vector<Range> all_ranges_of(
    const std::vector<fusion::Token>& toks,
    const std::string& name)
{
    std::vector<Range> out;
    for (const auto& tok : toks) {
        if (tok.kind == fusion::TokenKind::Ident && tok.ident == name)
            out.push_back(tok_range(tok));
    }
    return out;
}

// ---- Document highlight ----

nlohmann::json document_highlight(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch)
{
    const fusion::Token* tok = token_at(toks, line, ch);
    if (!tok) return nlohmann::json::array();

    nlohmann::json arr = nlohmann::json::array();
    for (const Range& r : all_ranges_of(toks, tok->ident))
        arr.push_back({{"range", to_json(r)}, {"kind", 1}});  // 1 = Text
    return arr;
}

// ---- References ----

nlohmann::json reference_locations_in_file(
    const std::vector<fusion::Token>& toks,
    const std::string& name,
    const std::string& uri)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const Range& r : all_ranges_of(toks, name))
        arr.push_back({{"uri", uri}, {"range", to_json(r)}});
    return arr;
}

nlohmann::json references(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& uri,
    int line, int ch,
    bool /*include_declaration*/)
{
    const fusion::Token* tok = token_at(toks, line, ch);
    if (!tok) return nlohmann::json::array();
    return reference_locations_in_file(toks, tok->ident, uri);
}

// ---- Prepare rename ----

std::optional<nlohmann::json> prepare_rename(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    int line, int ch)
{
    const fusion::Token* tok = token_at(toks, line, ch);
    if (!tok) return std::nullopt;
    // Only allow renaming symbols that appear in the index.
    if (idx.by_name.find(tok->ident) == idx.by_name.end()) return std::nullopt;

    return nlohmann::json{
        {"range",       to_json(tok_range(*tok))},
        {"placeholder", tok->ident},
    };
}

// ---- Rename ----

std::optional<nlohmann::json> rename_symbol(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& uri,
    int line, int ch,
    const std::string& new_name)
{
    const fusion::Token* tok = token_at(toks, line, ch);
    if (!tok) return std::nullopt;

    nlohmann::json edits = nlohmann::json::array();
    for (const Range& r : all_ranges_of(toks, tok->ident))
        edits.push_back({{"range", to_json(r)}, {"newText", new_name}});

    if (edits.empty()) return std::nullopt;
    return nlohmann::json{{"changes", {{uri, edits}}}};
}

// ---- Semantic tokens ----

// Semantic token type indices (must match legend in on_initialize).
static constexpr int STT_Keyword      = 0;
static constexpr int STT_Type         = 1;
static constexpr int STT_Function     = 2;
static constexpr int STT_Variable     = 3;
static constexpr int STT_Parameter    = 4;
static constexpr int STT_Struct       = 5;
static constexpr int STT_Comment      = 6;
static constexpr int STT_Module       = 7;

static int semantic_type_of(const fusion::Token& tok, const FileIndex& idx) {
    using K = fusion::TokenKind;
    switch (tok.kind) {
        // Language keywords.
        case K::KwFn: case K::KwLet: case K::KwIf: case K::KwElse:
        case K::KwElif: case K::KwFor: case K::KwReturn: case K::KwAs:
        case K::KwStruct: case K::KwOpaque: case K::KwIn:
            return STT_Keyword;
        // Module keywords (extern, import, export, lib).
        case K::KwExtern: case K::KwImport: case K::KwExport: case K::KwLib:
            return STT_Module;
        // Primitive type keywords.
        case K::KwI32: case K::KwI64: case K::KwF32: case K::KwF64:
        case K::KwPtr: case K::KwVoid: case K::KwU32: case K::KwU64:
        case K::KwChar:
            return STT_Type;
        // Named identifiers: look up semantic role.
        case K::Ident: {
            const SymInfo* best = nullptr;
            auto range = idx.by_name.equal_range(tok.ident);
            for (auto it = range.first; it != range.second; ++it) {
                const SymInfo& s = idx.syms[it->second];
                if (s.scope.empty()) { best = &s; break; }
                if (!best) best = &s;
            }
            if (best) {
                if (best->kind == SymKind::Function) return STT_Function;
                if (best->kind == SymKind::Struct)   return STT_Struct;
                if (!best->scope.empty())            return STT_Parameter;
                return STT_Variable;
            }
            // Not in index: treat builtin names as functions so they aren't colored as variables.
            if (is_builtin(tok.ident)) return STT_Function;
            return STT_Variable;
        }
        default:
            return -1;  // no semantic token for punctuation/literals
    }
}

nlohmann::json semantic_tokens_full(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::string& source)
{
    // Collect (line, col, length, type) for tokens and comments; then sort and delta-encode.
    using Entry = std::tuple<int, int, int, int>;
    std::vector<Entry> entries;

    for (const auto& tok : toks) {
        if (tok.kind == fusion::TokenKind::Eof) break;
        int type = semantic_type_of(tok, idx);
        if (type < 0) continue;
        int length = static_cast<int>(tok.ident.size());
        if (length <= 0) continue;
        int line = static_cast<int>(tok.line)   - 1;
        int col  = static_cast<int>(tok.column) - 1;
        entries.emplace_back(line, col, length, type);
    }

    // Scan source for # ... EOL comments (Fusion line comments).
    int line = 0, col = 0;
    for (size_t i = 0; i < source.size(); ) {
        if (source[i] == '#') {
            int sl = line, sc = col;
            size_t start = i;
            while (i < source.size() && source[i] != '\n') ++i;
            int len = static_cast<int>(i - start);
            if (len > 0)
                entries.emplace_back(sl, sc, len, STT_Comment);
        }
        if (i < source.size()) {
            if (source[i] == '\n') { ++line; col = 0; } else { ++col; }
            ++i;
        }
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
        return std::get<1>(a) < std::get<1>(b);
    });

    nlohmann::json data = nlohmann::json::array();
    int prev_line = 0, prev_char = 0;
    for (const auto& e : entries) {
        int tok_line = std::get<0>(e), tok_char = std::get<1>(e);
        int length = std::get<2>(e), type = std::get<3>(e);
        int delta_line = tok_line - prev_line;
        int delta_char = (delta_line == 0) ? (tok_char - prev_char) : tok_char;
        data.push_back(delta_line);
        data.push_back(delta_char);
        data.push_back(length);
        data.push_back(type);
        data.push_back(0);
        prev_line = tok_line;
        prev_char = tok_char;
    }
    return {{"data", data}};
}

// ---- Folding ranges ----

nlohmann::json folding_ranges(const std::vector<fusion::Token>& toks) {
    nlohmann::json arr = nlohmann::json::array();
    std::vector<int> stack;  // 0-indexed start lines of unclosed '{'

    for (const auto& tok : toks) {
        if (tok.kind == fusion::TokenKind::LCurly) {
            stack.push_back(static_cast<int>(tok.line) - 1);
        } else if (tok.kind == fusion::TokenKind::RCurly && !stack.empty()) {
            int start_ln = stack.back();
            stack.pop_back();
            int end_ln = static_cast<int>(tok.line) - 1;
            if (end_ln > start_ln)
                arr.push_back({{"startLine", start_ln}, {"endLine", end_ln}});
        }
    }
    return arr;
}

// ---- Inlay hints ----

// Extract ordered list of parameter names from a signature string like
// "fn add(x: i64, y: i64) -> i64"  →  ["x", "y"]
static std::vector<std::string> param_names_from_sig(const std::string& sig) {
    std::vector<std::string> names;
    size_t lp = sig.find('(');
    size_t rp = sig.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos || lp >= rp) return names;
    std::string plist = sig.substr(lp + 1, rp - lp - 1);
    if (plist.empty()) return names;

    size_t start = 0;
    while (true) {
        // Find next comma (ignoring nested parens — Fusion has none in params,
        // but being safe doesn't hurt)
        size_t comma = plist.find(',', start);
        size_t end = (comma == std::string::npos) ? plist.size() : comma;
        std::string param = plist.substr(start, end - start);

        // Trim leading whitespace, extract text before ':'
        size_t ps = param.find_first_not_of(' ');
        if (ps != std::string::npos) {
            size_t colon = param.find(':', ps);
            if (colon != std::string::npos) {
                std::string name = param.substr(ps, colon - ps);
                size_t pe = name.find_last_not_of(' ');
                if (pe != std::string::npos) names.push_back(name.substr(0, pe + 1));
            }
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return names;
}

nlohmann::json inlay_hints(
    const std::vector<fusion::Token>& toks,
    const FileIndex& idx,
    const std::vector<fusion::CallSiteArgSpans>& call_sites,
    int start_line, int end_line)
{
    (void)toks;
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& cs : call_sites) {
        const SymInfo* fn_sym = nullptr;
        auto range = idx.by_name.equal_range(cs.callee);
        for (auto it = range.first; it != range.second; ++it) {
            const SymInfo& s = idx.syms[it->second];
            if (s.kind == SymKind::Function) {
                if (s.scope.empty()) { fn_sym = &s; break; }
                if (!fn_sym) fn_sym = &s;
            }
        }
        if (!fn_sym) continue;

        std::vector<std::string> params = param_names_from_sig(fn_sym->sig);

        for (size_t k = 0; k < cs.arg_positions.size(); k++) {
            if (k >= params.size()) break;
            const std::string& pname = params[k];
            if (pname.empty() || pname == "_") continue;

            size_t line = cs.arg_positions[k].first;
            size_t col  = cs.arg_positions[k].second;
            int ln = (line > 0) ? static_cast<int>(line) - 1 : 0;
            if (ln < start_line || ln > end_line) continue;

            int ch = (col > 0) ? static_cast<int>(col) - 1 : 0;
            arr.push_back({
                {"position",     {{"line", ln}, {"character", ch}}},
                {"label",        pname + ":"},
                {"kind",         2},    // 2 = Parameter
                {"paddingRight", true},
            });
        }
    }
    return arr;
}

} // namespace lsp
