/*
* Copyright (c) 2021 Pedro Falcato
* This file is part of Onyx, and is released under the terms of the MIT License
* check LICENSE at the root directory for more information
*/

#ifndef _ONYX_KUNIT_H
#define _ONYX_KUNIT_H

#include <onyx/scheduler.h>
#include <onyx/completion.h>

#define KTEST_CASE_NAME(testsuite_name, test_name) \
ktest_case_ ## testsuite_name ## _ ## test_name

namespace kunit
{
    class ktest_case_base
    {
    private:
        const char *suite_name_;
        const char *test_name_;
        bool test_failed_;
        completion done_;
        /**
         * @brief Registers the test case in KUnit.
         * 
         */
        void register_test_case();
    public:
        ktest_case_base(const char *testsuite_name, const char *test_name) :
            suite_name_{testsuite_name}, test_name_{test_name}, test_failed_{false},
            done_{}
        {
            register_test_case();
        }

        /**
         * @brief Get the test suite name.
         * 
         * @return The test suite name.
         */
        const char *get_suite_name() const
        {
            return suite_name_;
        }

        /**
         * @brief Get the test name.
         * 
         * @return The test name.
         */
        const char *get_test_name() const
        {
            return test_name_;
        }

        /**
         * @brief Runs the test's body.
         * 
         */
        virtual void run_test() = 0;

        /**
         * @brief Fails the test.
         * 
         */
        void fail()
        {
            test_failed_ = true;
        }

        /**
         * @brief Ends the test.
         *
         * This function is called either when the test exits normally, or
         * on assert.
         * 
         */
        void end_test()
        {
            // We signal for completion and then exit the thread
            // it's not destructor safe but honestly, I don't know if it's even possible
            // to unwind destructors without doing C++ exception unwinding.
            // Maybe that's the way to go? Good question.
            // Until then, this is unsafe.
            done_.signal();
            thread_exit();
        }
    };

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
                       const char *function, ktest_case_base& test_case);
}

#define KTEST(testsuite_name, test_name)                         \
class KTEST_CASE_NAME(testsuite_name, test_name) :               \
      public kunit::ktest_case_base                              \
{                                                                \
public:                                                          \
    KTEST_CASE_NAME(testsuite_name, test_name)()                 \
       : kunit::ktest_case_base{#testsuite_name, #test_name} {}  \
                                                                 \
                                                                 \
    void run_test() override;                                    \
};                                                               \
void KTEST_CASE_NAME(testsuite_name, test_name)::run_test()

#define KUNIT_EXPECT_OR_ASSERT_EXPR(expression, is_fatal) \
if(!(expression)) {                                       \
    kunit::failed_expect(#expression, __FILE__, __LINE__, __func__, *this);  \
    if(is_fatal) this->end_test();                                   \
}

#define KUNIT_EXPECT_EXPR(expression) KUNIT_EXPECT_OR_ASSERT_EXPR(expression, false)

#define KUNIT_EXPECT_EQ(rhs, lhs)     KUNIT_EXPECT_EXPR(rhs == lhs)
#define KUNIT_EXPECT_NEQ(rhs, lhs)    KUNIT_EXPECT_EXPR(rhs != lhs)
#define KUNIT_EXPECT_GT(rhs, lhs)     KUNIT_EXPECT_EXPR(rhs > lhs)
#define KUNIT_EXPECT_LT(rhs, lhs)     KUNIT_EXPECT_EXPR(rhs > lhs)
#define KUNIT_EXPECT_STREQ(rhs, lhs)  KUNIT_EXPECT_EXPR(strcmp(rhs, lhs) == 0)

#define KUNIT_ASSERT_EXPR(expression) KUNIT_EXPECT_OR_ASSERT_EXPR(expression, true)

#define KUNIT_ASSERT_EQ(rhs, lhs)     KUNIT_ASSERT_EXPR(rhs == lhs)
#define KUNIT_ASSERT_NEQ(rhs, lhs)    KUNIT_ASSERT_EXPR(rhs != lhs)
#define KUNIT_ASSERT_GT(rhs, lhs)     KUNIT_ASSERT_EXPR(rhs > lhs)
#define KUNIT_ASSERT_LT(rhs, lhs)     KUNIT_ASSERT_EXPR(rhs > lhs)
#define KUNIT_ASSERT_STREQ(rhs, lhs)  KUNIT_ASSERT_EXPR(strcmp(rhs, lhs) == 0)

#endif
