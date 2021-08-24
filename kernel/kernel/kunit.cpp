/*
* Copyright (c) 2021 Pedro Falcato
* This file is part of Onyx, and is released under the terms of the MIT License
* check LICENSE at the root directory for more information
*/

#include <stdio.h>

#include <onyx/kunit.h>
#include <onyx/vector.h>
#include <onyx/rbtree.hpp>
#include <onyx/culstring.h>

namespace kunit
{

/**
 * @brief Print failed assertion/expect and fail the test.
 * 
 * @param expression Expression string.
 * @param file       File name string.
 * @param line       Line number.
 * @param function   Function name string.
 * @param test_case  Reference to ktest_case_base.
 */
void failed_expect(const char *expression, const char *file, int line,
                   const char *function, ktest_case_base& test_case)
{
    auto suite_name = test_case.get_suite_name();
    auto test_name = test_case.get_test_name();

    printf("[%s.%s] Failure: %s evaluates to false\n", suite_name, test_name, expression);
    printf("        at file %s:%d, function %s\n", expression, line, function);

    test_case.fail();
}

struct internal_testsuite
{
    cul::vector<kunit::ktest_case_base*> tests;
};

cul::rb_tree<cul::string, internal_testsuite> tree;

/**
 * @brief Registers the test case in KUnit.
 * 
 */
void ktest_case_base::register_test_case()
{
    cul::string name{get_suite_name()};
    auto it = tree.find(name);

    if(it == tree.end())
    {
        // Suite isn't registered, add it
        internal_testsuite ts{};
        tree.insert(cul::pair<cul::string&, const internal_testsuite&>{name, ts});
    }
}

void run_tests()
{
}

}
