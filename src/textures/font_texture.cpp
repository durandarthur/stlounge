// Font 1 look-alike, drawn from ProggyForever, the scalable font embedded in Dear
// ImGui (imgui_draw.cpp). A private copy of stb_truetype makes signed distance fields.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "platform/log.h"
#include "textures/artistic.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

// Defined in imgui_draw.cpp: the font file, compressed with stb_compress.
const char* ImGui_GetDefaultCompressedFontDataProggyForever(int* out_size);

namespace sp {

namespace {

// stb_decompress from stb.h (public domain, Sean Barrett), as used by imgui_draw.cpp,
// rewritten without global state and with bounds checks on the input.
class Decompressor {
public:
    std::vector<unsigned char> run(const unsigned char* in, size_t inSize) {
        if (inSize < 16 || in4(in) != 0x57bC0000u || in4(in + 4) != 0) return {};
        const unsigned olen = in4(in + 8);
        if (olen == 0 || olen > (64u << 20)) return {};
        std::vector<unsigned char> out(olen);
        outBegin_ = out.data();
        outEnd_ = out.data() + olen;
        dout_ = out.data();
        inBegin_ = in;
        const unsigned char* end = in + inSize;
        const unsigned char* i = in + 16;
        for (;;) {
            if (i + 6 > end) return {};
            const unsigned char* old = i;
            i = token(i);
            if (bad_) return {};
            if (i == old) {
                if (i[0] == 0x05 && i[1] == 0xfa && dout_ == outEnd_) return out;
                return {};
            }
        }
    }

private:
    static unsigned in2(const unsigned char* p) { return (p[0] << 8) + p[1]; }
    static unsigned in3(const unsigned char* p) { return (p[0] << 16) + in2(p + 1); }
    static unsigned in4(const unsigned char* p) { return (static_cast<unsigned>(p[0]) << 24) + in3(p + 1); }

    void match(const unsigned char* data, unsigned len) {
        if (dout_ + len > outEnd_ || data < outBegin_) {
            bad_ = true;
            return;
        }
        while (len--) *dout_++ = *data++;
    }
    void lit(const unsigned char* data, unsigned len) {
        if (dout_ + len > outEnd_ || data < inBegin_) {
            bad_ = true;
            return;
        }
        std::memcpy(dout_, data, len);
        dout_ += len;
    }
    const unsigned char* token(const unsigned char* i) {
        if (*i >= 0x20) {
            if (*i >= 0x80) match(dout_ - i[1] - 1, i[0] - 0x80 + 1), i += 2;
            else if (*i >= 0x40) match(dout_ - (in2(i) - 0x4000 + 1), i[2] + 1), i += 3;
            else lit(i + 1, i[0] - 0x20 + 1), i += 1 + (i[0] - 0x20 + 1);
        } else {
            if (*i >= 0x18) match(dout_ - (in3(i) - 0x180000 + 1), i[3] + 1), i += 4;
            else if (*i >= 0x10) match(dout_ - (in3(i) - 0x100000 + 1), in2(i + 3) + 1), i += 5;
            else if (*i >= 0x08) lit(i + 2, in2(i) - 0x0800 + 1), i += 2 + (in2(i) - 0x0800 + 1);
            else if (*i == 0x07) lit(i + 3, in2(i + 1) + 1), i += 3 + (in2(i + 1) + 1);
            else if (*i == 0x06) match(dout_ - (in3(i + 1) + 1), i[4] + 1), i += 5;
            else if (*i == 0x04) match(dout_ - (in3(i + 1) + 1), in2(i + 4) + 1), i += 6;
        }
        return i;
    }

    unsigned char *outBegin_ = nullptr, *outEnd_ = nullptr, *dout_ = nullptr;
    const unsigned char* inBegin_ = nullptr;
    bool bad_ = false;
};

constexpr int kCell = 64;
constexpr int kGrid = 16;
constexpr int kPadding = 12;           // distance range, in pixels, on each side of the edge
constexpr float kEmPixels = 46.0f;     // ascent + descent, in pixels
constexpr float kBolden = 2.0f;        // pixels added to each side of a stroke (Font 1 is bold)

}  // namespace

ImageData makeFontTexture() {
    const int size = kCell * kGrid;
    ImageData img;
    img.width = img.height = size;
    img.channels = 4;
    img.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    for (size_t i = 3; i < img.pixels.size(); i += 4) img.pixels[i] = 255;  // far from any glyph

    int compressedSize = 0;
    const auto* compressed =
        reinterpret_cast<const unsigned char*>(ImGui_GetDefaultCompressedFontDataProggyForever(&compressedSize));
    const std::vector<unsigned char> ttf =
        compressed ? Decompressor().run(compressed, static_cast<size_t>(compressedSize)) : std::vector<unsigned char>{};
    stbtt_fontinfo info;
    if (ttf.empty() || !stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        LOG_WARN("font texture: the embedded font can't be read; Font 1 stays empty");
        return img;
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, kEmPixels);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    const float em = static_cast<float>(ascent - descent) * scale;
    const int baseline = static_cast<int>(std::lround((kCell - em) * 0.5f + static_cast<float>(ascent) * scale));
    const float perPixel = 128.0f / kPadding;

    for (int c = 33; c < 256; ++c) {
        if (c >= 127 && c < 160) continue;  // control characters
        const int glyph = stbtt_FindGlyphIndex(&info, c);  // Latin-1 = the first 256 Unicode code points
        if (glyph == 0) continue;
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&info, glyph, &advance, &lsb);
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* sdf = stbtt_GetGlyphSDF(&info, scale, glyph, kPadding, 128, perPixel, &w, &h, &xoff, &yoff);
        if (!sdf) continue;
        const int cellX = (c % kGrid) * kCell, cellY = (c / kGrid) * kCell;
        const int ox = cellX + static_cast<int>(std::lround((kCell - static_cast<float>(advance) * scale) * 0.5f));
        const int oy = cellY + baseline;
        for (int sy = 0; sy < h; ++sy)
            for (int sx = 0; sx < w; ++sx) {
                const int x = ox + xoff + sx, y = oy + yoff + sy;
                if (x < cellX || x >= cellX + kCell || y < cellY || y >= cellY + kCell) continue;
                const float d = std::min(255.0f, sdf[sy * w + sx] + kBolden * perPixel);  // > 128 inside
                const float cover = std::min(1.0f, std::max(0.0f, (d - 128.0f) / perPixel + 0.5f));
                uint8_t* p = img.pixels.data() + (static_cast<size_t>(y) * size + x) * 4;
                p[0] = p[1] = p[2] = static_cast<uint8_t>(std::lround(cover * 255.0f));
                p[3] = static_cast<uint8_t>(255 - static_cast<int>(d));
            }
        stbtt_FreeSDF(sdf, nullptr);
    }
    return img;
}

}  // namespace sp
