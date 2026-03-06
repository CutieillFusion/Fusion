#include "ast.hpp"
#include "codegen.hpp"
#include "lexer.hpp"
#include "multifile.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

#ifdef FUSION_HAVE_LLVM
#include <llvm/IR/LLVMContext.h>
#endif

// Helper: create temp dir, write library_file_content to lib_name.fusion, parse main_source, run resolve_imports_and_merge.
// Returns (error_message, merged_program). If error_message is non-empty, merge failed.
static std::pair<std::string, fusion::ProgramPtr> run_multifile_merge(
    const std::string& main_source,
    const std::string& lib_name,
    const std::string& library_file_content) {
  char dir_tpl[] = "/tmp/fusion_mf_XXXXXX";
  if (!mkdtemp(dir_tpl)) return {"mkdtemp failed", nullptr};
  std::string dir(dir_tpl);
  std::string lib_path = dir + "/" + lib_name + ".fusion";
  std::ofstream lib_file(lib_path);
  if (!lib_file) { rmdir(dir_tpl); return {"cannot write lib file", nullptr}; }
  lib_file << library_file_content;
  lib_file.close();

  auto tokens = fusion::lex(main_source);
  auto parse_result = fusion::parse(tokens);
  if (!parse_result.ok()) {
    unlink(lib_path.c_str());
    rmdir(dir_tpl);
    return {"parse failed: " + parse_result.error.message, nullptr};
  }
  std::string main_path = dir + "/main.fusion";
  std::string err = fusion::resolve_imports_and_merge(main_path, parse_result.program.get());
  unlink(lib_path.c_str());
  rmdir(dir_tpl);
  if (!err.empty()) return {err, nullptr};
  return {"", std::move(parse_result.program)};
}

TEST(MultifileTests, NoImportLibsLeavesProgramUnchanged) {
  auto tokens = fusion::lex("struct Point { x: f64; y: f64; }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  std::string err = fusion::resolve_imports_and_merge("/tmp/any.fusion", result.program.get());
  EXPECT_TRUE(err.empty());
  EXPECT_EQ(result.program->struct_defs.size(), 1u);
  EXPECT_EQ(result.program->import_libs.size(), 0u);
}

TEST(MultifileTests, ImportOneLibMergesStructAndFn) {
  std::string main_src = R"(import lib "vec" { struct Vector; fn make_vec(x: f64, y: f64) -> Vector; };
let v = make_vec(1.0, 2.0);
print(v.x);
print(v.y))";
  std::string lib_src = R"(export struct Vector { x: f64; y: f64; };
export fn make_vec(x: f64, y: f64) -> Vector {
  let p = heap(Vector);
  p.x = x;
  p.y = y;
  return p;
})";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  EXPECT_EQ(prog->struct_defs.size(), 1u);
  EXPECT_EQ(prog->struct_defs[0].name, "Vector");
  EXPECT_EQ(prog->user_fns.size(), 1u);
  EXPECT_EQ(prog->user_fns[0].name, "make_vec");
}

TEST(MultifileTests, ImportLibWithExternMergesExtern) {
  std::string main_src = R"(import lib "mylib" { fn get_one() -> f64; };
print(get_one()))";
  std::string lib_src = R"(extern lib "libm.so.6" { fn cos(x: f64) -> f64; };
export fn get_one() -> f64 { return cos(0.0); })";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  EXPECT_GE(prog->libs.size(), 1u);
  EXPECT_GE(prog->extern_fns.size(), 1u);
  bool has_cos = false;
  for (const auto& e : prog->extern_fns) if (e.name == "cos") { has_cos = true; break; }
  EXPECT_TRUE(has_cos);
}

TEST(MultifileTests, MissingLibraryFileReturnsError) {
  std::string main_src = R"(import lib "nonexistent" { struct X; }; print(1))";
  auto tokens = fusion::lex(main_src);
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  std::string main_path = "/tmp/main.fusion";
  std::string err = fusion::resolve_imports_and_merge(main_path, result.program.get());
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("cannot open") != std::string::npos || err.find("nonexistent") != std::string::npos);
}

TEST(MultifileTests, MissingExportedStructReturnsError) {
  std::string main_src = R"(import lib "vec" { struct Vector; }; print(1))";
  std::string lib_src = R"(struct Point { x: f64; y: f64; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("missing exported struct") != std::string::npos);
  EXPECT_TRUE(err.find("Vector") != std::string::npos);
}

TEST(MultifileTests, NonExportedStructNotMerged) {
  std::string main_src = R"(import lib "vec" { struct Vector; }; print(1))";
  std::string lib_src = R"(struct Vector { x: f64; y: f64; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("missing exported struct") != std::string::npos);
}

TEST(MultifileTests, MissingExportedFnReturnsError) {
  std::string main_src = R"(import lib "vec" { fn make() -> i64; }; print(1))";
  std::string lib_src = R"(fn helper() -> i64 { return 1; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("missing") != std::string::npos || err.find("signature mismatch") != std::string::npos);
}

TEST(MultifileTests, NonExportedFnNotMerged) {
  std::string main_src = R"(import lib "vec" { fn pub_fn() -> i64; }; print(1))";
  std::string lib_src = R"(fn pub_fn() -> i64 { return 42; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
}

TEST(MultifileTests, FnSignatureMismatchReturnsError) {
  std::string main_src = R"(import lib "vec" { fn add(a: i64, b: i64) -> i64; }; print(1))";
  std::string lib_src = R"(export fn add(a: i64, b: i64) -> f64 { return 0.0; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("signature mismatch") != std::string::npos || err.find("missing") != std::string::npos);
}

TEST(MultifileTests, SubsetImportOnlyMergesRequested) {
  std::string main_src = R"(import lib "vec" { struct Vector; fn make_vec(x: f64, y: f64) -> Vector; };
let v = make_vec(1.0, 2.0);
print(v.x))";
  std::string lib_src = R"(export struct Vector { x: f64; y: f64; };
export struct Point { x: i64; y: i64; };
export fn make_vec(x: f64, y: f64) -> Vector {
  let p = heap(Vector);
  p.x = x;
  p.y = y;
  return p;
}
export fn make_point(x: i64, y: i64) -> Point { let p = heap(Point); p.x = x; p.y = y; return p; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  EXPECT_EQ(prog->struct_defs.size(), 1u);
  EXPECT_EQ(prog->struct_defs[0].name, "Vector");
  EXPECT_EQ(prog->user_fns.size(), 1u);
  EXPECT_EQ(prog->user_fns[0].name, "make_vec");
}

TEST(MultifileTests, DuplicateStructImportFromSameLibIsDeduped) {
  /* Same struct imported twice from the same library with identical definition
     should be merged once without a duplicate symbol error. */
  std::string main_src = R"(import lib "value" { struct Value; };
import lib "value" { struct Value; };
print(1))";
  std::string lib_src = R"(export struct Value { data: f64; grad: f64; };)";
  auto [err, prog] = run_multifile_merge(main_src, "value", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  size_t count_value = 0;
  for (const auto& s : prog->struct_defs) {
    if (s.name == "Value") ++count_value;
  }
  EXPECT_EQ(count_value, 1u);
}

TEST(MultifileTests, DuplicateStructImportWithDifferentShapeErrors) {
  /* Importing a struct with the same name but a different shape should still error. */
  std::string main_src = R"(import lib "value" { struct Value; };
struct Value { data: f64; extra: f64; };
print(1))";
  std::string lib_src = R"(export struct Value { data: f64; };)";
  auto [err, prog] = run_multifile_merge(main_src, "value", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("duplicate symbol") != std::string::npos);
}

TEST(MultifileTests, DuplicateFnImportFromSameLibIsDeduped) {
  /* Same function imported twice from the same library with identical signature
     should be merged once without a duplicate symbol error. */
  std::string main_src = R"(import lib "vec" { fn answer() -> i64; };
import lib "vec" { fn answer() -> i64; };
print(answer()))";
  std::string lib_src = R"(export fn answer() -> i64 { return 42; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  // Function should be present exactly once in the merged program.
  size_t count_answer = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "answer") ++count_answer;
  }
  EXPECT_EQ(count_answer, 1u);
}

TEST(MultifileTests, DuplicateSymbolFromMainReturnsError) {
  /* Import first so it is parsed; then struct Vector in main duplicates the one we merge from lib. */
  std::string main_src = R"(import lib "vec" { struct Vector; };
struct Vector { x: f64; };
print(1))";
  std::string lib_src = R"(export struct Vector { x: f64; y: f64; };)" ;
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("duplicate symbol") != std::string::npos);
}

TEST(MultifileTests, CircularImportReturnsError) {
  char dir_tpl[] = "/tmp/fusion_mf_XXXXXX";
  ASSERT_TRUE(mkdtemp(dir_tpl));
  std::string dir(dir_tpl);
  std::string a_path = dir + "/a.fusion";
  std::string b_path = dir + "/b.fusion";
  std::ofstream(a_path) << "import lib \"b\" { }; print(1)";
  std::ofstream(b_path) << "import lib \"a\" { }; print(1)";
  auto tokens = fusion::lex("import lib \"a\" { }; print(1)");
  auto result = fusion::parse(tokens);
  ASSERT_TRUE(result.ok());
  std::string main_path = dir + "/main.fusion";
  std::string err = fusion::resolve_imports_and_merge(main_path, result.program.get());
  unlink(a_path.c_str());
  unlink(b_path.c_str());
  rmdir(dir_tpl);
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(err.find("circular") != std::string::npos);
}

TEST(MultifileTests, SemaPassesAfterMerge) {
  std::string main_src = R"(import lib "vec" { struct V; fn id(x: i64) -> i64; };
print(id(7)))";
  std::string lib_src = R"(export struct V { x: i64; };
export fn id(x: i64) -> i64 { return x; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

TEST(MultifileTests, ImportFnWithPrivateHelperIsMerged) {
  std::string main_src = R"(import lib "mylib" { fn public_fn() -> i64; };
print(public_fn()))";
  std::string lib_src = R"(fn helper() -> i64 { return 41; }
export fn public_fn() -> i64 { return helper(); })";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
  size_t helper_count = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "helper") ++helper_count;
  }
  EXPECT_EQ(helper_count, 1u);
}

TEST(MultifileTests, ImportFnWithTransitivePrivateHelpersIsMerged) {
  std::string main_src = R"(import lib "mylib" { fn public_fn() -> i64; };
print(public_fn()))";
  std::string lib_src = R"(fn helper_leaf() -> i64 { return 41; }
fn helper_mid() -> i64 { return helper_leaf(); }
export fn public_fn() -> i64 { return helper_mid(); })";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
  size_t leaf_count = 0;
  size_t mid_count = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "helper_leaf") ++leaf_count;
    if (f.name == "helper_mid") ++mid_count;
  }
  EXPECT_EQ(leaf_count, 1u);
  EXPECT_EQ(mid_count, 1u);
}

TEST(MultifileTests, ImportFnWithGetFuncPtrHelperIsMerged) {
  std::string main_src = R"(import lib "mylib" { fn make() -> ptr[void]; };
print(0))";
  std::string lib_src = R"(fn target(x: i64) -> i64 { return x; }
export fn make() -> ptr[void] {
  let fp = get_func_ptr(target);
  return fp;
})";
  auto [err, prog] = run_multifile_merge(main_src, "mylib", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
  size_t target_count = 0;
  for (const auto& f : prog->user_fns) {
    if (f.name == "target") ++target_count;
  }
  EXPECT_EQ(target_count, 1u);
}

TEST(MultifileTests, ImportPtrArrayElementStructMatchesExport) {
  /* Import and export both use ptr[Point]; must match for merge to succeed. */
  std::string main_src = R"(import lib "moons" { struct Point; fn create_moons(n: i64, sigma: f64) -> ptr[Point]; };
let moons = create_moons(10, 0.1);
print(moons[0].x))";
  std::string lib_src = R"(export struct Point { x: f64; y: f64; class: f64; }
export fn create_moons(n: i64, sigma: f64) -> ptr[Point] {
  let arr = heap_array(ptr[void], n);
  for (let i = 0; i < n; i = i + 1) {
    let p = heap(Point);
    p.x = 0.0;
    p.y = 0.0;
    p.class = 0.0;
    arr[i] = p;
  }
  return arr;
})";
  auto [err, prog] = run_multifile_merge(main_src, "moons", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  EXPECT_TRUE(sema_result.ok) << sema_result.error.message;
}

#ifdef FUSION_HAVE_LLVM
TEST(MultifileTests, JitRunsAfterMerge) {
  std::string main_src = R"(import lib "vec" { fn answer() -> i64; };
print(answer()))";
  std::string lib_src = R"(export fn answer() -> i64 { return 42; })";
  auto [err, prog] = run_multifile_merge(main_src, "vec", lib_src);
  ASSERT_TRUE(err.empty()) << err;
  ASSERT_TRUE(prog);
  auto sema_result = fusion::check(prog.get());
  ASSERT_TRUE(sema_result.ok) << sema_result.error.message;
  auto ctx = std::make_unique<llvm::LLVMContext>();
  auto module = fusion::codegen(*ctx, prog.get());
  ASSERT_NE(module, nullptr);
  auto jit_result = fusion::run_jit(std::move(module), std::move(ctx));
  ASSERT_TRUE(jit_result.ok) << jit_result.error;
}
#endif
