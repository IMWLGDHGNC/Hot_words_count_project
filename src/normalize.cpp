#include "normalize.hpp"
#include <vector>
#include <iterator>
#include <fstream>

#include "utfcpp/source/utf8.h"

#if __has_include("uni-algo/norm.h")
#define HAS_UNI_ALGO 1
#include "uni_algo/norm.h"
#endif

static std::string sanitize_utf8(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    utf8::replace_invalid(raw.begin(), raw.end(), std::back_inserter(out));
    if (out.size() >= 3 &&
        static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

std::string normalize_utf8_nfc(const std::string& input) {
    std::string clean = sanitize_utf8(input);
#ifdef HAS_UNI_ALGO
    std::u32string u32;
    utf8::utf8to32(clean.begin(), clean.end(), std::back_inserter(u32));
    auto nfc32 = una::norm::to_nfc(u32);
    std::string out;
    utf8::utf32to8(nfc32.begin(), nfc32.end(), std::back_inserter(out));
    return out;
#else
    return clean;
#endif
}
