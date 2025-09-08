#include "vectors.h"
#define CARROT_INCLUDE_STRING_DEFS
#include "strings.h"
#undef CARROT_INCLUDE_STRING_DEFS
#define CARROT_INCLUDE_SYNTAX_DEFS
#include "syntax.h"
#undef CARROT_INCLUDE_SYNTAX_DEFS
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


struct vector_str8 {
    u8_string* data;
    unsigned int size;
    unsigned int capacity;
};

struct vector_str16 {
    u16_string* data;
    unsigned int size;
    unsigned int capacity;
};

struct vector_str32 {
    u32_string* data;
    unsigned int size;
    unsigned int capacity;
};

struct vector_docline {
    document_line** data;
    u32 size;
    u32 capacity;
};

// vector_str8 implementations
vector_str8* vec_str8_create() {
    vector_str8* vec = (vector_str8*)malloc(sizeof(vector_str8));
    vec->data = NULL;
    vec->size = 0;
    vec->capacity = 0;
    return vec;
}

void vec_str8_destroy(vector_str8* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u8str_destroy(&vec->data[i]);
    }
    free(vec->data);
    free(vec);
}

void vec_str8_clear(vector_str8* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u8str_destroy(&vec->data[i]);
    }
    vec->size = 0;
}

void vec_str8_reserve(vector_str8* vec, unsigned int count) {
    if (!vec || count <= vec->capacity) return;
    u8_string* new_data = (u8_string*)realloc(vec->data, count * sizeof(u8_string));
    if (new_data) {
        vec->data = new_data;
        vec->capacity = count;
    }
}

void vec_str8_resize(vector_str8* vec, unsigned int size) {
    if (!vec) return;
    
    if (size < vec->size) {
        for (unsigned int i = size; i < vec->size; i++) {
            u8str_destroy(&vec->data[i]);
        }
    } else if (size > vec->size) {
        if (size > vec->capacity) {
            vec_str8_reserve(vec, size);
        }
        for (unsigned int i = vec->size; i < size; i++) {
            u8_string* str = u8str_create();
            vec->data[i] = *str;
            free(str);
        }
    }
    vec->size = size;
}

void vec_str8_push(vector_str8* vec, u8_string* string) {
    if (!vec || !string) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str8_reserve(vec, new_cap);
    }
    
    vec->data[vec->size] = *string;
    vec->size++;
}

unsigned int vec_str8_size(vector_str8* vec) {
    return vec ? vec->size : 0;
}

u8_string* vec_str8_get(vector_str8* vec, unsigned int index) {
    if (!vec || index >= vec->size) return NULL;
    return &vec->data[index];
}

void vec_str8_insert(vector_str8* vec, unsigned int index, u8_string* string) {
    if (!vec || !string || index > vec->size) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str8_reserve(vec, new_cap);
    }
    
    for (unsigned int i = vec->size; i > index; i--) {
        vec->data[i] = vec->data[i - 1];
    }
    
    vec->data[index] = *string;
    vec->size++;
}

void vec_str8_remove(vector_str8* vec, unsigned int index) {
    if (!vec || index >= vec->size) return;
    
    u8str_destroy(&vec->data[index]);
    
    for (unsigned int i = index; i < vec->size - 1; i++) {
        vec->data[i] = vec->data[i + 1];
    }
    vec->size--;
}

vector_str8* vec_str8_clone(vector_str8* vec) {
    if (!vec) return NULL;
    
    vector_str8* clone = vec_str8_create();
    vec_str8_reserve(clone, vec->capacity);
    
    for (unsigned int i = 0; i < vec->size; i++) {
        clone->data[i] = vec->data[i];
    }
    clone->size = vec->size;
    
    return clone;
}

// vector_str16 implementations
vector_str16* vec_str16_create() {
    vector_str16* vec = (vector_str16*)malloc(sizeof(vector_str16));
    vec->data = NULL;
    vec->size = 0;
    vec->capacity = 0;
    return vec;
}

void vec_str16_destroy(vector_str16* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u16str_destroy(&vec->data[i]);
    }
    free(vec->data);
    free(vec);
}

void vec_str16_clear(vector_str16* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u16str_destroy(&vec->data[i]);
    }
    vec->size = 0;
}

void vec_str16_reserve(vector_str16* vec, unsigned int count) {
    if (!vec || count <= vec->capacity) return;
    u16_string* new_data = (u16_string*)realloc(vec->data, count * sizeof(u16_string));
    if (new_data) {
        vec->data = new_data;
        vec->capacity = count;
    }
}

void vec_str16_resize(vector_str16* vec, unsigned int size) {
    if (!vec) return;
    
    if (size < vec->size) {
        for (unsigned int i = size; i < vec->size; i++) {
            u16str_destroy(&vec->data[i]);
        }
    } else if (size > vec->size) {
        if (size > vec->capacity) {
            vec_str16_reserve(vec, size);
        }
        for (unsigned int i = vec->size; i < size; i++) {
            u16_string* str = u16str_create();
            vec->data[i] = *str;
            free(str);
        }
    }
    vec->size = size;
}

void vec_str16_push(vector_str16* vec, u16_string* string) {
    if (!vec || !string) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str16_reserve(vec, new_cap);
    }
    
    vec->data[vec->size] = *string;
    vec->size++;
}

unsigned int vec_str16_size(vector_str16* vec) {
    return vec ? vec->size : 0;
}

u16_string* vec_str16_get(vector_str16* vec, unsigned int index) {
    if (!vec || index >= vec->size) return NULL;
    return &vec->data[index];
}

void vec_str16_insert(vector_str16* vec, unsigned int index, u16_string* string) {
    if (!vec || !string || index > vec->size) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str16_reserve(vec, new_cap);
    }
    
    for (unsigned int i = vec->size; i > index; i--) {
        vec->data[i] = vec->data[i - 1];
    }
    
    vec->data[index] = *string;
    vec->size++;
}

void vec_str16_remove(vector_str16* vec, unsigned int index) {
    if (!vec || index >= vec->size) return;
    
    u16str_destroy(&vec->data[index]);
    
    for (unsigned int i = index; i < vec->size - 1; i++) {
        vec->data[i] = vec->data[i + 1];
    }
    vec->size--;
}

vector_str16* vec_str16_clone(vector_str16* vec) {
    if (!vec) return NULL;
    
    vector_str16* clone = vec_str16_create();
    vec_str16_reserve(clone, vec->capacity);
    
    for (unsigned int i = 0; i < vec->size; i++) {
        clone->data[i] = vec->data[i];
    }
    clone->size = vec->size;
    
    return clone;
}

// vector_str32 implementations
vector_str32* vec_str32_create() {
    vector_str32* vec = (vector_str32*)malloc(sizeof(vector_str32));
    vec->data = NULL;
    vec->size = 0;
    vec->capacity = 0;
    return vec;
}

void vec_str32_destroy(vector_str32* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u32str_destroy(&vec->data[i]);
    }
    free(vec->data);
    free(vec);
}

void vec_str32_clear(vector_str32* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u32str_destroy(&vec->data[i]);
    }
    vec->size = 0;
}

void vec_str32_reserve(vector_str32* vec, unsigned int count) {
    if (!vec || count <= vec->capacity) return;
    u32_string* new_data = (u32_string*)realloc(vec->data, count * sizeof(u32_string));
    if (new_data) {
        vec->data = new_data;
        vec->capacity = count;
    }
}

void vec_str32_resize(vector_str32* vec, unsigned int size) {
    if (!vec) return;
    
    if (size < vec->size) {
        for (unsigned int i = size; i < vec->size; i++) {
            u32str_destroy(&vec->data[i]);
        }
    } else if (size > vec->size) {
        if (size > vec->capacity) {
            vec_str32_reserve(vec, size);
        }
        for (unsigned int i = vec->size; i < size; i++) {
            u32_string* str = u32str_create();
            vec->data[i] = *str;
            free(str);
        }
    }
    vec->size = size;
}

void vec_str32_push(vector_str32* vec, u32_string* string) {
    if (!vec || !string) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str32_reserve(vec, new_cap);
    }
    
    vec->data[vec->size] = *string;
    vec->size++;
}

unsigned int vec_str32_size(vector_str32* vec) {
    return vec ? vec->size : 0;
}

u32_string* vec_str32_get(vector_str32* vec, unsigned int index) {
    if (!vec || index >= vec->size) return NULL;
    return &vec->data[index];
}

void vec_str32_insert(vector_str32* vec, unsigned int index, u32_string* string) {
    if (!vec || !string || index > vec->size) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str32_reserve(vec, new_cap);
    }
    
    for (unsigned int i = vec->size; i > index; i--) {
        vec->data[i] = vec->data[i - 1];
    }
    
    vec->data[index] = *string;
    vec->size++;
}

void vec_str32_remove(vector_str32* vec, unsigned int index) {
    if (!vec || index >= vec->size) return;
    
    u32str_destroy(&vec->data[index]);
    
    for (unsigned int i = index; i < vec->size - 1; i++) {
        vec->data[i] = vec->data[i + 1];
    }
    vec->size--;
}

vector_str32* vec_str32_clone(vector_str32* vec) {
    if (!vec) return NULL;
    
    vector_str32* clone = vec_str32_create();
    vec_str32_reserve(clone, vec->capacity);
    
    for (unsigned int i = 0; i < vec->size; i++) {
        clone->data[i] = vec->data[i];
    }
    clone->size = vec->size;
    
    return clone;
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