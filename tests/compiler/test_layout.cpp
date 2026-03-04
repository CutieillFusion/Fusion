#include "ast.hpp"
#include "layout.hpp"
#include <gtest/gtest.h>

TEST(LayoutTests, PointSizeAlignmentOffsets) {
  fusion::StructDef point;
  point.name = "Point";
  point.fields = {{"x", fusion::FfiType::F64}, {"y", fusion::FfiType::F64}};
  fusion::StructLayout layout = fusion::compute_layout(point);
  EXPECT_EQ(layout.size, 16u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 2u);
  EXPECT_EQ(layout.fields[0].first, "x");
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[0].second.type, fusion::FfiType::F64);
  EXPECT_EQ(layout.fields[1].first, "y");
  EXPECT_EQ(layout.fields[1].second.offset, 8u);
  EXPECT_EQ(layout.fields[1].second.type, fusion::FfiType::F64);
}

TEST(LayoutTests, LayoutI64Fields) {
  fusion::StructDef s;
  s.name = "V";
  s.fields = {{"a", fusion::FfiType::I64}, {"b", fusion::FfiType::I64}};
  auto layout = fusion::compute_layout(s);
  EXPECT_EQ(layout.size, 16u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 2u);
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[0].second.type, fusion::FfiType::I64);
  EXPECT_EQ(layout.fields[1].second.offset, 8u);
  EXPECT_EQ(layout.fields[1].second.type, fusion::FfiType::I64);
}

TEST(LayoutTests, LayoutMixedI64F64) {
  fusion::StructDef s;
  s.name = "M";
  s.fields = {{"a", fusion::FfiType::I64}, {"b", fusion::FfiType::F64}};
  auto layout = fusion::compute_layout(s);
  EXPECT_EQ(layout.size, 16u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 2u);
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[1].second.offset, 8u);
}

TEST(LayoutTests, LayoutI32Fields) {
  fusion::StructDef s;
  s.name = "S";
  s.fields = {{"x", fusion::FfiType::I32}, {"y", fusion::FfiType::I32}};
  auto layout = fusion::compute_layout(s);
  EXPECT_EQ(layout.size, 8u);
  EXPECT_EQ(layout.alignment, 4u);
  ASSERT_EQ(layout.fields.size(), 2u);
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[0].second.type, fusion::FfiType::I32);
  EXPECT_EQ(layout.fields[1].second.offset, 4u);
}

TEST(LayoutTests, LayoutPtrField) {
  fusion::StructDef s;
  s.name = "P";
  s.fields = {{"next", fusion::FfiType::Ptr}};
  auto layout = fusion::compute_layout(s);
  EXPECT_EQ(layout.size, 8u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 1u);
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[0].second.type, fusion::FfiType::Ptr);
}

TEST(LayoutTests, LayoutSingleF64Field) {
  fusion::StructDef s;
  s.name = "A";
  s.fields = {{"x", fusion::FfiType::F64}};
  auto layout = fusion::compute_layout(s);
  EXPECT_EQ(layout.size, 8u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 1u);
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
}

TEST(LayoutTests, LayoutMultipleStructsIndependent) {
  fusion::StructDef s1;
  s1.name = "A";
  s1.fields = {{"x", fusion::FfiType::F64}};

  fusion::StructDef s2;
  s2.name = "B";
  s2.fields = {{"a", fusion::FfiType::I64}, {"b", fusion::FfiType::I64}, {"c", fusion::FfiType::I64}};

  auto l1 = fusion::compute_layout(s1);
  auto l2 = fusion::compute_layout(s2);

  EXPECT_EQ(l1.size, 8u);
  EXPECT_EQ(l1.alignment, 8u);
  EXPECT_EQ(l2.size, 24u);
  EXPECT_EQ(l2.alignment, 8u);
  ASSERT_EQ(l2.fields.size(), 3u);
  EXPECT_EQ(l2.fields[0].second.offset, 0u);
  EXPECT_EQ(l2.fields[1].second.offset, 8u);
  EXPECT_EQ(l2.fields[2].second.offset, 16u);
}

TEST(LayoutTests, LayoutStructWithPtrCharField) {
  // As produced by parser for: struct User { name: ptr[char]; age: i64; }
  fusion::StructDef def;
  def.name = "User";
  def.fields = {{"name", fusion::FfiType::Ptr}, {"age", fusion::FfiType::I64}};
  def.field_type_names = {"char", ""};

  fusion::StructLayout layout = fusion::compute_layout(def, {});

  EXPECT_EQ(layout.size, 16u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 2u);
  EXPECT_EQ(layout.fields[0].first, "name");
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[0].second.type, fusion::FfiType::Ptr);
  EXPECT_EQ(layout.fields[1].first, "age");
  EXPECT_EQ(layout.fields[1].second.offset, 8u);
  EXPECT_EQ(layout.fields[1].second.type, fusion::FfiType::I64);
}

TEST(LayoutTests, BuildLayoutMapStructWithPtrChar) {
  fusion::StructDef def;
  def.name = "User";
  def.fields = {{"name", fusion::FfiType::Ptr}, {"age", fusion::FfiType::I64}};
  def.field_type_names = {"char", ""};

  fusion::LayoutMap map = fusion::build_layout_map({def});

  ASSERT_EQ(map.count("User"), 1u);
  const fusion::StructLayout& layout = map.at("User");
  EXPECT_EQ(layout.size, 16u);
  EXPECT_EQ(layout.alignment, 8u);
  ASSERT_EQ(layout.fields.size(), 2u);
  EXPECT_EQ(layout.fields[0].first, "name");
  EXPECT_EQ(layout.fields[0].second.offset, 0u);
  EXPECT_EQ(layout.fields[0].second.type, fusion::FfiType::Ptr);
  EXPECT_EQ(layout.fields[1].first, "age");
  EXPECT_EQ(layout.fields[1].second.offset, 8u);
  EXPECT_EQ(layout.fields[1].second.type, fusion::FfiType::I64);
}
