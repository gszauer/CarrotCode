#ifndef _H_VECTORS_CARROT_
#define _H_VECTORS_CARROT_ 

#include "types.h"
// vectors own the strings they contain! If a string vectory is destroyed, it's contained strings will be destroyed as well

vector_str8* vec_str8_create(); // Empty vector
void vec_str8_destroy(vector_str8* vec); // Destroy / free / cleanup
void vec_str8_clear(vector_str8* vec); // Only affects size, but does destroy strings
void vec_str8_reserve(vector_str8* vec, unsigned int count);
void vec_str8_resize(vector_str8* vec, unsigned int size); // Allocates empty strings if new size is big
void vec_str8_push(vector_str8* vec, u8_string* string); // Makes a copy, owns copy

unsigned int vec_str8_size(vector_str8* vec); // Returns size of vector
u8_string* vec_str8_get(vector_str8* vec, unsigned int index); // Returns a string that can be edited
void vec_str8_insert(vector_str8* vec, unsigned int index, u8_string* string); // Insert at index
void vec_str8_remove(vector_str8* vec, unsigned int index); // Remove element at index
vector_str8* vec_str8_clone(vector_str8* vec); // Deep copy

// vector_str16
vector_str16* vec_str16_create(); // Empty vector
void vec_str16_destroy(vector_str16* vec); // Destroy / free / cleanup
void vec_str16_clear(vector_str16* vec); // Only affects size, but does destroy strings
void vec_str16_reserve(vector_str16* vec, unsigned int count);
void vec_str16_resize(vector_str16* vec, unsigned int size); // Allocates empty strings if new size is big
void vec_str16_push(vector_str16* vec, u16_string* string); // Makes a copy, owns copy

unsigned int vec_str16_size(vector_str16* vec); // Returns size of vector
u16_string* vec_str16_get(vector_str16* vec, unsigned int index); // Returns a string that can be edited
void vec_str16_insert(vector_str16* vec, unsigned int index, u16_string* string); // Insert at index
void vec_str16_remove(vector_str16* vec, unsigned int index); // Remove element at index
vector_str16* vec_str16_clone(vector_str16* vec); // Deep copy

// vector_str32
vector_str32* vec_str32_create(); // Empty vector
void vec_str32_destroy(vector_str32* vec); // Destroy / free / cleanup
void vec_str32_clear(vector_str32* vec); // Only affects size, but does destroy strings
void vec_str32_reserve(vector_str32* vec, unsigned int count);
void vec_str32_resize(vector_str32* vec, unsigned int size); // Allocates empty strings if new size is big
void vec_str32_push(vector_str32* vec, u32_string* string); // Makes a copy, owns copy

unsigned int vec_str32_size(vector_str32* vec); // Returns size of vector
u32_string* vec_str32_get(vector_str32* vec, unsigned int index); // Returns a string that can be edited
void vec_str32_insert(vector_str32* vec, unsigned int index, u32_string* string); // Insert at index
void vec_str32_remove(vector_str32* vec, unsigned int index); // Remove element at index
vector_str32* vec_str32_clone(vector_str32* vec); // Deep copy

// vector_docline - forward declaration for document_line is in syntax.h
struct document_line;

vector_docline* vec_docline_create(); // Empty vector
void vec_docline_destroy(vector_docline* vec); // Destroy / free / cleanup
void vec_docline_clear(vector_docline* vec); // Only affects size, but does destroy lines
void vec_docline_reserve(vector_docline* vec, u32 count);
void vec_docline_resize(vector_docline* vec, u32 size); // Allocates empty lines if new size is bigger
void vec_docline_push(vector_docline* vec, document_line* line); // Takes ownership of line

u32 vec_docline_size(vector_docline* vec); // Returns size of vector
document_line* vec_docline_get(vector_docline* vec, u32 index); // Returns a line that can be edited
void vec_docline_insert(vector_docline* vec, u32 index, document_line* line); // Insert at index
void vec_docline_remove(vector_docline* vec, u32 index); // Remove element at index
vector_docline* vec_docline_clone(vector_docline* vec); // Deep copy

#endif
