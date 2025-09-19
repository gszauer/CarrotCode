#ifndef _H_TYPES_CARROT_
#define _H_TYPES_CARROT_

typedef char i8;
typedef unsigned char u8;

typedef short i16;
typedef unsigned short u16;

typedef int i32;
typedef unsigned int u32;

typedef float f32;
typedef double d64;

// Forward declarations for string types
struct u8_string;
struct u16_string;
struct u32_string;

// Forward declaration for vector types
// vectors own the strings they contain!
struct vector_docline;

struct canvas;
struct font;

struct document;

#endif
