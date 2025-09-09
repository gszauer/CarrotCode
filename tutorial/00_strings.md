# Tutorial: The `u32_string` Container

Welcome to the CarrotCode string library. This tutorial provides a comprehensive guide to the `u32_string` container, which is the primary string type used throughout the editor for text manipulation.

## Why a Custom String Library?

While `std::string` is powerful, it is fundamentally a `char`-based container, making it cumbersome for handling Unicode. For a text editor that needs to correctly handle a wide range of languages and symbols, it's often better to work with a string type that treats Unicode code points as first-class citizens.

CarrotCode uses a `u32_string` internally, which is a string of 32-bit unsigned integers. Each `u32` directly represents a single UTF-32 code point. This simplifies many operations, as we no longer need to worry about multi-byte character sequences when indexing or splitting strings.

## The `u32_string` Struct

The `u32_string` is a C-style struct that manages a dynamic buffer of `u32` characters.

```c
struct u32_string {
    u32* buffer;       // Null-terminated buffer holding the UTF-32 characters.
    u32 lengthChars;   // The number of logical characters (code points) in the string.
    u32 sizeBytes;     // The size of the string data in bytes (lengthChars * 4).
    u32 capacityBytes; // The total allocated size of the buffer in bytes.
};
```

## Function Reference

All string manipulation is done through a C-style API. A `u32_string*` must be passed to each function.

---

### `u32_string* u32str_create()`

- **Description**: Creates a new, empty `u32_string`.
- **Implementation Details**: Allocates memory for the `u32_string` struct itself using `malloc`. The internal fields (`buffer`, `lengthChars`, `sizeBytes`, `capacityBytes`) are initialized to zero or `NULL`. No buffer is allocated at this stage.
- **Example**:
```c
#include "strings.h"

u32_string* s = u32str_create();
// s now points to a valid, empty string.
// s->buffer is NULL, s->lengthChars is 0.
u32str_destroy(s);
```

---

### `u32_string* u32str_init(u32* data)`

- **Description**: Creates a new `u32_string` by copying from a null-terminated array of `u32` characters.
- **Implementation Details**: First, it calculates the length of the input `data` by scanning for the null terminator. It then allocates memory for the `u32_string` struct and a new buffer of the required size. Finally, it copies the content from `data` into the new buffer using `memcpy`.
- **Example**:
```c
u32 initial_data[] = {'H', 'e', 'l', 'l', 'o', 0};
u32_string* s = u32str_init(initial_data);
// s now contains "Hello".
u32str_destroy(s);
```

---

### `void u32str_destroy(u32_string* str)`

- **Description**: Frees all memory associated with a `u32_string`.
- **Implementation Details**: First, it checks if the internal `buffer` is not `NULL` and frees it using `free`. Then, it frees the `u32_string` struct itself. This prevents memory leaks.
- **Example**:
```c
u32_string* s = u32str_create();
u32str_destroy(s); // Safely cleans up the string.
```

---

### `u32 u32str_get(u32_string* target, u32 index)`

- **Description**: Gets the character (code point) at a specific index.
- **Implementation Details**: Performs a bounds check on the `index` against `target->lengthChars`. If the index is valid, it returns the `u32` value at `target->buffer[index]`. Otherwise, it returns 0.
- **Example**:
```c
u32_string* s = u32str_init((u32[]){L'A', L'B', L'C', 0});
u32 char_b = u32str_get(s, 1); // Returns 'B' (66)
u32str_destroy(s);
```

---

### `void u32str_set(u32_string* target, u32 index, u32 value)`

- **Description**: Sets the character (code point) at a specific index.
- **Implementation Details**: Performs a bounds check on the `index`. If valid, it directly overwrites the value at `target->buffer[index]` with `value`.
- **Example**:
```c
u32_string* s = u32str_init((u32[]){L'A', L'B', L'C', 0});
u32str_set(s, 1, L'Z'); // String is now "AZC"
u32str_destroy(s);
```

---

### `void u32str_clear(u32_string* target)`

- **Description**: Clears the string, setting its length to 0. The allocated capacity is preserved.
- **Implementation Details**: Sets `target->lengthChars` and `target->sizeBytes` to 0. If the buffer exists, it places a null terminator at `buffer[0]`. It does not `free` the buffer.
- **Example**:
```c
u32_string* s = u32str_init((u32[]){L'T', L'e', L's', L't', 0});
u32str_clear(s); // s is now "", but may still have capacity.
u32str_destroy(s);
```

---

### `void u32str_reserve(u32_string* target, u32 minSize)`

- **Description**: Ensures the string has enough capacity for at least `minSize` characters.
- **Implementation Details**: Calculates the required capacity in bytes (`(minSize + 1) * sizeof(u32)` for the null terminator). If this is greater than the current capacity, it uses `realloc` to resize the buffer. The capacity typically grows by a factor of 2 to avoid frequent reallocations.
- **Example**:
```c
u32_string* s = u32str_create();
// Reserve space for 100 characters to avoid reallocations later.
u32str_reserve(s, 100);
u32str_destroy(s);
```

---

### `void u32str_remove(u32_string* target, u32 startIndex, u32 length)`

- **Description**: Removes `length` characters from the string, starting at `startIndex`.
- **Implementation Details**: After bounds checking, it uses `memmove` to shift the portion of the string that comes after the deleted section over the deleted part. `memmove` is used instead of `memcpy` because the source and destination memory regions may overlap.
- **Example**:
```c
u32_string* s = u32str_init((u32[]){L'H', L'e', L'l', L'l', L'o', 0});
u32str_remove(s, 1, 3); // Removes "ell", string becomes "Ho"
u32str_destroy(s);
```

---

### `void u32str_insert(u32_string* target, const u32_string* source, u32 targetStart, u32 sourceStart, u32 length)`

- **Description**: Inserts `length` characters from `source` into `target`.
- **Implementation Details**: First, it ensures `target` has enough capacity by calling `u32str_reserve`. It then uses `memmove` to shift the part of `target`'s content after `targetStart` to the right, creating a gap. Finally, it uses `memcpy` to copy the content from `source` into this gap.
- **Example**:
```c
u32_string* s1 = u32str_init((u32[]){L'H', L'o', 0});
u32_string* s2 = u32str_init((u32[]){L'e', L'l', L'l', 0});
// Insert s2 into s1 at index 1
u32str_insert(s1, s2, 1, 0, 3);
// s1 is now "Hello"
u32str_destroy(s1);
u32str_destroy(s2);
```

---

### `u32_string* u32str_substr(u32_string* target, u32 startIndex, u32 length)`

- **Description**: Creates a new `u32_string` that is a copy of a substring of `target`.
- **Implementation Details**: It creates a new, empty `u32_string`. It reserves capacity for the substring, then `memcpy`s the specified portion from `target->buffer` into the new string's buffer.
- **Example**:
```c
u32_string* s = u32str_init((u32[]){L'H', L'e', L'l', L'l', L'o', 0});
u32_string* sub = u32str_substr(s, 1, 3); // sub is "ell"
u32str_destroy(s);
u32str_destroy(sub);
```

---

### `i32 u32str_compare(u32_string* a, u32_string* b)`

- **Description**: Performs a lexicographical comparison of two strings.
- **Implementation Details**: It iterates through both strings character by character up to the length of the shorter string, comparing code points. If a difference is found, it returns -1 or 1. If the strings are identical up to that point, it compares their lengths.
- **Example**:
```c
u32_string* s1 = u32str_init((u32[]){L'A', 0});
u32_string* s2 = u32str_init((u32[]){L'B', 0});
i32 result = u32str_compare(s1, s2); // result will be < 0
u32str_destroy(s1);
u32str_destroy(s2);
```

---

### `i32 u32str_indexOf(u32_string* target, u32 character)`

- **Description**: Finds the first occurrence of a character in the string.
- **Implementation Details**: A simple loop iterates from the start of the string's buffer to the end, returning the index of the first matching character. If no match is found after checking all characters, it returns -1.
- **Example**:
```c
u32_string* s = u32str_init((u32[]){L'H', L'e', L'l', L'l', L'o', 0});
i32 index = u32str_indexOf(s, L'l'); // index will be 2
u32str_destroy(s);
```

---

### `u32 u32str_length(u32_string* target)`

- **Description**: Returns the number of characters (code points) in the string.
- **Implementation Details**: This is a fast operation. It simply returns the value of the `lengthChars` field of the `u32_string` struct. It does not need to calculate the length.
- **Example**:
```c
u32_string* s = u32str_init((u32[]){L'H', L'e', L'l', L'l', L'o', 0});
u32 len = u32str_length(s); // len will be 5
u32str_destroy(s);
```

---

### `void u32str_concat(u32_string* target, u32 numStrings, u32_string** stringArray)`

- **Description**: Concatenates an array of `u32_string` pointers onto the end of `target`.
- **Implementation Details**: It iterates through the `stringArray` and calls `u32str_insert` for each string, inserting it at the end of the `target` string.
- **Example**:
```c
u32_string* s1 = u32str_init((u32[]){L'A', 0});
u32_string* s2 = u32str_init((u32[]){L'B', 0});
u32_string* s3 = u32str_init((u32[]){L'C', 0});
u32_string* parts[] = {s2, s3};
u32str_concat(s1, 2, parts);
// s1 is now "ABC"
u32str_destroy(s1);
u32str_destroy(s2);
u32str_destroy(s3);
```