#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <cmath>

namespace apex::test {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline bool register_test(const std::string& name, std::function<void()> func) {
    get_registry().push_back({name, std::move(func)});
    return true;
}

#define TEST_CASE(name) \
    void test_func_##name(); \
    static bool reg_##name = ::apex::test::register_test(#name, test_func_##name); \
    void test_func_##name()

#define REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "  \033[31m[FAILED]\033[0m " << #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            throw std::runtime_error("Assertion failed: " #expr); \
        } \
    } while (0)

#define REQUIRE_FALSE(expr) REQUIRE(!(expr))

#define REQUIRE_NEAR(a, b, eps) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            std::cerr << "  \033[31m[FAILED]\033[0m |" << #a << " - " << #b << "| > " << eps \
                      << " (" << (a) << " vs " << (b) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            throw std::runtime_error("Near assertion failed"); \
        } \
    } while (0)

inline int run_all() {
    auto& tests = get_registry();
    std::cout << "\033[36m================ Running " << tests.size() << " ApexEdge Unit Tests ================\033[0m\n";
    int passed = 0;
    int failed = 0;

    for (const auto& t : tests) {
        std::cout << "• [RUN]  " << t.name << " ... ";
        try {
            t.func();
            std::cout << "\033[32mPASSED\033[0m\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "\033[31mFAILED\033[0m (" << e.what() << ")\n";
            ++failed;
        }
    }

    std::cout << "\033[36m=================================================================\033[0m\n";
    std::cout << "Results: \033[32m" << passed << " passed\033[0m, \033[" << (failed > 0 ? "31" : "32") << "m" 
              << failed << " failed\033[0m\n";
    return failed == 0 ? 0 : 1;
}

} // namespace apex::test
