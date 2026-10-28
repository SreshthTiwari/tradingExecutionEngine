#ifndef TEST_UTIL_HPP
#define TEST_UTIL_HPP

#include <iostream>

inline int &failures()
{
    static int count = 0;
    return count;
}

#define CHECK(cond)                                                                  \
    do                                                                               \
    {                                                                                \
        if (!(cond))                                                                 \
        {                                                                            \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures();                                                            \
        }                                                                            \
    } while (0)

#define TEST_MAIN_RESULT() \
    (failures() ? (std::cerr << failures() << " check(s) failed\n", 1) : (std::cout << "all checks passed\n", 0))

#endif // TEST_UTIL_HPP
