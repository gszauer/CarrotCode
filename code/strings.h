#ifndef _H_STRINGS_CARROT_
#define _H_STRINGS_CARROT_ 

#include "types.h"

// String types are forward declared in types.h
// Their internal structure is private and defined in strings.cpp

// Conversion functions, always returns a new string
u16_string* u32str_to_u16str(u32_string* target);
u8_string*  u32str_to_u8str(u32_string* target);

u32_string* u16str_to_u32str(u16_string* target);
u8_string*  u16strto_u8str(u16_string* target);

u32_string* u8str_to_u32str(u8_string* target);
u16_string* u8str_to_u16str(u8_string* target);

// u32_string functions
u32_string* u32str_create(); // Empty string
u32_string* u32str_init(u32* data); // Expected data to be null terminated
void u32str_destroy(u32_string* str);

u32 u32str_get(u32_string* target, u32 index);
void u32str_set(u32_string* target, u32 index, u32 value);

char u32str_getChar(u32_string* target, u32 index);
void u32str_setChar(u32_string* target, u32 index, char value);

void u32str_clear(u32_string* target);
void u32str_reserve(u32_string* target, u32 minSize);

void u32str_remove(u32_string* target, u32 startIndex, u32 length);
void u32str_insert(u32_string* target, const u32_string* source, u32 targetStart, u32 sourceStart, u32 length);
u32_string* u32str_substr(u32_string* target, u32 startIndex, u32 length);
i32 u32str_compare(u32_string* a, u32_string* b); // like strcmp
i32 u32str_indexOf(u32_string* target, u32 character);

u32 u32str_length(u32_string* target); // Length in code points
void u32str_concat(u32_string* target, u32 numStrings, u32_string** stringArray); // implement using insert

// u16 string functions
u16_string* u16str_create(); // Empty string
u16_string* u16str_init(u16* data); // Expected data to be null terminated
void u16str_destroy(u16_string* str);

u16 u16str_get(u16_string* target, u32 index);
void u16str_set(u16_string* target, u32 index, u16 value);

char u16str_getChar(u16_string* target, u32 index);
void u16str_setChar(u16_string* target, u32 index, char value);
void u16str_clear(u16_string* target);

void u16str_reserve(u16_string* target, u32 minSize);
void u16str_grow(u16_string* target, u32 minSize);

void u16str_remove(u16_string* target, u32 startIndex, u32 length);
void u16str_insert(u16_string* target, const u16_string* source, u32 targetStart, u32 sourceStart, u32 length);
u16_string* u16str_substr(u16_string* target, u32 startIndex, u32 length);
i32 u16str_compare(u16_string* a, u16_string* b);
i32 u16str_indexOf(u16_string* target, u16 character);

u32 u16str_length(u16_string* target);
void u16str_concat(u16_string* target, u32 numStrings, u16_string** stringArray); // implement using insert

// u8 string functions
u8_string* u8str_create(); // Empty string
u8_string* u8str_init(u8* data); // Expected data to be null terminated
void u8str_destroy(u8_string* str);

u8 u8str_get(u8_string* target, u32 index);
void u8str_set(u8_string* target, u32 index, u8 value);

char u8str_getChar(u8_string* target, u32 index);
void u8str_setChar(u8_string* target, u32 index, char value);
void u8str_clear(u8_string* target);

void u8str_reserve(u8_string* target, u32 minSize);
void u8str_grow(u8_string* target, u32 minSize);

void u8str_remove(u8_string* target, u32 startIndex, u32 length);
void u8str_insert(u8_string* target, const u8_string* source, u32 targetStart, u32 sourceStart, u32 length);
u8_string* u8str_substr(u8_string* target, u32 startIndex, u32 length);
i32 u8str_compare(u8_string* a, u8_string* b);
i32 u8str_indexOf(u8_string* target, u8 character);

u32 u8str_length(u8_string* target);
void u8str_concat(u8_string* target, u32 numStrings, u8_string** stringArray); // implement using insert

#endif