#pragma once

#include <string>

typedef void(*PlatformSaveAsResult)(const char* path);
extern "C" void PlatformSaveAs(const unsigned char* data, unsigned int size, PlatformSaveAsResult result);

typedef void(*PlatformSelectFileResult)(const char* path, unsigned char* buffer, unsigned int size);
extern "C" void PlatformSelectFile(const char* filter, PlatformSelectFileResult result);

typedef void PlatformYesNoResult(bool result);
extern "C" void PlatformYesNoAlert(const char* message, PlatformYesNoResult callback);

void PlatformWriteClipboardU16(const std::wstring& text);
std::wstring PlatformReadClipboardU16();
extern "C" void PlatformSetNextSaveAsName(const char* filename);

#ifndef __EMSCRIPTEN__
typedef void PlatofrmHasFileResult(const char* url, bool result);
extern "C" void PlatformHasFile(const char* url, PlatofrmHasFileResult callback);

typedef void(*PlatformWriteFileResult)(const char* path, bool success, void* userData);
extern "C" void PlatformWriteFile(const char* path, unsigned char* buffer, unsigned int size, PlatformWriteFileResult callback, void* userData, unsigned int userDataSize);

typedef void PlatformReadFileResult(const char* path, void* data, unsigned int size);
extern "C" void PlatformReadFile(const char* path, PlatformReadFileResult callback);

void PlatformExit();
#endif