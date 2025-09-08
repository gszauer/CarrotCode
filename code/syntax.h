#ifndef _H_SYNTAX_CARROT_
#define _H_SYNTAX_CARROT_

#include "types.h"
#include "strings.h"

enum token_type {
    TOKEN_NONE,
    TOKEN_KEYWORD,
    TOKEN_COMMENT,
    TOKEN_PREPROCESSOR,
    TOKEN_WHITESPACE
};

struct token_span {
    u32 start;
    u32 end;
    token_type type;
};

struct document_line {
    u32_string* text;
    bool dirty;
    token_span* tokens;
    u32 token_count;
    u32 token_capacity;
};

struct vector_docline;

vector_docline* vec_docline_create();
void vec_docline_destroy(vector_docline* vec);
void vec_docline_clear(vector_docline* vec);
void vec_docline_reserve(vector_docline* vec, u32 count);
void vec_docline_resize(vector_docline* vec, u32 size);
void vec_docline_push(vector_docline* vec, document_line* line);

u32 vec_docline_size(vector_docline* vec);
document_line* vec_docline_get(vector_docline* vec, u32 index);
void vec_docline_insert(vector_docline* vec, u32 index, document_line* line);
void vec_docline_remove(vector_docline* vec, u32 index);
vector_docline* vec_docline_clone(vector_docline* vec);

document_line* docline_create();
document_line* docline_create_with_text(u32_string* text);
void docline_destroy(document_line* line);
void docline_mark_dirty(document_line* line);
void docline_tokenize(document_line* line);
void docline_clear_tokens(document_line* line);
void docline_add_token(document_line* line, u32 start, u32 end, token_type type);

#endif