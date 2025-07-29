#include <iostream>
#include <cstring>
#include "strings.h"
#include "types.h"

int tests_run = 0;
int tests_passed = 0;

#define TEST(name) void test_##name(); tests_run++; std::cout << "Running " << #name << "... "; test_##name(); std::cout << "PASSED" << std::endl; tests_passed++;
#define ASSERT(condition) if (!(condition)) { std::cout << "FAILED at line " << __LINE__ << ": " << #condition << std::endl; exit(1); }
#define ASSERT_EQ(a, b) if ((a) != (b)) { std::cout << "FAILED at line " << __LINE__ << ": " << #a << " != " << #b << " (" << (a) << " != " << (b) << ")" << std::endl; exit(1); }

// Test u32_string functions
void test_u32str_create() {
    u32_string* str = u32str_create();
    ASSERT(str != nullptr);
    ASSERT_EQ(u32str_length(str), 0);
    u32str_destroy(str);
}

void test_u32str_init() {
    u32 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u32_string* str = u32str_init(data);
    ASSERT(str != nullptr);
    ASSERT_EQ(u32str_length(str), 5);
    ASSERT_EQ(u32str_get(str, 0), 'H');
    ASSERT_EQ(u32str_get(str, 4), 'o');
    u32str_destroy(str);
}

void test_u32str_get_set() {
    u32 data[] = {'T', 'e', 's', 't', 0};
    u32_string* str = u32str_init(data);
    
    ASSERT_EQ(u32str_get(str, 1), 'e');
    u32str_set(str, 1, 'E');
    ASSERT_EQ(u32str_get(str, 1), 'E');
    
    ASSERT_EQ(u32str_getChar(str, 0), 'T');
    u32str_setChar(str, 0, 't');
    ASSERT_EQ(u32str_getChar(str, 0), 't');
    
    u32str_destroy(str);
}

void test_u32str_clear() {
    u32 data[] = {'T', 'e', 's', 't', 0};
    u32_string* str = u32str_init(data);
    
    u32str_clear(str);
    ASSERT_EQ(u32str_length(str), 0);
    
    u32str_destroy(str);
}

void test_u32str_reserve() {
    u32_string* str = u32str_create();
    u32str_reserve(str, 100);
    // Just ensure it doesn't crash
    u32str_destroy(str);
}

void test_u32str_remove() {
    u32 data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', 0};
    u32_string* str = u32str_init(data);
    
    u32str_remove(str, 5, 6); // Remove " World"
    ASSERT_EQ(u32str_length(str), 5);
    ASSERT_EQ(u32str_get(str, 4), 'o');
    
    u32str_destroy(str);
}

void test_u32str_insert() {
    u32 data1[] = {'H', 'e', 'l', 'l', 'o', 0};
    u32 data2[] = {' ', 'W', 'o', 'r', 'l', 'd', 0};
    u32_string* str1 = u32str_init(data1);
    u32_string* str2 = u32str_init(data2);
    
    u32str_insert(str1, str2, 5, 0, 6);
    ASSERT_EQ(u32str_length(str1), 11);
    ASSERT_EQ(u32str_get(str1, 5), ' ');
    ASSERT_EQ(u32str_get(str1, 10), 'd');
    
    u32str_destroy(str1);
    u32str_destroy(str2);
}

void test_u32str_substr() {
    u32 data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', 0};
    u32_string* str = u32str_init(data);
    
    u32_string* sub = u32str_substr(str, 6, 5);
    ASSERT_EQ(u32str_length(sub), 5);
    ASSERT_EQ(u32str_get(sub, 0), 'W');
    ASSERT_EQ(u32str_get(sub, 4), 'd');
    
    u32str_destroy(str);
    u32str_destroy(sub);
}

void test_u32str_compare() {
    u32 data1[] = {'A', 'B', 'C', 0};
    u32 data2[] = {'A', 'B', 'C', 0};
    u32 data3[] = {'A', 'B', 'D', 0};
    u32 data4[] = {'A', 'B', 0};
    
    u32_string* str1 = u32str_init(data1);
    u32_string* str2 = u32str_init(data2);
    u32_string* str3 = u32str_init(data3);
    u32_string* str4 = u32str_init(data4);
    
    ASSERT_EQ(u32str_compare(str1, str2), 0);
    ASSERT(u32str_compare(str1, str3) < 0);
    ASSERT(u32str_compare(str3, str1) > 0);
    ASSERT(u32str_compare(str1, str4) > 0);
    ASSERT(u32str_compare(str4, str1) < 0);
    
    u32str_destroy(str1);
    u32str_destroy(str2);
    u32str_destroy(str3);
    u32str_destroy(str4);
}

void test_u32str_indexOf() {
    u32 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u32_string* str = u32str_init(data);
    
    ASSERT_EQ(u32str_indexOf(str, 'l'), 2);
    ASSERT_EQ(u32str_indexOf(str, 'o'), 4);
    ASSERT_EQ(u32str_indexOf(str, 'x'), -1);
    
    u32str_destroy(str);
}

void test_u32str_concat() {
    u32 data1[] = {'H', 'e', 'l', 'l', 'o', 0};
    u32 data2[] = {' ', 0};
    u32 data3[] = {'W', 'o', 'r', 'l', 'd', 0};
    
    u32_string* str1 = u32str_init(data1);
    u32_string* str2 = u32str_init(data2);
    u32_string* str3 = u32str_init(data3);
    u32_string* result = u32str_create();
    
    u32_string* arr[] = {str1, str2, str3};
    u32str_concat(result, 3, arr);
    
    ASSERT_EQ(u32str_length(result), 11);
    ASSERT_EQ(u32str_get(result, 0), 'H');
    ASSERT_EQ(u32str_get(result, 5), ' ');
    ASSERT_EQ(u32str_get(result, 10), 'd');
    
    u32str_destroy(str1);
    u32str_destroy(str2);
    u32str_destroy(str3);
    u32str_destroy(result);
}

// Test u16_string functions
void test_u16str_create() {
    u16_string* str = u16str_create();
    ASSERT(str != nullptr);
    ASSERT_EQ(u16str_length(str), 0);
    u16str_destroy(str);
}

void test_u16str_init() {
    u16 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u16_string* str = u16str_init(data);
    ASSERT(str != nullptr);
    ASSERT_EQ(u16str_length(str), 5);
    ASSERT_EQ(u16str_get(str, 0), 'H');
    ASSERT_EQ(u16str_get(str, 4), 'o');
    u16str_destroy(str);
}

void test_u16str_get_set() {
    u16 data[] = {'T', 'e', 's', 't', 0};
    u16_string* str = u16str_init(data);
    
    ASSERT_EQ(u16str_get(str, 1), 'e');
    u16str_set(str, 1, 'E');
    ASSERT_EQ(u16str_get(str, 1), 'E');
    
    ASSERT_EQ(u16str_getChar(str, 0), 'T');
    u16str_setChar(str, 0, 't');
    ASSERT_EQ(u16str_getChar(str, 0), 't');
    
    u16str_destroy(str);
}

void test_u16str_clear() {
    u16 data[] = {'T', 'e', 's', 't', 0};
    u16_string* str = u16str_init(data);
    
    u16str_clear(str);
    ASSERT_EQ(u16str_length(str), 0);
    
    u16str_destroy(str);
}

void test_u16str_remove() {
    u16 data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', 0};
    u16_string* str = u16str_init(data);
    
    u16str_remove(str, 5, 6);
    ASSERT_EQ(u16str_length(str), 5);
    ASSERT_EQ(u16str_get(str, 4), 'o');
    
    u16str_destroy(str);
}

void test_u16str_insert() {
    u16 data1[] = {'H', 'e', 'l', 'l', 'o', 0};
    u16 data2[] = {' ', 'W', 'o', 'r', 'l', 'd', 0};
    u16_string* str1 = u16str_init(data1);
    u16_string* str2 = u16str_init(data2);
    
    u16str_insert(str1, str2, 5, 0, 6);
    ASSERT_EQ(u16str_length(str1), 11);
    ASSERT_EQ(u16str_get(str1, 5), ' ');
    ASSERT_EQ(u16str_get(str1, 10), 'd');
    
    u16str_destroy(str1);
    u16str_destroy(str2);
}

void test_u16str_substr() {
    u16 data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', 0};
    u16_string* str = u16str_init(data);
    
    u16_string* sub = u16str_substr(str, 6, 5);
    ASSERT_EQ(u16str_length(sub), 5);
    ASSERT_EQ(u16str_get(sub, 0), 'W');
    ASSERT_EQ(u16str_get(sub, 4), 'd');
    
    u16str_destroy(str);
    u16str_destroy(sub);
}

void test_u16str_compare() {
    u16 data1[] = {'A', 'B', 'C', 0};
    u16 data2[] = {'A', 'B', 'C', 0};
    u16 data3[] = {'A', 'B', 'D', 0};
    u16 data4[] = {'A', 'B', 0};
    
    u16_string* str1 = u16str_init(data1);
    u16_string* str2 = u16str_init(data2);
    u16_string* str3 = u16str_init(data3);
    u16_string* str4 = u16str_init(data4);
    
    ASSERT_EQ(u16str_compare(str1, str2), 0);
    ASSERT(u16str_compare(str1, str3) < 0);
    ASSERT(u16str_compare(str3, str1) > 0);
    ASSERT(u16str_compare(str1, str4) > 0);
    ASSERT(u16str_compare(str4, str1) < 0);
    
    u16str_destroy(str1);
    u16str_destroy(str2);
    u16str_destroy(str3);
    u16str_destroy(str4);
}

void test_u16str_indexOf() {
    u16 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u16_string* str = u16str_init(data);
    
    ASSERT_EQ(u16str_indexOf(str, 'l'), 2);
    ASSERT_EQ(u16str_indexOf(str, 'o'), 4);
    ASSERT_EQ(u16str_indexOf(str, 'x'), -1);
    
    u16str_destroy(str);
}

void test_u16str_concat() {
    u16 data1[] = {'H', 'e', 'l', 'l', 'o', 0};
    u16 data2[] = {' ', 0};
    u16 data3[] = {'W', 'o', 'r', 'l', 'd', 0};
    
    u16_string* str1 = u16str_init(data1);
    u16_string* str2 = u16str_init(data2);
    u16_string* str3 = u16str_init(data3);
    u16_string* result = u16str_create();
    
    u16_string* arr[] = {str1, str2, str3};
    u16str_concat(result, 3, arr);
    
    ASSERT_EQ(u16str_length(result), 11);
    ASSERT_EQ(u16str_get(result, 0), 'H');
    ASSERT_EQ(u16str_get(result, 5), ' ');
    ASSERT_EQ(u16str_get(result, 10), 'd');
    
    u16str_destroy(str1);
    u16str_destroy(str2);
    u16str_destroy(str3);
    u16str_destroy(result);
}

// Test u8_string functions
void test_u8str_create() {
    u8_string* str = u8str_create();
    ASSERT(str != nullptr);
    ASSERT_EQ(u8str_length(str), 0);
    u8str_destroy(str);
}

void test_u8str_init() {
    u8 data[] = "Hello";
    u8_string* str = u8str_init(data);
    ASSERT(str != nullptr);
    ASSERT_EQ(u8str_length(str), 5);
    ASSERT_EQ(u8str_get(str, 0), 'H');
    ASSERT_EQ(u8str_get(str, 4), 'o');
    u8str_destroy(str);
}

void test_u8str_get_set() {
    u8 data[] = "Test";
    u8_string* str = u8str_init(data);
    
    ASSERT_EQ(u8str_get(str, 1), 'e');
    u8str_set(str, 1, 'E');
    ASSERT_EQ(u8str_get(str, 1), 'E');
    
    ASSERT_EQ(u8str_getChar(str, 0), 'T');
    u8str_setChar(str, 0, 't');
    ASSERT_EQ(u8str_getChar(str, 0), 't');
    
    u8str_destroy(str);
}

void test_u8str_clear() {
    u8 data[] = "Test";
    u8_string* str = u8str_init(data);
    
    u8str_clear(str);
    ASSERT_EQ(u8str_length(str), 0);
    
    u8str_destroy(str);
}

void test_u8str_remove() {
    u8 data[] = "Hello World";
    u8_string* str = u8str_init(data);
    
    u8str_remove(str, 5, 6);
    ASSERT_EQ(u8str_length(str), 5);
    ASSERT_EQ(u8str_get(str, 4), 'o');
    
    u8str_destroy(str);
}

void test_u8str_insert() {
    u8 data1[] = "Hello";
    u8 data2[] = " World";
    u8_string* str1 = u8str_init(data1);
    u8_string* str2 = u8str_init(data2);
    
    u8str_insert(str1, str2, 5, 0, 6);
    ASSERT_EQ(u8str_length(str1), 11);
    ASSERT_EQ(u8str_get(str1, 5), ' ');
    ASSERT_EQ(u8str_get(str1, 10), 'd');
    
    u8str_destroy(str1);
    u8str_destroy(str2);
}

void test_u8str_substr() {
    u8 data[] = "Hello World";
    u8_string* str = u8str_init(data);
    
    u8_string* sub = u8str_substr(str, 6, 5);
    ASSERT_EQ(u8str_length(sub), 5);
    ASSERT_EQ(u8str_get(sub, 0), 'W');
    ASSERT_EQ(u8str_get(sub, 4), 'd');
    
    u8str_destroy(str);
    u8str_destroy(sub);
}

void test_u8str_compare() {
    u8 data1[] = "ABC";
    u8 data2[] = "ABC";
    u8 data3[] = "ABD";
    u8 data4[] = "AB";
    
    u8_string* str1 = u8str_init(data1);
    u8_string* str2 = u8str_init(data2);
    u8_string* str3 = u8str_init(data3);
    u8_string* str4 = u8str_init(data4);
    
    ASSERT_EQ(u8str_compare(str1, str2), 0);
    ASSERT(u8str_compare(str1, str3) < 0);
    ASSERT(u8str_compare(str3, str1) > 0);
    ASSERT(u8str_compare(str1, str4) > 0);
    ASSERT(u8str_compare(str4, str1) < 0);
    
    u8str_destroy(str1);
    u8str_destroy(str2);
    u8str_destroy(str3);
    u8str_destroy(str4);
}

void test_u8str_indexOf() {
    u8 data[] = "Hello";
    u8_string* str = u8str_init(data);
    
    ASSERT_EQ(u8str_indexOf(str, 'l'), 2);
    ASSERT_EQ(u8str_indexOf(str, 'o'), 4);
    ASSERT_EQ(u8str_indexOf(str, 'x'), -1);
    
    u8str_destroy(str);
}

void test_u8str_concat() {
    u8 data1[] = "Hello";
    u8 data2[] = " ";
    u8 data3[] = "World";
    
    u8_string* str1 = u8str_init(data1);
    u8_string* str2 = u8str_init(data2);
    u8_string* str3 = u8str_init(data3);
    u8_string* result = u8str_create();
    
    u8_string* arr[] = {str1, str2, str3};
    u8str_concat(result, 3, arr);
    
    ASSERT_EQ(u8str_length(result), 11);
    ASSERT_EQ(u8str_get(result, 0), 'H');
    ASSERT_EQ(u8str_get(result, 5), ' ');
    ASSERT_EQ(u8str_get(result, 10), 'd');
    
    u8str_destroy(str1);
    u8str_destroy(str2);
    u8str_destroy(str3);
    u8str_destroy(result);
}

// Test conversion functions
void test_u32_to_u16_conversion() {
    u32 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u32_string* u32str = u32str_init(data);
    
    u16_string* u16str = u32str_to_u16str(u32str);
    ASSERT_EQ(u16str_length(u16str), 5);
    ASSERT_EQ(u16str_get(u16str, 0), 'H');
    ASSERT_EQ(u16str_get(u16str, 4), 'o');
    
    u32str_destroy(u32str);
    u16str_destroy(u16str);
}

void test_u32_to_u8_conversion() {
    u32 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u32_string* u32str = u32str_init(data);
    
    u8_string* u8str = u32str_to_u8str(u32str);
    ASSERT_EQ(u8str_length(u8str), 5);
    ASSERT_EQ(u8str_get(u8str, 0), 'H');
    ASSERT_EQ(u8str_get(u8str, 4), 'o');
    
    u32str_destroy(u32str);
    u8str_destroy(u8str);
}

void test_u16_to_u32_conversion() {
    u16 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u16_string* u16str = u16str_init(data);
    
    u32_string* u32str = u16str_to_u32str(u16str);
    ASSERT_EQ(u32str_length(u32str), 5);
    ASSERT_EQ(u32str_get(u32str, 0), 'H');
    ASSERT_EQ(u32str_get(u32str, 4), 'o');
    
    u16str_destroy(u16str);
    u32str_destroy(u32str);
}

void test_u16_to_u8_conversion() {
    u16 data[] = {'H', 'e', 'l', 'l', 'o', 0};
    u16_string* u16str = u16str_init(data);
    
    u8_string* u8str = u16strto_u8str(u16str);
    ASSERT_EQ(u8str_length(u8str), 5);
    ASSERT_EQ(u8str_get(u8str, 0), 'H');
    ASSERT_EQ(u8str_get(u8str, 4), 'o');
    
    u16str_destroy(u16str);
    u8str_destroy(u8str);
}

void test_u8_to_u32_conversion() {
    u8 data[] = "Hello";
    u8_string* u8str = u8str_init(data);
    
    u32_string* u32str = u8str_to_u32str(u8str);
    ASSERT_EQ(u32str_length(u32str), 5);
    ASSERT_EQ(u32str_get(u32str, 0), 'H');
    ASSERT_EQ(u32str_get(u32str, 4), 'o');
    
    u8str_destroy(u8str);
    u32str_destroy(u32str);
}

void test_u8_to_u16_conversion() {
    u8 data[] = "Hello";
    u8_string* u8str = u8str_init(data);
    
    u16_string* u16str = u8str_to_u16str(u8str);
    ASSERT_EQ(u16str_length(u16str), 5);
    ASSERT_EQ(u16str_get(u16str, 0), 'H');
    ASSERT_EQ(u16str_get(u16str, 4), 'o');
    
    u8str_destroy(u8str);
    u16str_destroy(u16str);
}

// Test UTF-8 handling
void test_utf8_multibyte() {
    // Test with UTF-8 encoded string "Héllo" (é = U+00E9 = 0xC3 0xA9)
    u8 data[] = {0x48, 0xC3, 0xA9, 0x6C, 0x6C, 0x6F, 0};
    u8_string* str = u8str_init(data);
    
    ASSERT_EQ(u8str_length(str), 5); // 5 characters
    ASSERT_EQ(u8str_get(str, 0), 0x48); // 'H'
    ASSERT_EQ(u8str_get(str, 1), 0xC3); // First byte of 'é'
    ASSERT_EQ(u8str_getChar(str, 1), '?'); // Non-ASCII returns '?'
    
    u8str_destroy(str);
}

// Test UTF-16 surrogate pairs
void test_utf16_surrogate_pairs() {
    // Test with UTF-16 string containing emoji 😀 (U+1F600 = 0xD83D 0xDE00)
    u16 data[] = {0x48, 0x69, 0xD83D, 0xDE00, 0x21, 0}; // "Hi😀!"
    u16_string* str = u16str_init(data);
    
    ASSERT_EQ(u16str_length(str), 4); // 4 characters (H, i, 😀, !)
    ASSERT_EQ(u16str_get(str, 0), 0x48); // 'H'
    ASSERT_EQ(u16str_get(str, 2), 0xD83D); // High surrogate of emoji
    
    u16str_destroy(str);
}

// Test edge cases
void test_edge_cases() {
    // Test null pointers
    ASSERT_EQ(u32str_length(nullptr), 0);
    ASSERT_EQ(u16str_length(nullptr), 0);
    ASSERT_EQ(u8str_length(nullptr), 0);
    
    // Test empty strings
    u32_string* empty32 = u32str_create();
    u16_string* empty16 = u16str_create();
    u8_string* empty8 = u8str_create();
    
    ASSERT_EQ(u32str_length(empty32), 0);
    ASSERT_EQ(u16str_length(empty16), 0);
    ASSERT_EQ(u8str_length(empty8), 0);
    
    // Test operations on empty strings
    u32str_remove(empty32, 0, 10);
    u16str_remove(empty16, 0, 10);
    u8str_remove(empty8, 0, 10);
    
    ASSERT_EQ(u32str_length(empty32), 0);
    ASSERT_EQ(u16str_length(empty16), 0);
    ASSERT_EQ(u8str_length(empty8), 0);
    
    u32str_destroy(empty32);
    u16str_destroy(empty16);
    u8str_destroy(empty8);
}

int main() {
    std::cout << "Running string unit tests..." << std::endl;
    
    // u32_string tests
    TEST(u32str_create);
    TEST(u32str_init);
    TEST(u32str_get_set);
    TEST(u32str_clear);
    TEST(u32str_reserve);
    TEST(u32str_remove);
    TEST(u32str_insert);
    TEST(u32str_substr);
    TEST(u32str_compare);
    TEST(u32str_indexOf);
    TEST(u32str_concat);
    
    // u16_string tests
    TEST(u16str_create);
    TEST(u16str_init);
    TEST(u16str_get_set);
    TEST(u16str_clear);
    TEST(u16str_remove);
    TEST(u16str_insert);
    TEST(u16str_substr);
    TEST(u16str_compare);
    TEST(u16str_indexOf);
    TEST(u16str_concat);
    
    // u8_string tests
    TEST(u8str_create);
    TEST(u8str_init);
    TEST(u8str_get_set);
    TEST(u8str_clear);
    TEST(u8str_remove);
    TEST(u8str_insert);
    TEST(u8str_substr);
    TEST(u8str_compare);
    TEST(u8str_indexOf);
    TEST(u8str_concat);
    
    // Conversion tests
    TEST(u32_to_u16_conversion);
    TEST(u32_to_u8_conversion);
    TEST(u16_to_u32_conversion);
    TEST(u16_to_u8_conversion);
    TEST(u8_to_u32_conversion);
    TEST(u8_to_u16_conversion);
    
    // UTF encoding tests
    TEST(utf8_multibyte);
    TEST(utf16_surrogate_pairs);
    
    // Edge case tests
    TEST(edge_cases);
    
    std::cout << "\nAll tests completed!" << std::endl;
    std::cout << "Tests run: " << tests_run << std::endl;
    std::cout << "Tests passed: " << tests_passed << std::endl;
    
    return 0;
}