#include <cstring>

#include "test_framework.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0;
    int failed = 0;
    for (const auto& t : sptest::registry()) {
        if (filter && std::strstr(t.name, filter) == nullptr) {
            continue;
        }
        try {
            t.fn();
            ++passed;
            std::cout << "[ PASS ] " << t.name << "\n";
        } catch (const sptest::Failure& f) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << "\n         " << f.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << " threw: " << e.what() << "\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
