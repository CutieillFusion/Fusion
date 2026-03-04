#include "index.hpp"

#include <algorithm>
#include <unordered_set>

using namespace fusion;

namespace lsp {

// ---- Type name helpers ----

static std::string ffi_str(FfiType t, const std::string& named = "") {
    if (!named.empty()) return named;
    switch (t) {
        case FfiType::Void: return "void";
        case FfiType::I8:   return "i8";
        case FfiType::I32:  return "i32";
        case FfiType::I64:  return "i64";
        case FfiType::F32:  return "f32";
        case FfiType::F64:  return "f64";
        case FfiType::Ptr:  return "ptr";
    }
    return "?";
}

static std::string fn_sig(const FnDef& fn) {
    std::string s = "fn " + fn.name + "(";
    for (size_t i = 0; i < fn.params.size(); i++) {
        if (i) s += ", ";
        const std::string& tn = (i < fn.param_type_names.size()) ? fn.param_type_names[i] : "";
        s += fn.params[i].first + ": " + ffi_str(fn.params[i].second, tn);
    }
    s += ") -> " + ffi_str(fn.return_type, fn.return_type_name);
    return s;
}

static std::string ext_sig(const ExternFn& fn) {
    std::string s = "extern fn " + fn.name + "(";
    for (size_t i = 0; i < fn.params.size(); i++) {
        if (i) s += ", ";
        const std::string& tn = (i < fn.param_type_names.size()) ? fn.param_type_names[i] : "";
        s += fn.params[i].first + ": " + ffi_str(fn.params[i].second, tn);
    }
    s += ") -> " + ffi_str(fn.return_type, fn.return_type_name);
    return s;
}

// ---- Index builder ----

FileIndex build_index(const std::vector<Token>& toks, const Program& prog) {
    FileIndex idx;

    // Fast AST lookups by name.
    std::unordered_map<std::string, const FnDef*>   fn_map;
    std::unordered_map<std::string, const ExternFn*> ext_map;
    for (const auto& fn  : prog.user_fns)   fn_map[fn.name]   = &fn;
    for (const auto& ext : prog.extern_fns) ext_map[ext.name] = &ext;

    // Set of known struct names (for type inference).
    std::unordered_set<std::string> struct_set;
    for (const auto& sd : prog.struct_defs) struct_set.insert(sd.name);

    auto add_sym = [&](SymInfo s) {
        idx.by_name.insert({s.name, idx.syms.size()});
        idx.syms.push_back(std::move(s));
    };

    // Single left-to-right token scan to collect definition positions.
    for (size_t i = 0; i + 1 < toks.size(); i++) {
        const Token& t  = toks[i];
        const Token& t1 = toks[i + 1];

        // fn <name>
        if (t.kind == TokenKind::KwFn && t1.kind == TokenKind::Ident) {
            SymInfo s;
            s.name = t1.ident;
            s.line = t1.line;
            s.col  = t1.column;

            auto fit = fn_map.find(s.name);
            auto eit = ext_map.find(s.name);
            if (fit != fn_map.end()) {
                s.kind = SymKind::Function;
                s.sig  = fn_sig(*fit->second);
            } else if (eit != ext_map.end()) {
                s.kind      = SymKind::Function;
                s.is_extern = true;
                s.sig       = ext_sig(*eit->second);
            } else {
                continue; // unknown fn (shouldn't happen after successful parse)
            }
            add_sym(std::move(s));
            continue;
        }

        // struct <name>
        if (t.kind == TokenKind::KwStruct && t1.kind == TokenKind::Ident) {
            SymInfo s;
            s.name = t1.ident;
            s.kind = SymKind::Struct;
            s.line = t1.line;
            s.col  = t1.column;
            s.sig  = "struct " + s.name;
            add_sym(std::move(s));
            continue;
        }

        // let <name>
        if (t.kind == TokenKind::KwLet && t1.kind == TokenKind::Ident) {
            SymInfo s;
            s.name = t1.ident;
            s.kind = SymKind::Variable;
            s.line = t1.line;
            s.col  = t1.column;
            s.sig  = "let " + s.name;

            // Try to infer struct type from the declaration.
            if (i + 2 < toks.size()) {
                const Token& t2 = toks[i + 2];
                // Pattern: let name: StructType ...
                if (t2.kind == TokenKind::Colon && i + 3 < toks.size()) {
                    const Token& t3 = toks[i + 3];
                    if (t3.kind == TokenKind::Ident && struct_set.count(t3.ident))
                        s.struct_name = t3.ident;
                }
                // Pattern: let name = fn_call(...)
                else if (t2.kind == TokenKind::Equals && i + 4 < toks.size()) {
                    const Token& t3 = toks[i + 3];
                    const Token& t4 = toks[i + 4];
                    if (t3.kind == TokenKind::Ident && t4.kind == TokenKind::LParen) {
                        auto fit = fn_map.find(t3.ident);
                        if (fit != fn_map.end() &&
                            struct_set.count(fit->second->return_type_name))
                            s.struct_name = fit->second->return_type_name;
                    }
                }
            }

            add_sym(std::move(s));
            continue;
        }
    }

    // Function parameters — use the AST for names/types; approximate position
    // to the function's definition line since the parser doesn't record param positions.
    for (const auto& fn : prog.user_fns) {
        size_t fn_line = 0;
        auto range = idx.by_name.equal_range(fn.name);
        for (auto it = range.first; it != range.second; ++it) {
            if (idx.syms[it->second].kind == SymKind::Function) {
                fn_line = idx.syms[it->second].line;
                break;
            }
        }
        for (size_t pi = 0; pi < fn.params.size(); pi++) {
            const std::string& tn = (pi < fn.param_type_names.size()) ? fn.param_type_names[pi] : "";
            SymInfo s;
            s.name  = fn.params[pi].first;
            s.kind  = SymKind::Variable;
            s.line  = fn_line;
            s.col   = 0;
            s.sig   = s.name + ": " + ffi_str(fn.params[pi].second, tn);
            s.scope = fn.name;
            if (!tn.empty() && struct_set.count(tn)) s.struct_name = tn;
            add_sym(std::move(s));
        }
    }

    // Struct detail (fields) from the AST.
    for (const auto& sd : prog.struct_defs) {
        StructInfo info;
        info.name = sd.name;
        // Grab the position we already recorded.
        auto range = idx.by_name.equal_range(sd.name);
        for (auto it = range.first; it != range.second; ++it) {
            if (idx.syms[it->second].kind == SymKind::Struct) {
                info.line = idx.syms[it->second].line;
                info.col  = idx.syms[it->second].col;
                break;
            }
        }
        for (size_t fi = 0; fi < sd.fields.size(); fi++) {
            const std::string& tn = (fi < sd.field_type_names.size()) ? sd.field_type_names[fi] : "";
            info.fields.push_back({sd.fields[fi].first, ffi_str(sd.fields[fi].second, tn)});
        }
        idx.struct_by_name[sd.name] = idx.structs.size();
        idx.structs.push_back(std::move(info));
    }

    // Import lib struct names (no body): add to struct_by_name so cross-file references treat them as structs.
    for (const auto& il : prog.import_libs) {
        for (const std::string& sname : il.struct_names) {
            if (idx.struct_by_name.count(sname)) continue;  // already defined in this file
            StructInfo info;
            info.name = sname;
            info.line = 0;
            info.col  = 0;
            idx.struct_by_name[sname] = idx.structs.size();
            idx.structs.push_back(std::move(info));
        }
    }

    return idx;
}

// ---- Cursor helpers ----

const Token* token_at(const std::vector<Token>& toks, int line, int ch) {
    int tgt_line = line + 1;  // convert LSP 0-indexed → compiler 1-indexed
    int tgt_col  = ch   + 1;
    for (const auto& tok : toks) {
        if (tok.kind != TokenKind::Ident)           continue;
        if (static_cast<int>(tok.line) != tgt_line) continue;
        int end = static_cast<int>(tok.column) + static_cast<int>(tok.ident.size()) - 1;
        if (static_cast<int>(tok.column) <= tgt_col && tgt_col <= end) return &tok;
    }
    return nullptr;
}

std::string call_context(const std::vector<Token>& toks, int line, int ch,
                          int& active_param) {
    active_param = 0;
    if (toks.empty()) return "";

    int tgt_line = line + 1;
    int tgt_col  = ch   + 1;

    // Find the rightmost token whose start is strictly before the cursor.
    int pos = -1;
    for (int i = static_cast<int>(toks.size()) - 1; i >= 0; i--) {
        int tl = static_cast<int>(toks[i].line);
        int tc = static_cast<int>(toks[i].column);
        if (tl < tgt_line || (tl == tgt_line && tc < tgt_col)) {
            pos = i;
            break;
        }
    }
    if (pos < 0) return "";

    // Walk backwards tracking paren depth to find the enclosing call.
    int depth  = 0;
    int commas = 0;
    for (int i = pos; i >= 0; i--) {
        TokenKind k = toks[i].kind;
        if (k == TokenKind::RParen) {
            depth++;
        } else if (k == TokenKind::LParen) {
            if (depth == 0) {
                // This is the opening paren of our enclosing call.
                if (i > 0 && toks[i - 1].kind == TokenKind::Ident) {
                    active_param = commas;
                    return toks[i - 1].ident;
                }
                return "";
            }
            depth--;
        } else if (k == TokenKind::Comma && depth == 0) {
            commas++;
        }
    }
    return "";
}

} // namespace lsp
