#include "test.h"

int main() {
  int failedTests = 0;
  for (const TestCase& t : testRegistry()) {
    int before = currentFailures();
    try {
      t.fn();
    } catch (const std::exception& e) {
      std::cerr << "  unexpected exception: " << e.what() << "\n";
      currentFailures()++;
    }
    bool ok = currentFailures() == before;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << t.name << "\n";
    if (!ok) failedTests++;
  }
  std::cout << "\n" << (testRegistry().size() - failedTests) << "/"
            << testRegistry().size() << " tests passed\n";
  return failedTests == 0 ? 0 : 1;
}
