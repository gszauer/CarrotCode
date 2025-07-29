#include "strings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to print test results
void printTestResult(const char* testName, bool passed, const char* expected, const char* actual) {
    printf("  %s: %s\n", testName, passed ? "PASSED" : "FAILED");
    if (!passed) {
        printf("    Expected: %s\n", expected);
        printf("    Actual: %s\n", actual);
    }
}

void RunUnitTestsStrU8() {
    printf("\n=== U8 String Unit Tests ===\n");
    int totalTests = 0;
    int passedTests = 0;
    
    // Test 1: u8str_length
    {
        u8_string* str = u8str_init((u8*)"Hello");
        u32 length = u8str_length(str);
        bool passed = (length == 5);
        printTestResult("u8str_length basic", passed, "5", passed ? "5" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u8str_destroy(str);
    }
    
    // Test 2: u8str_get and u8str_set
    {
        u8_string* str = u8str_init((u8*)"Test");
        u8 original = u8str_get(str, 1); // 'e'
        u8str_set(str, 1, 'a');
        u8 modified = u8str_get(str, 1);
        bool passed = (original == 'e' && modified == 'a');
        printTestResult("u8str_get/set", passed, "e->a", passed ? "e->a" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        // Test edge case: out of bounds
        u8 outOfBounds = u8str_get(str, 10);
        bool boundsPassed = (outOfBounds == 0);
        printTestResult("u8str_get out of bounds", boundsPassed, "0", boundsPassed ? "0" : "non-zero");
        totalTests++;
        if (boundsPassed) passedTests++;
        
        u8str_destroy(str);
    }
    
    // Test 3: u8str_getChar and u8str_setChar
    {
        u8_string* str = u8str_init((u8*)"ABC");
        char ch = u8str_getChar(str, 0);
        u8str_setChar(str, 0, 'X');
        char newCh = u8str_getChar(str, 0);
        bool passed = (ch == 'A' && newCh == 'X');
        printTestResult("u8str_getChar/setChar", passed, "A->X", passed ? "A->X" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u8str_destroy(str);
    }
    
    // Test 4: u8str_clear
    {
        u8_string* str = u8str_init((u8*)"Content");
        u8str_clear(str);
        bool passed = (u8str_length(str) == 0 && u8str_data(str)[0] == 0);
        printTestResult("u8str_clear", passed, "length=0", passed ? "length=0" : "length!=0");
        totalTests++;
        if (passed) passedTests++;
        u8str_destroy(str);
    }
    
    // Test 5: u8str_reserve and u8str_grow
    {
        u8_string* str = u8str_init((u8*)"Small");
        u8str_reserve(str, 50);
        bool reservePassed = (u8str_capacity(str) == 50 && u8str_length(str) == 5);
        printTestResult("u8str_reserve", reservePassed, "capacity=50", reservePassed ? "capacity=50" : "incorrect");
        totalTests++;
        if (reservePassed) passedTests++;
        
        // Test grow (should double capacity if needed)
        u8str_grow(str, 60);
        bool growPassed = (u8str_capacity(str) >= 60);
        printTestResult("u8str_grow", growPassed, "capacity>=60", growPassed ? "capacity>=60" : "capacity<60");
        totalTests++;
        if (growPassed) passedTests++;
        
        u8str_destroy(str);
    }
    
    // Test 6: u8str_remove
    {
        u8_string* str = u8str_init((u8*)"HelloWorld");
        u8str_remove(str, 5, 5); // Remove "World"
        bool passed = (u8str_length(str) == 5 && memcmp(u8str_data(str), "Hello", 5) == 0);
        printTestResult("u8str_remove", passed, "Hello", passed ? "Hello" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        // Test edge case: remove beyond length
        u8str_remove(str, 3, 10); // Should only remove to end
        bool edgePassed = (u8str_length(str) == 3 && memcmp(u8str_data(str), "Hel", 3) == 0);
        printTestResult("u8str_remove edge case", edgePassed, "Hel", edgePassed ? "Hel" : "incorrect");
        totalTests++;
        if (edgePassed) passedTests++;
        
        u8str_destroy(str);
    }
    
    // Test 7: u8str_insert
    {
        u8_string* dest = u8str_init((u8*)"Hello");
        u8_string* src = u8str_init((u8*)"World");
        u8str_insert(dest, src, 5, 0, 5); // Insert at end
        bool passed = (u8str_length(dest) == 10 && memcmp(u8str_data(dest), "HelloWorld", 10) == 0);
        printTestResult("u8str_insert at end", passed, "HelloWorld", passed ? "HelloWorld" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        // Test insert in middle
        u8_string* dest2 = u8str_init((u8*)"AC");
        u8_string* src2 = u8str_init((u8*)"B");
        u8str_insert(dest2, src2, 1, 0, 1);
        bool midPassed = (u8str_length(dest2) == 3 && memcmp(u8str_data(dest2), "ABC", 3) == 0);
        printTestResult("u8str_insert in middle", midPassed, "ABC", midPassed ? "ABC" : "incorrect");
        totalTests++;
        if (midPassed) passedTests++;
        
        u8str_destroy(dest);
        u8str_destroy(src);
        u8str_destroy(dest2);
        u8str_destroy(src2);
    }
    
    // Test 8: u8str_substr
    {
        u8_string* str = u8str_init((u8*)"HelloWorld");
        u8_string* sub = u8str_substr(str, 5, 5);
        bool passed = (sub && u8str_length(sub) == 5 && memcmp(u8str_data(sub), "World", 5) == 0);
        printTestResult("u8str_substr", passed, "World", passed ? "World" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        // Test edge case: substr beyond length
        u8_string* sub2 = u8str_substr(str, 8, 10);
        bool edgePassed = (sub2 && u8str_length(sub2) == 2 && memcmp(u8str_data(sub2), "ld", 2) == 0);
        printTestResult("u8str_substr edge case", edgePassed, "ld", edgePassed ? "ld" : "incorrect");
        totalTests++;
        if (edgePassed) passedTests++;
        
        u8str_destroy(str);
        if (sub) { u8str_destroy(sub); }
        if (sub2) { u8str_destroy(sub2); }
    }
    
    // Test 9: u8str_compare
    {
        u8_string* str1 = u8str_init((u8*)"ABC");
        u8_string* str2 = u8str_init((u8*)"ABC");
        u8_string* str3 = u8str_init((u8*)"ABD");
        u8_string* str4 = u8str_init((u8*)"AB");
        
        bool equalPassed = (u8str_compare(str1, str2) == 0);
        printTestResult("u8str_compare equal", equalPassed, "0", equalPassed ? "0" : "non-zero");
        totalTests++;
        if (equalPassed) passedTests++;
        
        bool lessPassed = (u8str_compare(str1, str3) < 0);
        printTestResult("u8str_compare less", lessPassed, "<0", lessPassed ? "<0" : ">=0");
        totalTests++;
        if (lessPassed) passedTests++;
        
        bool greaterPassed = (u8str_compare(str1, str4) > 0);
        printTestResult("u8str_compare greater", greaterPassed, ">0", greaterPassed ? ">0" : "<=0");
        totalTests++;
        if (greaterPassed) passedTests++;
        
        u8str_destroy(str1);
        u8str_destroy(str2);
        u8str_destroy(str3);
        u8str_destroy(str4);
    }
    
    // Test 10: u8str_indexOf
    {
        u8_string* str = u8str_init((u8*)"Hello World");
        i32 idx1 = u8str_indexOf(str, 'W');
        i32 idx2 = u8str_indexOf(str, 'X');
        bool passed = (idx1 == 6 && idx2 == -1);
        printTestResult("u8str_indexOf", passed, "6,-1", passed ? "6,-1" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u8str_destroy(str);
    }
    
    // Test 11: u8str_concat
    {
        u8_string* dest = u8str_init((u8*)"Hello");
        u8_string* str1 = u8str_init((u8*)" ");
        u8_string* str2 = u8str_init((u8*)"World");
        u8_string* strs[2] = {str1, str2};
        
        u8str_concat(dest, 2, strs);
        bool passed = (u8str_length(dest) == 11 && memcmp(u8str_data(dest), "Hello World", 11) == 0);
        printTestResult("u8str_concat", passed, "Hello World", passed ? "Hello World" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        u8str_destroy(dest);
        u8str_destroy(str1);
        u8str_destroy(str2);
    }
    
    printf("\nU8 String Tests Summary: %d/%d passed (%.1f%%)\n", 
           passedTests, totalTests, (passedTests * 100.0) / totalTests);
}

void RunUnitTestsStrU16() {
    printf("\n=== U16 String Unit Tests ===\n");
    int totalTests = 0;
    int passedTests = 0;
    
    // Test 1: u16str_length
    {
        u16 data[] = {'H', 'e', 'l', 'l', 'o'};
        u16_string* str = u16str_initEx(data, 5);
        u32 length = u16str_length(str);
        bool passed = (length == 5);
        printTestResult("u16str_length basic", passed, "5", passed ? "5" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(str);
    }
    
    // Test 2: u16str_get and u16str_set with surrogate pairs
    {
        u16 data[] = {0xD800, 0xDC00, 'A'}; // Surrogate pair + 'A'
        u16_string* str = u16str_initEx(data, 3);
        u16 high = u16str_get(str, 0);
        u16 low = u16str_get(str, 1);
        bool passed = (high == 0xD800 && low == 0xDC00);
        printTestResult("u16str_get surrogate pair", passed, "0xD800,0xDC00", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        // Test set
        u16str_set(str, 2, 'B');
        u16 modified = u16str_get(str, 2);
        bool setPassed = (modified == 'B');
        printTestResult("u16str_set", setPassed, "B", setPassed ? "B" : "incorrect");
        totalTests++;
        if (setPassed) passedTests++;
        
        u16str_destroy(str);
    }
    
    // Test 3: u16str_clear
    {
        u16 data[] = {'T', 'e', 's', 't'};
        u16_string* str = u16str_initEx(data, 4);
        u16str_clear(str);
        bool passed = (u16str_length(str) == 0 && u16str_data(str)[0] == 0);
        printTestResult("u16str_clear", passed, "length=0", passed ? "length=0" : "length!=0");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(str);
    }
    
    // Test 4: u16str_remove with surrogate pairs
    {
        u16 data[] = {'A', 0xD800, 0xDC00, 'B', 'C'}; // A + surrogate pair + BC
        u16_string* str = u16str_initEx(data, 5);
        u16str_remove(str, 1, 2); // Remove surrogate pair
        bool passed = (u16str_length(str) == 3 && u16str_data(str)[0] == 'A' && u16str_data(str)[1] == 'B' && u16str_data(str)[2] == 'C');
        printTestResult("u16str_remove surrogate", passed, "ABC", passed ? "ABC" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(str);
    }
    
    // Test 5: u16str_insert
    {
        u16 destData[] = {'A', 'C'};
        u16 srcData[] = {'B'};
        u16_string* dest = u16str_initEx(destData, 2);
        u16_string* src = u16str_initEx(srcData, 1);
        u16str_insert(dest, src, 1, 0, 1);
        bool passed = (u16str_length(dest) == 3 && u16str_data(dest)[0] == 'A' && u16str_data(dest)[1] == 'B' && u16str_data(dest)[2] == 'C');
        printTestResult("u16str_insert", passed, "ABC", passed ? "ABC" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(dest);
        u16str_destroy(src);
    }
    
    // Test 6: u16str_substr with mixed content
    {
        u16 data[] = {'H', 'e', 0xD800, 0xDC00, 'l', 'o'}; // He[emoji]lo
        u16_string* str = u16str_initEx(data, 6);
        u16_string* sub = u16str_substr(str, 2, 4); // Extract surrogate pair + "lo"
        bool passed = (sub && u16str_length(sub) == 4 && u16str_data(sub)[0] == 0xD800 && u16str_data(sub)[1] == 0xDC00);
        printTestResult("u16str_substr with surrogate", passed, "surrogate+lo", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(str);
        if (sub) { u16str_destroy(sub); }
    }
    
    // Test 7: u16str_compare
    {
        u16 data1[] = {'A', 'B', 'C'};
        u16 data2[] = {'A', 'B', 'C'};
        u16 data3[] = {'A', 'B', 'D'};
        u16_string* str1 = u16str_initEx(data1, 3);
        u16_string* str2 = u16str_initEx(data2, 3);
        u16_string* str3 = u16str_initEx(data3, 3);
        
        bool equalPassed = (u16str_compare(str1, str2) == 0);
        bool lessPassed = (u16str_compare(str1, str3) < 0);
        printTestResult("u16str_compare", equalPassed && lessPassed, "equal&less", 
                       (equalPassed && lessPassed) ? "correct" : "incorrect");
        totalTests++;
        if (equalPassed && lessPassed) passedTests++;
        
        u16str_destroy(str1);
        u16str_destroy(str2);
        u16str_destroy(str3);
    }
    
    // Test 8: u16str_indexOf with surrogate
    {
        u16 data[] = {'A', 0xD800, 0xDC00, 'B'};
        u16_string* str = u16str_initEx(data, 4);
        i32 idx1 = u16str_indexOf(str, 0xD800); // Find high surrogate
        i32 idx2 = u16str_indexOf(str, 'B');
        bool passed = (idx1 == 1 && idx2 == 3);
        printTestResult("u16str_indexOf", passed, "1,3", passed ? "1,3" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(str);
    }
    
    // Test 9: u16str_concat
    {
        u16 data1[] = {'H', 'i'};
        u16_string* dest = u16str_initEx(data1, 2);
        u16 space[] = {' ', 0};
        u16_string* str1 = u16str_init(space);
        u16 emoji[] = {0xD83D, 0xDE00, 0};
        u16_string* str2 = u16str_init(emoji);
        u16_string* strs[2] = {str1, str2};
        
        u16str_concat(dest, 2, strs);
        bool passed = (u16str_length(dest) == 5 && u16str_data(dest)[3] == 0xD83D && u16str_data(dest)[4] == 0xDE00);
        printTestResult("u16str_concat with emoji", passed, "Hi 😀", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        u16str_destroy(dest);
        u16str_destroy(str1);
        u16str_destroy(str2);
    }
    
    printf("\nU16 String Tests Summary: %d/%d passed (%.1f%%)\n", 
           passedTests, totalTests, (passedTests * 100.0) / totalTests);
}

void RunUnitTestsStrU32() {
    printf("\n=== U32 String Unit Tests ===\n");
    int totalTests = 0;
    int passedTests = 0;
    
    // Test 1: u32str_length
    {
        u32 data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
        u32_string* str = u32str_initEx(data, 5);
        u32 length = u32str_length(str);
        bool passed = (length == 5);
        printTestResult("u32str_length basic", passed, "5", passed ? "5" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(str);
    }
    
    // Test 2: u32str_get/set with Unicode beyond BMP
    {
        u32 data[] = {0x41, 0x1F600, 0x42}; // A + emoji + B
        u32_string* str = u32str_initEx(data, 3);
        u32 emoji = u32str_get(str, 1);
        bool getPassed = (emoji == 0x1F600);
        printTestResult("u32str_get emoji", getPassed, "0x1F600", getPassed ? "0x1F600" : "incorrect");
        totalTests++;
        if (getPassed) passedTests++;
        
        u32str_set(str, 1, 0x1F601); // Different emoji
        u32 newEmoji = u32str_get(str, 1);
        bool setPassed = (newEmoji == 0x1F601);
        printTestResult("u32str_set emoji", setPassed, "0x1F601", setPassed ? "0x1F601" : "incorrect");
        totalTests++;
        if (setPassed) passedTests++;
        
        u32str_destroy(str);
    }
    
    // Test 3: u32str_getChar/setChar with ASCII
    {
        u32 data[] = {0x41, 0x42, 0x43}; // "ABC"
        u32_string* str = u32str_initEx(data, 3);
        char ch = u32str_getChar(str, 1);
        bool getPassed = (ch == 'B');
        printTestResult("u32str_getChar", getPassed, "B", getPassed ? "B" : "incorrect");
        totalTests++;
        if (getPassed) passedTests++;
        
        // Test non-ASCII returns null
        u32 data2[] = {0x1F600}; // Emoji
        u32_string* str2 = u32str_initEx(data2, 1);
        char nonAscii = u32str_getChar(str2, 0);
        bool nonAsciiPassed = (nonAscii == '\0');
        printTestResult("u32str_getChar non-ASCII", nonAsciiPassed, "\\0", nonAsciiPassed ? "\\0" : "non-null");
        totalTests++;
        if (nonAsciiPassed) passedTests++;
        
        u32str_destroy(str);
        u32str_destroy(str2);
    }
    
    // Test 4: u32str_remove with mixed content
    {
        u32 data[] = {0x41, 0x1F600, 0x1F601, 0x42, 0x43}; // A + 2 emojis + BC
        u32_string* str = u32str_initEx(data, 5);
        u32str_remove(str, 1, 2); // Remove both emojis
        bool passed = (u32str_length(str) == 3 && u32str_data(str)[0] == 0x41 && u32str_data(str)[1] == 0x42);
        printTestResult("u32str_remove emojis", passed, "ABC", passed ? "ABC" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(str);
    }
    
    // Test 5: u32str_insert with Unicode
    {
        u32 destData[] = {0x41, 0x43}; // AC
        u32 srcData[] = {0x1F600, 0x42}; // emoji + B
        u32_string* dest = u32str_initEx(destData, 2);
        u32_string* src = u32str_initEx(srcData, 2);
        u32str_insert(dest, src, 1, 0, 2);
        bool passed = (u32str_length(dest) == 4 && u32str_data(dest)[1] == 0x1F600 && u32str_data(dest)[2] == 0x42);
        printTestResult("u32str_insert with emoji", passed, "A😀BC", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(dest);
        u32str_destroy(src);
    }
    
    // Test 6: u32str_substr
    {
        u32 data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x1F600, 0x57, 0x6F, 0x72, 0x6C, 0x64}; // Hello😀World
        u32_string* str = u32str_initEx(data, 11);
        u32_string* sub = u32str_substr(str, 5, 6); // Extract emoji + World
        bool passed = (sub && u32str_length(sub) == 6 && u32str_data(sub)[0] == 0x1F600);
        printTestResult("u32str_substr with emoji", passed, "😀World", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(str);
        if (sub) { u32str_destroy(sub); }
    }
    
    // Test 7: u32str_compare with Unicode
    {
        u32 data1[] = {0x41, 0x1F600};
        u32 data2[] = {0x41, 0x1F601};
        u32_string* str1 = u32str_initEx(data1, 2);
        u32_string* str2 = u32str_initEx(data2, 2);
        bool passed = (u32str_compare(str1, str2) < 0); // 0x1F600 < 0x1F601
        printTestResult("u32str_compare Unicode", passed, "<0", passed ? "<0" : ">=0");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(str1);
        u32str_destroy(str2);
    }
    
    // Test 8: u32str_indexOf with Unicode
    {
        u32 data[] = {0x41, 0x42, 0x1F600, 0x43}; // AB😀C
        u32_string* str = u32str_initEx(data, 4);
        i32 idx = u32str_indexOf(str, 0x1F600);
        bool passed = (idx == 2);
        printTestResult("u32str_indexOf emoji", passed, "2", passed ? "2" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(str);
    }
    
    // Test 9: u32str_concat with mixed content
    {
        u32 data1[] = {0x48, 0x69}; // Hi
        u32_string* dest = u32str_initEx(data1, 2);
        u32 space[] = {0x20, 0};
        u32_string* str1 = u32str_init(space);
        u32 emoji[] = {0x1F44B, 0};
        u32_string* str2 = u32str_init(emoji);
        u32_string* strs[2] = {str1, str2};
        
        u32str_concat(dest, 2, strs);
        bool passed = (u32str_length(dest) == 4 && u32str_data(dest)[3] == 0x1F44B);
        printTestResult("u32str_concat with emoji", passed, "Hi 👋", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        
        u32str_destroy(dest);
        u32str_destroy(str1);
        u32str_destroy(str2);
    }
    
    printf("\nU32 String Tests Summary: %d/%d passed (%.1f%%)\n", 
           passedTests, totalTests, (passedTests * 100.0) / totalTests);
}

void RunUnitTestsStrConvert() {
    printf("\n=== String Conversion Unit Tests ===\n");
    int totalTests = 0;
    int passedTests = 0;
    
    // Test 1: u32 to u16 conversion with BMP characters
    {
        u32 data[] = {0x41, 0x42, 0x43}; // ABC
        u32_string* u32str = u32str_initEx(data, 3);
        u16_string* u16str = u32str_to_u16str(u32str);
        bool passed = (u16str && u16str_length(u16str) == 3 && 
                      u16str_data(u16str)[0] == 0x41 && u16str_data(u16str)[1] == 0x42 && u16str_data(u16str)[2] == 0x43);
        printTestResult("u32_to_u16 BMP", passed, "ABC", passed ? "ABC" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(u32str);
        if (u16str) { u16str_destroy(u16str); }
    }
    
    // Test 2: u32 to u16 conversion with surrogate pairs
    {
        u32 data[] = {0x41, 0x1F600, 0x42}; // A + grinning face + B
        u32_string* u32str = u32str_initEx(data, 3);
        u16_string* u16str = u32str_to_u16str(u32str);
        bool passed = (u16str && u16str_length(u16str) == 4 && 
                      u16str_data(u16str)[0] == 0x41 && 
                      u16str_data(u16str)[1] == 0xD83D && u16str_data(u16str)[2] == 0xDE00 && // Surrogate pair
                      u16str_data(u16str)[3] == 0x42);
        printTestResult("u32_to_u16 surrogate", passed, "A😀B", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(u32str);
        if (u16str) { u16str_destroy(u16str); }
    }
    
    // Test 3: u32 to u8 conversion (UTF-8)
    {
        u32 data[] = {0x41, 0x00E9, 0x1F600}; // A + é + emoji
        u32_string* u32str = u32str_initEx(data, 3);
        u8_string* u8str = u32str_to_u8str(u32str);
        bool passed = (u8str && u8str_length(u8str) == 7 && // A(1) + é(2) + emoji(4) = 7
                      u8str_data(u8str)[0] == 0x41 && // A
                      u8str_data(u8str)[1] == 0xC3 && u8str_data(u8str)[2] == 0xA9 && // é in UTF-8
                      u8str_data(u8str)[3] == 0xF0 && u8str_data(u8str)[4] == 0x9F && 
                      u8str_data(u8str)[5] == 0x98 && u8str_data(u8str)[6] == 0x80); // emoji in UTF-8
        printTestResult("u32_to_u8 mixed", passed, "Aé😀", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u32str_destroy(u32str);
        if (u8str) { u8str_destroy(u8str); }
    }
    
    // Test 4: u16 to u32 conversion with surrogates
    {
        u16 data[] = {0x41, 0xD83D, 0xDE00, 0x42}; // A + surrogate pair + B
        u16_string* u16str = u16str_initEx(data, 4);
        u32_string* u32str = u16str_to_u32str(u16str);
        bool passed = (u32str && u32str_length(u32str) == 3 &&
                      u32str_data(u32str)[0] == 0x41 &&
                      u32str_data(u32str)[1] == 0x1F600 && // Decoded emoji
                      u32str_data(u32str)[2] == 0x42);
        printTestResult("u16_to_u32 surrogate", passed, "A😀B", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(u16str);
        if (u32str) { u32str_destroy(u32str); }
    }
    
    // Test 5: u16 to u8 conversion
    {
        u16 data[] = {0x41, 0x00E9, 0xD83D, 0xDE00}; // A + é + emoji surrogate
        u16_string* u16str = u16str_initEx(data, 4);
        u8_string* u8str = u16strto_u8str(u16str);
        bool passed = (u8str && u8str_length(u8str) == 7); // A(1) + é(2) + emoji(4)
        printTestResult("u16_to_u8", passed, "7 bytes", passed ? "7 bytes" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(u16str);
        if (u8str) { u8str_destroy(u8str); }
    }
    
    // Test 6: u8 to u32 conversion
    {
        // Create UTF-8 string "Aé😀"
        u8 data[] = {0x41, 0xC3, 0xA9, 0xF0, 0x9F, 0x98, 0x80, 0};
        u8_string* u8str = u8str_init(data);
        
        u32_string* u32str = u8str_to_u32str(u8str);
        bool passed = (u32str && u32str_length(u32str) == 3 &&
                      u32str_data(u32str)[0] == 0x41 &&
                      u32str_data(u32str)[1] == 0x00E9 &&
                      u32str_data(u32str)[2] == 0x1F600);
        printTestResult("u8_to_u32", passed, "Aé😀", passed ? "correct" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u8str_destroy(u8str);
        if (u32str) { u32str_destroy(u32str); }
    }
    
    // Test 7: u8 to u16 conversion
    {
        u8_string* u8str = u8str_init((u8*)"Hello");
        u16_string* u16str = u8str_to_u16str(u8str);
        bool passed = (u16str && u16str_length(u16str) == 5 &&
                      u16str_data(u16str)[0] == 'H' && u16str_data(u16str)[4] == 'o');
        printTestResult("u8_to_u16 ASCII", passed, "Hello", passed ? "Hello" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u8str_destroy(u8str);
        if (u16str) { u16str_destroy(u16str); }
    }
    
    // Test 8: Invalid UTF-8 handling
    {
        // Create invalid UTF-8 string "A[invalid]B"
        u8 data[] = {0x41, 0xFF, 0x42, 0};
        u8_string* u8str = u8str_init(data);
        
        u32_string* u32str = u8str_to_u32str(u8str);
        bool passed = (u32str && u32str_length(u32str) == 3 &&
                      u32str_data(u32str)[0] == 0x41 &&
                      u32str_data(u32str)[1] == 0xFFFD && // Replacement character
                      u32str_data(u32str)[2] == 0x42);
        printTestResult("Invalid UTF-8", passed, "A�B", passed ? "A�B" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u8str_destroy(u8str);
        if (u32str) { u32str_destroy(u32str); }
    }
    
    // Test 9: Lone surrogate handling
    {
        u16 data[] = {0x41, 0xD800, 0x42}; // A + lone high surrogate + B
        u16_string* u16str = u16str_initEx(data, 3);
        u32_string* u32str = u16str_to_u32str(u16str);
        bool passed = (u32str && u32str_length(u32str) == 3 &&
                      u32str_data(u32str)[0] == 0x41 &&
                      u32str_data(u32str)[1] == 0xFFFD && // Replacement for invalid surrogate
                      u32str_data(u32str)[2] == 0x42);
        printTestResult("Lone surrogate", passed, "A�B", passed ? "A�B" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        u16str_destroy(u16str);
        if (u32str) { u32str_destroy(u32str); }
    }
    
    // Test 10: Round-trip conversion
    {
        u32 data[] = {0x41, 0x00E9, 0x1F600, 0x42}; // A + é + emoji + B
        u32_string* original = u32str_initEx(data, 4);
        
        // Convert u32 -> u8 -> u32
        u8_string* u8temp = u32str_to_u8str(original);
        u32_string* roundtrip = u8str_to_u32str(u8temp);
        
        bool passed = (u32str_compare(original, roundtrip) == 0);
        printTestResult("Round-trip u32->u8->u32", passed, "identical", passed ? "identical" : "different");
        totalTests++;
        if (passed) passedTests++;
        
        u32str_destroy(original);
        if (u8temp) { u8str_destroy(u8temp); }
        if (roundtrip) { u32str_destroy(roundtrip); }
    }
    
    printf("\nConversion Tests Summary: %d/%d passed (%.1f%%)\n", 
           passedTests, totalTests, (passedTests * 100.0) / totalTests);
}

void RunUnitTestsStrInit() {
    printf("\n=== String Initialization Unit Tests ===\n");
    int totalTests = 0;
    int passedTests = 0;
    
    // Test 1: u32str_create - empty string
    {
        u32_string* str = u32str_create();
        bool passed = (str && u32str_length(str) == 0 && u32str_capacity(str) > 0 && 
                      u32str_data(str) && u32str_data(str)[0] == 0);
        printTestResult("u32str_create empty", passed, "empty string", passed ? "empty string" : "failed");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u32str_destroy(str); }
    }
    
    // Test 2: u32str_init with null
    {
        u32_string* str = u32str_init(NULL);
        bool passed = (str && u32str_length(str) == 0);
        printTestResult("u32str_init NULL", passed, "empty string", passed ? "empty string" : "failed");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u32str_destroy(str); }
    }
    
    // Test 3: u32str_init with data
    {
        u32 data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0}; // "Hello\0"
        u32_string* str = u32str_init(data);
        bool passed = (str && u32str_length(str) == 5 && 
                      memcmp(u32str_data(str), data, 5 * sizeof(u32)) == 0);
        printTestResult("u32str_init with data", passed, "Hello", passed ? "Hello" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u32str_destroy(str); }
    }
    
    // Test 4: u32str_initEx with data
    {
        u32 data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello" without null
        u32_string* str = u32str_initEx(data, 5);
        bool passed = (str && u32str_length(str) == 5 && 
                      memcmp(u32str_data(str), data, 5 * sizeof(u32)) == 0 &&
                      u32str_data(str)[5] == 0); // Check null termination
        printTestResult("u32str_initEx", passed, "Hello", passed ? "Hello" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u32str_destroy(str); }
    }
    
    // Test 5: u32str_initEx with NULL data
    {
        u32_string* str = u32str_initEx(NULL, 10);
        bool passed = (str && u32str_length(str) == 10 && u32str_data(str)[10] == 0);
        printTestResult("u32str_initEx NULL data", passed, "allocated buffer", 
                       passed ? "allocated buffer" : "failed");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u32str_destroy(str); }
    }
    
    // Test 6: u16str_create
    {
        u16_string* str = u16str_create();
        bool passed = (str && u16str_length(str) == 0 && u16str_capacity(str) > 0 && 
                      u16str_data(str) && u16str_data(str)[0] == 0);
        printTestResult("u16str_create empty", passed, "empty string", passed ? "empty string" : "failed");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u16str_destroy(str); }
    }
    
    // Test 7: u16str_init with UTF-16 data including surrogate
    {
        u16 data[] = {0x48, 0x69, 0xD83D, 0xDE00, 0}; // "Hi😀\0"
        u16_string* str = u16str_init(data);
        bool passed = (str && u16str_length(str) == 4 && 
                      memcmp(u16str_data(str), data, 4 * sizeof(u16)) == 0);
        printTestResult("u16str_init with surrogate", passed, "Hi😀", passed ? "Hi😀" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u16str_destroy(str); }
    }
    
    // Test 8: u16str_initEx
    {
        u16 data[] = {0x41, 0x42, 0x43}; // "ABC"
        u16_string* str = u16str_initEx(data, 3);
        bool passed = (str && u16str_length(str) == 3 && 
                      u16str_data(str)[0] == 0x41 && u16str_data(str)[2] == 0x43 &&
                      u16str_data(str)[3] == 0);
        printTestResult("u16str_initEx", passed, "ABC", passed ? "ABC" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u16str_destroy(str); }
    }
    
    // Test 9: u8str_create
    {
        u8_string* str = u8str_create();
        bool passed = (str && u8str_length(str) == 0 && u8str_capacity(str) > 0 && 
                      u8str_data(str) && u8str_data(str)[0] == 0);
        printTestResult("u8str_create empty", passed, "empty string", passed ? "empty string" : "failed");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u8str_destroy(str); }
    }
    
    // Test 10: u8str_init with ASCII
    {
        u8 data[] = "Hello World";
        u8_string* str = u8str_init(data);
        bool passed = (str && u8str_length(str) == 11 && 
                      memcmp(u8str_data(str), data, 11) == 0);
        printTestResult("u8str_init ASCII", passed, "Hello World", passed ? "Hello World" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u8str_destroy(str); }
    }
    
    // Test 11: u8str_init with UTF-8
    {
        u8 data[] = {0x48, 0xC3, 0xA9, 0x6C, 0x6C, 0xC3, 0xB6, 0}; // "Héllö"
        u8_string* str = u8str_init(data);
        bool passed = (str && u8str_length(str) == 7 && 
                      memcmp(u8str_data(str), data, 7) == 0);
        printTestResult("u8str_init UTF-8", passed, "Héllö", passed ? "Héllö" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u8str_destroy(str); }
    }
    
    // Test 12: u8str_initEx with partial UTF-8
    {
        u8 data[] = {0xF0, 0x9F, 0x98, 0x80, 0x41}; // 😀A
        u8_string* str = u8str_initEx(data, 5);
        bool passed = (str && u8str_length(str) == 5 && 
                      memcmp(u8str_data(str), data, 5) == 0 &&
                      u8str_data(str)[5] == 0);
        printTestResult("u8str_initEx UTF-8", passed, "😀A", passed ? "😀A" : "incorrect");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u8str_destroy(str); }
    }
    
    // Test 13: Capacity check
    {
        u32 data[] = {0x41, 0x42, 0}; // "AB"
        u32_string* str = u32str_init(data);
        bool passed = (str && u32str_capacity(str) >= u32str_length(str) && 
                      u32str_capacity(str) > 2); // Should have extra capacity
        printTestResult("Capacity > length", passed, "has extra capacity", 
                       passed ? "has extra capacity" : "no extra capacity");
        totalTests++;
        if (passed) passedTests++;
        if (str) { u32str_destroy(str); }
    }
    
    printf("\nInitialization Tests Summary: %d/%d passed (%.1f%%)\n", 
           passedTests, totalTests, (passedTests * 100.0) / totalTests);
}

// Main function to run all tests
int main() {
    printf("=== String Library Unit Tests ===\n");
    
    RunUnitTestsStrU8();
    RunUnitTestsStrU16();
    RunUnitTestsStrU32();
    RunUnitTestsStrConvert();
    RunUnitTestsStrInit();
    
    printf("\n=== All Tests Complete ===\n");
    return 0;
}