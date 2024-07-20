#pragma once

#include <winuser.h>

static const char TranslateButton = 'G';
static const char TranslateButtonMod = 0x07;

static const char HelperButton = 'H';
static const char HelperButtonMod = 0x07;

static const float SubtitleShadowRadius = 5.0f;

static const wchar_t* UserAgent = L"InGameTranslator/1.0";
static const wchar_t* ServerAddress = L"localhost";
static const INTERNET_PORT ServerPort = 8888;

static const wchar_t* FontPath = L".\\font.spritefont";