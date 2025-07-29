#include "strings.h"

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
