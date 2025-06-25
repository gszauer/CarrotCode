#ifdef _WIN32
#include "glad.h"
#endif // _WIN32
#include "application.h"
#include "Renderer.h"
#include "Font.h"
#include "DocumentView.h"
#include "FileMenu.h"
#include "DocumentContainer.h"
#include "Platform.h"
#include "ScriptingInterface.h"
#include "miniz.h"
#include "IncludedDocuments.h"

#include <string>     
#include <fstream>    
#include <sstream>    
#include <locale>     
#include <codecvt>    
#include <stdexcept>  
#include <vector>     
#include <iostream>

#include "Styles.h"
#include <vector>    

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#undef DrawText

std::shared_ptr<TextEdit::Renderer> gRenderer;
std::shared_ptr<TextEdit::FileMenu> gMenu;
std::shared_ptr<TextEdit::DocumentContainer> gDocContainer;
std::shared_ptr<TextEdit::Font> gLargeFont;
std::shared_ptr<TextEdit::Font> gSmallFont;
std::shared_ptr<TextEdit::Font> gMediumFont;

extern unsigned int Roboto_Size;
extern unsigned char Roboto[];

extern unsigned int NotoEmoji_Size;
extern unsigned char NotoEmoji[];

TextEdit::Rect contentArea;
TextEdit::Rect titleBarDragArea;

// Window state
bool gIsMaximized = false;

// Window button hover/click state
int gHoveredButton = -1; // -1: none, 0: minimize, 1: maximize, 2: close
int gClickedButton = -1; // -1: none, 0: minimize, 1: maximize, 2: close
float gLastMouseX = 0;
float gLastMouseY = 0;

void OnApplicationCloseButtonClicked();
void OnApplicationMaximizeButtonClicked();
void OnApplicationRestoreButtonClicked();
void OnApplicationMinimizeButtonClicked();

void CopyPrompt();
void BundleFiles();

std::u32string Utf8ToUtf32(const char* utf8_string, unsigned int bytes);
std::string Utf32ToUtf8(const std::u32string& utf32_string);
std::wstring u32string_to_wstring(const std::u32string& u32str);
std::u32string wstring_to_u32string(const std::wstring& wstr);

void GetTitleBarInteractiveRect(unsigned int* outX, unsigned int* outY, unsigned int* outW, unsigned int* outH) {
	if (outX != 0) {
		*outX = (unsigned int)titleBarDragArea.x;
	}
	if (outY != 0) {
		*outY = (unsigned int)titleBarDragArea.y;
	}
	if (outW != 0) {
		*outW = (unsigned int)titleBarDragArea.width;
	}
	if (outH != 0) {
		*outH = (unsigned int)titleBarDragArea.height;
	}
}

void ExternalCreateNewDocument() {
	std::shared_ptr<TextEdit::Document> document = TextEdit::Document::Create();
	gDocContainer->AddDocument(document);
}

bool Initialize(float dpi) {
	TextEdit::Styles::ApplyDPI(dpi);

	gLargeFont = TextEdit::Font::Create(Roboto, Roboto_Size, TextEdit::Styles::REGULAR_FONT_SIZE, dpi);
	gLargeFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::REGULAR_FONT_SIZE, dpi);

	gRenderer = TextEdit::Renderer::Create(dpi, gLargeFont);

	gSmallFont = TextEdit::Font::Create(Roboto, Roboto_Size, TextEdit::Styles::SMALL_FONT_SIZE, dpi);
	gSmallFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::SMALL_FONT_SIZE, dpi);

	gMediumFont = TextEdit::Font::Create(Roboto, Roboto_Size, TextEdit::Styles::MEDIUM_FONT_SIZE, dpi);
	gMediumFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::MEDIUM_FONT_SIZE, dpi);

	gDocContainer = std::make_shared<TextEdit::DocumentContainer>(gRenderer, gLargeFont, gSmallFont);
#if 0
	gDocContainer->AddDocument(CreateWelcomeDocument());
#endif

	gMenu = std::make_shared<TextEdit::FileMenu>(gRenderer, gMediumFont);

	std::vector<TextEdit::FileMenu::MenuItem> fileMenu = {
		{ U"New", []() {
			std::shared_ptr<TextEdit::Document> document = TextEdit::Document::Create();
			gDocContainer->AddDocument(document);
		}, true },
		{ U"Open", []() {
			PlatformSelectFile(0, [](const char* path, unsigned char* buffer, unsigned int size) {
				std::shared_ptr<TextEdit::Document> document = TextEdit::Document::Create();
				document->SetSource(path, false);
				document->Insert(Utf8ToUtf32((const char*)buffer, size));
				document->ClearHistory();
				document->MarkClean();
				gDocContainer->AddDocument(document);
			});
		}, true },
		{ U"Close", []() {
			gDocContainer->CloseActiveDocumentView();
		}, true },
#ifndef __EMSCRIPTEN__
		{ U"---", []() {}, true },
#endif // !__EMSCRIPTEN__
		{ U"Save", []() {
			auto docView = gDocContainer->GetActiveDocumentView();
			if (docView != nullptr) {
				docView->GetTarget()->Save();
			}
		}, true },
#ifndef __EMSCRIPTEN__
		{ U"Save As", []() { // Not in web builds
			auto docView = gDocContainer->GetActiveDocumentView();
			if (docView != nullptr) {
				docView->GetTarget()->SaveAs();
			}
		}, true },
#endif // !__EMSCRIPTEN__
		{ U"---", []() {}, true },
		{ U"Zip All", []() {
			mz_zip_archive zip;
			memset(&zip, 0, sizeof(zip));

			// Initialize zip archive in memory
			if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
				// Handle error
				return;
			}

			auto openDocs = gDocContainer->GetAllOpenDocuments();
			for (int i = 0, size = (int)openDocs.size(); i < size; ++i) {
				auto utf32str = openDocs[i]->DocumentAsString();
				std::string fileContent = Utf32ToUtf8(utf32str);
				utf32str = openDocs[i]->GetName();
				std::string fileName = Utf32ToUtf8(utf32str);

				// Add file to zip archive
				mz_zip_writer_add_mem(&zip, fileName.c_str(),
									  fileContent.data(), fileContent.size(),
									  MZ_DEFAULT_COMPRESSION);
			}

			// Finalize the archive
			void* pBuf = NULL;
			size_t pSize = 0;
			if (!mz_zip_writer_finalize_heap_archive(&zip, &pBuf, &pSize)) {
				mz_zip_writer_end(&zip);
				// Handle error
				return;
			}

			// Now we have the zip data
			const unsigned char* zip_data = (const unsigned char*)pBuf;
			unsigned int zip_bytes = (unsigned int)pSize;
			PlatformSetNextSaveAsName("CarrotCodeExport.zip");
			PlatformSaveAs(zip_data, zip_bytes, 0);

			// Clean up
			mz_zip_writer_end(&zip);
			mz_free(pBuf);
		}, true },
#ifndef __EMSCRIPTEN__
		{ U"Save All", []() {
			gDocContainer->SaveAll();
		}, true },
#endif // !__EMSCRIPTEN__
		{ U"Close All", []() {
			gDocContainer->CloseAll();
		}, true },
#ifndef __EMSCRIPTEN__
		{ U"---", []() {}, true },
		{ U"Exit", []() {
			PlatformExit();
		}, true },
#endif // !__EMSCRIPTEN__
	};
	gMenu->AddMenuOption(U"FILE", fileMenu, 1.2f);

	std::vector<TextEdit::FileMenu::MenuItem> editMenu = {
		{ U"Undo", []() {
			auto docView = gDocContainer->GetActiveDocumentView();
			if (docView != nullptr) {
				docView->GetTarget()->Undo();
			}
		}, true },
		{ U"Redo", []() {
		auto docView = gDocContainer->GetActiveDocumentView();
			if (docView != nullptr) {
				docView->GetTarget()->Redo();
			}
		}, true },
		{ U"---", []() {}, true },
		{ U"Cut", []() {
			auto docView = gDocContainer->GetActiveDocumentView();
			if (docView != nullptr) {
				docView->PerformCut();
			}
		}, true },
		{ U"Copy", []() {
			auto docView = gDocContainer->GetActiveDocumentView();
			if (docView != nullptr) {
				docView->PerformCopy();
			}
		}, true },
		{ U"Paste", []() {
			auto docView = gDocContainer->GetActiveDocumentView();
			if (docView != nullptr) {
				docView->PerformPaste();
			}
		}, true }
	};
	gMenu->AddMenuOption(U"EDIT", editMenu, 1.0f);

	std::vector<TextEdit::FileMenu::MenuItem> viewMenu = {
		{ U"Bundle Files", []() {
			BundleFiles();
		}, true },
		{ U"Copy Prompt", []() {
			CopyPrompt();
		}, true },
		{ U"Execute File", []() {
			auto active = gDocContainer->GetActiveDocumentView();
			if (active == nullptr) {
				return;
			}
			auto doc = active->GetTarget();
			if (doc == nullptr) {
				return;
			}

			TextEdit::ScriptingInterface scripting(gDocContainer);
			if (!scripting.Initialize()) {
				std::cerr << "Failed to initialize scripting interface" << std::endl;
				return;
			}

			std::string to_execute = Utf32ToUtf8(doc->DocumentAsString());
			scripting.ExecuteScript(to_execute);

		}, true },
	};
	gMenu->AddMenuOption(U"AUTOMATION", viewMenu, 1.5f);

	std::vector<TextEdit::FileMenu::MenuItem> fontMenu = {
		{ U"50%", []() {
			gLargeFont->LoadGlyphs(Roboto, Roboto_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 0.5f, TextEdit::Styles::DPI);
			gLargeFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 0.5f, TextEdit::Styles::DPI);
		}, true },
		{ U"75%", []() {
			gLargeFont->LoadGlyphs(Roboto, Roboto_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 0.75f, TextEdit::Styles::DPI);
			gLargeFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 0.75f, TextEdit::Styles::DPI);
		}, true },
		{ U"100%", []() {
			gLargeFont->LoadGlyphs(Roboto, Roboto_Size, TextEdit::Styles::REGULAR_FONT_SIZE			       , TextEdit::Styles::DPI);
			gLargeFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::REGULAR_FONT_SIZE	       , TextEdit::Styles::DPI);
		}, true },
		{ U"125%", []() {
			gLargeFont->LoadGlyphs(Roboto, Roboto_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 1.25f, TextEdit::Styles::DPI);
			gLargeFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 1.25f, TextEdit::Styles::DPI);
		}, true },
		{ U"150%", []() {
			gLargeFont->LoadGlyphs(Roboto, Roboto_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 1.5f, TextEdit::Styles::DPI);
			gLargeFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 1.5f, TextEdit::Styles::DPI);
		}, true },
		{ U"200%", []() {
			gLargeFont->LoadGlyphs(Roboto, Roboto_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 2.0f, TextEdit::Styles::DPI);
			gLargeFont->LoadEmojis(NotoEmoji, NotoEmoji_Size, TextEdit::Styles::REGULAR_FONT_SIZE * 2.0f, TextEdit::Styles::DPI);
		}, true },
	};
	gMenu->AddMenuOption(U"FONT", fontMenu, 0.8f);

	std::vector<TextEdit::FileMenu::MenuItem> aboutMenu = {
		{ U"About", []() {
			gDocContainer->AddDocument(CreateAboutDocument());
		}, true },
		{ U"How To", []() {
			gDocContainer->AddDocument(CreateHowToDocument());
		}, true },
		{ U"Script API", []() {

			gDocContainer->AddDocument(CreateScriptAPIDocument());
		}, true},
	};
	gMenu->AddMenuOption(U"HELP", aboutMenu, 1.2f);

	titleBarDragArea.x = 0;
	titleBarDragArea.y = 0;
	titleBarDragArea.width = 6000;
	titleBarDragArea.height = TextEdit::Styles::FILE_MENU_HEIGHT;

	return true;
}

bool Tick(unsigned int screenWidth, unsigned int screenHeight, float deltaTime) {
	glClearColor(TextEdit::Styles::BGColor.r, TextEdit::Styles::BGColor.g, TextEdit::Styles::BGColor.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	contentArea = TextEdit::Rect(0.0f, TextEdit::Styles::FILE_MENU_HEIGHT, (float)screenWidth, (float)screenHeight - TextEdit::Styles::FILE_MENU_HEIGHT);

	gRenderer->StartFrame(0, 0, screenWidth, screenHeight);

	gDocContainer->Update(deltaTime);
	gDocContainer->Display(contentArea.x, contentArea.y, contentArea.width, contentArea.height);

	gRenderer->ClearClip();
	titleBarDragArea.x = gMenu->Display(0, 0, (float)screenWidth, (float)screenHeight);
	titleBarDragArea.y = 0;
	titleBarDragArea.width = (float)screenWidth - titleBarDragArea.x - TextEdit::Styles::WINDOW_BUTTON_WIDTH * 3.0f;
	titleBarDragArea.height = TextEdit::Styles::FILE_MENU_HEIGHT;

#ifndef __EMSCRIPTEN__
	// Draw window title
	float titleX_01 = titleBarDragArea.x + titleBarDragArea.width * 0.5f;
	float titleX_02 = screenWidth * 0.5f;
	std::u32string title = U"Carrot.Code";

	// Center the title by measuring it first
	gRenderer->SetFont(gMediumFont);
	float titleWidth = 0;
	for (char32_t c : title) {
		auto glyph = gMediumFont->GetGlyph(c);
		titleWidth += glyph.advance;
	}
	
	float titleX = titleX_02 - titleWidth * 0.5f;
	if (titleX < titleBarDragArea.x) {
		titleX = titleX_01 - titleWidth * 0.5f;
	}


	float titleY = (TextEdit::Styles::FILE_MENU_HEIGHT - gMediumFont->GetLineHeight()) / 2.0f;
	gRenderer->SetClip(titleBarDragArea.x, titleBarDragArea.y, titleBarDragArea.width, titleBarDragArea.height);
	gRenderer->DrawText(title, titleX, titleY,
		TextEdit::Styles::TitleBarTextColor.r,
		TextEdit::Styles::TitleBarTextColor.g,
		TextEdit::Styles::TitleBarTextColor.b);
	gRenderer->ClearClip();

	// Draw window control buttons (minimize, maximize/restore, close)
	float buttonWidth = TextEdit::Styles::WINDOW_BUTTON_WIDTH;
	float buttonHeight = TextEdit::Styles::FILE_MENU_HEIGHT;
	float buttonY = 0;

	// Determine which button is hovered based on current mouse position
	gHoveredButton = -1;
	if (gLastMouseY >= 0 && gLastMouseY <= buttonHeight) {
		float closeX = (float)screenWidth - buttonWidth;
		float maxX = closeX - buttonWidth;
		float minX = maxX - buttonWidth;

		if (gLastMouseX >= closeX && gLastMouseX <= closeX + buttonWidth) {
			gHoveredButton = 2; // Close button
		}
		else if (gLastMouseX >= maxX && gLastMouseX <= maxX + buttonWidth) {
			gHoveredButton = 1; // Maximize button
		}
		else if (gLastMouseX >= minX && gLastMouseX <= minX + buttonWidth) {
			gHoveredButton = 0; // Minimize button
		}
	}

	// Close button
	float closeX = (float)screenWidth - buttonWidth;
	TextEdit::Styles::Color closeColor = TextEdit::Styles::WindowButtonColor;
	if (gHoveredButton == 2) {
		closeColor = (gClickedButton == 2) ? TextEdit::Styles::WindowButtonCloseHoverColor : TextEdit::Styles::WindowButtonCloseHoverColor;
		// Make it darker when clicked
		if (gClickedButton == 2) {
			closeColor.r *= 0.8f;
			closeColor.g *= 0.8f;
			closeColor.b *= 0.8f;
		}
	}
	gRenderer->DrawRect(closeX, buttonY, buttonWidth, buttonHeight,
		closeColor.r, closeColor.g, closeColor.b);

	// Draw close icon
	float iconY = (buttonHeight - gMediumFont->GetLineHeight()) / 2.0f;
	float closeIconX = closeX + (buttonWidth - gMediumFont->GetGlyph(U'❌').advance) / 2.0f;
	gRenderer->DrawText(U"❌", closeIconX, iconY,
		TextEdit::Styles::WindowButtonIconColor.r,
		TextEdit::Styles::WindowButtonIconColor.g,
		TextEdit::Styles::WindowButtonIconColor.b);

	// Maximize/Restore button
	float maxX = closeX - buttonWidth;
	TextEdit::Styles::Color maxColor = TextEdit::Styles::WindowButtonColor;
	if (gHoveredButton == 1) {
		maxColor = TextEdit::Styles::WindowButtonHoverColor;
		// Make it darker when clicked
		if (gClickedButton == 1) {
			maxColor.r *= 0.8f;
			maxColor.g *= 0.8f;
			maxColor.b *= 0.8f;
		}
	}
	gRenderer->DrawRect(maxX, buttonY, buttonWidth, buttonHeight,
		maxColor.r, maxColor.g, maxColor.b);

	// Draw maximize/restore icon
	{
		char32_t maxIcon = U'🔲';
		float maxIconX = maxX + (buttonWidth - gMediumFont->GetGlyph(maxIcon).advance) * 0.5f;
		float maxIconY = iconY + buttonHeight * 0.5f;
		if (gIsMaximized) {
			gRenderer->SetLayoutScale(0.75f);
			maxIconX = maxX + (buttonWidth - gMediumFont->GetGlyph(maxIcon).advance) * 0.75f;
		}
		gRenderer->DrawChar(maxIcon, maxIconX, maxIconY, TextEdit::Styles::WindowButtonIconColor.r, TextEdit::Styles::WindowButtonIconColor.g, TextEdit::Styles::WindowButtonIconColor.b);
		if (gIsMaximized) {
			maxIconX = maxX + (buttonWidth - gMediumFont->GetGlyph(maxIcon).advance) * 0.6f;
			maxIconY += buttonHeight * 0.2f;
			gRenderer->DrawChar(maxIcon, maxIconX, maxIconY, TextEdit::Styles::WindowButtonIconColor.r, TextEdit::Styles::WindowButtonIconColor.g, TextEdit::Styles::WindowButtonIconColor.b);

			gRenderer->SetLayoutScale(1.0f);
		}
	}


	// Minimize button
	float minX = maxX - buttonWidth;
	TextEdit::Styles::Color minColor = TextEdit::Styles::WindowButtonColor;
	if (gHoveredButton == 0) {
		minColor = TextEdit::Styles::WindowButtonHoverColor;
		// Make it darker when clicked
		if (gClickedButton == 0) {
			minColor.r *= 0.8f;
			minColor.g *= 0.8f;
			minColor.b *= 0.8f;
		}
	}
	gRenderer->DrawRect(minX, buttonY, buttonWidth, buttonHeight,
		minColor.r, minColor.g, minColor.b);

	// Draw minimize icon
	float minIconX = minX + (buttonWidth - gMediumFont->GetGlyph(U'➖').advance) / 2.0f;
	gRenderer->DrawText(U"➖", minIconX, iconY + 6 * TextEdit::Styles::DPI,
		TextEdit::Styles::WindowButtonIconColor.r,
		TextEdit::Styles::WindowButtonIconColor.g,
		TextEdit::Styles::WindowButtonIconColor.b);
#endif // !__EMSCRIPTEN__


	gRenderer->EndFrame();

	return true;
}

void Shutdown() {
	gRenderer = nullptr;
	gDocContainer = nullptr;
	gMenu = nullptr;
	gLargeFont = nullptr;
	gSmallFont = nullptr;
}

void OnInput(const InputEvent& e) {
	bool skipInput = false;

	// Update mouse position for hover state
	if (e.type == InputEvent::Type::MOUSE_MOVE) {
		gLastMouseX = (float)e.mouse.x;
		gLastMouseY = (float)e.mouse.y;
	}

	// Handle window control buttons
	if (e.type == InputEvent::Type::MOUSE_DOWN) {
		float buttonWidth = TextEdit::Styles::WINDOW_BUTTON_WIDTH;
		float buttonHeight = TextEdit::Styles::FILE_MENU_HEIGHT;

		// Check if click is in window control area
		if (e.mouse.y >= 0 && e.mouse.y <= buttonHeight) {
			// Get screen width from content area + its x position
			float screenWidth = contentArea.x + contentArea.width;

#ifndef __EMSCRIPTEN__
			// Close button
			float closeX = screenWidth - buttonWidth;
			if (e.mouse.x >= closeX && e.mouse.x <= closeX + buttonWidth) {
				gClickedButton = 2;
				OnApplicationCloseButtonClicked();
				
				return;
			}

			// Maximize/Restore button
			float maxX = closeX - buttonWidth;
			if (e.mouse.x >= maxX && e.mouse.x <= maxX + buttonWidth) {
				gClickedButton = 1;
				if (gIsMaximized) {
					OnApplicationRestoreButtonClicked();
				}
				else {
					OnApplicationMaximizeButtonClicked();
				}
				// Toggle maximized state (fake)
				gIsMaximized = !gIsMaximized;
				return;
			}

			// Minimize button
			float minX = maxX - buttonWidth;
			if (e.mouse.x >= minX && e.mouse.x <= minX + buttonWidth) {
				gClickedButton = 0;
				OnApplicationMinimizeButtonClicked();

				return;
			}
#endif
		}
	}

	// Clear clicked state on mouse up
	if (e.type == InputEvent::Type::MOUSE_UP) {
		gClickedButton = -1;
	}

	// Check if we should skip input based on menu interaction
	if (gMenu->IsMenuVisible() && e.type == InputEvent::Type::MOUSE_DOWN) {
		TextEdit::Rect menuRect = gMenu->GetOpenMenuRect();
		// Only skip input if the click is within the open menu dropdown
		if (menuRect.Contains((float)e.mouse.x, (float)e.mouse.y)) {
			skipInput = true;
		}
	}

	if (e.type == InputEvent::Type::MOUSE_DOWN) {
		// Check if click is in the file menu bar area
		if (e.mouse.y < TextEdit::Styles::FILE_MENU_HEIGHT) {
			// File menu area clicked
			gDocContainer->CloseMenus();
		}
	}

	gMenu->OnInput(e);

	if (e.type == InputEvent::Type::MOUSE_DOWN) {
		if (!contentArea.Contains((float)e.mouse.x, (float)e.mouse.y)) {
			return;
		}
	}
	if (skipInput) {
		return;
	}
	gDocContainer->OnInput(e);
}

void OnFileDropped(const char* path, const void* data, unsigned int bytes) {
	auto newDoc = TextEdit::Document::Create();
	std::u32string u32content = Utf8ToUtf32((const char*)data, bytes);
	newDoc->Insert(u32content);
	newDoc->SetSource(path, false);
	newDoc->ClearHistory();
	newDoc->MarkClean();
	gDocContainer->AddDocument(newDoc);
}

#ifdef __EMSCRIPTEN__
std::string Utf32ToUtf8(const std::u32string& utf32_string) {
	if (utf32_string.empty()) {
		return "";
	}

	std::string utf8_string;
	utf8_string.reserve(utf32_string.length() * 4); // Reserve for worst case

	for (char32_t codepoint : utf32_string) {
		if (codepoint <= 0x7F) {
			// 1-byte sequence
			utf8_string.push_back(static_cast<char>(codepoint));
		}
		else if (codepoint <= 0x7FF) {
			// 2-byte sequence
			utf8_string.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
			utf8_string.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
		else if (codepoint <= 0xFFFF) {
			// 3-byte sequence
			// Check for invalid surrogates
			if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
				// Invalid surrogate, use replacement character
				utf8_string.push_back(static_cast<char>(0xEF));
				utf8_string.push_back(static_cast<char>(0xBF));
				utf8_string.push_back(static_cast<char>(0xBD));
			}
			else {
				utf8_string.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
				utf8_string.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
				utf8_string.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			}
		}
		else if (codepoint <= 0x10FFFF) {
			// 4-byte sequence
			utf8_string.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
			utf8_string.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
			utf8_string.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			utf8_string.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
		else {
			// Invalid codepoint, use replacement character (U+FFFD)
			utf8_string.push_back(static_cast<char>(0xEF));
			utf8_string.push_back(static_cast<char>(0xBF));
			utf8_string.push_back(static_cast<char>(0xBD));
		}
	}

	return utf8_string;
}

std::u32string Utf8ToUtf32(const char* utf8_string, unsigned int bytes) {
	if (bytes == 0 || !utf8_string) {
		return U"";
	}

	std::u32string utf32_string;
	utf32_string.reserve(bytes); // Reserve memory

	for (unsigned int i = 0; i < bytes; ) {
		unsigned char c = static_cast<unsigned char>(utf8_string[i]);
		char32_t codepoint = 0;
		int sequence_length = 0;

		// Determine the sequence length and initial bits
		if ((c & 0x80) == 0) {
			// 1-byte sequence (0xxxxxxx)
			codepoint = c;
			sequence_length = 1;
		}
		else if ((c & 0xE0) == 0xC0) {
			// 2-byte sequence (110xxxxx)
			codepoint = c & 0x1F;
			sequence_length = 2;
		}
		else if ((c & 0xF0) == 0xE0) {
			// 3-byte sequence (1110xxxx)
			codepoint = c & 0x0F;
			sequence_length = 3;
		}
		else if ((c & 0xF8) == 0xF0) {
			// 4-byte sequence (11110xxx)
			codepoint = c & 0x07;
			sequence_length = 4;
		}
		else {
			// Invalid UTF-8 start byte, skip it
			i++;
			utf32_string.push_back(U'\uFFFD'); // Replacement character
			continue;
		}

		// Check if we have enough bytes remaining
		if (i + sequence_length > bytes) {
			// Incomplete sequence at end of string
			utf32_string.push_back(U'\uFFFD'); // Replacement character
			break;
		}

		// Process continuation bytes
		bool valid = true;
		for (int j = 1; j < sequence_length; j++) {
			unsigned char next = static_cast<unsigned char>(utf8_string[i + j]);
			if ((next & 0xC0) != 0x80) {
				// Invalid continuation byte
				valid = false;
				break;
			}
			codepoint = (codepoint << 6) | (next & 0x3F);
		}

		if (!valid) {
			// Invalid sequence, add replacement character and skip this byte
			utf32_string.push_back(U'\uFFFD');
			i++;
			continue;
		}

		// Validate the codepoint
		if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
			// Invalid codepoint (too large or surrogate)
			utf32_string.push_back(U'\uFFFD');
		}
		else {
			// Check for overlong sequences
			bool overlong = false;
			if (sequence_length == 2 && codepoint < 0x80) overlong = true;
			else if (sequence_length == 3 && codepoint < 0x800) overlong = true;
			else if (sequence_length == 4 && codepoint < 0x10000) overlong = true;

			if (overlong) {
				utf32_string.push_back(U'\uFFFD');
			}
			else {
				utf32_string.push_back(codepoint);
			}
		}

		i += sequence_length;
	}

	return utf32_string;
}
#else
#include <Windows.h>
#undef min
#undef max
#undef DrawText
std::string Utf32ToUtf8(const std::u32string& utf32_string) {
	if (utf32_string.empty()) {
		return "";
	}

	// 1. First convert UTF-32 to UTF-16, handling surrogate pairs
	std::wstring utf16_string;
	utf16_string.reserve(utf32_string.length() * 2); // Reserve memory for worst case

	for (char32_t codepoint : utf32_string) {
		if (codepoint <= 0xFFFF) {
			// Character is in the Basic Multilingual Plane (BMP)
			// Check if it's a surrogate value (invalid as standalone)
			if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
				// Invalid surrogate values should not appear in UTF-32
				// You could handle this error differently if needed
				utf16_string.push_back(L'?'); // Replacement character
			}
			else {
				utf16_string.push_back(static_cast<wchar_t>(codepoint));
			}
		}
		else if (codepoint <= 0x10FFFF) {
			// Character requires a surrogate pair
			codepoint -= 0x10000;
			wchar_t high = static_cast<wchar_t>((codepoint >> 10) + 0xD800);
			wchar_t low = static_cast<wchar_t>((codepoint & 0x3FF) + 0xDC00);
			utf16_string.push_back(high);
			utf16_string.push_back(low);
		}
		else {
			// Invalid Unicode codepoint (> 0x10FFFF)
			utf16_string.push_back(L'?'); // Replacement character
		}
	}

	// 2. Convert UTF-16 to UTF-8 using WideCharToMultiByte
	if (utf16_string.empty()) {
		return "";
	}

	// Get the required buffer size
	const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, utf16_string.c_str(),
		static_cast<int>(utf16_string.length()),
		NULL, 0, NULL, NULL);
	if (utf8_length == 0) {
		// You could call GetLastError() here for more detailed diagnostics
		return "Error: Cannot determine length of the converted string.";
	}

	// Perform the conversion
	std::string utf8_string(utf8_length, '\0');
	if (WideCharToMultiByte(CP_UTF8, 0, utf16_string.c_str(),
		static_cast<int>(utf16_string.length()),
		&utf8_string[0], utf8_length, NULL, NULL) == 0) {
		// You could call GetLastError() here for more detailed diagnostics
		return "Error: Failed to convert from UTF-16 to UTF-8.";
	}

	return utf8_string;
}

std::u32string Utf8ToUtf32(const char* utf8_string, unsigned int bytes) {
	if (bytes == 0 || !utf8_string) {
		return U"";
	}

	// 1. Convert UTF-8 to UTF-16 using MultiByteToWideChar.
	// The function expects the byte count as a signed int.
	const int utf16_length = MultiByteToWideChar(CP_UTF8, 0, utf8_string, (int)bytes, NULL, 0);
	if (utf16_length == 0) {
		// You could call GetLastError() here for more detailed diagnostics
		return U"Error: Cannot determine length of the converted string.";
	}

	std::wstring utf16_string(utf16_length, L'\0');
	if (MultiByteToWideChar(CP_UTF8, 0, utf8_string, (int)bytes, &utf16_string[0], utf16_length) == 0) {
		// You could call GetLastError() here for more detailed diagnostics
		return U"Error: Failed to convert from UTF-8 to UTF-16.";
	}

	// 2. Convert the resulting UTF-16 to UTF-32 by handling surrogate pairs.
	std::u32string utf32_string;
	utf32_string.reserve(utf16_string.length()); // Reserve memory for efficiency

	for (size_t i = 0; i < utf16_string.length(); ++i) {
		wchar_t high = utf16_string[i];
		// Check for a high surrogate
		if (high >= 0xD800 && high <= 0xDBFF && i + 1 < utf16_string.length()) {
			wchar_t low = utf16_string[i + 1];
			// Check for a low surrogate
			if (low >= 0xDC00 && low <= 0xDFFF) {
				// Combine them to form the full code point
				char32_t codepoint = 0x10000 + (((high & 0x3FF) << 10) | (low & 0x3FF));
				utf32_string.push_back(codepoint);
				i++; // Skip the low surrogate since it has been processed
				continue;
			}
		}
		// It's a regular character (in the Basic Multilingual Plane) or an unpaired surrogate.
		utf32_string.push_back(high);
	}
	return utf32_string;
}

#endif // __EMSCRIPTEN__ 

void BundleFiles() {
	auto allDocs = gDocContainer->GetAllOpenDocuments();

	std::u32string prompt;

	//prompt += U"# Files\n\n";
	for (int i = 0, size = (int)allDocs.size(); i < size; ++i) {
		prompt += U"## ";
		prompt += allDocs[i]->GetName();
		prompt += U"\n```\n";
		prompt += allDocs[i]->DocumentAsString();
		prompt += U"\n```\n\n";
	}

	PlatformWriteClipboardU16(u32string_to_wstring(prompt));
}

void CopyPrompt() {
	auto allDocs = gDocContainer->GetAllOpenDocuments();
	std::shared_ptr<TextEdit::Document> openDoc = nullptr;
	if (gDocContainer->GetActiveDocumentView() != nullptr) {
		openDoc = gDocContainer->GetActiveDocumentView()->GetTarget();
	}

	std::u32string prompt;

	if (allDocs.size() > 0) {
		prompt += U"# Files\n";
		for (int i = 0, size = (int)allDocs.size(); i < size; ++i) {
			if (allDocs[i].get() == openDoc.get()) {
				continue;
			}
			prompt += U"## ";
			prompt += allDocs[i]->GetName();
			prompt += U"\n```\n";
			prompt += allDocs[i]->DocumentAsString();
			prompt += U"\n```\n";
		}
		prompt += U"\n";
	}

	prompt += GeneratePrompt();

	if (openDoc != nullptr) {
		prompt += U"# User Prompt\n\n";
		prompt += openDoc->DocumentAsString();
		prompt += U"\n\n";
	}

	PlatformWriteClipboardU16(u32string_to_wstring(prompt));
}

// Helper for string conversions (simplified) Needed for copy / paste on windows (should be in platform layer)
std::wstring u32string_to_wstring(const std::u32string& u32str) {
	std::wstring result;
	result.reserve(u32str.length());
	for (char32_t c : u32str) {
		if (c <= 0xFFFF) {
			result.push_back(static_cast<wchar_t>(c));
		}
		else if (c <= 0x10FFFF) {
			c -= 0x10000;
			result.push_back(static_cast<wchar_t>(0xD800 | (c >> 10)));
			result.push_back(static_cast<wchar_t>(0xDC00 | (c & 0x3FF)));
		}
	}
	return result;
}

std::u32string wstring_to_u32string(const std::wstring& wstr) {
	std::u32string result;
	result.reserve(wstr.length());
	for (size_t i = 0; i < wstr.length(); ++i) {
		wchar_t wc = wstr[i];
		if (wc >= 0xD800 && wc <= 0xDBFF) {
			if (i + 1 < wstr.length()) {
				wchar_t wc2 = wstr[i + 1];
				if (wc2 >= 0xDC00 && wc2 <= 0xDFFF) {
					char32_t codepoint = 0x10000;
					codepoint += static_cast<char32_t>((wc & 0x3FF) << 10);
					codepoint += static_cast<char32_t>(wc2 & 0x3FF);
					result.push_back(codepoint);
					i++;
					continue;
				}
			}
			result.push_back(0xFFFD);
		}
		else {
			result.push_back(static_cast<char32_t>(wc));
		}
	}
	return result;
}