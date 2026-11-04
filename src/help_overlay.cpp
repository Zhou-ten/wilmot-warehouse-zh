#define WIN32_LEAN_AND_MEAN
#include "help_overlay.h"
#include "help_label_signatures.h"
#include "asset_io.h"
#include "MinHook.h"
#include "lodepng.h"
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>

namespace {
using namespace mod_assets;
constexpr GLenum bgra = 0x80e1, bgr = 0x80e0;
constexpr GLenum unsigned8888Rev = 0x8367, unpackBufferBinding = 0x88ef;
constexpr std::array<unsigned, 7> helpIds{0x58, 0x60, 0x62, 0x64, 0x67, 0x71, 0x74};

struct Rectangle {
    std::uint16_t x, y, width, height;
    bool within(unsigned w, unsigned h) const {
        return width && height && x + width <= w && y + height <= h;
    }
};
struct Patch {
    Rectangle rect;
    Bytes original, translated;
};
struct Probe {
    unsigned x, y;
    std::array<unsigned char, 4> color;
};
struct Artwork {
    unsigned id, width, height;
    std::array<Probe, 384> probes;
    std::vector<Patch> patches;
};
struct Texture {
    HGLRC context;
    GLuint name;
    const Artwork* art;
    GLint x, y;
    bool flipped;
    bool chinese;
    std::optional<Patch> label;
};

struct GlyphMask {
    unsigned width{}, height{};
    Bytes opacity;
};

std::vector<Artwork> artwork;
std::vector<Texture> textures;
std::array<GlyphMask, 3> labelMasks;
decltype(&glTexImage2D) originalImage;
decltype(&glTexSubImage2D) originalSubImage;
decltype(&glDeleteTextures) originalDelete;
decltype(&wglGetProcAddress) originalWglProc;
void (*writeLog)(const char*);
bool chinese;

bool readRect(Reader& reader, Rectangle& r) {
    return reader.integer(r.x) && reader.integer(r.y) &&
           reader.integer(r.width) && reader.integer(r.height);
}

void copyPixels(Bytes& destination, unsigned destinationStride, unsigned dx, unsigned dy,
                const Bytes& source, unsigned sourceStride, const Rectangle& rect) {
    for (unsigned row = 0; row < rect.height; ++row) {
        std::memcpy(destination.data() + 4 * ((dy + row) * destinationStride + dx),
                    source.data() + 4 * ((rect.y + row) * sourceStride + rect.x),
                    4 * rect.width);
    }
}

bool loadArtwork(Reader& reader, const std::wstring& directory, Artwork& art) {
    std::uint16_t width, height;
    std::uint64_t expectedHash;
    std::uint32_t patchCount;
    if (!reader.integer(art.id) || !reader.integer(width) || !reader.integer(height) ||
        !reader.integer(expectedHash) || !reader.integer(patchCount) || !patchCount || patchCount > 64 ||
        std::find(helpIds.begin(), helpIds.end(), art.id) == helpIds.end()) return false;
    wchar_t name[24];
    swprintf(name, sizeof(name) / sizeof(*name), L"%x.png", art.id);
    auto source = readFile(directory + L"lib\\library\\" + name);
    Bytes pixels;
    if (hash(source) != expectedHash || lodepng::decode(pixels, art.width, art.height, source) ||
        width != art.width || height != art.height) return false;

    // Probe across the image, including the artwork, to identify the actual
    // game texture rather than merely matching its dimensions.
    std::uint32_t random = 0x57b1e7u;
    for (auto& probe : art.probes) {
        random = random * 1664525u + 1013904223u;
        unsigned p = (random >> 4) % (width * height);
        probe.x = p % width;
        probe.y = p / width;
        std::copy_n(pixels.data() + 4 * p, 4, probe.color.begin());
    }
    art.patches.reserve(patchCount);
    for (unsigned i = 0; i < patchCount; ++i) {
        Patch patch;
        auto& rect = patch.rect;
        std::uint32_t pngSize, copyCount;
        if (!readRect(reader, rect) || !rect.within(width, height) ||
            !reader.integer(pngSize) || !reader.integer(copyCount) || copyCount > 8) return false;
        const auto png = reader.take(pngSize);
        unsigned patchWidth, patchHeight;
        lodepng::State state;
        if (!png || lodepng_inspect(&patchWidth, &patchHeight, &state, png, pngSize) ||
            patchWidth != rect.width || patchHeight != rect.height ||
            lodepng::decode(patch.translated, patchWidth, patchHeight, png, pngSize)) return false;
        patch.original.resize(4u * rect.width * rect.height);
        copyPixels(patch.original, rect.width, 0, 0, pixels, width, rect);
        for (unsigned c = 0; c < copyCount; ++c) {
            Rectangle icon;
            std::uint16_t x, y;
            if (!readRect(reader, icon) || !reader.integer(x) || !reader.integer(y) ||
                !icon.within(width, height) || x < rect.x || y < rect.y ||
                x + icon.width > rect.x + rect.width || y + icon.height > rect.y + rect.height) return false;
            copyPixels(patch.translated, rect.width, x - rect.x, y - rect.y, pixels, width, icon);
        }
        art.patches.push_back(std::move(patch));
    }
    return true;
}

unsigned padded(unsigned value) {
    unsigned n = 1;
    while (n < value) n *= 2;
    return n;
}

bool plausibleSize(const Artwork& art, GLsizei width, GLsizei height) {
    return width > 0 && height > 0 &&
           (unsigned(width) == art.width || unsigned(width) == padded(art.width)) &&
           (unsigned(height) == art.height || unsigned(height) == padded(art.height));
}

struct Unpack {
    GLint alignment{}, rowLength{}, skipRows{}, skipPixels{};
    Unpack() {
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
    }
    void tight() const {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    }
    void restore() const {
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
    }
};

bool hasUnpackBuffer() {
    GLint name = 0;
    glGetIntegerv(unpackBufferBinding, &name);
    return name != 0;
}

void apply(Texture& texture) {
    Unpack unpack;
    unpack.tight();
    Bytes flipped;
    const auto applyPatch = [&](const Patch& patch) {
        const auto& rect = patch.rect;
        const Bytes& selected = chinese ? patch.translated : patch.original;
        const unsigned y = texture.flipped ? texture.art->height - rect.y - rect.height : rect.y;
        const unsigned char* data = selected.data();
        if (texture.flipped) {
            flipped.resize(selected.size());
            const unsigned rowBytes = rect.width * 4;
            for (unsigned row = 0; row < rect.height; ++row)
                std::memcpy(flipped.data() + row * rowBytes,
                            data + (rect.height - 1 - row) * rowBytes, rowBytes);
            data = flipped.data();
        }
        originalSubImage(GL_TEXTURE_2D, 0, texture.x + rect.x, texture.y + y,
                         rect.width, rect.height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    };
    if (texture.label) applyPatch(*texture.label);
    else for (const auto& patch : texture.art->patches) applyPatch(patch);
    unpack.restore();
    texture.chinese = chinese;
}

void forget(HGLRC context, GLuint name) {
    textures.erase(std::remove_if(textures.begin(), textures.end(), [=](const Texture& t) {
        return t.context == context && t.name == name;
    }), textures.end());
}

void registerTexture(GLint x, GLint y, const Artwork* art, bool flipped, std::optional<Patch> label = {}) {
    GLint name = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
    const auto context = wglGetCurrentContext();
    if (!name || !context) return;
    forget(context, name);
    textures.push_back({context, static_cast<GLuint>(name), art, x, y, flipped, false, std::move(label)});
    if (chinese) apply(textures.back());
}

using help_labels::Kind;

GlyphMask extractInk(const Patch& patch, bool darkInk) {
    const unsigned width = patch.rect.width, height = patch.rect.height;
    const auto opacity = [&](unsigned x, unsigned y) {
        const auto red = patch.translated[4 * (y * width + x)];
        return darkInk ? 255 - red : red;
    };
    unsigned left = width, top = height, right = 0, bottom = 0;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            if (opacity(x, y)) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
    if (left >= right || top >= bottom) return {};
    GlyphMask mask{right - left, bottom - top, {}};
    mask.opacity.reserve(mask.width * mask.height);
    for (unsigned y = top; y < bottom; ++y)
        for (unsigned x = left; x < right; ++x)
            mask.opacity.push_back(static_cast<unsigned char>(opacity(x, y)));
    return mask;
}

bool nearAspect(unsigned aspect, unsigned expected) {
    return aspect * 100 >= expected * 94 && aspect * 100 <= expected * 106;
}

bool registerLabel(GLint x, GLint y, unsigned width, unsigned height, unsigned stride,
                   bool blueFirst, const unsigned char* pixels) {
    const auto pixel = [&](unsigned px, unsigned py) { return pixels + py * stride + px * 4; };
    const unsigned aspect = width * 100 / height;
    for (const auto& signature : help_labels::signatures) {
        if (!nearAspect(aspect, signature.aspectPercent)) continue;
        const auto matches = [&](const auto& samples, bool light) {
            return std::count_if(samples.begin(), samples.end(), [&](unsigned sample) {
                const auto p = pixel((sample >> 8) * width / 256, (sample & 255) * height / 256);
                return p[3] >= 225 && (light ? p[1] > 165 : p[1] < 90);
            }) >= 21;
        };
        if (!matches(signature.ink, !signature.darkInk) ||
            !matches(signature.background, signature.darkInk)) continue;

        const auto& mask = labelMasks[static_cast<unsigned>(signature.kind)];
        if (mask.opacity.empty()) return false;
        const bool title = signature.kind == Kind::Title;
        const unsigned left = width * (title ? 3 : 18) / 100;
        const unsigned top = height * (title ? 19 : 20) / 100;
        const unsigned right = width * (title ? 79 : 82) / 100;
        const unsigned bottom = height * (title ? 81 : 85) / 100;
        const unsigned patchWidth = right - left, patchHeight = bottom - top;
        Patch patch{{static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(top),
                     static_cast<std::uint16_t>(patchWidth), static_cast<std::uint16_t>(patchHeight)}, {}, {}};
        const auto paper = pixel(left, top);
        if (paper[3] != 255) return false;
        const std::array<unsigned char, 4> background{
            paper[blueFirst ? 2 : 0], paper[1], paper[blueFirst ? 0 : 2], 255};
        const std::array<unsigned char, 3> ink = signature.darkInk ?
            std::array<unsigned char, 3>{0, 0, 0} : std::array<unsigned char, 3>{255, 252, 234};
        patch.original.resize(4 * patchWidth * patchHeight);
        patch.translated.resize(patch.original.size());
        for (unsigned py = 0; py < patchHeight; ++py) {
            for (unsigned px = 0; px < patchWidth; ++px) {
                const auto p = pixel(left + px, top + py);
                const unsigned offset = 4 * (py * patchWidth + px);
                patch.original[offset] = p[blueFirst ? 2 : 0];
                patch.original[offset + 1] = p[1];
                patch.original[offset + 2] = p[blueFirst ? 0 : 2];
                patch.original[offset + 3] = p[3];
                std::copy(background.begin(), background.end(), patch.translated.begin() + offset);
            }
        }
        const unsigned glyphHeight = std::max(1u, height * (title ? 43 : 28) / 100);
        const unsigned margin = title ? width / 75 : 1;
        const unsigned glyphWidth = std::clamp(glyphHeight * mask.width / mask.height, 1u, patchWidth - margin * 2);
        const unsigned dx = title ? margin : (patchWidth - glyphWidth) / 2;
        const unsigned dy = (patchHeight - glyphHeight) / 2;
        for (unsigned gy = 0; gy < glyphHeight; ++gy) {
            for (unsigned gx = 0; gx < glyphWidth; ++gx) {
                const unsigned alpha = mask.opacity[(gy * mask.height / glyphHeight) * mask.width +
                                                    gx * mask.width / glyphWidth];
                const unsigned offset = 4 * ((dy + gy) * patchWidth + dx + gx);
                for (unsigned c = 0; c < 3; ++c)
                    patch.translated[offset + c] = static_cast<unsigned char>(
                        (background[c] * (255 - alpha) + ink[c] * alpha + 127) / 255);
            }
        }
        registerTexture(x, y, nullptr, false, std::move(patch));
        writeLog(title ? "Help heading matched." :
                 signature.kind == Kind::Keyboard ? "Help keyboard tab matched." : "Help joypad tab matched.");
        return true;
    }
    return false;
}

void registerUpload(GLint x, GLint y, GLsizei width, GLsizei height,
                    GLenum format, GLenum type, const void* pixels) {
    if (!pixels || width <= 0 || height <= 0 || (type != GL_UNSIGNED_BYTE && type != unsigned8888Rev) ||
        (format != GL_RGB && format != bgr && format != GL_RGBA && format != bgra)) return;
    const auto candidates = [&](const Artwork& art) { return plausibleSize(art, width, height); };
    const unsigned channels = (format == GL_RGB || format == bgr) ? 3 : 4;
    const bool possibleLabel = channels == 4 && type == GL_UNSIGNED_BYTE && height >= 25 && height <= 512 &&
        (nearAspect(unsigned(width) * 100 / height, 272) || nearAspect(unsigned(width) * 100 / height, 509));
    if ((!possibleLabel && std::none_of(artwork.begin(), artwork.end(), candidates)) || hasUnpackBuffer()) return;
    if (type == unsigned8888Rev && channels != 4) return;
    const bool blueFirst = (format == bgra || format == bgr);
    const Unpack unpack;
    const unsigned rowBytes = channels * (unpack.rowLength ? unpack.rowLength : width);
    const unsigned stride = (rowBytes + unpack.alignment - 1) & ~(unpack.alignment - 1);
    const auto start = static_cast<const unsigned char*>(pixels) +
                       unpack.skipRows * stride + unpack.skipPixels * channels;
    for (const auto& art : artwork) {
        if (!candidates(art)) continue;
        for (bool flipped : {false, true}) {
            const bool match = std::all_of(art.probes.begin(), art.probes.end(), [&](const Probe& p) {
                const auto q = start + (flipped ? art.height - 1 - p.y : p.y) * stride + p.x * channels;
                if (channels == 4 && q[3] != p.color[3]) return false;
                if (p.color[3] != 255) return true;
                return q[blueFirst ? 2 : 0] == p.color[0] && q[1] == p.color[1] &&
                       q[blueFirst ? 0 : 2] == p.color[2];
            });
            if (!match) continue;
            registerTexture(x, y, &art, flipped);
            char message[96];
            std::snprintf(message, sizeof(message), "Help texture %x.png matched (%dx%d).", art.id, width, height);
            writeLog(message);
            return;
        }
    }
    if (possibleLabel) registerLabel(x, y, width, height, stride, blueFirst, start);
}

void WINAPI imageHook(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                      GLint border, GLenum format, GLenum type, const void* pixels) {
    originalImage(target, level, internal, width, height, border, format, type, pixels);
    if (target != GL_TEXTURE_2D || level != 0 || border != 0) return;
    if (!textures.empty()) {
        GLint name = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
        forget(wglGetCurrentContext(), name);
    }
    registerUpload(0, 0, width, height, format, type, pixels);
}

void WINAPI subImageHook(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
                         GLsizei height, GLenum format, GLenum type, const void* pixels) {
    originalSubImage(target, level, x, y, width, height, format, type, pixels);
    if (target == GL_TEXTURE_2D && level == 0) registerUpload(x, y, width, height, format, type, pixels);
}

void WINAPI deleteHook(GLsizei count, const GLuint* names) {
    if (!textures.empty() && names) {
        const auto context = wglGetCurrentContext();
        for (GLsizei i = 0; i < count; ++i) forget(context, names[i]);
    }
    originalDelete(count, names);
}

PROC WINAPI wglProcHook(LPCSTR name) {
    auto address = originalWglProc(name);
    if (!address || !name) return address;
    // SDL may receive driver entry points directly, bypassing opengl32 exports.
    // Route the three core functions through the same hooks in either case.
    if (!std::strcmp(name, "glTexImage2D")) return reinterpret_cast<PROC>(imageHook);
    if (!std::strcmp(name, "glTexSubImage2D")) return reinterpret_cast<PROC>(subImageHook);
    if (!std::strcmp(name, "glDeleteTextures")) return reinterpret_cast<PROC>(deleteHook);
    return address;
}
} // namespace

void prepareHelpOverlay(HMODULE module, bool initialChinese, void (*logger)(const char*)) {
    wchar_t location[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, location, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    std::wstring directory(location, length);
    directory.resize(directory.find_last_of(L"\\/") + 1);
    const auto file = readFile(directory + L"wilmzh-help.wzh");
    if (file.empty()) return;
    Reader reader{file.data(), file.size()};
    const auto magic = reader.take(4);
    std::uint32_t count;
    if (!magic || std::memcmp(magic, "WHI1", 4) || !reader.integer(count) || count != helpIds.size()) {
        logger("Help overlay recipe is invalid; leaving images in English.");
        return;
    }
    std::vector<Artwork> loaded;
    for (unsigned i = 0; i < count; ++i) {
        Artwork art;
        if (!loadArtwork(reader, directory, art) ||
            std::any_of(loaded.begin(), loaded.end(), [&](const Artwork& a) { return a.id == art.id; })) {
            logger("Help overlay assets did not match; leaving images in English.");
            return;
        }
        loaded.push_back(std::move(art));
    }
    if (reader.remaining) return;
    const std::array<void*, 4> targets{reinterpret_cast<void*>(glTexImage2D),
                                        reinterpret_cast<void*>(glTexSubImage2D),
                                        reinterpret_cast<void*>(glDeleteTextures),
                                        reinterpret_cast<void*>(wglGetProcAddress)};
    if (MH_CreateHook(targets[0], reinterpret_cast<void*>(imageHook), reinterpret_cast<void**>(&originalImage)) != MH_OK ||
        MH_CreateHook(targets[1], reinterpret_cast<void*>(subImageHook), reinterpret_cast<void**>(&originalSubImage)) != MH_OK ||
        MH_CreateHook(targets[2], reinterpret_cast<void*>(deleteHook), reinterpret_cast<void**>(&originalDelete)) != MH_OK ||
        MH_CreateHook(targets[3], reinterpret_cast<void*>(wglProcHook), reinterpret_cast<void**>(&originalWglProc)) != MH_OK) {
        for (auto target : targets) MH_RemoveHook(target);
        logger("Help texture hooks unavailable; leaving images in English.");
        return;
    }
    artwork = std::move(loaded);
    for (const auto& art : artwork) {
        if (art.id == 0x71) labelMasks[static_cast<unsigned>(Kind::Keyboard)] = extractInk(art.patches.front(), true);
        if (art.id == 0x74) labelMasks[static_cast<unsigned>(Kind::Joypad)] = extractInk(art.patches.front(), false);
        if (art.id == 0x62)
            for (const auto& patch : art.patches)
                if (patch.rect.x == 37 && patch.rect.y == 34)
                    labelMasks[static_cast<unsigned>(Kind::Title)] = extractInk(patch, true);
    }
    chinese = initialChinese;
    writeLog = logger;
    logger("Loaded overlays for 7 help images.");
}

void setHelpLanguage(bool enabled) {
    chinese = enabled;
    if (textures.empty()) return;
    const auto context = wglGetCurrentContext();
    if (!context || hasUnpackBuffer()) return;
    GLint binding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    for (auto& texture : textures) {
        if (texture.context != context || texture.chinese == chinese) continue;
        glBindTexture(GL_TEXTURE_2D, texture.name);
        apply(texture);
    }
    glBindTexture(GL_TEXTURE_2D, binding);
}
