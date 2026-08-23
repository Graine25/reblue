/**
 * @file    core/text_wrap.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/text_wrap.h"

namespace bd {

std::string WordWrap(const std::string &text, int maxChars) {
  std::string result;
  int lineLen = 0;
  size_t i = 0;
  while (i < text.size()) {
    size_t spacePos = text.find(' ', i);
    size_t nlPos = text.find('\n', i);
    if (spacePos == std::string::npos)
      spacePos = text.size();
    if (nlPos == std::string::npos)
      nlPos = text.size();

    // A real newline terminates the word only when it is strictly before the
    // next space. An end-of-string tie (both clamped to size()) is neither, and
    // must not suppress the space that separates the final word.
    bool hitNewline = nlPos < spacePos;
    size_t wordEnd = hitNewline ? nlPos : spacePos;
    int wordLen = static_cast<int>(wordEnd - i);

    if (lineLen > 0 && lineLen + 1 + wordLen > maxChars) {
      result += '\n';
      lineLen = 0;
    } else if (lineLen > 0 && !hitNewline) {
      result += ' ';
      lineLen++;
    }

    result.append(text, i, wordLen);
    if (hitNewline) {
      result += '\n';
      lineLen = 0;
    } else {
      lineLen += wordLen;
    }
    i = wordEnd + 1;
  }
  return result;
}

std::array<std::string, 2> WrapTwoLines(const std::string &text, int maxChars) {
  const std::string wrapped = WordWrap(text, maxChars);
  const size_t nl = wrapped.find('\n');
  if (nl == std::string::npos)
    return {wrapped, std::string()};

  const size_t nl2 = wrapped.find('\n', nl + 1);
  return {wrapped.substr(0, nl),
          wrapped.substr(nl + 1, nl2 == std::string::npos ? std::string::npos
                                                          : nl2 - nl - 1)};
}

} // namespace bd
