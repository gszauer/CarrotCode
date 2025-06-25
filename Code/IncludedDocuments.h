#pragma once

#include "Document.h"

std::shared_ptr<TextEdit::Document> CreateWelcomeDocument();
std::shared_ptr<TextEdit::Document> CreateAboutDocument();
std::shared_ptr<TextEdit::Document> CreateHowToDocument();
std::shared_ptr<TextEdit::Document> CreateScriptAPIDocument();
std::u32string GeneratePrompt();