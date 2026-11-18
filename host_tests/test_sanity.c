#include "unity.h"
#include "core.h"

void setUp(void) {}
void tearDown(void) {}

static void test_one_plus_one(void)
{
    TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

static void test_core_links_without_esp_idf(void)
{
    core_placeholder();   /* core'un ESP-IDF'siz derlendiğinin kanıtı */
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_plus_one);
    RUN_TEST(test_core_links_without_esp_idf);
    return UNITY_END();
}