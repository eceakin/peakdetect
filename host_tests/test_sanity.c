#include "unity.h"
#include "pd_types.h"
#include "app_config.h"
void setUp(void) {}
void tearDown(void) {}

static void test_one_plus_one(void)
{
    TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}



int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_plus_one);
    return UNITY_END();
}