#pragma once
// A deliberately tiny test framework (no external dependencies).
//
//   TEST(name) { CHECK(x == 1); CHECK_EQ(a, b); }
//
// Each TEST registers itself in a global list before main() runs; test_main.cpp
// runs them all and reports failures.
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct TestCase {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& testRegistry() {
  static std::vector<TestCase> tests;
  return tests;
}

inline int& currentFailures() {
  static int failures = 0;
  return failures;
}

struct TestRegistrar {
  TestRegistrar(const char* name, std::function<void()> fn) {
    testRegistry().push_back({name, std::move(fn)});
  }
};

#define TEST(name)                                         \
  static void test_##name();                               \
  static TestRegistrar registrar_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond   \
                << ") failed\n";                                             \
      currentFailures()++;                                                   \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    auto va_ = (a);                                                          \
    auto vb_ = (b);                                                          \
    if (!(va_ == vb_)) {                                                     \
      std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #a   \
                << ", " #b ") failed: " << va_ << " vs " << vb_ << "\n";     \
      currentFailures()++;                                                   \
    }                                                                        \
  } while (0)
