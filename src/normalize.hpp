#pragma once
#include <string>

// Normalize and sanitize text:
// - Fix invalid UTF-8 sequences and strip BOM
// - Optionally normalize to NFC/NFKC if uni-algo is available

// Returns valid UTF-8. If NFC is not available, returns sanitized input.
std::string normalize_utf8_nfc(const std::string& input);
