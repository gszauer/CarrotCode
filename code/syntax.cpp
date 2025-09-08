#define CARROT_INCLUDE_SYNTAX_DEFS
#include "syntax.h"
#undef CARROT_INCLUDE_SYNTAX_DEFS
#include "vectors.h"
#include <cstdlib>
#include <cstring>
#include <cctype>


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