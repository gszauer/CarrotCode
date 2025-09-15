#ifndef _H_SYNTAX_CARROT_
#define _H_SYNTAX_CARROT_

#include "types.h"
#include "strings.h"

#ifdef CARROT_INCLUDE_SYNTAX_DEFS
enum token_type {
    TOKEN_NONE,
    TOKEN_KEYWORD,
    TOKEN_COMMENT,
    TOKEN_PREPROCESSOR,
    TOKEN_WHITESPACE,
    TOKEN_LITERAL
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
#else
struct document_line;
struct token_span;
#endif


document_line* docline_create();
document_line* docline_create_with_text(u32_string* text);
void docline_destroy(document_line* line);
void docline_mark_dirty(document_line* line);
void docline_tokenize(document_line* line);
void docline_clear_tokens(document_line* line);
#ifdef CARROT_INCLUDE_SYNTAX_DEFS
void docline_add_token(document_line* line, u32 start, u32 end, token_type type);
#endif

#endif