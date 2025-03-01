// Copyright 2017-2021 The Verible Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "common/formatting/layout_optimizer.h"

#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "common/formatting/basic_format_style.h"
#include "common/formatting/layout_optimizer_internal.h"
#include "common/formatting/token_partition_tree.h"
#include "common/formatting/unwrapped_line.h"
#include "common/formatting/unwrapped_line_test_utils.h"
#include "common/strings/split.h"
#include "common/util/spacer.h"
#include "gtest/gtest.h"

namespace verible {

namespace {

template <typename T>
std::string ToString(const T& value) {
  std::ostringstream s;
  s << value;
  return s.str();
}

TEST(LayoutTypeTest, ToString) {
  EXPECT_EQ(ToString(LayoutType::kLine), "line");
  EXPECT_EQ(ToString(LayoutType::kJuxtaposition), "juxtaposition");
  EXPECT_EQ(ToString(LayoutType::kStack), "stack");
  EXPECT_EQ(ToString(static_cast<LayoutType>(-1)), "???");
}

bool TokenRangeEqual(const UnwrappedLine& left, const UnwrappedLine& right) {
  return left.TokensRange() == right.TokensRange();
}

class LayoutTest : public ::testing::Test, public UnwrappedLineMemoryHandler {
 public:
  LayoutTest()
      : sample_(
            "short_line\n"
            "loooooong_line"),
        tokens_(absl::StrSplit(sample_, '\n')) {
    for (const auto token : tokens_) {
      ftokens_.emplace_back(TokenInfo{1, token});
    }
    CreateTokenInfos(ftokens_);
  }

 protected:
  const std::string sample_;
  const std::vector<absl::string_view> tokens_;
  std::vector<TokenInfo> ftokens_;
};

TEST_F(LayoutTest, LineLayoutItemToString) {
  const auto begin = pre_format_tokens_.begin();

  UnwrappedLine short_line(0, begin);
  short_line.SpanUpToToken(begin + 1);
  UnwrappedLine long_line(0, begin + 1);
  long_line.SpanUpToToken(begin + 2);
  UnwrappedLine empty_line(0, begin);

  pre_format_tokens_[0].before.spaces_required = 1;
  pre_format_tokens_[1].before.break_decision = SpacingOptions::MustWrap;

  {
    LayoutItem item(short_line, 0);
    EXPECT_EQ(ToString(item),
              "[ short_line ], length: 10, indentation: 0, spacing: 1, must "
              "wrap: no");
  }
  {
    LayoutItem item(short_line, 3);
    EXPECT_EQ(ToString(item),
              "[ short_line ], length: 10, indentation: 3, spacing: 1, must "
              "wrap: no");
  }
  {
    LayoutItem item(long_line, 5);
    EXPECT_EQ(
        ToString(item),
        "[ loooooong_line ], length: 14, indentation: 5, spacing: 0, must "
        "wrap: YES");
  }
  {
    LayoutItem item(long_line, 7);
    EXPECT_EQ(
        ToString(item),
        "[ loooooong_line ], length: 14, indentation: 7, spacing: 0, must "
        "wrap: YES");
  }
  {
    LayoutItem item(empty_line, 11);
    EXPECT_EQ(ToString(item),
              "[  ], length: 0, indentation: 11, spacing: 0, must wrap: no");
  }
  {
    LayoutItem item(empty_line, 13);
    EXPECT_EQ(ToString(item),
              "[  ], length: 0, indentation: 13, spacing: 0, must wrap: no");
  }
}

TEST_F(LayoutTest, JuxtapositionLayoutItemToString) {
  {
    LayoutItem item(LayoutType::kJuxtaposition, 3, false, 5);
    EXPECT_EQ(ToString(item),
              "[<juxtaposition>], indentation: 5, spacing: 3, must wrap: no");
  }
  {
    LayoutItem item(LayoutType::kJuxtaposition, 7, true, 11);
    EXPECT_EQ(ToString(item),
              "[<juxtaposition>], indentation: 11, spacing: 7, must wrap: YES");
  }
}

TEST_F(LayoutTest, StackLayoutItemToString) {
  {
    LayoutItem item(LayoutType::kStack, 3, false, 5);
    EXPECT_EQ(ToString(item),
              "[<stack>], indentation: 5, spacing: 3, must wrap: no");
  }
  {
    LayoutItem item(LayoutType::kStack, 7, true, 11);
    EXPECT_EQ(ToString(item),
              "[<stack>], indentation: 11, spacing: 7, must wrap: YES");
  }
}

TEST_F(LayoutTest, AsUnwrappedLine) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();

  UnwrappedLine short_line(0, begin);
  short_line.SpanUpToToken(begin + 1);

  LayoutItem layout_short(short_line);

  const auto uwline = layout_short.ToUnwrappedLine();
  EXPECT_EQ(uwline.IndentationSpaces(), 0);
  EXPECT_EQ(uwline.TokensRange().begin(), short_line.TokensRange().begin());
  EXPECT_EQ(uwline.TokensRange().end(), short_line.TokensRange().end());
}

TEST_F(LayoutTest, LineLayout) {
  const auto begin = pre_format_tokens_.begin();

  {
    UnwrappedLine short_line(0, begin);
    short_line.SpanUpToToken(begin + 1);

    LayoutItem layout(short_line);
    EXPECT_EQ(layout.Type(), LayoutType::kLine);
    EXPECT_EQ(layout.IndentationSpaces(), 0);
    EXPECT_EQ(layout.SpacesBefore(), 0);
    EXPECT_EQ(layout.MustWrap(), false);
    EXPECT_EQ(layout.Length(), 10);
    EXPECT_EQ(layout.Text(), "short_line");
  }
  {
    UnwrappedLine empty_line(0, begin);

    LayoutItem layout(empty_line);
    EXPECT_EQ(layout.Type(), LayoutType::kLine);
    EXPECT_EQ(layout.IndentationSpaces(), 0);
    EXPECT_EQ(layout.SpacesBefore(), 0);
    EXPECT_EQ(layout.MustWrap(), false);
    EXPECT_EQ(layout.Length(), 0);
    EXPECT_EQ(layout.Text(), "");
  }
}

TEST_F(LayoutTest, TestHorizontalAndVerticalLayouts) {
  const auto spaces_before = 3;

  LayoutItem horizontal_layout(LayoutType::kJuxtaposition, spaces_before,
                               false);
  EXPECT_EQ(horizontal_layout.Type(), LayoutType::kJuxtaposition);
  EXPECT_EQ(horizontal_layout.SpacesBefore(), spaces_before);
  EXPECT_EQ(horizontal_layout.MustWrap(), false);

  LayoutItem vertical_layout(LayoutType::kStack, spaces_before, true);
  EXPECT_EQ(vertical_layout.Type(), LayoutType::kStack);
  EXPECT_EQ(vertical_layout.SpacesBefore(), spaces_before);
  EXPECT_EQ(vertical_layout.MustWrap(), true);
}

class LayoutFunctionTest : public ::testing::Test {
 public:
  LayoutFunctionTest()
      : layout_function_(LayoutFunction{{0, layout_, 10, 101.0F, 11},
                                        {1, layout_, 20, 202.0F, 22},
                                        {2, layout_, 30, 303.0F, 33},
                                        {3, layout_, 40, 404.0F, 44},
                                        {40, layout_, 50, 505.0F, 55},
                                        {50, layout_, 60, 606.0F, 66}}),
        const_layout_function_(layout_function_) {}

 protected:
  static const LayoutTree layout_;
  LayoutFunction layout_function_;
  const LayoutFunction const_layout_function_;
};
const LayoutTree LayoutFunctionTest::layout_ =
    LayoutTree(LayoutItem(LayoutType::kLine, 0, false));

TEST_F(LayoutFunctionTest, LayoutFunctionSegmentToString) {
  EXPECT_EQ(
      ToString(layout_function_[0]),
      "[  0] (101.000 + 11*x), span: 10, layout:\n"
      "      { ([  ], length: 0, indentation: 0, spacing: 0, must wrap: no) }");
  EXPECT_EQ(
      ToString(layout_function_[5]),
      "[ 50] (606.000 + 66*x), span: 60, layout:\n"
      "      { ([  ], length: 0, indentation: 0, spacing: 0, must wrap: no) }");
}

TEST_F(LayoutFunctionTest, LayoutFunctionToString) {
  EXPECT_EQ(ToString(layout_function_),
            "{\n"
            "  [  0] ( 101.000 +   11*x), span:  10, layout:\n"
            "        { ([  ], length: 0, indentation: 0, spacing: 0, must "
            "wrap: no) }\n"
            "  [  1] ( 202.000 +   22*x), span:  20, layout:\n"
            "        { ([  ], length: 0, indentation: 0, spacing: 0, must "
            "wrap: no) }\n"
            "  [  2] ( 303.000 +   33*x), span:  30, layout:\n"
            "        { ([  ], length: 0, indentation: 0, spacing: 0, must "
            "wrap: no) }\n"
            "  [  3] ( 404.000 +   44*x), span:  40, layout:\n"
            "        { ([  ], length: 0, indentation: 0, spacing: 0, must "
            "wrap: no) }\n"
            "  [ 40] ( 505.000 +   55*x), span:  50, layout:\n"
            "        { ([  ], length: 0, indentation: 0, spacing: 0, must "
            "wrap: no) }\n"
            "  [ 50] ( 606.000 +   66*x), span:  60, layout:\n"
            "        { ([  ], length: 0, indentation: 0, spacing: 0, must "
            "wrap: no) }\n"
            "}");
  EXPECT_EQ(ToString(LayoutFunction{}), "{}");
}

TEST_F(LayoutFunctionTest, Size) {
  EXPECT_EQ(layout_function_.size(), 6);
  EXPECT_FALSE(layout_function_.empty());

  EXPECT_EQ(const_layout_function_.size(), 6);
  EXPECT_FALSE(const_layout_function_.empty());

  LayoutFunction empty_layout_function{};
  EXPECT_EQ(empty_layout_function.size(), 0);
  EXPECT_TRUE(empty_layout_function.empty());
}

TEST_F(LayoutFunctionTest, Iteration) {
  static const auto columns = {0, 1, 2, 3, 40, 50};

  {
    auto it = layout_function_.begin();
    EXPECT_NE(it, layout_function_.end());
    EXPECT_EQ(it + 6, layout_function_.end());
    EXPECT_EQ(it->column, 0);

    auto column_it = columns.begin();
    for (auto& segment : layout_function_) {
      EXPECT_EQ(segment.column, *column_it);
      EXPECT_NE(column_it, columns.end());
      ++column_it;
    }
    EXPECT_EQ(column_it, columns.end());
  }
  {
    auto it = const_layout_function_.begin();
    EXPECT_NE(it, const_layout_function_.end());
    EXPECT_EQ(it + 6, const_layout_function_.end());
    EXPECT_EQ(it->column, 0);

    auto column_it = columns.begin();
    for (auto& segment : const_layout_function_) {
      EXPECT_EQ(segment.column, *column_it);
      EXPECT_NE(column_it, columns.end());
      ++column_it;
    }
    EXPECT_EQ(column_it, columns.end());
  }
  {
    LayoutFunction empty_layout_function{};

    auto it = empty_layout_function.begin();
    EXPECT_EQ(it, empty_layout_function.end());
    for (auto& segment [[maybe_unused]] : empty_layout_function) {
      EXPECT_FALSE(true);
    }
  }
}

TEST_F(LayoutFunctionTest, AtOrToTheLeftOf) {
  {
    EXPECT_EQ(layout_function_.AtOrToTheLeftOf(0), layout_function_.begin());
    EXPECT_EQ(layout_function_.AtOrToTheLeftOf(1),
              layout_function_.begin() + 1);
    EXPECT_EQ(layout_function_.AtOrToTheLeftOf(2),
              layout_function_.begin() + 2);
    for (int i = 3; i < 40; ++i) {
      EXPECT_EQ(layout_function_.AtOrToTheLeftOf(i),
                layout_function_.begin() + 3)
          << "i: " << i;
    }
    for (int i = 40; i < 50; ++i) {
      EXPECT_EQ(layout_function_.AtOrToTheLeftOf(i),
                layout_function_.begin() + 4)
          << "i: " << i;
    }
    for (int i = 50; i < 70; ++i) {
      EXPECT_EQ(layout_function_.AtOrToTheLeftOf(i),
                layout_function_.begin() + 5)
          << "i: " << i;
    }
    EXPECT_EQ(layout_function_.AtOrToTheLeftOf(std::numeric_limits<int>::max()),
              layout_function_.begin() + 5);
  }
  {
    LayoutFunction empty_layout_function{};
    EXPECT_EQ(empty_layout_function.AtOrToTheLeftOf(0),
              empty_layout_function.end());
    EXPECT_EQ(empty_layout_function.AtOrToTheLeftOf(1),
              empty_layout_function.end());
    EXPECT_EQ(
        empty_layout_function.AtOrToTheLeftOf(std::numeric_limits<int>::max()),
        empty_layout_function.end());
  }
  {
    EXPECT_EQ(const_layout_function_.AtOrToTheLeftOf(0),
              const_layout_function_.begin());
    EXPECT_EQ(const_layout_function_.AtOrToTheLeftOf(1),
              const_layout_function_.begin() + 1);
    EXPECT_EQ(const_layout_function_.AtOrToTheLeftOf(2),
              const_layout_function_.begin() + 2);
    for (int i = 3; i < 40; ++i) {
      EXPECT_EQ(const_layout_function_.AtOrToTheLeftOf(i),
                const_layout_function_.begin() + 3)
          << "i: " << i;
    }
    for (int i = 40; i < 50; ++i) {
      EXPECT_EQ(const_layout_function_.AtOrToTheLeftOf(i),
                const_layout_function_.begin() + 4)
          << "i: " << i;
    }
    for (int i = 50; i < 70; ++i) {
      EXPECT_EQ(const_layout_function_.AtOrToTheLeftOf(i),
                const_layout_function_.begin() + 5)
          << "i: " << i;
    }
    EXPECT_EQ(
        const_layout_function_.AtOrToTheLeftOf(std::numeric_limits<int>::max()),
        const_layout_function_.begin() + 5);
  }
  {
    const LayoutFunction empty_layout_function{};
    EXPECT_EQ(empty_layout_function.AtOrToTheLeftOf(0),
              empty_layout_function.end());
    EXPECT_EQ(empty_layout_function.AtOrToTheLeftOf(1),
              empty_layout_function.end());
    EXPECT_EQ(
        empty_layout_function.AtOrToTheLeftOf(std::numeric_limits<int>::max()),
        empty_layout_function.end());
  }
}

TEST_F(LayoutFunctionTest, Insertion) {
  layout_function_.push_back({60, layout_, 1, 6.0F, 6});
  ASSERT_EQ(layout_function_.size(), 7);
  EXPECT_EQ(layout_function_[6].column, 60);

  layout_function_.push_back({70, layout_, 1, 6.0F, 6});
  ASSERT_EQ(layout_function_.size(), 8);
  EXPECT_EQ(layout_function_[6].column, 60);
  EXPECT_EQ(layout_function_[7].column, 70);

  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(layout_function_[i].column, const_layout_function_[i].column)
        << "i: " << i;
  }
}

TEST_F(LayoutFunctionTest, Subscript) {
  EXPECT_EQ(layout_function_[0].column, 0);
  EXPECT_EQ(layout_function_[1].column, 1);
  EXPECT_EQ(layout_function_[2].column, 2);
  EXPECT_EQ(layout_function_[3].column, 3);
  EXPECT_EQ(layout_function_[4].column, 40);
  EXPECT_EQ(layout_function_[5].column, 50);
  layout_function_[5].column += 5;
  EXPECT_EQ(layout_function_[5].column, 55);

  EXPECT_EQ(const_layout_function_[0].column, 0);
  EXPECT_EQ(const_layout_function_[1].column, 1);
  EXPECT_EQ(const_layout_function_[2].column, 2);
  EXPECT_EQ(const_layout_function_[3].column, 3);
  EXPECT_EQ(const_layout_function_[4].column, 40);
  EXPECT_EQ(const_layout_function_[5].column, 50);
}

class LayoutFunctionIteratorTest : public LayoutFunctionTest {};

TEST_F(LayoutFunctionIteratorTest, ToString) {
  {
    std::ostringstream addr;
    addr << &layout_function_;
    EXPECT_EQ(ToString(layout_function_.begin()),
              absl::StrCat(addr.str(), "[0/6]"));
    EXPECT_EQ(ToString(layout_function_.end()),
              absl::StrCat(addr.str(), "[6/6]"));
  }
  {
    std::ostringstream addr;
    addr << &const_layout_function_;
    EXPECT_EQ(ToString(const_layout_function_.begin()),
              absl::StrCat(addr.str(), "[0/6]"));
    EXPECT_EQ(ToString(const_layout_function_.end()),
              absl::StrCat(addr.str(), "[6/6]"));
  }
  {
    LayoutFunction empty_layout_function{};
    std::ostringstream addr;
    addr << &empty_layout_function;
    EXPECT_EQ(ToString(empty_layout_function.begin()),
              absl::StrCat(addr.str(), "[0/0]"));
    EXPECT_EQ(ToString(empty_layout_function.end()),
              absl::StrCat(addr.str(), "[0/0]"));
  }
}

TEST_F(LayoutFunctionIteratorTest, MoveToKnotAtOrToTheLeftOf) {
  {
    auto it = layout_function_.begin();

    it.MoveToKnotAtOrToTheLeftOf(2);
    EXPECT_EQ(it, layout_function_.begin() + 2);
    it.MoveToKnotAtOrToTheLeftOf(0);
    EXPECT_EQ(it, layout_function_.begin());
    it.MoveToKnotAtOrToTheLeftOf(std::numeric_limits<int>::max());
    EXPECT_EQ(it, layout_function_.begin() + 5);
    it.MoveToKnotAtOrToTheLeftOf(1);
    EXPECT_EQ(it, layout_function_.begin() + 1);
    it.MoveToKnotAtOrToTheLeftOf(1);
    EXPECT_EQ(it, layout_function_.begin() + 1);
    for (int i = 3; i < 40; ++i) {
      it.MoveToKnotAtOrToTheLeftOf(i);
      EXPECT_EQ(it, layout_function_.begin() + 3) << "i: " << i;
    }
    for (int i = 50; i < 70; ++i) {
      it.MoveToKnotAtOrToTheLeftOf(i);
      EXPECT_EQ(it, layout_function_.begin() + 5) << "i: " << i;
    }
    for (int i = 49; i >= 40; --i) {
      it.MoveToKnotAtOrToTheLeftOf(i);
      EXPECT_EQ(it, layout_function_.begin() + 4) << "i: " << i;
    }
  }
  {
    LayoutFunction empty_layout_function{};
    auto it = empty_layout_function.begin();

    it.MoveToKnotAtOrToTheLeftOf(0);
    EXPECT_EQ(it, empty_layout_function.end());
    it.MoveToKnotAtOrToTheLeftOf(std::numeric_limits<int>::max());
    EXPECT_EQ(it, empty_layout_function.end());
    it.MoveToKnotAtOrToTheLeftOf(1);
    EXPECT_EQ(it, empty_layout_function.end());
  }

  {
    auto it = const_layout_function_.begin();

    it.MoveToKnotAtOrToTheLeftOf(2);
    EXPECT_EQ(it, const_layout_function_.begin() + 2);
    it.MoveToKnotAtOrToTheLeftOf(0);
    EXPECT_EQ(it, const_layout_function_.begin());
    it.MoveToKnotAtOrToTheLeftOf(std::numeric_limits<int>::max());
    EXPECT_EQ(it, const_layout_function_.begin() + 5);
    it.MoveToKnotAtOrToTheLeftOf(1);
    EXPECT_EQ(it, const_layout_function_.begin() + 1);
    it.MoveToKnotAtOrToTheLeftOf(1);
    EXPECT_EQ(it, const_layout_function_.begin() + 1);
    for (int i = 3; i < 40; ++i) {
      it.MoveToKnotAtOrToTheLeftOf(i);
      EXPECT_EQ(it, const_layout_function_.begin() + 3) << "i: " << i;
    }
    for (int i = 50; i < 70; ++i) {
      it.MoveToKnotAtOrToTheLeftOf(i);
      EXPECT_EQ(it, const_layout_function_.begin() + 5) << "i: " << i;
    }
    for (int i = 49; i >= 40; --i) {
      it.MoveToKnotAtOrToTheLeftOf(i);
      EXPECT_EQ(it, const_layout_function_.begin() + 4) << "i: " << i;
    }
  }
  {
    const LayoutFunction empty_layout_function{};
    auto it = empty_layout_function.begin();

    it.MoveToKnotAtOrToTheLeftOf(0);
    EXPECT_EQ(it, empty_layout_function.end());
    it.MoveToKnotAtOrToTheLeftOf(std::numeric_limits<int>::max());
    EXPECT_EQ(it, empty_layout_function.end());
    it.MoveToKnotAtOrToTheLeftOf(1);
    EXPECT_EQ(it, empty_layout_function.end());
  }
}

TEST_F(LayoutFunctionIteratorTest, ContainerRelatedMethods) {
  {
    int i = 0;
    auto it = layout_function_.begin();
    while (it != layout_function_.end()) {
      EXPECT_EQ(&it.Container(), &layout_function_);
      EXPECT_EQ(it.Index(), i);
      EXPECT_FALSE(it.IsEnd());
      ++it;
      ++i;
    }
    EXPECT_EQ(&it.Container(), &layout_function_);
    EXPECT_EQ(it.Index(), i);
    EXPECT_TRUE(it.IsEnd());
  }
  {
    int i = 0;
    auto it = const_layout_function_.begin();
    while (it != const_layout_function_.end()) {
      EXPECT_EQ(&it.Container(), &const_layout_function_);
      EXPECT_EQ(it.Index(), i);
      EXPECT_FALSE(it.IsEnd());
      ++it;
      ++i;
    }
    EXPECT_EQ(&it.Container(), &const_layout_function_);
    EXPECT_EQ(it.Index(), i);
    EXPECT_TRUE(it.IsEnd());
  }
}

std::ostream& PrintIndented(std::ostream& stream, absl::string_view str,
                            int indentation) {
  for (const auto& line : verible::SplitLinesKeepLineTerminator(str))
    stream << verible::Spacer(indentation) << line;
  return stream;
}

class LayoutFunctionFactoryTest : public ::testing::Test,
                                  public UnwrappedLineMemoryHandler {
 public:
  LayoutFunctionFactoryTest()
      : sample_(
            //   :    |10  :    |20  :    |30  :    |40
            "This line is short.\n"
            "This line is so long that it exceeds column limit.\n"
            "        Indented  line  with  many  spaces .\n"

            "One under 40 column limit (39 columns).\n"
            "Exactly at 40 column limit (40 columns).\n"
            "One over 40 column limit (41 characters).\n"

            "One under 30 limit (29 cols).\n"
            "Exactly at 30 limit (30 cols).\n"
            "One over 30 limit (31 columns).\n"

            "10 columns"),
        tokens_(
            absl::StrSplit(sample_, absl::ByAnyChar(" \n"), absl::SkipEmpty())),
        style_(CreateStyle()),
        factory_(LayoutFunctionFactory(style_)) {
    for (const auto token : tokens_) {
      ftokens_.emplace_back(TokenInfo{1, token});
    }
    CreateTokenInfosExternalStringBuffer(ftokens_);
    ConnectPreFormatTokensPreservedSpaceStarts(sample_.data(),
                                               &pre_format_tokens_);

    // Create UnwrappedLine for each sample text's line and set token properties
    uwlines_.emplace_back(0, pre_format_tokens_.begin());
    for (auto token_it = pre_format_tokens_.begin();
         token_it != pre_format_tokens_.end(); ++token_it) {
      const auto leading_spaces = token_it->OriginalLeadingSpaces();

      // First token in a line
      if (absl::StrContains(leading_spaces, '\n')) {
        token_it->before.break_decision = SpacingOptions::MustWrap;

        uwlines_.back().SpanUpToToken(token_it);
        uwlines_.emplace_back(0, token_it);
      }

      // Count spaces preceding the token and set spaces_required accordingly
      auto last_non_space_offset = leading_spaces.find_last_not_of(' ');
      if (last_non_space_offset != absl::string_view::npos) {
        token_it->before.spaces_required =
            leading_spaces.size() - 1 - last_non_space_offset;
      } else {
        token_it->before.spaces_required = leading_spaces.size();
      }
    }
    uwlines_.back().SpanUpToToken(pre_format_tokens_.end());
  }

 protected:
  // Readable names for each line
  static constexpr int kShortLineId = 0;
  static constexpr int kLongLineId = 1;
  static constexpr int kIndentedLineId = 2;

  static constexpr int kOneUnder40LimitLineId = 3;
  static constexpr int kExactlyAt40LimitLineId = 4;
  static constexpr int kOneOver40LimitLineId = 5;

  static constexpr int kOneUnder30LimitLineId = 6;
  static constexpr int kExactlyAt30LimitLineId = 7;
  static constexpr int kOneOver30LimitLineId = 8;

  static constexpr int k10ColumnsLineId = 9;

  static BasicFormatStyle CreateStyle() {
    BasicFormatStyle style;
    // Hardcode everything to prevent failures when defaults change.
    style.indentation_spaces = 2;
    style.wrap_spaces = 4;
    style.column_limit = 40;
    style.over_column_limit_penalty = 100;
    style.line_break_penalty = 2;
    return style;
  }

  static void ExpectLayoutFunctionsEqual(const LayoutFunction& actual,
                                         const LayoutFunction& expected,
                                         int line_no) {
    using ::testing::PrintToString;
    std::ostringstream msg;
    if (actual.size() != expected.size()) {
      msg << "invalid value of size():\n"
          << "  actual:   " << actual.size() << "\n"
          << "  expected: " << expected.size() << "\n\n";
    }

    for (int i = 0; i < std::min(actual.size(), expected.size()); ++i) {
      std::ostringstream segment_msg;

      if (actual[i].column != expected[i].column) {
        segment_msg << "  invalid column:\n"
                    << "    actual:   " << actual[i].column << "\n"
                    << "    expected: " << expected[i].column << "\n";
      }
      if (actual[i].intercept != expected[i].intercept) {
        segment_msg << "  invalid intercept:\n"
                    << "    actual:   " << actual[i].intercept << "\n"
                    << "    expected: " << expected[i].intercept << "\n";
      }
      if (actual[i].gradient != expected[i].gradient) {
        segment_msg << "  invalid gradient:\n"
                    << "    actual:   " << actual[i].gradient << "\n"
                    << "    expected: " << expected[i].gradient << "\n";
      }
      if (actual[i].span != expected[i].span) {
        segment_msg << "  invalid span:\n"
                    << "    actual:   " << actual[i].span << "\n"
                    << "    expected: " << expected[i].span << "\n";
      }
      auto layout_diff = DeepEqual(actual[i].layout, expected[i].layout);
      if (layout_diff.left != nullptr) {
        segment_msg << "  invalid layout (fragment):\n"
                    << "    actual:\n";
        PrintIndented(segment_msg, PrintToString(*layout_diff.left), 6) << "\n";
        segment_msg << "    expected:\n";
        PrintIndented(segment_msg, PrintToString(*layout_diff.right), 6)
            << "\n";
      }
      if (auto str = segment_msg.str(); !str.empty())
        msg << "segment[" << i << "]:\n" << str << "\n";
    }

    if (const auto str = msg.str(); !str.empty()) {
      ADD_FAILURE_AT(__FILE__, line_no) << "LayoutFunctions differ.\nActual:\n"
                                        << actual << "\nExpected:\n"
                                        << expected << "\n\nDetails:\n\n"
                                        << str;
    } else {
      SUCCEED();
    }
  }

  const std::string sample_;
  const std::vector<absl::string_view> tokens_;
  std::vector<TokenInfo> ftokens_;
  std::vector<UnwrappedLine> uwlines_;
  const BasicFormatStyle style_;
  const LayoutFunctionFactory factory_;
};

TEST_F(LayoutFunctionFactoryTest, Line) {
  using LT = LayoutTree;
  using LI = LayoutItem;

  {
    const auto lf = factory_.Line(uwlines_[kShortLineId]);
    const auto expected_lf = LayoutFunction{
        {0, LT(LI(uwlines_[kShortLineId])), 19, 0.0F, 0},
        {21, LT(LI(uwlines_[kShortLineId])), 19, 0.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }

  {
    const auto lf = factory_.Line(uwlines_[kLongLineId]);
    const auto expected_lf = LayoutFunction{
        {0, LT(LI(uwlines_[kLongLineId])), 50, 1000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Line(uwlines_[kIndentedLineId]);
    const auto expected_lf = LayoutFunction{
        {0, LT(LI(uwlines_[kIndentedLineId])), 36, 0.0F, 0},
        {4, LT(LI(uwlines_[kIndentedLineId])), 36, 0.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Line(uwlines_[kOneUnder40LimitLineId]);
    const auto expected_lf = LayoutFunction{
        {0, LT(LI(uwlines_[kOneUnder40LimitLineId])), 39, 0.0F, 0},
        {1, LT(LI(uwlines_[kOneUnder40LimitLineId])), 39, 0.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Line(uwlines_[kExactlyAt40LimitLineId]);
    const auto expected_lf = LayoutFunction{
        {0, LT(LI(uwlines_[kExactlyAt40LimitLineId])), 40, 0.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Line(uwlines_[kOneOver40LimitLineId]);
    const auto expected_lf = LayoutFunction{
        {0, LT(LI(uwlines_[kOneOver40LimitLineId])), 41, 100.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
}

TEST_F(LayoutFunctionFactoryTest, Stack) {
  using LT = LayoutTree;
  using LI = LayoutItem;

  {
    const auto lf = factory_.Stack({});
    const auto expected_lf = LayoutFunction{};
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto line = factory_.Line(uwlines_[kShortLineId]);
    const auto lf = factory_.Stack({line});
    const auto& expected_lf = line;
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 10, 2.0F, 0},
        {21, expected_layout, 10, 2.0F, 100},
        {30, expected_layout, 10, 902.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 19, 2.0F, 0},
        {21, expected_layout, 19, 2.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kLongLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[kLongLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 50, 1002.0F, 100},
        {21, expected_layout, 50, 3102.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kLongLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, true),  //
                                    LT(LI(uwlines_[kLongLineId])),    //
                                    LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 19, 1002.0F, 100},
        {21, expected_layout, 19, 3102.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kLongLineId]),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[kLongLineId])),     //
                                    LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 10, 1004.0F, 100},
        {21, expected_layout, 10, 3104.0F, 200},
        {30, expected_layout, 10, 4904.0F, 300},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kIndentedLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[kIndentedLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 36, 2.0F, 0},
        {4, expected_layout, 36, 2.0F, 100},
        {21, expected_layout, 36, 1702.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kOneUnder40LimitLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[kOneUnder40LimitLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 39, 2.0F, 0},
        {1, expected_layout, 39, 2.0F, 100},
        {21, expected_layout, 39, 2002.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kOneOver40LimitLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[kOneOver40LimitLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 41, 102.0F, 100},
        {21, expected_layout, 41, 2202.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kExactlyAt40LimitLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, false),  //
                                    LT(LI(uwlines_[kShortLineId])),    //
                                    LT(LI(uwlines_[kExactlyAt40LimitLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 40, 2.0F, 100},
        {21, expected_layout, 40, 2102.0F, 100},
    };
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kOneUnder40LimitLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kStack, 0, true),           //
           LT(LI(uwlines_[kOneUnder40LimitLineId])),  //
           LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 19, 2.0F, 0},
        {1, expected_layout, 19, 2.0F, 100},
        {21, expected_layout, 19, 2002.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kOneOver40LimitLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, true),          //
                                    LT(LI(uwlines_[kOneOver40LimitLineId])),  //
                                    LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 19, 102.0F, 100},
        {21, expected_layout, 19, 2202.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kExactlyAt40LimitLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kStack, 0, true),            //
           LT(LI(uwlines_[kExactlyAt40LimitLineId])),  //
           LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 40, 2.0F, 100},
        {21, expected_layout, 40, 2102.0F, 100},
    };
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kLongLineId]),
        factory_.Stack({
            factory_.Line(uwlines_[kIndentedLineId]),
            factory_.Line(uwlines_[kOneUnder40LimitLineId]),
            factory_.Line(uwlines_[kExactlyAt40LimitLineId]),
            factory_.Line(uwlines_[kOneOver40LimitLineId]),
            factory_.Line(uwlines_[k10ColumnsLineId]),
        }),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kStack, 0, false),           //
           LT(LI(uwlines_[kShortLineId])),             //
           LT(LI(uwlines_[kLongLineId])),              //
           LT(LI(uwlines_[kIndentedLineId])),          //
           LT(LI(uwlines_[kOneUnder40LimitLineId])),   //
           LT(LI(uwlines_[kExactlyAt40LimitLineId])),  //
           LT(LI(uwlines_[kOneOver40LimitLineId])),    //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 10, 1112.0F, 300},
        {1, expected_layout, 10, 1412.0F, 400},
        {4, expected_layout, 10, 2612.0F, 500},
        {21, expected_layout, 10, 11112.0F, 600},
        {30, expected_layout, 10, 16512.0F, 700},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    // Expected result here is the same as in the test case above
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kLongLineId]),
        factory_.Line(uwlines_[kIndentedLineId]),
        factory_.Stack({
            factory_.Line(uwlines_[kOneUnder40LimitLineId]),
            factory_.Line(uwlines_[kExactlyAt40LimitLineId]),
            factory_.Line(uwlines_[kOneOver40LimitLineId]),
        }),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kStack, 0, false),           //
           LT(LI(uwlines_[kShortLineId])),             //
           LT(LI(uwlines_[kLongLineId])),              //
           LT(LI(uwlines_[kIndentedLineId])),          //
           LT(LI(uwlines_[kOneUnder40LimitLineId])),   //
           LT(LI(uwlines_[kExactlyAt40LimitLineId])),  //
           LT(LI(uwlines_[kOneOver40LimitLineId])),    //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 10, 1112.0F, 300},
        {1, expected_layout, 10, 1412.0F, 400},
        {4, expected_layout, 10, 2612.0F, 500},
        {21, expected_layout, 10, 11112.0F, 600},
        {30, expected_layout, 10, 16512.0F, 700},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
}

TEST_F(LayoutFunctionFactoryTest, Juxtaposition) {
  using LT = LayoutTree;
  using LI = LayoutItem;

  static const auto kSampleStackLayout =
      LT(LI(LayoutType::kStack, 0, false),  //
         LT(LI(uwlines_[kShortLineId])),    //
         LT(LI(uwlines_[kLongLineId])),     //
         LT(LI(uwlines_[k10ColumnsLineId])));
  // Result of:
  // factory_.Stack({
  //     factory_.Line(uwlines_[kShortLineId]),
  //     factory_.Line(uwlines_[kLongLineId]),
  //     factory_.Line(uwlines_[k10ColumnsLineId]),
  // });
  static const auto kSampleStackLayoutFunction = LayoutFunction{
      {0, kSampleStackLayout, 10, 1004.0F, 100},
      {21, kSampleStackLayout, 10, 3104.0F, 200},
      {30, kSampleStackLayout, 10, 4904.0F, 300},
  };

  {
    const auto lf = factory_.Juxtaposition({});
    const auto expected_lf = LayoutFunction{};
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto line = factory_.Line(uwlines_[kShortLineId]);
    const auto lf = factory_.Juxtaposition({line});
    const auto& expected_lf = line;
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LT(LI(uwlines_[kShortLineId])),            //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 29, 0.0F, 0},
        {11, expected_layout, 29, 0.0F, 100},
        {21, expected_layout, 29, 1000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LT(LI(uwlines_[kShortLineId])),            //
           LT(LI(uwlines_[k10ColumnsLineId])),        //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 39, 0.0F, 0},
        {1, expected_layout, 39, 0.0F, 100},
        {11, expected_layout, 39, 1000.0F, 100},
        {21, expected_layout, 39, 2000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kJuxtaposition, 0, true),  //
                                    LT(LI(uwlines_[k10ColumnsLineId])),       //
                                    LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 29, 0.0F, 0},
        {11, expected_layout, 29, 0.0F, 100},
        {30, expected_layout, 29, 1900.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kIndentedLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LT(LI(uwlines_[kShortLineId])),            //
           LT(LI(uwlines_[kIndentedLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 63, 2300.0F, 100},
        {21, expected_layout, 63, 3600.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[kIndentedLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kJuxtaposition, 8, true),  //
                                    LT(LI(uwlines_[kIndentedLineId])),        //
                                    LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 55, 1500.0F, 100},
        {4, expected_layout, 55, 1900.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        kSampleStackLayoutFunction,
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           kSampleStackLayout,                        //
           LT(LI(uwlines_[kShortLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 29, 1004.0F, 100},
        {11, expected_layout, 29, 2104.0F, 200},
        {21, expected_layout, 29, 4104.0F, 300},
        {30, expected_layout, 29, 6804.0F, 300},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[kShortLineId]),
        kSampleStackLayoutFunction,
    });
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LT(LI(uwlines_[kShortLineId])),            //
           kSampleStackLayout);
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 29, 2904.0F, 100},
        {2, expected_layout, 29, 3104.0F, 200},
        {11, expected_layout, 29, 4904.0F, 300},
        {21, expected_layout, 29, 7904.0F, 300},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf =
        factory_.Juxtaposition({factory_.Line(uwlines_[kOneUnder30LimitLineId]),
                                factory_.Line(uwlines_[k10ColumnsLineId])});
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, true),   //
           LT(LI(uwlines_[kOneUnder30LimitLineId])),  //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 39, 0.0F, 0},
        {1, expected_layout, 39, 0.0F, 100},
        {11, expected_layout, 39, 1000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition(
        {factory_.Line(uwlines_[kExactlyAt30LimitLineId]),
         factory_.Line(uwlines_[k10ColumnsLineId])});
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, true),    //
           LT(LI(uwlines_[kExactlyAt30LimitLineId])),  //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 40, 0.0F, 100},
        {10, expected_layout, 40, 1000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf =
        factory_.Juxtaposition({factory_.Line(uwlines_[kOneOver30LimitLineId]),
                                factory_.Line(uwlines_[k10ColumnsLineId])});
    const auto expected_layout = LT(LI(LayoutType::kJuxtaposition, 0, true),  //
                                    LT(LI(uwlines_[kOneOver30LimitLineId])),  //
                                    LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 41, 100.0F, 100},
        {9, expected_layout, 41, 1000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kLongLineId]),
        factory_.Juxtaposition({
            factory_.Line(uwlines_[kIndentedLineId]),
            factory_.Line(uwlines_[kOneUnder40LimitLineId]),
            factory_.Line(uwlines_[kExactlyAt40LimitLineId]),
            factory_.Line(uwlines_[kOneOver40LimitLineId]),
            factory_.Line(uwlines_[k10ColumnsLineId]),
        }),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, false),   //
           LT(LI(uwlines_[kShortLineId])),             //
           LT(LI(uwlines_[kLongLineId])),              //
           LT(LI(uwlines_[kIndentedLineId])),          //
           LT(LI(uwlines_[kOneUnder40LimitLineId])),   //
           LT(LI(uwlines_[kExactlyAt40LimitLineId])),  //
           LT(LI(uwlines_[kOneOver40LimitLineId])),    //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 243, 19500.0F, 100},
        {21, expected_layout, 243, 21600.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    // Expected result here is the same as in the test case above
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kLongLineId]),
        factory_.Line(uwlines_[kIndentedLineId]),
        factory_.Juxtaposition({
            factory_.Line(uwlines_[kOneUnder40LimitLineId]),
            factory_.Line(uwlines_[kExactlyAt40LimitLineId]),
            factory_.Line(uwlines_[kOneOver40LimitLineId]),
        }),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout =
        LT(LI(LayoutType::kJuxtaposition, 0, false),   //
           LT(LI(uwlines_[kShortLineId])),             //
           LT(LI(uwlines_[kLongLineId])),              //
           LT(LI(uwlines_[kIndentedLineId])),          //
           LT(LI(uwlines_[kOneUnder40LimitLineId])),   //
           LT(LI(uwlines_[kExactlyAt40LimitLineId])),  //
           LT(LI(uwlines_[kOneOver40LimitLineId])),    //
           LT(LI(uwlines_[k10ColumnsLineId])));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 243, 19500.0F, 100},
        {21, expected_layout, 243, 21600.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
}

TEST_F(LayoutFunctionFactoryTest, Choice) {
  using LT = LayoutTree;
  using LI = LayoutItem;

  struct ChoiceTestCase {
    int line_no;
    const std::initializer_list<LayoutFunction> choices;
    const LayoutFunction expected;
  };

  // Layout doesn't really matter in this test
  static const auto layout = LT(LI(LayoutType::kLine, 0, false));

  static const ChoiceTestCase kTestCases[] = {
      {__LINE__, {}, LayoutFunction{}},
      {__LINE__,
       {
           LayoutFunction{{0, layout, 10, 100.0F, 10}},
       },
       LayoutFunction{{0, layout, 10, 100.0F, 10}}},
      {__LINE__,
       {
           LayoutFunction{{0, layout, 10, 100.0F, 10}},
           LayoutFunction{{0, layout, 10, 200.0F, 10}},
       },
       LayoutFunction{{0, layout, 10, 100.0F, 10}}},
      {__LINE__,
       {
           LayoutFunction{{0, layout, 10, 200.0F, 10}},
           LayoutFunction{{0, layout, 10, 100.0F, 10}},
       },
       LayoutFunction{{0, layout, 10, 100.0F, 10}}},
      {__LINE__,
       {
           LayoutFunction{{0, layout, 10, 100.0F, 10}},
           LayoutFunction{{0, layout, 10, 100.0F, 10}},
       },
       LayoutFunction{{0, layout, 10, 100.0F, 10}}},
      {__LINE__,
       {
           LayoutFunction{{0, layout, 10, 100.0F, 1}},
           LayoutFunction{{0, layout, 10, 0.0F, 3}},
       },
       LayoutFunction{
           {0, layout, 10, 0.0F, 3},
           {50, layout, 10, 150.0F, 1},
       }},
      {__LINE__,
       {
           LayoutFunction{
               {0, layout, 10, 100.0F, 1},
           },
           LayoutFunction{
               {0, layout, 10, 0.0F, 3},
               {50, layout, 10, 150.0F, 0},
           },
       },
       LayoutFunction{
           {0, layout, 10, 0.0F, 3},
           {50, layout, 10, 150.0F, 0},
       }},
      {__LINE__,
       {
           LayoutFunction{
               {0, layout, 10, 100.0F, 1},
           },
           LayoutFunction{
               {0, layout, 10, 0.0F, 3},
               {50, layout, 10, 160.0F, 0},
           },
       },
       LayoutFunction{
           {0, layout, 10, 0.0F, 3},
           {50, layout, 10, 150.0F, 1},
           {60, layout, 10, 160.0F, 0},
       }},
      {__LINE__,
       {
           LayoutFunction{
               {0, layout, 10, 100.0F, 1},
           },
           LayoutFunction{
               {0, layout, 10, 0.0F, 3},
               {50, layout, 10, 160.0F, 0},
           },
       },
       LayoutFunction{
           {0, layout, 10, 0.0F, 3},
           {50, layout, 10, 150.0F, 1},
           {60, layout, 10, 160.0F, 0},
       }},
      {__LINE__,
       {
           LayoutFunction{
               {0, layout, 10, 100.0F, 1},
               {50, layout, 10, 150.0F, 0},
           },
           LayoutFunction{
               {0, layout, 10, 125.0F, 0},
               {75, layout, 10, 125.0F, 1},
           },
       },
       LayoutFunction{
           {0, layout, 10, 100.0F, 1},
           {25, layout, 10, 125.0F, 0},
           {75, layout, 10, 125.0F, 1},
           {100, layout, 10, 150.0F, 0},
       }},
      {__LINE__,
       {
           LayoutFunction{
               {0, layout, 1, 50.0F, 0},
           },
           LayoutFunction{
               {0, layout, 2, 0.0F, 10},
           },
           LayoutFunction{
               {0, layout, 3, 999.0F, 0},
               {10, layout, 3, 0.0F, 10},
           },
           LayoutFunction{
               {0, layout, 4, 999.0F, 0},
               {20, layout, 4, 0.0F, 10},
           },
       },
       LayoutFunction{
           {0, layout, 2, 0.0F, 10},
           {5, layout, 1, 50.0F, 0},
           {10, layout, 3, 0.0F, 10},
           {15, layout, 1, 50.0F, 0},
           {20, layout, 4, 0.0F, 10},
           {25, layout, 1, 50.0F, 0},
       }},
  };

  for (const auto& test_case : kTestCases) {
    const LayoutFunction choice_result = factory_.Choice(test_case.choices);
    ExpectLayoutFunctionsEqual(choice_result, test_case.expected,
                               test_case.line_no);
  }
}

TEST_F(LayoutFunctionFactoryTest, Wrap) {
  using LT = LayoutTree;
  using LI = LayoutItem;

  {
    const auto lf = factory_.Wrap({});
    const auto expected_lf = LayoutFunction{};
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_lf = factory_.Line(uwlines_[kShortLineId]);
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_vh =
        LT(LI(LayoutType::kStack, 0, true),             //
           LT(LI(LayoutType::kJuxtaposition, 0, true),  //
              LI(uwlines_[k10ColumnsLineId]),           //
              LI(uwlines_[kShortLineId])),              //
           LI(uwlines_[kShortLineId]));
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, true),  //
           LI(uwlines_[k10ColumnsLineId]),           //
           LI(uwlines_[kShortLineId]),               //
           LI(uwlines_[kShortLineId]));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, true),  //
                                      LI(uwlines_[k10ColumnsLineId]),   //
                                      LI(uwlines_[kShortLineId]),       //
                                      LI(uwlines_[kShortLineId]));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_vh, 19, 2.0F, 0},
        {11, expected_layout_vh, 19, 2.0F, 100},
        {12, expected_layout_v, 19, 4.0F, 0},
        {21, expected_layout_v, 19, 4.0F, 200},
        {30, expected_layout_v, 19, 1804.0F, 300},
        {40, expected_layout_h, 48, 4800.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_hv =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LT(LI(LayoutType::kStack, 0, false),       //
              LI(uwlines_[kShortLineId]),             //
              LI(uwlines_[k10ColumnsLineId])),        //
           LI(uwlines_[kShortLineId]));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, false),  //
                                      LI(uwlines_[kShortLineId]),        //
                                      LI(uwlines_[k10ColumnsLineId]),    //
                                      LI(uwlines_[kShortLineId]));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_hv, 29, 2.0F, 0},
        {11, expected_layout_hv, 29, 2.0F, 100},
        {12, expected_layout_v, 19, 4.0F, 0},
        {21, expected_layout_v, 19, 4.0F, 200},
        {30, expected_layout_v, 19, 1804.0F, 300},
        {40, expected_layout_hv, 29, 4802.0F, 200},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kOneUnder40LimitLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, true),  //
           LI(uwlines_[kOneUnder40LimitLineId]),     //
           LI(uwlines_[kShortLineId]));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, true),       //
                                      LI(uwlines_[kOneUnder40LimitLineId]),  //
                                      LI(uwlines_[kShortLineId]));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_v, 19, 2.0F, 0},
        {1, expected_layout_v, 19, 2.0F, 100},
        {21, expected_layout_v, 19, 2002.0F, 200},
        {40, expected_layout_h, 58, 5800.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kExactlyAt40LimitLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, true),  //
           LI(uwlines_[kExactlyAt40LimitLineId]),    //
           LI(uwlines_[kShortLineId]));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, true),        //
                                      LI(uwlines_[kExactlyAt40LimitLineId]),  //
                                      LI(uwlines_[kShortLineId]));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_v, 19, 2.0F, 100},
        {21, expected_layout_v, 19, 2102.0F, 200},
        {40, expected_layout_h, 59, 5900.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kOneOver40LimitLineId]),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, true),  //
           LI(uwlines_[kOneOver40LimitLineId]),      //
           LI(uwlines_[kShortLineId]));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, true),      //
                                      LI(uwlines_[kOneOver40LimitLineId]),  //
                                      LI(uwlines_[kShortLineId]));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_v, 19, 102.0F, 100},
        {21, expected_layout_v, 19, 2202.0F, 200},
        {40, expected_layout_h, 60, 6000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
}

TEST_F(LayoutFunctionFactoryTest, Indent) {
  using LT = LayoutTree;
  using LI = LayoutItem;

  {
    const auto lf =
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 29);
    const auto expected_layout = LT(LI(uwlines_[k10ColumnsLineId], 29));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 39, 0.0F, 0},
        {1, expected_layout, 39, 0.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf =
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 30);
    const auto expected_layout = LT(LI(uwlines_[k10ColumnsLineId], 30));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 40, 0.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf =
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 31);
    const auto expected_layout = LT(LI(uwlines_[k10ColumnsLineId], 31));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 41, 100.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Indent(factory_.Line(uwlines_[kLongLineId]), 5);
    const auto expected_layout = LT(LI(uwlines_[kLongLineId], 5));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 55, 1500.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
}

TEST_F(LayoutFunctionFactoryTest, IndentWithOtherCombinators) {
  using LT = LayoutTree;
  using LI = LayoutItem;

  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 9),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kJuxtaposition, 0, true),  //
                                    LI(uwlines_[k10ColumnsLineId], 0),        //
                                    LI(uwlines_[k10ColumnsLineId], 9),        //
                                    LI(uwlines_[k10ColumnsLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 39, 0.0F, 0},
        {1, expected_layout, 39, 0.0F, 100},
        {11, expected_layout, 39, 1000.0F, 100},
        {30, expected_layout, 39, 2900.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 10),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kJuxtaposition, 0, true),  //
                                    LI(uwlines_[k10ColumnsLineId], 0),        //
                                    LI(uwlines_[k10ColumnsLineId], 10),       //
                                    LI(uwlines_[k10ColumnsLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 40, 0.0F, 100},
        {10, expected_layout, 40, 1000.0F, 100},
        {30, expected_layout, 40, 3000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Juxtaposition({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 11),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kJuxtaposition, 0, true),  //
                                    LI(uwlines_[k10ColumnsLineId], 0),        //
                                    LI(uwlines_[k10ColumnsLineId], 11),       //
                                    LI(uwlines_[k10ColumnsLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 41, 100.0F, 100},
        {9, expected_layout, 41, 1000.0F, 100},
        {30, expected_layout, 41, 3100.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }

  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 29),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, true),     //
                                    LI(uwlines_[k10ColumnsLineId], 0),   //
                                    LI(uwlines_[k10ColumnsLineId], 29),  //
                                    LI(uwlines_[k10ColumnsLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 10, 4.0F, 0},
        {1, expected_layout, 10, 4.0F, 100},
        {30, expected_layout, 10, 2904.0F, 300},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 30),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, true),     //
                                    LI(uwlines_[k10ColumnsLineId], 0),   //
                                    LI(uwlines_[k10ColumnsLineId], 30),  //
                                    LI(uwlines_[k10ColumnsLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 10, 4.0F, 100},
        {30, expected_layout, 10, 3004.0F, 300},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Stack({
        factory_.Line(uwlines_[k10ColumnsLineId]),
        factory_.Indent(factory_.Line(uwlines_[k10ColumnsLineId]), 31),
        factory_.Line(uwlines_[k10ColumnsLineId]),
    });
    const auto expected_layout = LT(LI(LayoutType::kStack, 0, true),     //
                                    LI(uwlines_[k10ColumnsLineId], 0),   //
                                    LI(uwlines_[k10ColumnsLineId], 31),  //
                                    LI(uwlines_[k10ColumnsLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout, 10, 104.0F, 100},
        {30, expected_layout, 10, 3104.0F, 300},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }

  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Indent(factory_.Line(uwlines_[kShortLineId]), 1),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LI(uwlines_[kShortLineId], 0),             //
           LI(uwlines_[kShortLineId], 1));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, false),  //
                                      LI(uwlines_[kShortLineId], 0),     //
                                      LI(uwlines_[kShortLineId], 1));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_h, 39, 0.0F, 0},
        {1, expected_layout_h, 39, 0.0F, 100},
        {2, expected_layout_v, 20, 2.0F, 0},
        {20, expected_layout_v, 20, 2.0F, 100},
        {21, expected_layout_v, 20, 102.0F, 200},
        {40, expected_layout_h, 39, 3900.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Indent(factory_.Line(uwlines_[kShortLineId]), 2),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LI(uwlines_[kShortLineId], 0),             //
           LI(uwlines_[kShortLineId], 2));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, false),  //
                                      LI(uwlines_[kShortLineId], 0),     //
                                      LI(uwlines_[kShortLineId], 2));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_h, 40, 0.0F, 100},
        {1, expected_layout_v, 21, 2.0F, 0},
        {19, expected_layout_v, 21, 2.0F, 100},
        {21, expected_layout_v, 21, 202.0F, 200},
        {40, expected_layout_h, 40, 4000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Line(uwlines_[kShortLineId]),
        factory_.Indent(factory_.Line(uwlines_[kShortLineId]), 3),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LI(uwlines_[kShortLineId], 0),             //
           LI(uwlines_[kShortLineId], 3));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, false),  //
                                      LI(uwlines_[kShortLineId], 0),     //
                                      LI(uwlines_[kShortLineId], 3));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_v, 22, 2.0F, 0},
        {18, expected_layout_v, 22, 2.0F, 100},
        {21, expected_layout_v, 22, 302.0F, 200},
        {40, expected_layout_h, 41, 4100.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }

  {
    const auto lf = factory_.Wrap({
        factory_.Indent(factory_.Line(uwlines_[kShortLineId]), 1),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LI(uwlines_[kShortLineId], 1),             //
           LI(uwlines_[kShortLineId], 0));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, false),  //
                                      LI(uwlines_[kShortLineId], 1),     //
                                      LI(uwlines_[kShortLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_h, 39, 0.0F, 0},
        {1, expected_layout_h, 39, 0.0F, 100},
        {2, expected_layout_v, 19, 2.0F, 0},
        {20, expected_layout_v, 19, 2.0F, 100},
        {21, expected_layout_v, 19, 102.0F, 200},
        {40, expected_layout_h, 39, 3900.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Indent(factory_.Line(uwlines_[kShortLineId]), 2),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LI(uwlines_[kShortLineId], 2),             //
           LI(uwlines_[kShortLineId], 0));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, false),  //
                                      LI(uwlines_[kShortLineId], 2),     //
                                      LI(uwlines_[kShortLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_h, 40, 0.0F, 100},
        {1, expected_layout_v, 19, 2.0F, 0},
        {19, expected_layout_v, 19, 2.0F, 100},
        {21, expected_layout_v, 19, 202.0F, 200},
        {40, expected_layout_h, 40, 4000.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
  {
    const auto lf = factory_.Wrap({
        factory_.Indent(factory_.Line(uwlines_[kShortLineId]), 3),
        factory_.Line(uwlines_[kShortLineId]),
    });
    const auto expected_layout_h =
        LT(LI(LayoutType::kJuxtaposition, 0, false),  //
           LI(uwlines_[kShortLineId], 3),             //
           LI(uwlines_[kShortLineId], 0));
    const auto expected_layout_v = LT(LI(LayoutType::kStack, 0, false),  //
                                      LI(uwlines_[kShortLineId], 3),     //
                                      LI(uwlines_[kShortLineId], 0));
    const auto expected_lf = LayoutFunction{
        {0, expected_layout_v, 19, 2.0F, 0},
        {18, expected_layout_v, 19, 2.0F, 100},
        {21, expected_layout_v, 19, 302.0F, 200},
        {40, expected_layout_h, 41, 4100.0F, 100},
    };
    ExpectLayoutFunctionsEqual(lf, expected_lf, __LINE__);
  }
}

class TreeReconstructorTest : public ::testing::Test,
                              public UnwrappedLineMemoryHandler {
 public:
  TreeReconstructorTest()
      : sample_("first_line second_line third_line fourth_line"),
        tokens_(absl::StrSplit(sample_, ' ')) {
    for (const auto token : tokens_) {
      ftokens_.emplace_back(TokenInfo{1, token});
    }
    CreateTokenInfos(ftokens_);
  }

 protected:
  const std::string sample_;
  const std::vector<absl::string_view> tokens_;
  std::vector<TokenInfo> ftokens_;
};

TEST_F(TreeReconstructorTest, SingleLine) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine single_line(0, begin);
  single_line.SpanUpToToken(begin + 1);

  const auto layout_tree = LayoutTree(LayoutItem(single_line));
  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree(UnwrappedLine(0, begin));
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{single_line,  //
                           Tree{single_line}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, HorizontalLayoutWithOneLine) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine uwline(0, begin);
  uwline.SpanUpToToken(begin + 1);

  const auto layout_tree =
      LayoutTree(LayoutItem(LayoutType::kJuxtaposition, 0, false),
                 LayoutTree(LayoutItem(uwline)));

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{uwline,  //
                           Tree{uwline}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, HorizontalLayoutSingleLines) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine left_line(0, begin);
  left_line.SpanUpToToken(begin + 1);
  UnwrappedLine right_line(0, begin + 1);
  right_line.SpanUpToToken(begin + 2);
  UnwrappedLine all(0, left_line.TokensRange().begin());
  all.SpanUpToToken(right_line.TokensRange().end());

  const auto layout_tree = LayoutTree(
      LayoutItem(LayoutType::kJuxtaposition, 0,
                 left_line.TokensRange().front().before.break_decision ==
                     SpacingOptions::MustWrap),
      LayoutTree(LayoutItem(left_line)), LayoutTree(LayoutItem(right_line)));

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree(UnwrappedLine(0, begin));
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected(all,  //
                           Tree(all));
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, EmptyHorizontalLayout) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine upper_line(0, begin);
  upper_line.SpanUpToToken(begin + 1);
  UnwrappedLine lower_line(0, begin + 1);
  lower_line.SpanUpToToken(begin + 2);
  UnwrappedLine all(0, upper_line.TokensRange().begin());
  all.SpanUpToToken(lower_line.TokensRange().end());

  const auto layout_tree =
      LayoutTree(LayoutItem(LayoutType::kJuxtaposition, 0, false),
                 LayoutTree(LayoutItem(upper_line)),
                 LayoutTree(LayoutItem(LayoutType::kJuxtaposition, 0, false)),
                 LayoutTree(LayoutItem(lower_line)));

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{all,  //
                           Tree{all}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, VerticalLayoutWithOneLine) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine uwline(0, begin);
  uwline.SpanUpToToken(begin + 1);

  const auto layout_tree = LayoutTree(LayoutItem(LayoutType::kStack, 0, false),
                                      LayoutTree(LayoutItem(uwline)));

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{uwline,  //
                           Tree{uwline}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, VerticalLayoutSingleLines) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine upper_line(0, begin);
  upper_line.SpanUpToToken(begin + 1);
  UnwrappedLine lower_line(0, begin + 1);
  lower_line.SpanUpToToken(begin + 2);
  UnwrappedLine all(0, upper_line.TokensRange().begin());
  all.SpanUpToToken(lower_line.TokensRange().end());

  const auto layout_tree = LayoutTree(
      LayoutItem(LayoutType::kStack, 0,
                 upper_line.TokensRange().front().before.break_decision ==
                     SpacingOptions::MustWrap),
      LayoutTree(LayoutItem(upper_line)), LayoutTree(LayoutItem(lower_line)));

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{all,               //
                           Tree{upper_line},  //
                           Tree{lower_line}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, EmptyVerticalLayout) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine upper_line(0, begin);
  upper_line.SpanUpToToken(begin + 1);
  UnwrappedLine lower_line(0, begin + 1);
  lower_line.SpanUpToToken(begin + 2);
  UnwrappedLine all(0, upper_line.TokensRange().begin());
  all.SpanUpToToken(lower_line.TokensRange().end());

  const auto layout_tree =
      LayoutTree(LayoutItem(LayoutType::kStack, 0, false),
                 LayoutTree(LayoutItem(upper_line)),
                 LayoutTree(LayoutItem(LayoutType::kStack, 0, false)),
                 LayoutTree(LayoutItem(lower_line)));

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{all,               //
                           Tree{upper_line},  //
                           Tree{lower_line}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, VerticallyJoinHorizontalLayouts) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine first_line(0, begin);
  first_line.SpanUpToToken(begin + 1);
  UnwrappedLine second_line(0, begin + 1);
  second_line.SpanUpToToken(begin + 2);
  UnwrappedLine third_line(0, begin + 2);
  third_line.SpanUpToToken(begin + 3);
  UnwrappedLine fourth_line(0, begin + 3);
  fourth_line.SpanUpToToken(begin + 4);

  UnwrappedLine upper_line(0, first_line.TokensRange().begin());
  upper_line.SpanUpToToken(second_line.TokensRange().end());
  UnwrappedLine lower_line(0, third_line.TokensRange().begin());
  lower_line.SpanUpToToken(fourth_line.TokensRange().end());

  UnwrappedLine all(0, upper_line.TokensRange().begin());
  all.SpanUpToToken(lower_line.TokensRange().end());
  const LayoutTree layout_tree{
      LayoutItem(LayoutType::kStack, 0, false),
      LayoutTree(LayoutItem(LayoutType::kJuxtaposition, 0, false),
                 LayoutTree(LayoutItem(first_line)),
                 LayoutTree(LayoutItem(second_line))),
      LayoutTree(LayoutItem(LayoutType::kJuxtaposition, 0, false),
                 LayoutTree(LayoutItem(third_line)),
                 LayoutTree(LayoutItem(fourth_line)))};

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{all,               //
                           Tree{upper_line},  //
                           Tree{lower_line}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, HorizontallyJoinVerticalLayouts) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine first_line(0, begin);
  first_line.SpanUpToToken(begin + 1);
  UnwrappedLine second_line(0, begin + 1);
  second_line.SpanUpToToken(begin + 2);
  UnwrappedLine third_line(0, begin + 2);
  third_line.SpanUpToToken(begin + 3);
  UnwrappedLine fourth_line(0, begin + 3);
  fourth_line.SpanUpToToken(begin + 4);

  UnwrappedLine upper_line(0, first_line.TokensRange().begin());
  upper_line.SpanUpToToken(first_line.TokensRange().end());
  UnwrappedLine middle_line(0, second_line.TokensRange().begin());
  middle_line.SpanUpToToken(third_line.TokensRange().end());
  UnwrappedLine bottom_line(0, fourth_line.TokensRange().begin());
  bottom_line.SpanUpToToken(fourth_line.TokensRange().end());

  UnwrappedLine all(0, upper_line.TokensRange().begin());
  all.SpanUpToToken(bottom_line.TokensRange().end());

  const LayoutTree layout_tree{
      LayoutItem(LayoutType::kJuxtaposition, 0, false),
      LayoutTree(LayoutItem(LayoutType::kStack, 0, false),
                 LayoutTree(LayoutItem(first_line)),
                 LayoutTree(LayoutItem(second_line))),
      LayoutTree(LayoutItem(LayoutType::kStack, 0, false),
                 LayoutTree(LayoutItem(third_line)),
                 LayoutTree(LayoutItem(fourth_line)))};

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{all,                //
                           Tree{upper_line},   //
                           Tree{middle_line},  //
                           Tree{bottom_line}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";
}

TEST_F(TreeReconstructorTest, IndentSingleLine) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();
  BasicFormatStyle style;

  UnwrappedLine single_line(0, begin);
  single_line.SpanUpToToken(begin + 1);

  const auto indent = 7;
  LayoutTree layout_tree{LayoutItem(single_line)};
  layout_tree.Value().SetIndentationSpaces(indent);

  TreeReconstructor tree_reconstructor(0, style);
  tree_reconstructor.TraverseTree(layout_tree);

  auto optimized_tree = TokenPartitionTree{UnwrappedLine(0, begin)};
  tree_reconstructor.ReplaceTokenPartitionTreeNode(&optimized_tree,
                                                   &pre_format_tokens_);

  using Tree = TokenPartitionTree;
  const Tree tree_expected{single_line,  //
                           Tree{single_line}};
  const auto diff = DeepEqual(optimized_tree, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << optimized_tree << "\n";

  EXPECT_EQ(optimized_tree.Children()[0].Value().IndentationSpaces(), indent);
}

class OptimizeTokenPartitionTreeTest : public ::testing::Test,
                                       public UnwrappedLineMemoryHandler {
 public:
  OptimizeTokenPartitionTreeTest()
      : sample_(
            "function_fffffffffff( type_a_aaaa, "
            "type_b_bbbbb, type_c_cccccc, "
            "type_d_dddddddd, type_e_eeeeeeee, type_f_ffff);"),
        tokens_(absl::StrSplit(sample_, ' ')) {
    for (const auto token : tokens_) {
      ftokens_.emplace_back(TokenInfo{1, token});
    }
    CreateTokenInfos(ftokens_);
  }

 protected:
  const std::string sample_;
  const std::vector<absl::string_view> tokens_;
  std::vector<TokenInfo> ftokens_;
};

TEST_F(OptimizeTokenPartitionTreeTest, OneLevelFunctionCall) {
  const auto& preformat_tokens = pre_format_tokens_;
  const auto begin = preformat_tokens.begin();

  UnwrappedLine function_name(0, begin);
  function_name.SpanUpToToken(begin + 1);
  UnwrappedLine arg_a(0, begin + 1);
  arg_a.SpanUpToToken(begin + 2);
  UnwrappedLine arg_b(0, begin + 2);
  arg_b.SpanUpToToken(begin + 3);
  UnwrappedLine arg_c(0, begin + 3);
  arg_c.SpanUpToToken(begin + 4);
  UnwrappedLine arg_d(0, begin + 4);
  arg_d.SpanUpToToken(begin + 5);
  UnwrappedLine arg_e(0, begin + 5);
  arg_e.SpanUpToToken(begin + 6);
  UnwrappedLine arg_f(0, begin + 6);
  arg_f.SpanUpToToken(begin + 7);

  function_name.SetPartitionPolicy(PartitionPolicyEnum::kAlwaysExpand);
  arg_a.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);
  arg_b.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);
  arg_c.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);
  arg_d.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);
  arg_e.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);
  arg_f.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);

  UnwrappedLine header(0, function_name.TokensRange().begin());
  header.SpanUpToToken(function_name.TokensRange().end());
  UnwrappedLine args(0, arg_a.TokensRange().begin());
  args.SpanUpToToken(arg_f.TokensRange().end());

  header.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);
  args.SetPartitionPolicy(PartitionPolicyEnum::kFitOnLineElseExpand);

  UnwrappedLine all(0, header.TokensRange().begin());
  all.SpanUpToToken(args.TokensRange().end());
  all.SetPartitionPolicy(PartitionPolicyEnum::kOptimalFunctionCallLayout);

  using Tree = TokenPartitionTree;
  Tree tree_under_test{all,               //
                       Tree{header},      //
                       Tree{args,         //
                            Tree{arg_a},  //
                            Tree{arg_b},  //
                            Tree{arg_c},  //
                            Tree{arg_d},  //
                            Tree{arg_e},  //
                            Tree{arg_f}}};

  BasicFormatStyle style;
  style.column_limit = 40;
  OptimizeTokenPartitionTree(style, &tree_under_test, &pre_format_tokens_);

  UnwrappedLine args_top_line(0, arg_a.TokensRange().begin());
  args_top_line.SpanUpToToken(arg_b.TokensRange().end());
  UnwrappedLine args_middle_line(0, arg_c.TokensRange().begin());
  args_middle_line.SpanUpToToken(arg_d.TokensRange().end());
  UnwrappedLine args_bottom_line(0, arg_e.TokensRange().begin());
  args_bottom_line.SpanUpToToken(arg_f.TokensRange().end());

  const Tree tree_expected{all,                     //
                           Tree{header},            //
                           Tree{args_top_line},     //
                           Tree{args_middle_line},  //
                           Tree{args_bottom_line}};

  const auto diff = DeepEqual(tree_under_test, tree_expected, TokenRangeEqual);
  EXPECT_EQ(diff.left, nullptr) << "Expected:\n"
                                << tree_expected << "\nGot:\n"
                                << tree_under_test << "\n";

  // header
  EXPECT_EQ(tree_under_test.Children()[0].Value().IndentationSpaces(), 0);
  // args_top_line (wrapped)
  EXPECT_EQ(tree_under_test.Children()[1].Value().IndentationSpaces(), 4);
  // args_middle_line (wrapped)
  EXPECT_EQ(tree_under_test.Children()[2].Value().IndentationSpaces(), 4);
  // args_bottom_line (wrapped)
  EXPECT_EQ(tree_under_test.Children()[3].Value().IndentationSpaces(), 4);
}

}  // namespace
}  // namespace verible
