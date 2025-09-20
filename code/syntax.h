#ifndef _H_SYNTAX_CARROT_
#define _H_SYNTAX_CARROT_

#include "types.h"
#include "strings.h"

enum token_type {
    TOKEN_NONE,
    TOKEN_KEYWORD,
    TOKEN_COMMENT,
    TOKEN_PREPROCESSOR,
    TOKEN_WHITESPACE,
    TOKEN_LITERAL
};


struct token_span;
struct document_line;

document_line* docline_create();
document_line* docline_create_with_text(u32_string* text);
void docline_destroy(document_line* line);
void docline_mark_dirty(document_line* line); // TODO: If we don't allow direct editing of a docline, this can be refactored out!
void docline_tokenize(document_line* line);
void docline_clear_tokens(document_line* line);

#ifdef CARROT_INCLUDE_SYNTAX_DEFS
void docline_add_token(document_line* line, u32 start, u32 end, token_type type);
#endif

u32_string* docline_access_text(document_line* lineNum);
token_span* docline_access_tokens(document_line* line);

u32 docline_get_token_count(document_line* line);
u32 docline_get_text_length(document_line* lineNum);
void docline_text_remove(document_line* line, u32 startIndex, u32 length);
u32_string* docline_text_substr(document_line* line, u32 startIndex, u32 length);
void docline_text_insert(document_line* line, const u32_string* source, u32 targetStart, u32 sourceStart, u32 length);
void docline_text_insert_char(document_line* line, u32 index, u32 character);
bool docline_is_dirty(document_line* line);


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

u32 token_span_get_start(token_span* span);
u32 token_span_get_end(token_span* span);
token_type token_span_get_type(token_span* span);
token_span* token_span_array_at(token_span* base, u32 index);

#endif