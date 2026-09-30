// Minimal self-registering test harness (no external dependencies).
#pragma once

#include <cstdio>
#include <cstring>

namespace t {
struct Case {
  const char *name;
  void (*fn)();
  Case *next;
};
inline Case *&head() { static Case *h = nullptr; return h; }
inline int &failures() { static int f = 0; return f; }
inline int &checks() { static int c = 0; return c; }
struct Reg {
  Case c;
  Reg(const char *n, void (*f)()) : c{n, f, nullptr} { c.next = head(); head() = &c; }
};
}  // namespace t

#define TEST(name)                                   \
  static void test_##name();                         \
  static t::Reg reg_##name(#name, &test_##name);     \
  static void test_##name()

#define CHECK(cond)                                                              \
  do {                                                                           \
    ++t::checks();                                                               \
    if (!(cond)) {                                                               \
      ++t::failures();                                                           \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    ++t::checks();                                                               \
    const long long _a = (long long)(a), _b = (long long)(b);                    \
    if (_a != _b) {                                                              \
      ++t::failures();                                                           \
      std::printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, \
                  #a, #b, _a, _b);                                               \
    }                                                                            \
  } while (0)

#define CHECK_STR(a, b)                                                          \
  do {                                                                           \
    ++t::checks();                                                               \
    if (std::strcmp((a), (b)) != 0) {                                            \
      ++t::failures();                                                           \
      std::printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
    }                                                                            \
  } while (0)
