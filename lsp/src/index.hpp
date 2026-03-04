#pragma once

#include "ast.hpp"
#include "lexer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace lsp {

// LSP DocumentSymbol.kind integer values used by Fusion.
enum class SymKind : int {
    Function = 12,
    Variable = 13,
    Struct   = 23,
    Field    = 8,
};

struct SymInfo {
    std::string name;
    SymKind     kind        = SymKind::Variable;
    size_t      line        = 0;   // 1-indexed (compiler convention)
    size_t      col         = 0;   // 1-indexed
    std::string sig;               // e.g. "fn add(x: i64, y: i64) -> i64"
    std::string scope;             // empty = global; function name = local to that fn
    std::string struct_name;       // non-empty if variable has a known struct type
    bool        is_extern   = false;
};

struct StructFieldDef {
    std::string name;
    std::string type;
};

struct StructInfo {
    std::string                  name;
    size_t                       line = 0, col = 0;
    std::vector<StructFieldDef>  fields;
};

struct FileIndex {
    std::vector<SymInfo>   syms;
    std::vector<StructInfo> structs;

    // name → indices into syms (multiple entries for same name in different scopes)
    std::unordered_multimap<std::string, size_t> by_name;
    // struct name → index into structs
    std::unordered_map<std::string, size_t>      struct_by_name;

    bool empty() const { return syms.empty(); }
};

// Build a FileIndex by scanning the token stream for definition positions
// and cross-referencing the AST for type signatures.
FileIndex build_index(const std::vector<fusion::Token>& toks,
                      const fusion::Program& prog);

// Return the identifier token at the given 0-indexed LSP cursor position.
// Returns nullptr if the cursor is not over an identifier.
const fusion::Token* token_at(const std::vector<fusion::Token>& toks,
                               int line, int ch);

// Walk backwards from the cursor to find the innermost open call.
// Sets active_param to the 0-indexed parameter position (comma count).
// Returns the callee name, or "" if not inside a call.
std::string call_context(const std::vector<fusion::Token>& toks,
                         int line, int ch, int& active_param);

} // namespace lsp
