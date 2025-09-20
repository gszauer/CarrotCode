#include "syntax.h"
#include <cstdlib>
#include <cstring>
#include <cctype>

struct document_line {
    u32_string* text;
    bool dirty;
    token_span* tokens;
    u32 token_count;
    u32 token_capacity;
};

struct vector_docline {
    document_line** data;
    u32 size;
    u32 capacity;
};

struct token_span {
    u32 start;
    u32 end;
    token_type type;
};

token_span* token_span_array_at(token_span* base, u32 index) {
    if (base) {
        return &base[index];
    }
    return 0;
}

u32 token_span_get_start(token_span* span) {
    if (span) {
        return span->start;
    }
    return 0;
}

u32 token_span_get_end(token_span* span) {
    if (span) {
        return span->end;
    }
    return 0;
}

token_type token_span_get_type(token_span* span) {
    if (span) {
        return span->type;
    }
    return TOKEN_NONE;
}

token_span* docline_access_tokens(document_line* line) {
    if (line) {
        return line->tokens;
    }
    return 0;
}

u32 docline_get_token_count(document_line* line) {
    if (line) {
        return line->token_count;
    }
    return 0;
}

u32_string* docline_access_text(document_line* lineNum) {
    if (lineNum) {
        return lineNum->text;
    }
    return 0;
}

u32 docline_get_text_length(document_line* lineNum) {
    if (lineNum) {
        return u32str_length(lineNum->text);
    }
    return 0;
}

bool docline_is_dirty(document_line* line) {
    if (line) {
        return line->dirty;
    }
    return false;
}

void docline_text_remove(document_line* line, u32 startIndex, u32 length) {
    if (line) {
        u32str_remove(line->text, startIndex, length);
    }
}

u32_string* docline_text_substr(document_line* line, u32 startIndex, u32 length) {
    if (line) {
        u32str_substr(line->text, startIndex, length);
    }
    return 0;
}

void docline_text_insert(document_line* line, const u32_string* source, u32 targetStart, u32 sourceStart, u32 length) {
    if (line) {
        u32str_insert(line->text, source, targetStart, sourceStart, length);
    }
}

void docline_text_insert_char(document_line* line, u32 index, u32 character) {
    if (line) {
        u32str_insert_char(line->text, index, character);
    }
}

document_line* docline_create() {
    document_line* line = (document_line*)malloc(sizeof(document_line));
    line->text = u32str_create();
    line->dirty = true;
    line->tokens = nullptr;
    line->token_count = 0;
    line->token_capacity = 0;
    return line;
}

document_line* docline_create_with_text(u32_string* text) {
    document_line* line = (document_line*)malloc(sizeof(document_line));
    if (text) {
        line->text = u32str_substr(text, 0, u32str_length(text));
    } else {
        line->text = u32str_create();
    }
    line->dirty = true;
    line->tokens = nullptr;
    line->token_count = 0;
    line->token_capacity = 0;
    return line;
}

void docline_destroy(document_line* line) {
    if (!line) return;
    
    if (line->text) {
        u32str_destroy(line->text);
    }
    
    if (line->tokens) {
        free(line->tokens);
    }
    
    free(line);
}

void docline_mark_dirty(document_line* line) {
    if (line) {
        line->dirty = true;
    }
}

void docline_clear_tokens(document_line* line) {
    if (line) {
        line->token_count = 0;
    }
}

void docline_add_token(document_line* line, u32 start, u32 end, token_type type) {
    if (!line) return;
    
    if (line->token_count >= line->token_capacity) {
        u32 new_capacity = line->token_capacity == 0 ? 16 : line->token_capacity * 2;
        token_span* new_tokens = (token_span*)realloc(line->tokens, new_capacity * sizeof(token_span));
        if (new_tokens) {
            line->tokens = new_tokens;
            line->token_capacity = new_capacity;
        } else {
            return;
        }
    }
    
    line->tokens[line->token_count].start = start;
    line->tokens[line->token_count].end = end;
    line->tokens[line->token_count].type = type;
    line->token_count++;
}

static bool is_keyword(const char* word, u32 len) {
    const char* keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while", "_Bool", "_Complex", "_Imaginary",
        "bool", "true", "false", "nullptr", "class", "namespace", "template",
        "typename", "this", "new", "delete", "try", "catch", "throw", "virtual",
        "override", "final", "public", "private", "protected", "friend", "using",
        "i8", "i16", "i32", "u8", "u16", "u32", "f32", "d64", "size_t", "ptrdiff_t",
        "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t"
    };
    
    for (const char* kw : keywords) {
        if (strlen(kw) == len && memcmp(word, kw, len) == 0) {
            return true;
        }
    }
    return false;
}

void docline_tokenize(document_line* line) {
    if (!line || !line->dirty) return;
    
    docline_clear_tokens(line);
    
    u32 len = u32str_length(line->text);
    u32 i = 0;
    
    while (i < len) {
        u32 ch = u32str_get(line->text, i);
        
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            u32 start = i;
            while (i < len) {
                ch = u32str_get(line->text, i);
                if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
                i++;
            }
            docline_add_token(line, start, i, TOKEN_WHITESPACE);
        }
        else if (ch == '/' && i + 1 < len && u32str_get(line->text, i + 1) == '/') {
            u32 start = i;
            i = len;
            docline_add_token(line, start, i, TOKEN_COMMENT);
        }
        else if (ch == '#') {
            u32 start = i;
            i = len;
            docline_add_token(line, start, i, TOKEN_PREPROCESSOR);
        }
        else if (ch == '"') {
            u32 start = i;
            i++;
            while (i < len) {
                ch = u32str_get(line->text, i);
                if (ch == '"' && (i == 0 || u32str_get(line->text, i - 1) != '\\')) {
                    i++;
                    break;
                }
                if (ch == '\\' && i + 1 < len) {
                    i += 2;
                } else {
                    i++;
                }
            }
            docline_add_token(line, start, i, TOKEN_LITERAL);
        }
        else if (ch == '\'') {
            u32 start = i;
            i++;
            if (i < len && u32str_get(line->text, i) == '\\') {
                i += 2;
            } else {
                i++;
            }
            if (i < len && u32str_get(line->text, i) == '\'') {
                i++;
            }
            docline_add_token(line, start, i, TOKEN_LITERAL);
        }
        else if ((ch >= '0' && ch <= '9') || (ch == '.' && i + 1 < len && u32str_get(line->text, i + 1) >= '0' && u32str_get(line->text, i + 1) <= '9')) {
            u32 start = i;
            bool has_dot = (ch == '.');
            i++;
            
            while (i < len) {
                ch = u32str_get(line->text, i);
                if ((ch >= '0' && ch <= '9') || 
                    (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') ||
                    ch == 'x' || ch == 'X' || ch == 'b' || ch == 'B' ||
                    ch == 'u' || ch == 'U' || ch == 'l' || ch == 'L' ||
                    ch == 'f' || ch == 'F' || ch == 'e' || ch == 'E' ||
                    ch == '+' || ch == '-' || (!has_dot && ch == '.')) {
                    if (ch == '.') has_dot = true;
                    i++;
                } else {
                    break;
                }
            }
            docline_add_token(line, start, i, TOKEN_LITERAL);
        }
        else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
            u32 start = i;
            i++;
            
            while (i < len) {
                ch = u32str_get(line->text, i);
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || 
                    (ch >= '0' && ch <= '9') || ch == '_') {
                    i++;
                } else {
                    break;
                }
            }
            
            u32 word_len = i - start;
            char* word = (char*)malloc(word_len + 1);
            for (u32 j = 0; j < word_len; j++) {
                word[j] = (char)u32str_get(line->text, start + j);
            }
            word[word_len] = '\0';
            
            token_type type = TOKEN_NONE;
            if (is_keyword(word, word_len)) {
                type = TOKEN_KEYWORD;
            }
            
            free(word);
            docline_add_token(line, start, i, type);
        }
        else if (strchr("+-*/%=<>!&|^~?:", ch)) {
            u32 start = i;
            i++;
            
            if (i < len) {
                u32 next = u32str_get(line->text, i);
                if ((ch == '+' && next == '+') || (ch == '-' && next == '-') ||
                    (ch == '&' && next == '&') || (ch == '|' && next == '|') ||
                    (ch == '<' && next == '<') || (ch == '>' && next == '>') ||
                    (ch == '=' && next == '=') || (ch == '!' && next == '=') ||
                    (ch == '<' && next == '=') || (ch == '>' && next == '=') ||
                    (ch == '+' && next == '=') || (ch == '-' && next == '=') ||
                    (ch == '*' && next == '=') || (ch == '/' && next == '=') ||
                    (ch == '%' && next == '=') || (ch == '&' && next == '=') ||
                    (ch == '|' && next == '=') || (ch == '^' && next == '=') ||
                    (ch == '<' && next == '<' && i + 1 < len && u32str_get(line->text, i + 1) == '=') ||
                    (ch == '>' && next == '>' && i + 1 < len && u32str_get(line->text, i + 1) == '=') ||
                    (ch == '-' && next == '>') || (ch == ':' && next == ':')) {
                    i++;
                    if ((ch == '<' && next == '<' || ch == '>' && next == '>') && 
                        i < len && u32str_get(line->text, i) == '=') {
                        i++;
                    }
                }
            }
            
            docline_add_token(line, start, i, TOKEN_NONE);
        }
        else if (strchr("()[]{},.;", ch)) {
            docline_add_token(line, i, i + 1, TOKEN_NONE);
            i++;
        }
        else {
            i++;
        }
    }
    
    line->dirty = false;
}




// vector_docline implementations
vector_docline* vec_docline_create() {
    vector_docline* vec = (vector_docline*)malloc(sizeof(vector_docline));
    vec->data = nullptr;
    vec->size = 0;
    vec->capacity = 0;
    return vec;
}

void vec_docline_destroy(vector_docline* vec) {
    if (!vec) return;
    
    for (u32 i = 0; i < vec->size; i++) {
        docline_destroy(vec->data[i]);
    }
    
    if (vec->data) {
        free(vec->data);
    }
    free(vec);
}

void vec_docline_clear(vector_docline* vec) {
    if (!vec) return;
    
    for (u32 i = 0; i < vec->size; i++) {
        docline_destroy(vec->data[i]);
    }
    vec->size = 0;
}

void vec_docline_reserve(vector_docline* vec, u32 count) {
    if (!vec || count <= vec->capacity) return;
    
    document_line** new_data = (document_line**)realloc(vec->data, count * sizeof(document_line*));
    if (new_data) {
        vec->data = new_data;
        vec->capacity = count;
    }
}

void vec_docline_resize(vector_docline* vec, u32 size) {
    if (!vec) return;
    
    if (size > vec->capacity) {
        vec_docline_reserve(vec, size);
    }
    
    if (size > vec->size) {
        for (u32 i = vec->size; i < size; i++) {
            vec->data[i] = docline_create();
        }
    } else if (size < vec->size) {
        for (u32 i = size; i < vec->size; i++) {
            docline_destroy(vec->data[i]);
        }
    }
    
    vec->size = size;
}

void vec_docline_push(vector_docline* vec, document_line* line) {
    if (!vec || !line) return;
    
    if (vec->size >= vec->capacity) {
        u32 new_capacity = vec->capacity == 0 ? 16 : vec->capacity * 2;
        vec_docline_reserve(vec, new_capacity);
    }
    
    vec->data[vec->size] = line;
    vec->size++;
}

u32 vec_docline_size(vector_docline* vec) {
    return vec ? vec->size : 0;
}

document_line* vec_docline_get(vector_docline* vec, u32 index) {
    if (!vec || index >= vec->size) return nullptr;
    return vec->data[index];
}

void vec_docline_insert(vector_docline* vec, u32 index, document_line* line) {
    if (!vec || !line || index > vec->size) return;
    
    if (vec->size >= vec->capacity) {
        u32 new_capacity = vec->capacity == 0 ? 16 : vec->capacity * 2;
        vec_docline_reserve(vec, new_capacity);
    }
    
    for (u32 i = vec->size; i > index; i--) {
        vec->data[i] = vec->data[i - 1];
    }
    
    vec->data[index] = line;
    vec->size++;
}

void vec_docline_remove(vector_docline* vec, u32 index) {
    if (!vec || index >= vec->size) return;
    
    docline_destroy(vec->data[index]);
    
    for (u32 i = index; i < vec->size - 1; i++) {
        vec->data[i] = vec->data[i + 1];
    }
    
    vec->size--;
}

vector_docline* vec_docline_clone(vector_docline* vec) {
    if (!vec) return nullptr;
    
    vector_docline* clone = vec_docline_create();
    vec_docline_reserve(clone, vec->capacity);
    
    for (u32 i = 0; i < vec->size; i++) {
        document_line* original = vec->data[i];
        document_line* line_clone = docline_create_with_text(original->text);
        line_clone->dirty = original->dirty;
        
        if (original->token_count > 0) {
            line_clone->tokens = (token_span*)malloc(original->token_capacity * sizeof(token_span));
            memcpy(line_clone->tokens, original->tokens, original->token_count * sizeof(token_span));
            line_clone->token_count = original->token_count;
            line_clone->token_capacity = original->token_capacity;
        }
        
        vec_docline_push(clone, line_clone);
    }
    
    return clone;
}