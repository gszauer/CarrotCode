#include "vectors.h"
#include "strings.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

struct vector_str8 {
    u8_string** data;
    unsigned int size;
    unsigned int capacity;
};

struct vector_str16 {
    u16_string** data;
    unsigned int size;
    unsigned int capacity;
};

struct vector_str32 {
    u32_string** data;
    unsigned int size;
    unsigned int capacity;
};

// Helper function to copy a u8_string
static u8_string* copy_u8_string(u8_string* src) {
    if (!src) return u8str_create();
    u32 len = u8str_length(src);
    u8_string* copy = u8str_create();
    if (len > 0) {
        u8str_insert(copy, src, 0, 0, len);
    }
    return copy;
}

// Helper function to copy a u16_string
static u16_string* copy_u16_string(u16_string* src) {
    if (!src) return u16str_create();
    u32 len = u16str_length(src);
    u16_string* copy = u16str_create();
    if (len > 0) {
        u16str_insert(copy, src, 0, 0, len);
    }
    return copy;
}

// Helper function to copy a u32_string
static u32_string* copy_u32_string(u32_string* src) {
    if (!src) return u32str_create();
    u32 len = u32str_length(src);
    u32_string* copy = u32str_create();
    if (len > 0) {
        u32str_insert(copy, src, 0, 0, len);
    }
    return copy;
}

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
        u8str_destroy(vec->data[i]);
    }
    free(vec->data);
    free(vec);
}

void vec_str8_clear(vector_str8* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u8str_destroy(vec->data[i]);
    }
    vec->size = 0;
}

void vec_str8_reserve(vector_str8* vec, unsigned int count) {
    if (!vec || count <= vec->capacity) return;
    u8_string** new_data = (u8_string**)realloc(vec->data, count * sizeof(u8_string*));
    if (new_data) {
        vec->data = new_data;
        vec->capacity = count;
    }
}

void vec_str8_resize(vector_str8* vec, unsigned int size) {
    if (!vec) return;
    
    if (size < vec->size) {
        for (unsigned int i = size; i < vec->size; i++) {
            u8str_destroy(vec->data[i]);
        }
    } else if (size > vec->size) {
        if (size > vec->capacity) {
            vec_str8_reserve(vec, size);
        }
        for (unsigned int i = vec->size; i < size; i++) {
            vec->data[i] = u8str_create();
        }
    }
    vec->size = size;
}

void vec_str8_push(vector_str8* vec, u8_string* string) {
    if (!vec) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str8_reserve(vec, new_cap);
    }
    
    vec->data[vec->size] = copy_u8_string(string);
    vec->size++;
}

unsigned int vec_str8_size(vector_str8* vec) {
    return vec ? vec->size : 0;
}

u8_string* vec_str8_get(vector_str8* vec, unsigned int index) {
    if (!vec || index >= vec->size) return NULL;
    return vec->data[index];
}

void vec_str8_insert(vector_str8* vec, unsigned int index, u8_string* string) {
    if (!vec || index > vec->size) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str8_reserve(vec, new_cap);
    }
    
    for (unsigned int i = vec->size; i > index; i--) {
        vec->data[i] = vec->data[i - 1];
    }
    
    vec->data[index] = copy_u8_string(string);
    vec->size++;
}

void vec_str8_remove(vector_str8* vec, unsigned int index) {
    if (!vec || index >= vec->size) return;
    
    u8str_destroy(vec->data[index]);
    
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
        clone->data[i] = copy_u8_string(vec->data[i]);
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
        u16str_destroy(vec->data[i]);
    }
    free(vec->data);
    free(vec);
}

void vec_str16_clear(vector_str16* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u16str_destroy(vec->data[i]);
    }
    vec->size = 0;
}

void vec_str16_reserve(vector_str16* vec, unsigned int count) {
    if (!vec || count <= vec->capacity) return;
    u16_string** new_data = (u16_string**)realloc(vec->data, count * sizeof(u16_string*));
    if (new_data) {
        vec->data = new_data;
        vec->capacity = count;
    }
}

void vec_str16_resize(vector_str16* vec, unsigned int size) {
    if (!vec) return;
    
    if (size < vec->size) {
        for (unsigned int i = size; i < vec->size; i++) {
            u16str_destroy(vec->data[i]);
        }
    } else if (size > vec->size) {
        if (size > vec->capacity) {
            vec_str16_reserve(vec, size);
        }
        for (unsigned int i = vec->size; i < size; i++) {
            vec->data[i] = u16str_create();
        }
    }
    vec->size = size;
}

void vec_str16_push(vector_str16* vec, u16_string* string) {
    if (!vec) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str16_reserve(vec, new_cap);
    }
    
    vec->data[vec->size] = copy_u16_string(string);
    vec->size++;
}

unsigned int vec_str16_size(vector_str16* vec) {
    return vec ? vec->size : 0;
}

u16_string* vec_str16_get(vector_str16* vec, unsigned int index) {
    if (!vec || index >= vec->size) return NULL;
    return vec->data[index];
}

void vec_str16_insert(vector_str16* vec, unsigned int index, u16_string* string) {
    if (!vec || index > vec->size) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str16_reserve(vec, new_cap);
    }
    
    for (unsigned int i = vec->size; i > index; i--) {
        vec->data[i] = vec->data[i - 1];
    }
    
    vec->data[index] = copy_u16_string(string);
    vec->size++;
}

void vec_str16_remove(vector_str16* vec, unsigned int index) {
    if (!vec || index >= vec->size) return;
    
    u16str_destroy(vec->data[index]);
    
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
        clone->data[i] = copy_u16_string(vec->data[i]);
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
        u32str_destroy(vec->data[i]);
    }
    free(vec->data);
    free(vec);
}

void vec_str32_clear(vector_str32* vec) {
    if (!vec) return;
    for (unsigned int i = 0; i < vec->size; i++) {
        u32str_destroy(vec->data[i]);
    }
    vec->size = 0;
}

void vec_str32_reserve(vector_str32* vec, unsigned int count) {
    if (!vec || count <= vec->capacity) return;
    u32_string** new_data = (u32_string**)realloc(vec->data, count * sizeof(u32_string*));
    if (new_data) {
        vec->data = new_data;
        vec->capacity = count;
    }
}

void vec_str32_resize(vector_str32* vec, unsigned int size) {
    if (!vec) return;
    
    if (size < vec->size) {
        for (unsigned int i = size; i < vec->size; i++) {
            u32str_destroy(vec->data[i]);
        }
    } else if (size > vec->size) {
        if (size > vec->capacity) {
            vec_str32_reserve(vec, size);
        }
        for (unsigned int i = vec->size; i < size; i++) {
            vec->data[i] = u32str_create();
        }
    }
    vec->size = size;
}

void vec_str32_push(vector_str32* vec, u32_string* string) {
    if (!vec) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str32_reserve(vec, new_cap);
    }
    
    vec->data[vec->size] = copy_u32_string(string);
    vec->size++;
}

unsigned int vec_str32_size(vector_str32* vec) {
    return vec ? vec->size : 0;
}

u32_string* vec_str32_get(vector_str32* vec, unsigned int index) {
    if (!vec || index >= vec->size) return NULL;
    return vec->data[index];
}

void vec_str32_insert(vector_str32* vec, unsigned int index, u32_string* string) {
    if (!vec || index > vec->size) return;
    
    if (vec->size == vec->capacity) {
        unsigned int new_cap = vec->capacity == 0 ? 4 : vec->capacity * 2;
        vec_str32_reserve(vec, new_cap);
    }
    
    for (unsigned int i = vec->size; i > index; i--) {
        vec->data[i] = vec->data[i - 1];
    }
    
    vec->data[index] = copy_u32_string(string);
    vec->size++;
}

void vec_str32_remove(vector_str32* vec, unsigned int index) {
    if (!vec || index >= vec->size) return;
    
    u32str_destroy(vec->data[index]);
    
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
        clone->data[i] = copy_u32_string(vec->data[i]);
    }
    clone->size = vec->size;
    
    return clone;
}