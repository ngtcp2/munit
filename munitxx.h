/* µnit Testing Framework
 * Copyright (c) 2013-2017 Evan Nemerson <evan@nemerson.com>
 * Copyright (c) 2023 munit contributors
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MUNITXX_H
#define MUNITXX_H

#include <munit.h>

#include <type_traits>
#include <string>
#include <string_view>
#include <source_location>
#include <format>
#include <expected>
#include <span>

template <>
struct std::formatter<std::ranges::equal_to>
  : public std::formatter<std::string_view> {
  auto format(std::ranges::equal_to, auto &ctx) const {
    return std::formatter<std::string_view>::format("==", ctx);
  }
};

template <>
struct std::formatter<std::ranges::not_equal_to>
  : public std::formatter<std::string_view> {
  auto format(std::ranges::not_equal_to, auto &ctx) const {
    return std::formatter<std::string_view>::format("!=", ctx);
  }
};

template <>
struct std::formatter<std::ranges::greater>
  : public std::formatter<std::string_view> {
  auto format(std::ranges::greater, auto &ctx) const {
    return std::formatter<std::string_view>::format(">", ctx);
  }
};

template <>
struct std::formatter<std::ranges::less>
  : public std::formatter<std::string_view> {
  auto format(std::ranges::less, auto &ctx) const {
    return std::formatter<std::string_view>::format("<", ctx);
  }
};

template <>
struct std::formatter<std::ranges::greater_equal>
  : public std::formatter<std::string_view> {
  auto format(std::ranges::greater_equal, auto &ctx) const {
    return std::formatter<std::string_view>::format(">=", ctx);
  }
};

template <>
struct std::formatter<std::ranges::less_equal>
  : public std::formatter<std::string_view> {
  auto format(std::ranges::less_equal, auto &ctx) const {
    return std::formatter<std::string_view>::format("<=", ctx);
  }
};

template <typename T>
requires(!std::is_void_v<T> && !std::same_as<std::remove_cv_t<T>, char>)
struct std::formatter<T *> : public std::formatter<const void *> {
  auto format(T *ptr, auto &ctx) const {
    return std::formatter<const void *>::format(static_cast<const void *>(ptr),
                                                ctx);
  }
};

namespace munit {

template <typename T>
concept byte_range =
  std::ranges::contiguous_range<T> &&
  (std::same_as<std::ranges::range_value_t<T>, char> ||
   std::same_as<std::ranges::range_value_t<T>, unsigned char> ||
   std::same_as<std::ranges::range_value_t<T>, std::byte>);

template <typename T>
requires(byte_range<T>)
std::string escape_non_printable(T &&s) {
  std::string res;

  auto v = []<typename U>(U &&range) {
    if constexpr (std::is_pointer_v<std::decay_t<U>>) {
      return std::string_view{reinterpret_cast<const char *>(range)};
    } else {
      return std::forward<U>(range);
    }
  }(std::forward<T>(s));

  using namespace std::string_view_literals;

  constexpr auto UPPER_XDIGITS = "0123456789ABCDEF"sv;

  for (auto c : v) {
    auto x = static_cast<uint8_t>(c);

    switch (x) {
    case '\t':
      res += "\\t";
      break;
    case '\n':
      res += "\\n";
      break;
    case '\r':
      res += "\\r";
      break;
    case '"':
      res += "\\\"";
      break;
    case '\\':
      res += "\\\\";
      break;
    default:
      if (x >= 0x20 && x <= 0x7E) {
        res += static_cast<char>(x);
      } else {
        res += "\\x{";
        res += UPPER_XDIGITS[static_cast<size_t>(x >> 4)];
        res += UPPER_XDIGITS[static_cast<size_t>(x & 0xF)];
        res += "}";
      }
    }
  }

  return res;
}

template <typename T1, typename Op, typename T2>
void assert_op(
  std::string_view a_arg, std::string_view b_arg, T1 &&a, Op &&op, T2 &&b,
  const std::source_location loc = std::source_location::current()) {
  if (!op(a, b)) {
    // For sequences of char or unsigned char, hexdump them for better
    // inspection.
    if constexpr (byte_range<T1> && byte_range<T2>) {
      munit_hexdump_diff(stderr, std::ranges::data(a),
                         std::span{a}.size_bytes(), std::ranges::data(b),
                         std::span{b}.size_bytes());
    }

    auto msg = std::format("{} {} {} ", a_arg, op, b_arg);

    if constexpr (byte_range<T1> && byte_range<T2>) {
      // We would like to use {:?} here, but some older libstdc++
      // libraries do not support it.
      msg += std::format(R"(("{}" {} "{}"))", escape_non_printable(a), op,
                         escape_non_printable(b));
    } else {
      msg += std::format(R"(({} {} {}))", a, op, b);
    }

    munit_errorf_ex(loc.file_name(), static_cast<int>(loc.line()),
                    "assertion failed: %s", msg.c_str());
  }
}

template <typename T1, typename Op, typename T2, typename E>
void assert_ok_op(
  std::string_view a_arg, std::string_view b_arg, T1 &&a, Op &&op,
  const std::expected<T2, E> &exp,
  const std::source_location loc = std::source_location::current()) {
  if (!exp.has_value()) {
    auto msg =
      std::format("{} {} {}: 2nd argument has no value", a_arg, op, b_arg);

    munit_errorf_ex(loc.file_name(), static_cast<int>(loc.line()),
                    "assertion failed: %s", msg.c_str());
  }

  assert_op(a_arg, b_arg, std::forward<T1>(a), std::forward<Op>(op),
            exp.value(), loc);
}

template <typename T1, typename E>
void assert_ok(
  std::string_view arg, const std::expected<T1, E> &exp,
  const std::source_location loc = std::source_location::current()) {
  if (!exp.has_value()) {
    auto msg = std::format("{}: the 2nd argument has no value", arg);

    munit_errorf_ex(loc.file_name(), static_cast<int>(loc.line()),
                    "assertion failed: %s", msg.c_str());
  }
}

template <typename T, typename E>
void assert_err(
  std::string_view e_arg, std::string_view arg, E &&e,
  const std::expected<T, E> &exp,
  const std::source_location loc = std::source_location::current()) {
  if (exp.has_value()) {
    auto msg = std::format("{}: the argument has value", arg);

    munit_errorf_ex(loc.file_name(), static_cast<int>(loc.line()),
                    "assertion failed: %s", msg.c_str());
  }

  if (e != exp.error()) {
    auto msg =
      std::format(R"({} == {} ({} == {}))", e_arg, arg, e, exp.error());

    munit_errorf_ex(loc.file_name(), static_cast<int>(loc.line()),
                    "assertion failed: %s", msg.c_str());
  }
}

} // namespace munit

#define munit_assert_eq(a, b)                                                  \
  munit::assert_op(#a, #b, a, std::ranges::equal_to{}, b)

#define munit_assert_ne(a, b)                                                  \
  munit::assert_op(#a, #b, a, std::ranges::not_equal_to{}, b)

#define munit_assert_gt(a, b)                                                  \
  munit::assert_op(#a, #b, a, std::ranges::greater{}, b)

#define munit_assert_lt(a, b)                                                  \
  munit::assert_op(#a, #b, a, std::ranges::less{}, b)

#define munit_assert_gte(a, b)                                                 \
  munit::assert_op(#a, #b, a, std::ranges::greater_equal{}, b)

#define munit_assert_lte(a, b)                                                 \
  munit::assert_op(#a, #b, a, std::ranges::less_equal{}, b)

#define munit_assert_ok_eq(a, b)                                               \
  munit::assert_ok_op(#a, #b, a, std::ranges::equal_to{}, b)

#define munit_assert_ok(a) munit::assert_ok(#a, a)

#define munit_assert_err(e, a) munit::assert_err(#e, #a, e, a)

#define munit_assert_stdstring_equal(a, b)                                     \
  do {                                                                         \
    const std::string munit_tmp_a_ = (a);                                      \
    const std::string munit_tmp_b_ = (b);                                      \
    if (MUNIT_UNLIKELY(munit_tmp_a_ != munit_tmp_b_)) {                        \
      munit_hexdump_diff(stderr, munit_tmp_a_.c_str(), munit_tmp_a_.size(),    \
                         munit_tmp_b_.c_str(), munit_tmp_b_.size());           \
      munit_errorf("assertion failed: string %s == %s (\"%s\" == \"%s\")", #a, \
                   #b, munit_tmp_a_.c_str(), munit_tmp_b_.c_str());            \
    }                                                                          \
    MUNIT_PUSH_DISABLE_MSVC_C4127_                                             \
  } while (0) MUNIT_POP_DISABLE_MSVC_C4127_

#if __cplusplus >= 201703L
#  define munit_assert_stdsv_equal(a, b)                                       \
    do {                                                                       \
      const std::string_view munit_tmp_a_ = (a);                               \
      const std::string_view munit_tmp_b_ = (b);                               \
      if (MUNIT_UNLIKELY(munit_tmp_a_ != munit_tmp_b_)) {                      \
        munit_hexdump_diff(stderr, munit_tmp_a_.data(), munit_tmp_a_.size(),   \
                           munit_tmp_b_.data(), munit_tmp_b_.size());          \
        munit_errorf(                                                          \
          "assertion failed: string %s == %s (\"%.*s\" == \"%.*s\")", #a, #b,  \
          (int)munit_tmp_a_.size(), munit_tmp_a_.data(),                       \
          (int)munit_tmp_b_.size(), munit_tmp_b_.data());                      \
      }                                                                        \
      MUNIT_PUSH_DISABLE_MSVC_C4127_                                           \
    } while (0) MUNIT_POP_DISABLE_MSVC_C4127_
#endif // __cplusplus >= 201703L

#define munit_assert_enum_class(a, op, b)                                      \
  do {                                                                         \
    auto munit_tmp_a_ = (a);                                                   \
    auto munit_tmp_b_ = (b);                                                   \
    if (!(munit_tmp_a_ op munit_tmp_b_)) {                                     \
      auto munit_tmp_a_str_ = std::to_string(                                  \
        static_cast<std::underlying_type_t<decltype(munit_tmp_a_)>>(           \
          munit_tmp_a_));                                                      \
      auto munit_tmp_b_str_ = std::to_string(                                  \
        static_cast<std::underlying_type_t<decltype(munit_tmp_b_)>>(           \
          munit_tmp_b_));                                                      \
      munit_errorf("assertion failed: %s %s %s (%s %s %s)", #a, #op, #b,       \
                   munit_tmp_a_str_.c_str(), #op, munit_tmp_b_str_.c_str());   \
    }                                                                          \
    MUNIT_PUSH_DISABLE_MSVC_C4127_                                             \
  } while (0) MUNIT_POP_DISABLE_MSVC_C4127_

#if defined(MUNIT_ENABLE_ASSERT_ALIASES)

#  define assert_eq(a, b) munit_assert_eq(a, b)
#  define assert_ne(a, b) munit_assert_ne(a, b)
#  define assert_gt(a, b) munit_assert_gt(a, b)
#  define assert_lt(a, b) munit_assert_lt(a, b)
#  define assert_gte(a, b) munit_assert_gte(a, b)
#  define assert_lte(a, b) munit_assert_lte(a, b)
#  define assert_ok_eq(a, b) munit_assert_ok_eq(a, b)
#  define assert_ok(a) munit_assert_ok(a)
#  define assert_err(e, a) munit_assert_err(e, a)
#  define assert_stdstring_equal(a, b) munit_assert_stdstring_equal(a, b)
#  define assert_stdsv_equal(a, b) munit_assert_stdsv_equal(a, b)
#  define assert_enum_class(a, op, b) munit_assert_enum_class(a, op, b)

#endif /* defined(MUNIT_ENABLE_ASSERT_ALIASES) */

#endif // MUNITXX_H
