#include "strings.h"
#include <cstring>
#include <cstdlib>


struct u8_string {
    u8* buffer;        // Null terminated, utf8 string (may be null if capacity is 0)
    u32 lengthChars;   // How many logical characters / codepoints are in the string (if there are unicode pairs, length can be less than size)
    u32 sizeBytes;     // How many bytes the string takes up (always less than or equal to capacity)
    u32 capacityBytes; // How many bytes buffer is
};

struct u16_string {
    u16* buffer;       // Null terminated, utf16 string (may be null if capacity is 0)
    u32 lengthChars;   // How many logical characters / codepoints are in the string (if there are unicode pairs, length can be less than size)
    u32 sizeBytes;     // How many bytes the string takes up (always less than or equal to capacity)
    u32 capacityBytes; // How many bytes buffer is
};

struct u32_string {
    u32* buffer;       // Null terminated, utf16 string (may be null if capacity is 0)
    u32 lengthChars;   // How many logical characters / codepoints are in the string (if there are unicode pairs, length can be less than size)
    u32 sizeBytes;     // How many bytes the string takes up (always less than or equal to capacity)
    u32 capacityBytes; // How many bytes buffer is
};

// Helper functions for UTF-8 encoding/decoding
u32 utf8_decode_char(const u8* str, u32* bytes_read) {
    if ((str[0] & 0x80) == 0) {
        *bytes_read = 1;
        return str[0];
    } else if ((str[0] & 0xE0) == 0xC0) {
        *bytes_read = 2;
        return ((str[0] & 0x1F) << 6) | (str[1] & 0x3F);
    } else if ((str[0] & 0xF0) == 0xE0) {
        *bytes_read = 3;
        return ((str[0] & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
    } else if ((str[0] & 0xF8) == 0xF0) {
        *bytes_read = 4;
        return ((str[0] & 0x07) << 18) | ((str[1] & 0x3F) << 12) | ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
    }
    *bytes_read = 1;
    return 0xFFFD;
}

u32 utf8_encode_char(u32 codepoint, u8* out) {
    if (codepoint <= 0x7F) {
        out[0] = (u8)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        out[0] = 0xC0 | (codepoint >> 6);
        out[1] = 0x80 | (codepoint & 0x3F);
        return 2;
    } else if (codepoint <= 0xFFFF) {
        out[0] = 0xE0 | (codepoint >> 12);
        out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        out[2] = 0x80 | (codepoint & 0x3F);
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        out[0] = 0xF0 | (codepoint >> 18);
        out[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        out[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        out[3] = 0x80 | (codepoint & 0x3F);
        return 4;
    }
    return 0;
}

// Helper functions for UTF-16 encoding/decoding
u32 utf16_decode_char(const u16* str, u32* units_read) {
    if (str[0] < 0xD800 || str[0] > 0xDFFF) {
        *units_read = 1;
        return str[0];
    }
    if (str[0] >= 0xD800 && str[0] <= 0xDBFF) {
        *units_read = 2;
        u32 high = str[0] - 0xD800;
        u32 low = str[1] - 0xDC00;
        return 0x10000 + (high << 10) + low;
    }
    *units_read = 1;
    return 0xFFFD;
}

u32 utf16_encode_char(u32 codepoint, u16* out) {
    if (codepoint <= 0xFFFF) {
        out[0] = (u16)codepoint;
        return 1;
    } else if (codepoint <= 0x10FFFF) {
        codepoint -= 0x10000;
        out[0] = 0xD800 + (codepoint >> 10);
        out[1] = 0xDC00 + (codepoint & 0x3FF);
        return 2;
    }
    return 0;
}

// Conversion functions
u16_string* u32str_to_u16str(u32_string* target) {
    if (!target) return nullptr;
    
    u16_string* result = u16str_create();
    u32 estimated_size = target->lengthChars * 2;
    u16str_reserve(result, estimated_size);
    
    for (u32 i = 0; i < target->lengthChars; i++) {
        u16 temp[2];
        u32 units = utf16_encode_char(target->buffer[i], temp);
        for (u32 j = 0; j < units; j++) {
            result->buffer[result->sizeBytes / 2] = temp[j];
            result->sizeBytes += 2;
        }
    }
    
    result->lengthChars = target->lengthChars;
    result->buffer[result->sizeBytes / 2] = 0;
    return result;
}

u8_string* u32str_to_u8str(u32_string* target) {
    if (!target) return nullptr;
    
    u8_string* result = u8str_create();
    u32 estimated_size = target->lengthChars * 4;
    u8str_reserve(result, estimated_size);
    
    for (u32 i = 0; i < target->lengthChars; i++) {
        u8 temp[4];
        u32 bytes = utf8_encode_char(target->buffer[i], temp);
        for (u32 j = 0; j < bytes; j++) {
            result->buffer[result->sizeBytes] = temp[j];
            result->sizeBytes++;
        }
    }
    
    result->lengthChars = target->lengthChars;
    result->buffer[result->sizeBytes] = 0;
    return result;
}

u32_string* u16str_to_u32str(u16_string* target) {
    if (!target) return nullptr;
    
    u32_string* result = u32str_create();
    u32str_reserve(result, target->lengthChars);
    
    u32 i = 0;
    u32 char_count = 0;
    while (i < target->sizeBytes / 2) {
        u32 units_read;
        u32 codepoint = utf16_decode_char(&target->buffer[i], &units_read);
        result->buffer[char_count] = codepoint;
        char_count++;
        i += units_read;
    }
    
    result->lengthChars = char_count;
    result->sizeBytes = char_count * 4;
    result->buffer[char_count] = 0;
    return result;
}

u8_string* u16strto_u8str(u16_string* target) {
    if (!target) return nullptr;
    
    u32_string* temp = u16str_to_u32str(target);
    u8_string* result = u32str_to_u8str(temp);
    u32str_destroy(temp);
    return result;
}

u32_string* u8str_to_u32str(u8_string* target) {
    if (!target) return nullptr;
    
    u32_string* result = u32str_create();
    u32str_reserve(result, target->lengthChars);
    
    u32 i = 0;
    u32 char_count = 0;
    while (i < target->sizeBytes) {
        u32 bytes_read;
        u32 codepoint = utf8_decode_char(&target->buffer[i], &bytes_read);
        result->buffer[char_count] = codepoint;
        char_count++;
        i += bytes_read;
    }
    
    result->lengthChars = char_count;
    result->sizeBytes = char_count * 4;
    result->buffer[char_count] = 0;
    return result;
}

u16_string* u8str_to_u16str(u8_string* target) {
    if (!target) return nullptr;
    
    u32_string* temp = u8str_to_u32str(target);
    u16_string* result = u32str_to_u16str(temp);
    u32str_destroy(temp);
    return result;
}

// u32_string functions
u32_string* u32str_create() {
    u32_string* str = (u32_string*)malloc(sizeof(u32_string));
    str->buffer = nullptr;
    str->lengthChars = 0;
    str->sizeBytes = 0;
    str->capacityBytes = 0;
    return str;
}

u32_string* u32str_init(u32* data) {
    if (!data) return u32str_create();
    
    u32_string* str = u32str_create();
    u32 len = 0;
    while (data[len] != 0) len++;
    
    u32str_reserve(str, len);
    memcpy(str->buffer, data, len * sizeof(u32));
    str->buffer[len] = 0;
    str->lengthChars = len;
    str->sizeBytes = len * sizeof(u32);
    return str;
}

void u32str_destroy(u32_string* str) {
    if (str) {
        if (str->buffer) free(str->buffer);
        free(str);
    }
}

u32 u32str_get(u32_string* target, u32 index) {
    if (!target || index >= target->lengthChars) return 0;
    return target->buffer[index];
}

void u32str_set(u32_string* target, u32 index, u32 value) {
    if (!target || index >= target->lengthChars) return;
    target->buffer[index] = value;
}

char u32str_getChar(u32_string* target, u32 index) {
    if (!target || index >= target->lengthChars) return 0;
    u32 value = target->buffer[index];
    if (value <= 127) return (char)value;
    return '?';
}

void u32str_setChar(u32_string* target, u32 index, char value) {
    if (!target || index >= target->lengthChars) return;
    target->buffer[index] = (u32)(u8)value;
}

void u32str_clear(u32_string* target) {
    if (!target) return;
    target->lengthChars = 0;
    target->sizeBytes = 0;
    if (target->buffer && target->capacityBytes > 0) {
        target->buffer[0] = 0;
    }
}

void u32str_reserve(u32_string* target, u32 minSize) {
    if (!target) return;
    
    u32 needed = (minSize + 1) * sizeof(u32);
    if (needed <= target->capacityBytes) return;
    
    u32 newCapacity = needed;
    if (newCapacity < 16) newCapacity = 16;
    else if (newCapacity < target->capacityBytes * 2) newCapacity = target->capacityBytes * 2;
    
    u32* newBuffer = (u32*)malloc(newCapacity);
    if (target->buffer) {
        memcpy(newBuffer, target->buffer, target->sizeBytes);
        free(target->buffer);
    }
    
    target->buffer = newBuffer;
    target->capacityBytes = newCapacity;
    target->buffer[target->lengthChars] = 0;
}

void u32str_remove(u32_string* target, u32 startIndex, u32 length) {
    if (!target || startIndex >= target->lengthChars) return;
    
    if (startIndex + length > target->lengthChars) {
        length = target->lengthChars - startIndex;
    }
    
    memmove(&target->buffer[startIndex], &target->buffer[startIndex + length], 
            (target->lengthChars - startIndex - length) * sizeof(u32));
    
    target->lengthChars -= length;
    target->sizeBytes = target->lengthChars * sizeof(u32);
    target->buffer[target->lengthChars] = 0;
}

void u32str_insert(u32_string* target, const u32_string* source, u32 targetStart, u32 sourceStart, u32 length) {
    if (!target || !source) return;
    if (sourceStart >= source->lengthChars) return;
    
    if (sourceStart + length > source->lengthChars) {
        length = source->lengthChars - sourceStart;
    }
    
    if (targetStart > target->lengthChars) {
        targetStart = target->lengthChars;
    }
    
    u32str_reserve(target, target->lengthChars + length);
    
    memmove(&target->buffer[targetStart + length], &target->buffer[targetStart],
            (target->lengthChars - targetStart) * sizeof(u32));
    
    memcpy(&target->buffer[targetStart], &source->buffer[sourceStart], length * sizeof(u32));
    
    target->lengthChars += length;
    target->sizeBytes = target->lengthChars * sizeof(u32);
    target->buffer[target->lengthChars] = 0;
}

void u32str_insert_char(u32_string* target, u32 index, u32 character) {
    if (!target) return;

    if (index > target->lengthChars) {
        index = target->lengthChars;
    }

    // Reserve space for one more character
    u32str_reserve(target, target->lengthChars + 1);

    // Move existing characters to make room
    if (index < target->lengthChars) {
        memmove(&target->buffer[index + 1], &target->buffer[index],
                (target->lengthChars - index) * sizeof(u32));
    }

    // Insert the character
    target->buffer[index] = character;
    target->lengthChars++;
    target->sizeBytes = target->lengthChars * sizeof(u32);
    target->buffer[target->lengthChars] = 0;
}

u32_string* u32str_substr(u32_string* target, u32 startIndex, u32 length) {
    if (!target) return nullptr;
    
    u32_string* result = u32str_create();
    if (startIndex >= target->lengthChars) return result;
    
    if (startIndex + length > target->lengthChars) {
        length = target->lengthChars - startIndex;
    }
    
    u32str_reserve(result, length);
    memcpy(result->buffer, &target->buffer[startIndex], length * sizeof(u32));
    result->lengthChars = length;
    result->sizeBytes = length * sizeof(u32);
    result->buffer[length] = 0;
    
    return result;
}

u32* u32str_getBuffer(u32_string* target) {
    if (target) {
        return target->buffer;
    }
    return 0;
}

i32 u32str_compare(u32_string* a, u32_string* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    u32 minLen = a->lengthChars < b->lengthChars ? a->lengthChars : b->lengthChars;
    
    for (u32 i = 0; i < minLen; i++) {
        if (a->buffer[i] < b->buffer[i]) return -1;
        if (a->buffer[i] > b->buffer[i]) return 1;
    }
    
    if (a->lengthChars < b->lengthChars) return -1;
    if (a->lengthChars > b->lengthChars) return 1;
    return 0;
}

i32 u32str_indexOf(u32_string* target, u32 character) {
    if (!target) return -1;
    
    for (u32 i = 0; i < target->lengthChars; i++) {
        if (target->buffer[i] == character) return i;
    }
    return -1;
}

u32 u32str_length(u32_string* target) {
    if (!target) return 0;
    return target->lengthChars;
}

void u32str_concat(u32_string* target, u32 numStrings, u32_string** stringArray) {
    if (!target || !stringArray || numStrings == 0) return;
    
    for (u32 i = 0; i < numStrings; i++) {
        if (stringArray[i]) {
            u32str_insert(target, stringArray[i], target->lengthChars, 0, stringArray[i]->lengthChars);
        }
    }
}

// u16_string functions
u16_string* u16str_create() {
    u16_string* str = (u16_string*)malloc(sizeof(u16_string));
    str->buffer = nullptr;
    str->lengthChars = 0;
    str->sizeBytes = 0;
    str->capacityBytes = 0;
    return str;
}

u16_string* u16str_init(u16* data) {
    if (!data) return u16str_create();
    
    u16_string* str = u16str_create();
    u32 len = 0;
    u32 chars = 0;
    
    while (data[len] != 0) {
        u32 units_read;
        utf16_decode_char(&data[len], &units_read);
        len += units_read;
        chars++;
    }
    
    u16str_reserve(str, len);
    memcpy(str->buffer, data, len * sizeof(u16));
    str->buffer[len] = 0;
    str->lengthChars = chars;
    str->sizeBytes = len * sizeof(u16);
    return str;
}

void u16str_destroy(u16_string* str) {
    if (str) {
        if (str->buffer) free(str->buffer);
        free(str);
    }
}

u16 u16str_get(u16_string* target, u32 index) {
    if (!target || index >= target->lengthChars) return 0;
    
    u32 pos = 0;
    for (u32 i = 0; i < index; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[pos], &units_read);
        pos += units_read;
    }
    return target->buffer[pos];
}

void u16str_set(u16_string* target, u32 index, u16 value) {
    if (!target || index >= target->lengthChars) return;
    
    u32 pos = 0;
    for (u32 i = 0; i < index; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[pos], &units_read);
        pos += units_read;
    }
    target->buffer[pos] = value;
}

char u16str_getChar(u16_string* target, u32 index) {
    if (!target || index >= target->lengthChars) return 0;
    
    u32 pos = 0;
    for (u32 i = 0; i < index; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[pos], &units_read);
        pos += units_read;
    }
    
    u32 units_read;
    u32 codepoint = utf16_decode_char(&target->buffer[pos], &units_read);
    if (codepoint <= 127) return (char)codepoint;
    return '?';
}

void u16str_setChar(u16_string* target, u32 index, char value) {
    if (!target || index >= target->lengthChars) return;
    u16str_set(target, index, (u16)(u8)value);
}

void u16str_clear(u16_string* target) {
    if (!target) return;
    target->lengthChars = 0;
    target->sizeBytes = 0;
    if (target->buffer && target->capacityBytes > 0) {
        target->buffer[0] = 0;
    }
}

void u16str_reserve(u16_string* target, u32 minSize) {
    if (!target) return;
    
    u32 needed = (minSize + 1) * sizeof(u16);
    if (needed <= target->capacityBytes) return;
    
    u32 newCapacity = needed;
    if (newCapacity < 16) newCapacity = 16;
    else if (newCapacity < target->capacityBytes * 2) newCapacity = target->capacityBytes * 2;
    
    u16* newBuffer = (u16*)malloc(newCapacity);
    if (target->buffer) {
        memcpy(newBuffer, target->buffer, target->sizeBytes);
        free(target->buffer);
    }
    
    target->buffer = newBuffer;
    target->capacityBytes = newCapacity;
    target->buffer[target->sizeBytes / 2] = 0;
}

void u16str_grow(u16_string* target, u32 minSize) {
    u16str_reserve(target, minSize);
}

void u16str_remove(u16_string* target, u32 startIndex, u32 length) {
    if (!target || startIndex >= target->lengthChars) return;
    
    if (startIndex + length > target->lengthChars) {
        length = target->lengthChars - startIndex;
    }
    
    u32 start_pos = 0;
    for (u32 i = 0; i < startIndex; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[start_pos], &units_read);
        start_pos += units_read;
    }
    
    u32 end_pos = start_pos;
    for (u32 i = 0; i < length; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[end_pos], &units_read);
        end_pos += units_read;
    }
    
    memmove(&target->buffer[start_pos], &target->buffer[end_pos],
            (target->sizeBytes / 2 - end_pos) * sizeof(u16));
    
    target->lengthChars -= length;
    target->sizeBytes -= (end_pos - start_pos) * sizeof(u16);
    target->buffer[target->sizeBytes / 2] = 0;
}

void u16str_insert(u16_string* target, const u16_string* source, u32 targetStart, u32 sourceStart, u32 length) {
    if (!target || !source) return;
    if (sourceStart >= source->lengthChars) return;
    
    if (sourceStart + length > source->lengthChars) {
        length = source->lengthChars - sourceStart;
    }
    
    if (targetStart > target->lengthChars) {
        targetStart = target->lengthChars;
    }
    
    u32 target_pos = 0;
    for (u32 i = 0; i < targetStart; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[target_pos], &units_read);
        target_pos += units_read;
    }
    
    u32 source_start_pos = 0;
    for (u32 i = 0; i < sourceStart; i++) {
        u32 units_read;
        utf16_decode_char(&source->buffer[source_start_pos], &units_read);
        source_start_pos += units_read;
    }
    
    u32 source_end_pos = source_start_pos;
    for (u32 i = 0; i < length; i++) {
        u32 units_read;
        utf16_decode_char(&source->buffer[source_end_pos], &units_read);
        source_end_pos += units_read;
    }
    
    u32 copy_units = source_end_pos - source_start_pos;
    u16str_reserve(target, (target->sizeBytes / 2) + copy_units);
    
    memmove(&target->buffer[target_pos + copy_units], &target->buffer[target_pos],
            (target->sizeBytes / 2 - target_pos) * sizeof(u16));
    
    memcpy(&target->buffer[target_pos], &source->buffer[source_start_pos], copy_units * sizeof(u16));
    
    target->lengthChars += length;
    target->sizeBytes += copy_units * sizeof(u16);
    target->buffer[target->sizeBytes / 2] = 0;
}

void u16str_insert_char(u16_string* target, u32 index, u16 character) {
    if (!target) return;

    if (index > target->lengthChars) {
        index = target->lengthChars;
    }

    // Find the byte position for the character index
    u32 byte_pos = 0;
    for (u32 i = 0; i < index && i < target->lengthChars; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[byte_pos], &units_read);
        byte_pos += units_read;
    }

    // Check if this is a high surrogate that needs a pair
    u32 units_to_insert = 1;
    u16 char_buffer[2] = { character, 0 };

    // Reserve space
    u16str_reserve(target, target->sizeBytes / sizeof(u16) + units_to_insert);

    // Move existing characters to make room
    u32 remaining_units = target->sizeBytes / sizeof(u16) - byte_pos;
    if (remaining_units > 0) {
        memmove(&target->buffer[byte_pos + units_to_insert],
                &target->buffer[byte_pos],
                remaining_units * sizeof(u16));
    }

    // Insert the character(s)
    memcpy(&target->buffer[byte_pos], char_buffer, units_to_insert * sizeof(u16));

    target->lengthChars++;
    target->sizeBytes += units_to_insert * sizeof(u16);
    target->buffer[target->sizeBytes / sizeof(u16)] = 0;
}

u16_string* u16str_substr(u16_string* target, u32 startIndex, u32 length) {
    if (!target) return nullptr;
    
    u16_string* result = u16str_create();
    if (startIndex >= target->lengthChars) return result;
    
    if (startIndex + length > target->lengthChars) {
        length = target->lengthChars - startIndex;
    }
    
    u32 start_pos = 0;
    for (u32 i = 0; i < startIndex; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[start_pos], &units_read);
        start_pos += units_read;
    }
    
    u32 end_pos = start_pos;
    for (u32 i = 0; i < length; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[end_pos], &units_read);
        end_pos += units_read;
    }
    
    u32 copy_units = end_pos - start_pos;
    u16str_reserve(result, copy_units);
    memcpy(result->buffer, &target->buffer[start_pos], copy_units * sizeof(u16));
    result->lengthChars = length;
    result->sizeBytes = copy_units * sizeof(u16);
    result->buffer[copy_units] = 0;
    
    return result;
}

i32 u16str_compare(u16_string* a, u16_string* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    u32 a_pos = 0, b_pos = 0;
    u32 chars_compared = 0;
    u32 min_chars = a->lengthChars < b->lengthChars ? a->lengthChars : b->lengthChars;
    
    while (chars_compared < min_chars) {
        u32 a_units, b_units;
        u32 a_char = utf16_decode_char(&a->buffer[a_pos], &a_units);
        u32 b_char = utf16_decode_char(&b->buffer[b_pos], &b_units);
        
        if (a_char < b_char) return -1;
        if (a_char > b_char) return 1;
        
        a_pos += a_units;
        b_pos += b_units;
        chars_compared++;
    }
    
    if (a->lengthChars < b->lengthChars) return -1;
    if (a->lengthChars > b->lengthChars) return 1;
    return 0;
}

i32 u16str_indexOf(u16_string* target, u16 character) {
    if (!target) return -1;
    
    u32 pos = 0;
    for (u32 i = 0; i < target->lengthChars; i++) {
        u32 units_read;
        utf16_decode_char(&target->buffer[pos], &units_read);
        if (target->buffer[pos] == character) return i;
        pos += units_read;
    }
    return -1;
}

u32 u16str_length(u16_string* target) {
    if (!target) return 0;
    return target->lengthChars;
}

void u16str_concat(u16_string* target, u32 numStrings, u16_string** stringArray) {
    if (!target || !stringArray || numStrings == 0) return;
    
    for (u32 i = 0; i < numStrings; i++) {
        if (stringArray[i]) {
            u16str_insert(target, stringArray[i], target->lengthChars, 0, stringArray[i]->lengthChars);
        }
    }
}

// u8_string functions
u8_string* u8str_create() {
    u8_string* str = (u8_string*)malloc(sizeof(u8_string));
    str->buffer = nullptr;
    str->lengthChars = 0;
    str->sizeBytes = 0;
    str->capacityBytes = 0;
    return str;
}

u8_string* u8str_init(u8* data) {
    if (!data) return u8str_create();
    
    u8_string* str = u8str_create();
    u32 len = 0;
    u32 chars = 0;
    
    while (data[len] != 0) {
        u32 bytes_read;
        utf8_decode_char(&data[len], &bytes_read);
        len += bytes_read;
        chars++;
    }
    
    u8str_reserve(str, len);
    memcpy(str->buffer, data, len);
    str->buffer[len] = 0;
    str->lengthChars = chars;
    str->sizeBytes = len;
    return str;
}

void u8str_destroy(u8_string* str) {
    if (str) {
        if (str->buffer) free(str->buffer);
        free(str);
    }
}

u8 u8str_get(u8_string* target, u32 index) {
    if (!target || index >= target->lengthChars) return 0;
    
    u32 pos = 0;
    for (u32 i = 0; i < index; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[pos], &bytes_read);
        pos += bytes_read;
    }
    return target->buffer[pos];
}

void u8str_set(u8_string* target, u32 index, u8 value) {
    if (!target || index >= target->lengthChars) return;
    
    u32 pos = 0;
    for (u32 i = 0; i < index; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[pos], &bytes_read);
        pos += bytes_read;
    }
    target->buffer[pos] = value;
}

char u8str_getChar(u8_string* target, u32 index) {
    if (!target || index >= target->lengthChars) return 0;
    
    u32 pos = 0;
    for (u32 i = 0; i < index; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[pos], &bytes_read);
        pos += bytes_read;
    }
    
    u32 bytes_read;
    u32 codepoint = utf8_decode_char(&target->buffer[pos], &bytes_read);
    if (codepoint <= 127) return (char)codepoint;
    return '?';
}

void u8str_setChar(u8_string* target, u32 index, char value) {
    if (!target || index >= target->lengthChars) return;
    u8str_set(target, index, (u8)value);
}

void u8str_clear(u8_string* target) {
    if (!target) return;
    target->lengthChars = 0;
    target->sizeBytes = 0;
    if (target->buffer && target->capacityBytes > 0) {
        target->buffer[0] = 0;
    }
}

void u8str_reserve(u8_string* target, u32 minSize) {
    if (!target) return;
    
    u32 needed = minSize + 1;
    if (needed <= target->capacityBytes) return;
    
    u32 newCapacity = needed;
    if (newCapacity < 16) newCapacity = 16;
    else if (newCapacity < target->capacityBytes * 2) newCapacity = target->capacityBytes * 2;
    
    u8* newBuffer = (u8*)malloc(newCapacity);
    if (target->buffer) {
        memcpy(newBuffer, target->buffer, target->sizeBytes);
        free(target->buffer);
    }
    
    target->buffer = newBuffer;
    target->capacityBytes = newCapacity;
    target->buffer[target->sizeBytes] = 0;
}

void u8str_grow(u8_string* target, u32 minSize) {
    u8str_reserve(target, minSize);
}

void u8str_remove(u8_string* target, u32 startIndex, u32 length) {
    if (!target || startIndex >= target->lengthChars) return;
    
    if (startIndex + length > target->lengthChars) {
        length = target->lengthChars - startIndex;
    }
    
    u32 start_pos = 0;
    for (u32 i = 0; i < startIndex; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[start_pos], &bytes_read);
        start_pos += bytes_read;
    }
    
    u32 end_pos = start_pos;
    for (u32 i = 0; i < length; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[end_pos], &bytes_read);
        end_pos += bytes_read;
    }
    
    memmove(&target->buffer[start_pos], &target->buffer[end_pos],
            target->sizeBytes - end_pos);
    
    target->lengthChars -= length;
    target->sizeBytes -= (end_pos - start_pos);
    target->buffer[target->sizeBytes] = 0;
}

void u8str_insert(u8_string* target, const u8_string* source, u32 targetStart, u32 sourceStart, u32 length) {
    if (!target || !source) return;
    if (sourceStart >= source->lengthChars) return;
    
    if (sourceStart + length > source->lengthChars) {
        length = source->lengthChars - sourceStart;
    }
    
    if (targetStart > target->lengthChars) {
        targetStart = target->lengthChars;
    }
    
    u32 target_pos = 0;
    for (u32 i = 0; i < targetStart; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[target_pos], &bytes_read);
        target_pos += bytes_read;
    }
    
    u32 source_start_pos = 0;
    for (u32 i = 0; i < sourceStart; i++) {
        u32 bytes_read;
        utf8_decode_char(&source->buffer[source_start_pos], &bytes_read);
        source_start_pos += bytes_read;
    }
    
    u32 source_end_pos = source_start_pos;
    for (u32 i = 0; i < length; i++) {
        u32 bytes_read;
        utf8_decode_char(&source->buffer[source_end_pos], &bytes_read);
        source_end_pos += bytes_read;
    }
    
    u32 copy_bytes = source_end_pos - source_start_pos;
    u8str_reserve(target, target->sizeBytes + copy_bytes);
    
    memmove(&target->buffer[target_pos + copy_bytes], &target->buffer[target_pos],
            target->sizeBytes - target_pos);
    
    memcpy(&target->buffer[target_pos], &source->buffer[source_start_pos], copy_bytes);
    
    target->lengthChars += length;
    target->sizeBytes += copy_bytes;
    target->buffer[target->sizeBytes] = 0;
}

void u8str_insert_char(u8_string* target, u32 index, u8 character) {
    if (!target) return;

    if (index > target->lengthChars) {
        index = target->lengthChars;
    }

    // Find the byte position for the character index
    u32 byte_pos = 0;
    for (u32 i = 0; i < index && i < target->lengthChars; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[byte_pos], &bytes_read);
        byte_pos += bytes_read;
    }

    // For u8 strings, ASCII characters are single byte
    // For simplicity, we'll assume single-byte character
    u32 bytes_to_insert = 1;

    // Reserve space
    u8str_reserve(target, target->sizeBytes + bytes_to_insert);

    // Move existing characters to make room
    if (byte_pos < target->sizeBytes) {
        memmove(&target->buffer[byte_pos + bytes_to_insert],
                &target->buffer[byte_pos],
                target->sizeBytes - byte_pos);
    }

    // Insert the character
    target->buffer[byte_pos] = character;

    target->lengthChars++;
    target->sizeBytes += bytes_to_insert;
    target->buffer[target->sizeBytes] = 0;
}

u8_string* u8str_substr(u8_string* target, u32 startIndex, u32 length) {
    if (!target) return nullptr;
    
    u8_string* result = u8str_create();
    if (startIndex >= target->lengthChars) return result;
    
    if (startIndex + length > target->lengthChars) {
        length = target->lengthChars - startIndex;
    }
    
    u32 start_pos = 0;
    for (u32 i = 0; i < startIndex; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[start_pos], &bytes_read);
        start_pos += bytes_read;
    }
    
    u32 end_pos = start_pos;
    for (u32 i = 0; i < length; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[end_pos], &bytes_read);
        end_pos += bytes_read;
    }
    
    u32 copy_bytes = end_pos - start_pos;
    u8str_reserve(result, copy_bytes);
    memcpy(result->buffer, &target->buffer[start_pos], copy_bytes);
    result->lengthChars = length;
    result->sizeBytes = copy_bytes;
    result->buffer[copy_bytes] = 0;
    
    return result;
}

i32 u8str_compare(u8_string* a, u8_string* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    u32 a_pos = 0, b_pos = 0;
    u32 chars_compared = 0;
    u32 min_chars = a->lengthChars < b->lengthChars ? a->lengthChars : b->lengthChars;
    
    while (chars_compared < min_chars) {
        u32 a_bytes, b_bytes;
        u32 a_char = utf8_decode_char(&a->buffer[a_pos], &a_bytes);
        u32 b_char = utf8_decode_char(&b->buffer[b_pos], &b_bytes);
        
        if (a_char < b_char) return -1;
        if (a_char > b_char) return 1;
        
        a_pos += a_bytes;
        b_pos += b_bytes;
        chars_compared++;
    }
    
    if (a->lengthChars < b->lengthChars) return -1;
    if (a->lengthChars > b->lengthChars) return 1;
    return 0;
}

i32 u8str_indexOf(u8_string* target, u8 character) {
    if (!target) return -1;
    
    u32 pos = 0;
    for (u32 i = 0; i < target->lengthChars; i++) {
        u32 bytes_read;
        utf8_decode_char(&target->buffer[pos], &bytes_read);
        if (target->buffer[pos] == character) return i;
        pos += bytes_read;
    }
    return -1;
}

u32 u8str_length(u8_string* target) {
    if (!target) return 0;
    return target->lengthChars;
}

void u8str_concat(u8_string* target, u32 numStrings, u8_string** stringArray) {
    if (!target || !stringArray || numStrings == 0) return;
    
    for (u32 i = 0; i < numStrings; i++) {
        if (stringArray[i]) {
            u8str_insert(target, stringArray[i], target->lengthChars, 0, stringArray[i]->lengthChars);
        }
    }
}
