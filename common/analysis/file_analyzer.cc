// Copyright 2017-2020 The Verible Authors.
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

// Implementation of FileAnalyzer methods.

#include "common/analysis/file_analyzer.h"

#include <sstream>  // IWYU pragma: keep  // for ostringstream
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "common/lexer/lexer.h"
#include "common/lexer/token_stream_adapter.h"
#include "common/parser/parse.h"
#include "common/strings/line_column_map.h"
#include "common/text/concrete_syntax_tree.h"
#include "common/text/text_structure.h"
#include "common/text/token_info.h"
#include "common/text/token_stream_view.h"
#include "common/util/spacer.h"

namespace verible {

// Translates phase enum into string for diagnostic messages.
static const char* AnalysisPhaseName(const AnalysisPhase& phase) {
  switch (phase) {
    case AnalysisPhase::kLexPhase:
      return "lexical";
    case AnalysisPhase::kPreprocessPhase:
      return "preprocessing";
    case AnalysisPhase::kParsePhase:
      return "syntax";
    default:
      return "UNKNOWN";
  }
}

std::ostream& operator<<(std::ostream& stream, const AnalysisPhase& phase) {
  return stream << AnalysisPhaseName(phase);
}

std::ostream& operator<<(std::ostream& stream, const RejectedToken& r) {
  return stream << r.token_info << " (" << r.phase << "): " << r.explanation;
}

// Grab tokens until EOF, and initialize a stream view with all tokens.
absl::Status FileAnalyzer::Tokenize(Lexer* lexer) {
  const auto buffer = Data().Contents();
  TokenSequence& tokens = MutableData().MutableTokenStream();

  if (auto lex_status = MakeTokenSequence(
          lexer, buffer, &tokens,
          [&](const TokenInfo& error_token) {
            VLOG(1) << "Lexical error with token: " << error_token;
            // Save error details in rejected_tokens_.
            rejected_tokens_.push_back(
                RejectedToken{error_token, AnalysisPhase::kLexPhase,
                              "" /* no detailed explanation */});
          });
      !lex_status.ok()) {
    return lex_status;
  }

  // Partition token stream into line-by-line slices.
  MutableData().CalculateFirstTokensPerLine();

  // Initialize filtered view of token stream.
  InitTokenStreamView(tokens, &MutableData().MutableTokenStreamView());
  return absl::OkStatus();
}

// Runs the parser on the current TokenStreamView.
absl::Status FileAnalyzer::Parse(Parser* parser) {
  absl::Status status = parser->Parse();
  // Transfer syntax tree root, even if there were (recovered) syntax errors,
  // because the partial tree can still be useful to analyze.
  MutableData().MutableSyntaxTree() = parser->TakeRoot();
  if (status.ok()) {
    CHECK(SyntaxTree().get()) << "Expected syntax tree from parsing \""
                              << filename_ << "\", but got none.";
  } else {
    for (const auto& token : parser->RejectedTokens()) {
      rejected_tokens_.push_back(RejectedToken{
          token, AnalysisPhase::kParsePhase, "" /* no detailed explanation */});
    }
  }
  return status;
}

// Reports human-readable token error.
std::string FileAnalyzer::TokenErrorMessage(
    const TokenInfo& error_token) const {
  // TODO(fangism): accept a RejectedToken to get an explanation message.
  std::ostringstream output_stream;
  if (!error_token.isEOF()) {
    // TODO(hzeller): simply print LineColumnRange ?
    LineColumnRange range = Data().GetRangeForToken(error_token);
    --range.end.column;  // Point to last character, not one-past-the-end.
    output_stream << "token: \"" << error_token.text() << "\" at "
                  << range.start;
    if (range.start.line == range.end.line) {
      // Only print upper bound if it differs by > 1 character.
      if (range.start.column + 1 < range.end.column) {
        // .column is 0-based index, so +1 to get 1-based index.
        output_stream << '-' << range.end.column + 1;
      }
    } else {
      // Already prints 1-based index.
      output_stream << '-' << range.end;
    }
  } else {
    const auto end = Data().GetLineColAtOffset(Data().Contents().length());
    output_stream << "token: <<EOF>> at " << end;
  }
  return output_stream.str();
}

std::vector<std::string> FileAnalyzer::TokenErrorMessages() const {
  std::vector<std::string> messages;
  messages.reserve(rejected_tokens_.size());
  for (const auto& rejected_token : rejected_tokens_) {
    messages.push_back(TokenErrorMessage(rejected_token.token_info));
  }
  return messages;
}

void FileAnalyzer::ExtractLinterTokenErrorDetail(
    const RejectedToken& error_token,
    const ReportLinterErrorFunction& error_report) const {
  const LineColumnRange range = Data().GetRangeForToken(error_token.token_info);
  absl::string_view context_line = "";
  const auto& lines = Data().Lines();
  if (range.start.line < static_cast<int>(lines.size())) {
    context_line = lines[range.start.line];
  }
  // TODO(b/63893567): Explain syntax errors by inspecting state stack.
  error_report(
      filename_, range, error_token.phase,
      error_token.token_info.isEOF() ? "<EOF>" : error_token.token_info.text(),
      context_line, error_token.explanation);
}

std::string FileAnalyzer::LinterTokenErrorMessage(
    const RejectedToken& error_token, bool diagnostic_context) const {
  std::ostringstream out;
  ExtractLinterTokenErrorDetail(
      error_token,
      [&](const std::string& filename, LineColumnRange range,
          AnalysisPhase phase, absl::string_view token_text,
          absl::string_view context_line, const std::string& message) {
        // TODO(hzeller): switch to printing range, but make sure that
        // potential users are not running into trouble.
        out << filename_ << ':' << range.start << ": " << phase;
        if (error_token.token_info.isEOF()) {
          out << " error (unexpected EOF) (syntax-error).";
        } else {
          out << " error, rejected \"" << token_text << "\" (syntax-error).";
        }
        if (diagnostic_context && !context_line.empty()) {
          out << "\n" << context_line << std::endl;
          out << verible::Spacer(range.start.column) << "^";
        }
      });
  return out.str();
}

std::vector<std::string> FileAnalyzer::LinterTokenErrorMessages(
    bool diagnostic_context) const {
  std::vector<std::string> messages;
  messages.reserve(rejected_tokens_.size());
  for (const auto& rejected_token : rejected_tokens_) {
    messages.push_back(
        LinterTokenErrorMessage(rejected_token, diagnostic_context));
  }
  return messages;
}

}  // namespace verible
