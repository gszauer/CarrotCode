# Carrot Code Text Editor
Carrot Code is a minimal text editor with syntax highlight support, inspired by [lite](https://github.com/rxi/lite).

[Carrot Code V1](https://github.com/gszauer/CarrotCode/tree/V1) was focused on performance with an OpenGL backend, full unicode rendering, and true MDI support. In contrast [V2](https://github.com/gszauer/CarrotCode/tree/V2) embeds [font 8x16](https://github.com/hubenchang0515/font8x16/tree/master), displaying only ascii characters. V2 also uses a tiled software renderer, and is written to be more C like. 

* [Run Carrot Code V1](https://gabormakesgames.com/Prototypes/CarrotV1/index.html)
* [Run Carrot Code V2](https://gabormakesgames.com/Prototypes/Carrot/index.html)

## MIT License

Copyright (c) 2025 [Gabor Szauer](https://gabormakesgames.com/)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


---

C:\Users\User\Git\CarrotCode>build_windows.bat
Building Carrot Code for Windows...
Compiling...
windows.cpp
application.cpp
software_renderer.cpp
strings.cpp
document.cpp
imgui.cpp
view.cpp
code\view.cpp(408): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(409): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(544): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(545): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(619): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(620): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(636): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(637): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(666): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(667): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(668): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(773): warning C4244: '=': conversion from 'f32' to 'u32', possible loss of data
code\view.cpp(774): warning C4244: '=': conversion from 'f32' to 'u32', possible loss of data
code\view.cpp(1352): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1353): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1368): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1369): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1400): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1403): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1527): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1528): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1555): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
code\view.cpp(1556): warning C4244: 'initializing': conversion from 'u32' to 'f32', possible loss of data
Linking...
document.obj : error LNK2019: unresolved external symbol "struct document_line * __cdecl docline_create(void)" (?docline_create@@YAPAUdocument_line@@XZ) referenced in function "struct document * __cdecl doc_create(unsigned int,bool)" (?doc_create@@YAPAUdocument@@I_N@Z)
document.obj : error LNK2019: unresolved external symbol "struct document_line * __cdecl docline_create_with_text(struct u32_string *)" (?docline_create_with_text@@YAPAUdocument_line@@PAUu32_string@@@Z) referenced in function "void __cdecl doc_append_line_str32(struct document *,struct u32_string *)" (?doc_append_line_str32@@YAXPAUdocument@@PAUu32_string@@@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl docline_mark_dirty(struct document_line *)" (?docline_mark_dirty@@YAXPAUdocument_line@@@Z) referenced in function "void __cdecl doc_delete_char(struct document *,unsigned int,unsigned int)" (?doc_delete_char@@YAXPAUdocument@@II@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl docline_tokenize(struct document_line *)" (?docline_tokenize@@YAXPAUdocument_line@@@Z) referenced in function "void __cdecl doc_tokenize_line(struct document *,unsigned int)" (?doc_tokenize_line@@YAXPAUdocument@@I@Z)
document.obj : error LNK2019: unresolved external symbol "struct u32_string * __cdecl docline_access_text(struct document_line *)" (?docline_access_text@@YAPAUu32_string@@PAUdocument_line@@@Z) referenced in function "struct u32_string * __cdecl doc_copy(struct document *)" (?doc_copy@@YAPAUu32_string@@PAUdocument@@@Z)
document.obj : error LNK2019: unresolved external symbol "struct token_span * __cdecl docline_access_tokens(struct document_line *)" (?docline_access_tokens@@YAPAUtoken_span@@PAUdocument_line@@@Z) referenced in function "struct token_span * __cdecl doc_get_line_tokens(struct document *,unsigned int)" (?doc_get_line_tokens@@YAPAUtoken_span@@PAUdocument@@I@Z)
document.obj : error LNK2019: unresolved external symbol "unsigned int __cdecl docline_get_token_count(struct document_line *)" (?docline_get_token_count@@YAIPAUdocument_line@@@Z) referenced in function "unsigned int __cdecl doc_get_line_token_count(struct document *,unsigned int)" (?doc_get_line_token_count@@YAIPAUdocument@@I@Z)
document.obj : error LNK2019: unresolved external symbol "unsigned int __cdecl docline_get_text_length(struct document_line *)" (?docline_get_text_length@@YAIPAUdocument_line@@@Z) referenced in function "struct u32_string * __cdecl doc_cut(struct document *)" (?doc_cut@@YAPAUu32_string@@PAUdocument@@@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl docline_text_remove(struct document_line *,unsigned int,unsigned int)" (?docline_text_remove@@YAXPAUdocument_line@@II@Z) referenced in function "void __cdecl doc_insert_str32(struct document *,unsigned int,unsigned int,struct u32_string *)" (?doc_insert_str32@@YAXPAUdocument@@IIPAUu32_string@@@Z)
document.obj : error LNK2019: unresolved external symbol "struct u32_string * __cdecl docline_text_substr(struct document_line *,unsigned int,unsigned int)" (?docline_text_substr@@YAPAUu32_string@@PAUdocument_line@@II@Z) referenced in function "struct u32_string * __cdecl doc_cut(struct document *)" (?doc_cut@@YAPAUu32_string@@PAUdocument@@@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl docline_text_insert(struct document_line *,struct u32_string const *,unsigned int,unsigned int,unsigned int)" (?docline_text_insert@@YAXPAUdocument_line@@PBUu32_string@@III@Z) referenced in function "void __cdecl doc_delete_char(struct document *,unsigned int,unsigned int)" (?doc_delete_char@@YAXPAUdocument@@II@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl docline_text_insert_char(struct document_line *,unsigned int,unsigned int)" (?docline_text_insert_char@@YAXPAUdocument_line@@II@Z) referenced in function "void __cdecl doc_insert_char(struct document *,unsigned int,unsigned int,unsigned int)" (?doc_insert_char@@YAXPAUdocument@@III@Z)
document.obj : error LNK2019: unresolved external symbol "bool __cdecl docline_is_dirty(struct document_line *)" (?docline_is_dirty@@YA_NPAUdocument_line@@@Z) referenced in function "unsigned int __cdecl doc_get_line_token_count(struct document *,unsigned int)" (?doc_get_line_token_count@@YAIPAUdocument@@I@Z)
document.obj : error LNK2019: unresolved external symbol "struct vector_docline * __cdecl vec_docline_create(void)" (?vec_docline_create@@YAPAUvector_docline@@XZ) referenced in function "struct document * __cdecl doc_create(unsigned int,bool)" (?doc_create@@YAPAUdocument@@I_N@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl vec_docline_destroy(struct vector_docline *)" (?vec_docline_destroy@@YAXPAUvector_docline@@@Z) referenced in function "void __cdecl doc_destroy(struct document *)" (?doc_destroy@@YAXPAUdocument@@@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl vec_docline_push(struct vector_docline *,struct document_line *)" (?vec_docline_push@@YAXPAUvector_docline@@PAUdocument_line@@@Z) referenced in function "struct document * __cdecl doc_create(unsigned int,bool)" (?doc_create@@YAPAUdocument@@I_N@Z)
document.obj : error LNK2019: unresolved external symbol "unsigned int __cdecl vec_docline_size(struct vector_docline *)" (?vec_docline_size@@YAIPAUvector_docline@@@Z) referenced in function "void __cdecl doc_append_line_str32(struct document *,struct u32_string *)" (?doc_append_line_str32@@YAXPAUdocument@@PAUu32_string@@@Z)
document.obj : error LNK2019: unresolved external symbol "struct document_line * __cdecl vec_docline_get(struct vector_docline *,unsigned int)" (?vec_docline_get@@YAPAUdocument_line@@PAUvector_docline@@I@Z) referenced in function "struct u32_string * __cdecl doc_copy(struct document *)" (?doc_copy@@YAPAUu32_string@@PAUdocument@@@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl vec_docline_insert(struct vector_docline *,unsigned int,struct document_line *)" (?vec_docline_insert@@YAXPAUvector_docline@@IPAUdocument_line@@@Z) referenced in function "void __cdecl doc_append_line_str32(struct document *,struct u32_string *)" (?doc_append_line_str32@@YAXPAUdocument@@PAUu32_string@@@Z)
document.obj : error LNK2019: unresolved external symbol "void __cdecl vec_docline_remove(struct vector_docline *,unsigned int)" (?vec_docline_remove@@YAXPAUvector_docline@@I@Z) referenced in function "struct u32_string * __cdecl doc_cut(struct document *)" (?doc_cut@@YAPAUu32_string@@PAUdocument@@@Z)
view.obj : error LNK2019: unresolved external symbol "unsigned int __cdecl token_span_get_start(struct token_span *)" (?token_span_get_start@@YAIPAUtoken_span@@@Z) referenced in function "void __cdecl document_view_render(struct document_view *,struct ImGui *,struct canvas *,struct font *,bool)" (?document_view_render@@YAXPAUdocument_view@@PAUImGui@@PAUcanvas@@PAUfont@@_N@Z)
view.obj : error LNK2019: unresolved external symbol "unsigned int __cdecl token_span_get_end(struct token_span *)" (?token_span_get_end@@YAIPAUtoken_span@@@Z) referenced in function "void __cdecl document_view_render(struct document_view *,struct ImGui *,struct canvas *,struct font *,bool)" (?document_view_render@@YAXPAUdocument_view@@PAUImGui@@PAUcanvas@@PAUfont@@_N@Z)
view.obj : error LNK2019: unresolved external symbol "enum token_type __cdecl token_span_get_type(struct token_span *)" (?token_span_get_type@@YA?AW4token_type@@PAUtoken_span@@@Z) referenced in function "void __cdecl document_view_render(struct document_view *,struct ImGui *,struct canvas *,struct font *,bool)" (?document_view_render@@YAXPAUdocument_view@@PAUImGui@@PAUcanvas@@PAUfont@@_N@Z)
view.obj : error LNK2019: unresolved external symbol "struct token_span * __cdecl token_span_array_at(struct token_span *,unsigned int)" (?token_span_array_at@@YAPAUtoken_span@@PAU1@I@Z) referenced in function "void __cdecl document_view_render(struct document_view *,struct ImGui *,struct canvas *,struct font *,bool)" (?document_view_render@@YAXPAUdocument_view@@PAUImGui@@PAUcanvas@@PAUfont@@_N@Z)
build\windows\CarrotCode.exe : fatal error LNK1120: 24 unresolved externals

Build failed!