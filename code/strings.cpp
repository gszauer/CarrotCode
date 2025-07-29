#include "strings.h"
#include <stdlib.h>
#include <string.h>

// Private structure definitions
struct u8_string {
    u8* str; // Null terminated, utf8 string
    u32 length;
    u32 capacity;
};

struct u16_string {
    u16* str; // Null terminated utf 16 string
    u32 length;
    u32 capacity;
};

struct u32_string {
    u32* str; // Null terminated utf 32 string
    u32 length;
    u32 capacity;
};

// Conversion functions
u16_string* u32str_to_u16str(u32_string* target) {
    if (!target || !target->str) {
        return NULL;
    }
    
    // Calculate required size (worst case is 2 u16s per u32 for surrogate pairs)
    u32 requiredSize = target->length * 2;
    
    u16_string* result = (u16_string*)malloc(sizeof(u16_string));
    result->length = 0;
    result->capacity = requiredSize;
    result->str = (u16*)malloc((requiredSize + 1) * sizeof(u16));
    
    for (u32 i = 0; i < target->length; i++) {
        u32 codepoint = target->str[i];
        if (codepoint <= 0xD7FF || (codepoint >= 0xE000 && codepoint <= 0xFFFF)) {
            // BMP character (excluding surrogates)
            result->str[result->length++] = (u16)codepoint;
        } else if (codepoint >= 0x10000 && codepoint <= 0x10FFFF) {
            // Convert to surrogate pair
            codepoint -= 0x10000;
            u16 high = (u16)((codepoint >> 10) + 0xD800);
            u16 low = (u16)((codepoint & 0x3FF) + 0xDC00);
            result->str[result->length++] = high;
            result->str[result->length++] = low;
        } else {
            // Invalid codepoint, replace with replacement character
            result->str[result->length++] = 0xFFFD;
        }
    }
    
    result->str[result->length] = 0;
    return result;
}

u8_string* u32str_to_u8str(u32_string* target) {
    if (!target || !target->str) {
        return NULL;
    }
    
    // Calculate required size (worst case is 4 bytes per character)
    u32 requiredSize = target->length * 4;
    
    u8_string* result = (u8_string*)malloc(sizeof(u8_string));
    result->length = 0;
    result->capacity = requiredSize;
    result->str = (u8*)malloc((requiredSize + 1) * sizeof(u8));
    
    for (u32 i = 0; i < target->length; i++) {
        u32 codepoint = target->str[i];
        
        if (codepoint <= 0x7F) {
            // 1-byte UTF-8
            result->str[result->length++] = (u8)codepoint;
        } else if (codepoint <= 0x7FF) {
            // 2-byte UTF-8
            result->str[result->length++] = (u8)(0xC0 | (codepoint >> 6));
            result->str[result->length++] = (u8)(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
            // 3-byte UTF-8
            result->str[result->length++] = (u8)(0xE0 | (codepoint >> 12));
            result->str[result->length++] = (u8)(0x80 | ((codepoint >> 6) & 0x3F));
            result->str[result->length++] = (u8)(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0x10FFFF) {
            // 4-byte UTF-8
            result->str[result->length++] = (u8)(0xF0 | (codepoint >> 18));
            result->str[result->length++] = (u8)(0x80 | ((codepoint >> 12) & 0x3F));
            result->str[result->length++] = (u8)(0x80 | ((codepoint >> 6) & 0x3F));
            result->str[result->length++] = (u8)(0x80 | (codepoint & 0x3F));
        } else {
            // Invalid codepoint, use replacement character
            result->str[result->length++] = 0xEF;
            result->str[result->length++] = 0xBF;
            result->str[result->length++] = 0xBD;
        }
    }
    
    result->str[result->length] = 0;
    return result;
}

u32_string* u16str_to_u32str(u16_string* target) {
    if (!target || !target->str) {
        return NULL;
    }
    
    u32_string* result = (u32_string*)malloc(sizeof(u32_string));
    result->length = 0;
    result->capacity = target->length;
    result->str = (u32*)malloc((target->length + 1) * sizeof(u32));
    
    for (u32 i = 0; i < target->length; i++) {
        u16 ch = target->str[i];
        
        // Check for high surrogate
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            if (i + 1 < target->length) {
                u16 low = target->str[i + 1];
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    // Valid surrogate pair
                    u32 codepoint = ((ch - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                    result->str[result->length++] = codepoint;
                    i++; // Skip the low surrogate
                    continue;
                }
            }
            // Invalid surrogate, use replacement character
            result->str[result->length++] = 0xFFFD;
        } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
            // Lone low surrogate, invalid
            result->str[result->length++] = 0xFFFD;
        } else {
            // Regular BMP character
            result->str[result->length++] = (u32)ch;
        }
    }
    
    result->str[result->length] = 0;
    return result;
}

u8_string* u16strto_u8str(u16_string* target) {
    if (!target || !target->str) {
        return NULL;
    }
    
    // First convert to u32, then to u8
    u32_string* temp = u16str_to_u32str(target);
    if (!temp) {
        return NULL;
    }
    
    u8_string* result = u32str_to_u8str(temp);
    
    // Clean up temporary u32 string
    free(temp->str);
    free(temp);
    
    return result;
}

u32_string* u8str_to_u32str(u8_string* target) {
    if (!target || !target->str) {
        return NULL;
    }
    
    u32_string* result = (u32_string*)malloc(sizeof(u32_string));
    result->length = 0;
    result->capacity = target->length; // Worst case: all ASCII
    result->str = (u32*)malloc((target->length + 1) * sizeof(u32));
    
    u32 i = 0;
    while (i < target->length) {
        u8 ch = target->str[i];
        u32 codepoint = 0;
        u32 bytesToRead = 0;
        
        if ((ch & 0x80) == 0) {
            // 1-byte character (ASCII)
            codepoint = ch;
            bytesToRead = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            // 2-byte character
            codepoint = ch & 0x1F;
            bytesToRead = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            // 3-byte character
            codepoint = ch & 0x0F;
            bytesToRead = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            // 4-byte character
            codepoint = ch & 0x07;
            bytesToRead = 4;
        } else {
            // Invalid UTF-8 start byte
            result->str[result->length++] = 0xFFFD;
            i++;
            continue;
        }
        
        // Read continuation bytes
        u32 j;
        for (j = 1; j < bytesToRead && i + j < target->length; j++) {
            u8 cont = target->str[i + j];
            if ((cont & 0xC0) != 0x80) {
                // Invalid continuation byte
                break;
            }
            codepoint = (codepoint << 6) | (cont & 0x3F);
        }
        
        if (j == bytesToRead) {
            // Valid sequence
            result->str[result->length++] = codepoint;
            i += bytesToRead;
        } else {
            // Invalid sequence
            result->str[result->length++] = 0xFFFD;
            i++;
        }
    }
    
    result->str[result->length] = 0;
    return result;
}

u16_string* u8str_to_u16str(u8_string* target) {
    if (!target || !target->str) {
        return NULL;
    }
    
    // First convert to u32, then to u16
    u32_string* temp = u8str_to_u32str(target);
    if (!temp) {
        return NULL;
    }
    
    u16_string* result = u32str_to_u16str(temp);
    
    // Clean up temporary u32 string
    free(temp->str);
    free(temp);
    
    return result;
}

// u32 string initialization functions
u32_string* u32str_create() {
    u32_string* str = (u32_string*)malloc(sizeof(u32_string));
    str->length = 0;
    str->capacity = 16; // Default initial capacity
    str->str = (u32*)malloc((str->capacity + 1) * sizeof(u32));
    str->str[0] = 0;
    return str;
}

u32_string* u32str_init(u32* data) {
    if (!data) {
        return u32str_create();
    }
    
    // Calculate length
    u32 len = 0;
    while (data[len] != 0) {
        len++;
    }
    
    u32_string* str = (u32_string*)malloc(sizeof(u32_string));
    str->length = len;
    str->capacity = len + 16; // Add some extra capacity
    str->str = (u32*)malloc((str->capacity + 1) * sizeof(u32));
    memcpy(str->str, data, len * sizeof(u32));
    str->str[len] = 0;
    return str;
}

u32_string* u32str_initEx(u32* data, u32 length) {
    u32_string* str = (u32_string*)malloc(sizeof(u32_string));
    str->length = length;
    str->capacity = length + 16; // Add some extra capacity
    str->str = (u32*)malloc((str->capacity + 1) * sizeof(u32));
    
    if (data) {
        memcpy(str->str, data, length * sizeof(u32));
    }
    str->str[length] = 0;
    return str;
}

u32 u32str_length(u32_string* target) {
    return target->length;
}

void u32str_concat(u32_string* target, u32 numStrings, u32_string** stringArray) {
    for (u32 i = 0; i < numStrings; i++) {
        u32str_insert(target, stringArray[i], target->length, 0, stringArray[i]->length);
    }
}

u32 u32str_get(u32_string* target, u32 index) {
    if (index < target->length) {
        return target->str[index];
    }
    return 0;
}

void u32str_set(u32_string* target, u32 index, u32 value) {
    if (index < target->length) {
        target->str[index] = value;
    }
}

char u32str_getChar(u32_string* target, u32 index) {
    if (index < target->length && target->str[index] < 128) {
        return (char)target->str[index];
    }
    return '\0';
}

void u32str_setChar(u32_string* target, u32 index, char value) {
    if (index < target->length) {
        target->str[index] = (u32)value;
    }
}

void u32str_reserve(u32_string* target, u32 minSize) {
    if (minSize > target->capacity) {
        u32* newStr = (u32*)malloc((minSize + 1) * sizeof(u32));
        if (target->str) {
            memcpy(newStr, target->str, (target->length + 1) * sizeof(u32));
            free(target->str);
        }
        target->str = newStr;
        target->capacity = minSize;
    }
}

void u32str_grow(u32_string* target, u32 minSize) {
    if (minSize > target->capacity) {
        u32 newCapacity = target->capacity * 2;
        if (newCapacity < minSize) {
            newCapacity = minSize;
        }
        u32str_reserve(target, newCapacity);
    }
}

void u32str_clear(u32_string* target) {
    target->length = 0;
    if (target->str) {
        target->str[0] = 0;
    }
}

void u32str_remove(u32_string* target, u32 startIndex, u32 length) {
    if (startIndex >= target->length) {
        return;
    }
    
    if (startIndex + length > target->length) {
        length = target->length - startIndex;
    }
    
    memmove(target->str + startIndex, 
            target->str + startIndex + length, 
            (target->length - startIndex - length + 1) * sizeof(u32));
    
    target->length -= length;
}

void u32str_insert(u32_string* target, const u32_string* source, u32 targetStart, u32 sourceStart, u32 length) {
    if (!source || sourceStart >= source->length || length == 0) {
        return;
    }
    
    if (targetStart > target->length) {
        targetStart = target->length;
    }
    
    if (sourceStart + length > source->length) {
        length = source->length - sourceStart;
    }
    
    u32 newLength = target->length + length;
    u32str_grow(target, newLength);
    
    memmove(target->str + targetStart + length,
            target->str + targetStart,
            (target->length - targetStart + 1) * sizeof(u32));
    
    memcpy(target->str + targetStart,
           source->str + sourceStart,
           length * sizeof(u32));
    
    target->length = newLength;
}

u32_string* u32str_substr(u32_string* target, u32 startIndex, u32 length) {
    if (startIndex >= target->length) {
        return NULL;
    }
    
    if (startIndex + length > target->length) {
        length = target->length - startIndex;
    }
    
    u32_string* result = (u32_string*)malloc(sizeof(u32_string));
    result->length = length;
    result->capacity = length;
    result->str = (u32*)malloc((length + 1) * sizeof(u32));
    
    memcpy(result->str, target->str + startIndex, length * sizeof(u32));
    result->str[length] = 0;
    
    return result;
}

i32 u32str_compare(u32_string* a, u32_string* b) {
    if (!a || !a->str) return (!b || !b->str) ? 0 : -1;
    if (!b || !b->str) return 1;
    
    u32 minLen = a->length < b->length ? a->length : b->length;
    
    for (u32 i = 0; i < minLen; i++) {
        if (a->str[i] < b->str[i]) return -1;
        if (a->str[i] > b->str[i]) return 1;
    }
    
    if (a->length < b->length) return -1;
    if (a->length > b->length) return 1;
    
    return 0;
}

i32 u32str_indexOf(u32_string* target, u32 character) {
    for (u32 i = 0; i < target->length; i++) {
        if (target->str[i] == character) {
            return (i32)i;
        }
    }
    return -1;
}

// u16 string initialization functions
u16_string* u16str_create() {
    u16_string* str = (u16_string*)malloc(sizeof(u16_string));
    str->length = 0;
    str->capacity = 16; // Default initial capacity
    str->str = (u16*)malloc((str->capacity + 1) * sizeof(u16));
    str->str[0] = 0;
    return str;
}

u16_string* u16str_init(u16* data) {
    if (!data) {
        return u16str_create();
    }
    
    // Calculate length
    u32 len = 0;
    while (data[len] != 0) {
        len++;
    }
    
    u16_string* str = (u16_string*)malloc(sizeof(u16_string));
    str->length = len;
    str->capacity = len + 16; // Add some extra capacity
    str->str = (u16*)malloc((str->capacity + 1) * sizeof(u16));
    memcpy(str->str, data, len * sizeof(u16));
    str->str[len] = 0;
    return str;
}

u16_string* u16str_initEx(u16* data, u32 length) {
    u16_string* str = (u16_string*)malloc(sizeof(u16_string));
    str->length = length;
    str->capacity = length + 16; // Add some extra capacity
    str->str = (u16*)malloc((str->capacity + 1) * sizeof(u16));
    
    if (data) {
        memcpy(str->str, data, length * sizeof(u16));
    }
    str->str[length] = 0;
    return str;
}

// u16 string functions
u32 u16str_length(u16_string* target) {
    return target->length;
}

void u16str_concat(u16_string* target, u32 numStrings, u16_string** stringArray) {
    for (u32 i = 0; i < numStrings; i++) {
        u16str_insert(target, stringArray[i], target->length, 0, stringArray[i]->length);
    }
}

u16 u16str_get(u16_string* target, u32 index) {
    if (index < target->length) {
        return target->str[index];
    }
    return 0;
}

void u16str_set(u16_string* target, u32 index, u16 value) {
    if (index < target->length) {
        target->str[index] = value;
    }
}

char u16str_getChar(u16_string* target, u32 index) {
    if (index < target->length && target->str[index] < 128) {
        return (char)target->str[index];
    }
    return '\0';
}

void u16str_setChar(u16_string* target, u32 index, char value) {
    if (index < target->length) {
        target->str[index] = (u16)value;
    }
}

void u16str_reserve(u16_string* target, u32 minSize) {
    if (minSize > target->capacity) {
        u16* newStr = (u16*)malloc((minSize + 1) * sizeof(u16));
        if (target->str) {
            memcpy(newStr, target->str, (target->length + 1) * sizeof(u16));
            free(target->str);
        }
        target->str = newStr;
        target->capacity = minSize;
    }
}

void u16str_grow(u16_string* target, u32 minSize) {
    if (minSize > target->capacity) {
        u32 newCapacity = target->capacity * 2;
        if (newCapacity < minSize) {
            newCapacity = minSize;
        }
        u16str_reserve(target, newCapacity);
    }
}

void u16str_clear(u16_string* target) {
    target->length = 0;
    if (target->str) {
        target->str[0] = 0;
    }
}

void u16str_remove(u16_string* target, u32 startIndex, u32 length) {
    if (startIndex >= target->length) {
        return;
    }
    
    if (startIndex + length > target->length) {
        length = target->length - startIndex;
    }
    
    memmove(target->str + startIndex, 
            target->str + startIndex + length, 
            (target->length - startIndex - length + 1) * sizeof(u16));
    
    target->length -= length;
}

void u16str_insert(u16_string* target, const u16_string* source, u32 targetStart, u32 sourceStart, u32 length) {
    if (!source || sourceStart >= source->length || length == 0) {
        return;
    }
    
    if (targetStart > target->length) {
        targetStart = target->length;
    }
    
    if (sourceStart + length > source->length) {
        length = source->length - sourceStart;
    }
    
    u32 newLength = target->length + length;
    u16str_grow(target, newLength);
    
    memmove(target->str + targetStart + length,
            target->str + targetStart,
            (target->length - targetStart + 1) * sizeof(u16));
    
    memcpy(target->str + targetStart,
           source->str + sourceStart,
           length * sizeof(u16));
    
    target->length = newLength;
}

u16_string* u16str_substr(u16_string* target, u32 startIndex, u32 length) {
    if (startIndex >= target->length) {
        return NULL;
    }
    
    if (startIndex + length > target->length) {
        length = target->length - startIndex;
    }
    
    u16_string* result = (u16_string*)malloc(sizeof(u16_string));
    result->length = length;
    result->capacity = length;
    result->str = (u16*)malloc((length + 1) * sizeof(u16));
    
    memcpy(result->str, target->str + startIndex, length * sizeof(u16));
    result->str[length] = 0;
    
    return result;
}

i32 u16str_compare(u16_string* a, u16_string* b) {
    if (!a || !a->str) return (!b || !b->str) ? 0 : -1;
    if (!b || !b->str) return 1;
    
    u32 minLen = a->length < b->length ? a->length : b->length;
    
    for (u32 i = 0; i < minLen; i++) {
        if (a->str[i] < b->str[i]) return -1;
        if (a->str[i] > b->str[i]) return 1;
    }
    
    if (a->length < b->length) return -1;
    if (a->length > b->length) return 1;
    
    return 0;
}

i32 u16str_indexOf(u16_string* target, u16 character) {
    for (u32 i = 0; i < target->length; i++) {
        if (target->str[i] == character) {
            return (i32)i;
        }
    }
    return -1;
}

// u8 string initialization functions
u8_string* u8str_create() {
    u8_string* str = (u8_string*)malloc(sizeof(u8_string));
    str->length = 0;
    str->capacity = 16; // Default initial capacity
    str->str = (u8*)malloc((str->capacity + 1) * sizeof(u8));
    str->str[0] = 0;
    return str;
}

u8_string* u8str_init(u8* data) {
    if (!data) {
        return u8str_create();
    }
    
    // Calculate length
    u32 len = 0;
    while (data[len] != 0) {
        len++;
    }
    
    u8_string* str = (u8_string*)malloc(sizeof(u8_string));
    str->length = len;
    str->capacity = len + 16; // Add some extra capacity
    str->str = (u8*)malloc((str->capacity + 1) * sizeof(u8));
    memcpy(str->str, data, len * sizeof(u8));
    str->str[len] = 0;
    return str;
}

u8_string* u8str_initEx(u8* data, u32 length) {
    u8_string* str = (u8_string*)malloc(sizeof(u8_string));
    str->length = length;
    str->capacity = length + 16; // Add some extra capacity
    str->str = (u8*)malloc((str->capacity + 1) * sizeof(u8));
    
    if (data) {
        memcpy(str->str, data, length * sizeof(u8));
    }
    str->str[length] = 0;
    return str;
}

// u8 string functions
u32 u8str_length(u8_string* target) {
    return target->length;
}

void u8str_concat(u8_string* target, u32 numStrings, u8_string** stringArray) {
    for (u32 i = 0; i < numStrings; i++) {
        u8str_insert(target, stringArray[i], target->length, 0, stringArray[i]->length);
    }
}

u8 u8str_get(u8_string* target, u32 index) {
    if (index < target->length) {
        return target->str[index];
    }
    return 0;
}

void u8str_set(u8_string* target, u32 index, u8 value) {
    if (index < target->length) {
        target->str[index] = value;
    }
}

char u8str_getChar(u8_string* target, u32 index) {
    if (index < target->length) {
        return (char)target->str[index];
    }
    return '\0';
}

void u8str_setChar(u8_string* target, u32 index, char value) {
    if (index < target->length) {
        target->str[index] = (u8)value;
    }
}

void u8str_reserve(u8_string* target, u32 minSize) {
    if (minSize > target->capacity) {
        u8* newStr = (u8*)malloc((minSize + 1) * sizeof(u8));
        if (target->str) {
            memcpy(newStr, target->str, (target->length + 1) * sizeof(u8));
            free(target->str);
        }
        target->str = newStr;
        target->capacity = minSize;
    }
}

void u8str_grow(u8_string* target, u32 minSize) {
    if (minSize > target->capacity) {
        u32 newCapacity = target->capacity * 2;
        if (newCapacity < minSize) {
            newCapacity = minSize;
        }
        u8str_reserve(target, newCapacity);
    }
}

void u8str_clear(u8_string* target) {
    target->length = 0;
    if (target->str) {
        target->str[0] = 0;
    }
}

void u8str_remove(u8_string* target, u32 startIndex, u32 length) {
    if (startIndex >= target->length) {
        return;
    }
    
    if (startIndex + length > target->length) {
        length = target->length - startIndex;
    }
    
    memmove(target->str + startIndex, 
            target->str + startIndex + length, 
            (target->length - startIndex - length + 1) * sizeof(u8));
    
    target->length -= length;
}

void u8str_insert(u8_string* target, const u8_string* source, u32 targetStart, u32 sourceStart, u32 length) {
    if (!source || sourceStart >= source->length || length == 0) {
        return;
    }
    
    if (targetStart > target->length) {
        targetStart = target->length;
    }
    
    if (sourceStart + length > source->length) {
        length = source->length - sourceStart;
    }
    
    u32 newLength = target->length + length;
    u8str_grow(target, newLength);
    
    memmove(target->str + targetStart + length,
            target->str + targetStart,
            (target->length - targetStart + 1) * sizeof(u8));
    
    memcpy(target->str + targetStart,
           source->str + sourceStart,
           length * sizeof(u8));
    
    target->length = newLength;
}

u8_string* u8str_substr(u8_string* target, u32 startIndex, u32 length) {
    if (startIndex >= target->length) {
        return NULL;
    }
    
    if (startIndex + length > target->length) {
        length = target->length - startIndex;
    }
    
    u8_string* result = (u8_string*)malloc(sizeof(u8_string));
    result->length = length;
    result->capacity = length;
    result->str = (u8*)malloc((length + 1) * sizeof(u8));
    
    memcpy(result->str, target->str + startIndex, length * sizeof(u8));
    result->str[length] = 0;
    
    return result;
}

i32 u8str_compare(u8_string* a, u8_string* b) {
    if (!a || !a->str) return (!b || !b->str) ? 0 : -1;
    if (!b || !b->str) return 1;
    
    u32 minLen = a->length < b->length ? a->length : b->length;
    
    for (u32 i = 0; i < minLen; i++) {
        if (a->str[i] < b->str[i]) return -1;
        if (a->str[i] > b->str[i]) return 1;
    }
    
    if (a->length < b->length) return -1;
    if (a->length > b->length) return 1;
    
    return 0;
}

i32 u8str_indexOf(u8_string* target, u8 character) {
    for (u32 i = 0; i < target->length; i++) {
        if (target->str[i] == character) {
            return (i32)i;
        }
    }
    return -1;
}

// Destructor functions
void u32str_destroy(u32_string* str) {
    if (str) {
        if (str->str) {
            free(str->str);
        }
        free(str);
    }
}

void u16str_destroy(u16_string* str) {
    if (str) {
        if (str->str) {
            free(str->str);
        }
        free(str);
    }
}

void u8str_destroy(u8_string* str) {
    if (str) {
        if (str->str) {
            free(str->str);
        }
        free(str);
    }
}

// Accessor functions
u32 u32str_capacity(u32_string* target) {
    return target ? target->capacity : 0;
}

u32* u32str_data(u32_string* target) {
    return target ? target->str : NULL;
}

u32 u16str_capacity(u16_string* target) {
    return target ? target->capacity : 0;
}

u16* u16str_data(u16_string* target) {
    return target ? target->str : NULL;
}

u32 u8str_capacity(u8_string* target) {
    return target ? target->capacity : 0;
}

u8* u8str_data(u8_string* target) {
    return target ? target->str : NULL;
}