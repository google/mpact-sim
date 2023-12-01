#include "mpact/sim/decoder/proto_constraint_value_set.h"

#include <cstdint>
#include <limits>
#include <memory>

#include "absl/log/check.h"
#include "googletest/include/gtest/gtest.h"
#include "mpact/sim/decoder/proto_constraint_expression.h"
#include "mpact/sim/decoder/proto_instruction_encoding.h"

namespace {

// The tests in this file tests the operation of the ProtoConstraintValueSet
// class in storing and manipulating ranges (intersections).

using ::mpact::sim::decoder::proto_fmt::ConstraintType;
using ::mpact::sim::decoder::proto_fmt::ProtoConstraint;
using ::mpact::sim::decoder::proto_fmt::ProtoConstraintValueExpression;
using ::mpact::sim::decoder::proto_fmt::ProtoConstraintValueSet;

constexpr int32_t kMin = 10;
constexpr int32_t kMax = 100;

TEST(ProtoConstraintValueSetTest, ConstructEmpty) {
  auto range = std::make_unique<ProtoConstraintValueSet>(
      /*min=*/nullptr, /*min_included=*/false, /*max=*/nullptr,
      /*max_included=*/false);
  EXPECT_TRUE(range->subranges().empty());
  EXPECT_TRUE(range->IsEmpty());
}

TEST(ProtoConstraintValueSetTest, ConstructFromBasic) {
  auto min_expr = std::make_unique<ProtoConstraintValueExpression>(kMin);
  auto max_expr = std::make_unique<ProtoConstraintValueExpression>(kMax);

  // Use the basic constructor.
  auto range = std::make_unique<ProtoConstraintValueSet>(
      min_expr.get(), /*min_included=*/true, max_expr.get(),
      /*max_included=*/true);
  // There should be one subrange.
  CHECK_EQ(range->subranges().size(), 1);
  // The expressions are cloned, so the pointers should be different.
  CHECK_NE(range->subranges().back().min, nullptr);
  CHECK_NE(range->subranges().back().max, nullptr);
  EXPECT_NE(range->subranges().back().min, min_expr.get());
  EXPECT_NE(range->subranges().back().max, max_expr.get());
  // But the values should be the same.
  EXPECT_EQ(range->subranges().back().min->GetValueAs<int32_t>(), kMin);
  EXPECT_EQ(range->subranges().back().max->GetValueAs<int32_t>(), kMax);
}

TEST(ProtoConstraintValueSetTest, ConstructFromGeConstraint) {
  auto min_expr = std::make_unique<ProtoConstraintValueExpression>(kMin);

  // Create a constraint to pass to the range constructor.
  auto constraint = std::make_unique<ProtoConstraint>(
      /*ctx=*/nullptr,
      /*field_descriptor=*/nullptr, ConstraintType::kGe, min_expr.get(),
      /*value=*/kMin, /*depends_on=*/nullptr);
  auto range = std::make_unique<ProtoConstraintValueSet>(constraint.get());
  // There should be one subrange for kGe.
  CHECK_EQ(range->subranges().size(), 1);
  CHECK_NE(range->subranges().back().min, nullptr);
  CHECK_NE(range->subranges().back().max, nullptr);
  EXPECT_NE(range->subranges().back().min, min_expr.get());
  // Verify value equality.
  EXPECT_EQ(range->subranges().back().min->GetValueAs<int32_t>(), kMin);
  EXPECT_EQ(range->subranges().back().max->GetValueAs<int32_t>(),
            std::numeric_limits<int32_t>::max());
  EXPECT_TRUE(range->subranges().back().min_included);
  EXPECT_TRUE(range->subranges().back().max_included);
  EXPECT_FALSE(range->IsEmpty());
}

TEST(ProtoConstraintValueSetTest, ConstructFromNeConstraint) {
  auto min_expr = std::make_unique<ProtoConstraintValueExpression>(kMin);
  // Create a kNe constraint to pass to the range constructor.
  auto constraint = std::make_unique<ProtoConstraint>(
      /*ctx=*/nullptr,
      /*field_descriptor=*/nullptr, ConstraintType::kNe, min_expr.get(),
      /*value=*/kMin, /*depends_on=*/nullptr);
  auto range = std::make_unique<ProtoConstraintValueSet>(constraint.get());
  // There should be two subranges for kNe (less than value, greater than
  // value).
  CHECK_EQ(range->subranges().size(), 2);
  // None of the min/max expressions should be equal to min_expr.
  CHECK_NE(range->subranges()[0].min, nullptr);
  CHECK_NE(range->subranges()[0].max, nullptr);
  EXPECT_NE(range->subranges()[0].min, min_expr.get());
  EXPECT_NE(range->subranges()[0].max, min_expr.get());
  CHECK_NE(range->subranges()[1].min, nullptr);
  CHECK_NE(range->subranges()[1].max, nullptr);
  EXPECT_NE(range->subranges()[1].min, min_expr.get());
  EXPECT_NE(range->subranges()[1].min, min_expr.get());
  // Verify value equality for the two ranges.
  EXPECT_EQ(range->subranges()[0].min->GetValueAs<int32_t>(),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(range->subranges()[0].max->GetValueAs<int32_t>(), kMin);
  EXPECT_TRUE(range->subranges()[0].min_included);
  EXPECT_FALSE(range->subranges()[0].max_included);

  EXPECT_EQ(range->subranges()[1].min->GetValueAs<int32_t>(), kMin);
  EXPECT_EQ(range->subranges()[1].max->GetValueAs<int32_t>(),
            std::numeric_limits<int32_t>::max());
  EXPECT_FALSE(range->subranges()[1].min_included);
  EXPECT_TRUE(range->subranges()[1].max_included);
  EXPECT_FALSE(range->IsEmpty());
}

TEST(ProtoConstraintValueSetTest, IntersectWithEmpty) {
  auto min_expr = std::make_unique<ProtoConstraintValueExpression>(kMin);
  auto max_expr = std::make_unique<ProtoConstraintValueExpression>(kMax);

  // Use the basic constructor.
  auto range = std::make_unique<ProtoConstraintValueSet>(
      min_expr.get(), /*min_included=*/true, max_expr.get(),
      /*max_included=*/true);
  auto empty_range = std::make_unique<ProtoConstraintValueSet>(
      /*min=*/nullptr, /*min_included=*/false, /*max=*/nullptr,
      /*max_included=*/false);

  auto status = range->IntersectWith(*empty_range);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(range->IsEmpty());
}

TEST(ProtoConstraintValueSetTest, SimpleIntersection) {
  auto min_expr1 = std::make_unique<ProtoConstraintValueExpression>(kMin);
  auto max_expr1 = std::make_unique<ProtoConstraintValueExpression>(kMax);
  auto range_10_100 = std::make_unique<ProtoConstraintValueSet>(
      min_expr1.get(), /*min_included=*/false, max_expr1.get(),
      /*max_included=*/true);

  constexpr int32_t kMin2 = 1;
  constexpr int32_t kMax2 = 20;

  auto min_expr2 = std::make_unique<ProtoConstraintValueExpression>(kMin2);
  auto max_expr2 = std::make_unique<ProtoConstraintValueExpression>(kMax2);

  auto range = std::make_unique<ProtoConstraintValueSet>(
      min_expr2.get(), /*min_included=*/false, max_expr2.get(),
      /*max_included=*/false);

  auto status = range->IntersectWith(*range_10_100);
  EXPECT_TRUE(status.ok());
  EXPECT_FALSE(range->IsEmpty());
  CHECK_EQ(range->subranges().size(), 1);
  // Verify the range endpoints.
  EXPECT_EQ(range->subranges().back().min->GetValueAs<int32_t>(), kMin);
  EXPECT_EQ(range->subranges().back().max->GetValueAs<int32_t>(), kMax2);
  // Verify that the endpoints are not included.
  EXPECT_FALSE(range->subranges().back().min_included);
  EXPECT_FALSE(range->subranges().back().max_included);
}

TEST(ProtoConstraintValueSetTest, ComplexIntersection) {
  auto expr_10 = std::make_unique<ProtoConstraintValueExpression>(kMin);
  // Create a kNe constraint to pass to the range constructor.
  // [int32_t_min, kMin) U (kMin, int32_t_max]
  auto constraint_10 = std::make_unique<ProtoConstraint>(
      /*ctx=*/nullptr,
      /*field_descriptor=*/nullptr, ConstraintType::kNe, expr_10.get(),
      /*value=*/kMin, /*depends_on=*/nullptr);
  auto range = std::make_unique<ProtoConstraintValueSet>(constraint_10.get());

  auto expr_100 = std::make_unique<ProtoConstraintValueExpression>(kMax);
  // Create a kNe constraint to pass to the range constructor.
  // [int32_t_min, kMax) U (kMax, int32_t_max]
  auto constraint_100 = std::make_unique<ProtoConstraint>(
      /*ctx=*/nullptr,
      /*field_descriptor=*/nullptr, ConstraintType::kNe, expr_100.get(),
      /*value=*/kMin, /*depends_on=*/nullptr);
  auto range_100 =
      std::make_unique<ProtoConstraintValueSet>(constraint_100.get());

  auto status = range->IntersectWith(*range_100);
  CHECK_OK(status);
  EXPECT_FALSE(range->IsEmpty());
  CHECK_EQ(range->subranges().size(), 3);
  // The ranges should be:
  // [int32_t_min, kMin) U (kMin, kMax) U (kMax, int32_t_max]
  EXPECT_EQ(range->subranges()[0].min->GetValueAs<int32_t>(),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(range->subranges()[0].max->GetValueAs<int32_t>(), kMin);

  EXPECT_EQ(range->subranges()[1].min->GetValueAs<int32_t>(), kMin);
  EXPECT_EQ(range->subranges()[1].max->GetValueAs<int32_t>(), kMax);

  EXPECT_EQ(range->subranges()[2].min->GetValueAs<int32_t>(), kMax);
  EXPECT_EQ(range->subranges()[2].max->GetValueAs<int32_t>(),
            std::numeric_limits<int32_t>::max());
}

}  // namespace
