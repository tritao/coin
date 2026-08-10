#ifndef COIN_TESTSUITE_COINTTEST_H
#define COIN_TESTSUITE_COINTTEST_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace CoinTest {

struct AbortTestCase { };

struct TestCase {
  const char * name;
  void (*fn)(void);
  const char * file;
  int line;
};

struct Failure {
  const char * file;
  int line;
  std::string message;
};

struct Context {
  int checks = 0;
  int failed = 0;
  std::vector<Failure> failures;
};

inline std::vector<TestCase> & registry(void)
{
  static std::vector<TestCase> r;
  return r;
}

inline Context *& current_context(void)
{
  static Context * ctx = NULL;
  return ctx;
}

struct Registrar {
  Registrar(const char * name, void (*fn)(void), const char * file, int line)
  {
    registry().push_back(TestCase{name, fn, file, line});
  }
};

inline void add_failure(const char * file, int line, const std::string & message)
{
  Context * ctx = current_context();
  if (!ctx) return;
  ctx->failed += 1;
  ctx->failures.push_back(Failure{file, line, message});
}

inline void add_check(void)
{
  Context * ctx = current_context();
  if (!ctx) return;
  ctx->checks += 1;
}

template <typename T>
std::string stringify(const T & v)
{
  std::ostringstream oss;
  oss << v;
  return oss.str();
}

inline void check(bool cond, const std::string & message, const char * file, int line)
{
  add_check();
  if (!cond) add_failure(file, line, message);
}

inline void require(bool cond, const std::string & message, const char * file, int line)
{
  add_check();
  if (!cond) {
    add_failure(file, line, message);
    throw AbortTestCase();
  }
}

template <typename A, typename B>
inline void check_equal(const A & a, const B & b,
                        const char * aexpr, const char * bexpr,
                        const char * file, int line)
{
  add_check();
  if (!(a == b)) {
    add_failure(file, line,
                std::string("check failed: (") + aexpr + " == " + bexpr + ") (" +
                  stringify(a) + " != " + stringify(b) + ")");
  }
}

inline void print_usage(const char * program)
{
  std::fprintf(stderr,
               "Usage: %s [--list] [--test NAME | --filter TEXT] [--max-tests N [--append-test NAME]]\n",
               program);
}

inline bool parse_max_tests(const char * text, size_t & value)
{
  if (!text || !text[0]) return false;
  char * end = NULL;
  const unsigned long parsed = std::strtoul(text, &end, 10);
  if (*end != '\0' || parsed == 0) return false;
  value = static_cast<size_t>(parsed);
  return true;
}

inline int run_all(int argc, char ** argv)
{
  const std::vector<TestCase> & tests = registry();
  const char * exact_name = NULL;
  const char * name_filter = NULL;
  const char * append_name = NULL;
  size_t max_tests = 0;
  bool list_tests = false;

  for (int i = 1; i < argc; ++i) {
    const char * arg = argv[i];
    if (std::strcmp(arg, "--list") == 0) {
      list_tests = true;
    }
    else if (std::strcmp(arg, "--test") == 0 ||
             std::strcmp(arg, "--filter") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 2;
      }
      if (std::strcmp(arg, "--test") == 0) {
        if (exact_name || name_filter) {
          print_usage(argv[0]);
          return 2;
        }
        exact_name = argv[++i];
      }
      else {
        if (exact_name || name_filter) {
          print_usage(argv[0]);
          return 2;
        }
        name_filter = argv[++i];
      }
    }
    else if (std::strcmp(arg, "--max-tests") == 0) {
      if (i + 1 >= argc || !parse_max_tests(argv[++i], max_tests)) {
        print_usage(argv[0]);
        return 2;
      }
    }
    else if (std::strcmp(arg, "--append-test") == 0) {
      if (i + 1 >= argc || append_name || exact_name || name_filter) {
        print_usage(argv[0]);
        return 2;
      }
      append_name = argv[++i];
    }
    else {
      print_usage(argv[0]);
      return 2;
    }
  }

  if (append_name && !max_tests) {
    print_usage(argv[0]);
    return 2;
  }

  if (list_tests) {
    for (size_t i = 0; i < tests.size(); ++i) {
      const TestCase & tc = tests[i];
      std::fprintf(stdout, "%zu\t%s\t%s:%d\n", i + 1, tc.name, tc.file, tc.line);
    }
    return 0;
  }

  int failedtests = 0;
  int totalchecks = 0;
  size_t selectedtests = 0;
  bool appended_test_run = false;

  for (size_t i = 0; i < tests.size(); ++i) {
    const TestCase & tc = tests[i];
    if (exact_name && std::strcmp(tc.name, exact_name) != 0) continue;
    if (name_filter && !std::strstr(tc.name, name_filter)) continue;
    const bool is_appended_test = append_name &&
      std::strcmp(tc.name, append_name) == 0;
    if (is_appended_test && appended_test_run) continue;
    if (max_tests && selectedtests >= max_tests && !is_appended_test) {
      if (!append_name) break;
      continue;
    }
    selectedtests += 1;
    if (is_appended_test) appended_test_run = true;

    std::fprintf(stderr, "[RUN %zu/%zu] %s (%s:%d)\n",
                 i + 1, tests.size(), tc.name, tc.file, tc.line);
    std::fflush(stderr);

    Context ctx;
    current_context() = &ctx;

    try {
      tc.fn();
    } catch (const AbortTestCase &) {
      // already recorded as failure(s)
    } catch (const std::exception & e) {
      add_failure(tc.file, tc.line, std::string("unhandled std::exception: ") + e.what());
    } catch (...) {
      add_failure(tc.file, tc.line, "unhandled non-std exception");
    }

    std::fprintf(stderr, "[DONE %zu/%zu] %s\n",
                 i + 1, tests.size(), tc.name);
    std::fflush(stderr);

    current_context() = NULL;
    totalchecks += ctx.checks;

    if (ctx.failed) {
      failedtests += 1;
      std::fprintf(stderr, "[FAIL] %s (%s:%d)\n", tc.name, tc.file, tc.line);
      for (size_t f = 0; f < ctx.failures.size(); ++f) {
        const Failure & fail = ctx.failures[f];
        std::fprintf(stderr, "  %s:%d: %s\n", fail.file, fail.line, fail.message.c_str());
      }
    }
  }

  if (selectedtests == 0) {
    std::fprintf(stderr, "[ERROR] no tests selected\n");
    return 2;
  }

  if (append_name && !appended_test_run) {
    std::fprintf(stderr, "[ERROR] appended test not found: %s\n", append_name);
    return 2;
  }

  if (failedtests == 0) {
    std::fprintf(stderr, "[OK] %zu tests, %d checks\n", selectedtests, totalchecks);
    return 0;
  }

  std::fprintf(stderr, "[FAIL] %d/%zu tests failed, %d checks\n",
               failedtests, selectedtests, totalchecks);
  return 1;
}

inline int run_all(void)
{
  return run_all(0, NULL);
}

} // namespace CoinTest

// ---------------------------------------------------------------------------
// Minimal Boost.Test compatibility macros used by Coin's testsuite extractor
// ---------------------------------------------------------------------------

#define BOOST_TEST_NO_LIB 1

#define COIN_TEST_CONCAT_INNER(a, b) a##b
#define COIN_TEST_CONCAT(a, b) COIN_TEST_CONCAT_INNER(a, b)

#define BOOST_AUTO_TEST_SUITE(name) namespace name { enum { coin_testsuite_dummy = 0 };
#define BOOST_AUTO_TEST_SUITE_END() }
#define BOOST_AUTO_TEST_CASE_EXPECTED_FAILURES(name, n) /* no-op */

#define BOOST_AUTO_TEST_CASE(name)                                            \
  static void COIN_TEST_CONCAT(coin_test_fn_, __LINE__)(void);                \
  static ::CoinTest::Registrar COIN_TEST_CONCAT(coin_test_reg_, __LINE__)(    \
    #name, &COIN_TEST_CONCAT(coin_test_fn_, __LINE__), __FILE__, __LINE__);  \
  static void COIN_TEST_CONCAT(coin_test_fn_, __LINE__)(void)

#define BOOST_CHECK_MESSAGE(cond, msg) \
  ::CoinTest::check(!!(cond), (msg), __FILE__, __LINE__)

#define BOOST_CHECK(cond) \
  ::CoinTest::check(!!(cond), std::string("check failed: ") + #cond, __FILE__, __LINE__)

#define BOOST_CHECK_EQUAL(a, b) \
  ::CoinTest::check_equal((a), (b), #a, #b, __FILE__, __LINE__)

#define BOOST_REQUIRE_MESSAGE(cond, msg) \
  ::CoinTest::require(!!(cond), (msg), __FILE__, __LINE__)

#define BOOST_REQUIRE(cond) \
  ::CoinTest::require(!!(cond), std::string("require failed: ") + #cond, __FILE__, __LINE__)

#define BOOST_REQUIRE_EQUAL(a, b) \
  ::CoinTest::require(((a) == (b)), std::string("require failed: (") + #a + " == " + #b + ")", __FILE__, __LINE__)

#define BOOST_REQUIRE_NE(a, b) \
  ::CoinTest::require(((a) != (b)), std::string("require failed: (") + #a + " != " + #b + ")", __FILE__, __LINE__)

#define BOOST_ASSERT(cond) BOOST_REQUIRE(cond)

#define BOOST_REQUIRE_THROW(expr, exctype)                                    \
  do {                                                                        \
    bool coin_threw = false;                                                  \
    try { (void)(expr); } catch (const exctype &) { coin_threw = true; }      \
    ::CoinTest::require(coin_threw, std::string("expected throw: ") + #expr, __FILE__, __LINE__); \
  } while (0)

#define BOOST_STATIC_ASSERT(expr) static_assert((expr), #expr)

#endif // !COIN_TESTSUITE_COINTTEST_H
