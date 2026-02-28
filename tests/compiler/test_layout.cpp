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
