#include <cstdio>
#include <cstring>
#include <vector>

#include "check.hpp"

int main(int argc, char **argv) {
  std::vector<t::Case *> cases;
  for (t::Case *c = t::head(); c; c = c->next) cases.insert(cases.begin(), c);
  int ran = 0, failed_cases = 0;
  for (t::Case *c : cases) {
    if (argc > 1 && !std::strstr(c->name, argv[1])) continue;
    const int before = t::failures();
    c->fn();
    ++ran;
    const bool ok = t::failures() == before;
    if (!ok) ++failed_cases;
    std::printf("%s %s\n", ok ? "[ ok ]" : "[FAIL]", c->name);
  }
  std::printf("\n%d tests, %d checks, %d failed tests, %d failed checks\n", ran, t::checks(), failed_cases,
              t::failures());
  return t::failures() ? 1 : 0;
}
