#include "GEdge.h"
#include "include/GBitmap.h"
#include "include/GBlendMode.h"
#include "include/GColor.h"
#include "include/GMath.h"
#include "include/GMatrix.h"
#include "include/GPaint.h"
#include "include/GPixel.h"
#include "include/GPoint.h"
#include "include/GShader.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

/** Helper Functions */
inline unsigned int div255(unsigned int n) {
    return ((n + 128) * 257) >> 16;
    // return (n * ((1 << 16) + (1 << 8) + 1) + (1 << 23)) >> 24;
}

inline GPixel colorToPixel(GColor color) {
    return GPixel_PackARGB(GRoundToInt(color.a * 255), GRoundToInt(color.r * color.a * 255),
                           GRoundToInt(color.g * color.a * 255),
                           GRoundToInt(color.b * color.a * 255));
}

inline void src_row(GPixel* row_ptr, int count, GColor color) {
    auto pixelToFill = colorToPixel(color);
    for (auto x = 0; x < count; x++) {
        row_ptr[x] = pixelToFill;
    }
}

inline void blendClear(GPixel src, GPixel* dst) { *dst = 0; }

inline void blendSrc(GPixel src, GPixel* dst) { *dst = src; }

inline void blendDst(GPixel src, GPixel* dst) {}

inline void blendSrcOver(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(GPixel_GetA(src) + div255((255 - GPixel_GetA(src)) * GPixel_GetA(*dst)),
                           GPixel_GetR(src) + div255((255 - GPixel_GetA(src)) * GPixel_GetR(*dst)),
                           GPixel_GetG(src) + div255((255 - GPixel_GetA(src)) * GPixel_GetG(*dst)),
                           GPixel_GetB(src) + div255((255 - GPixel_GetA(src)) * GPixel_GetB(*dst)));
}

inline void blendDstOver(GPixel src, GPixel* dst) {
    *dst =
        GPixel_PackARGB(GPixel_GetA(*dst) + div255((255 - GPixel_GetA(*dst)) * GPixel_GetA(src)),
                        GPixel_GetR(*dst) + div255((255 - GPixel_GetA(*dst)) * GPixel_GetR(src)),
                        GPixel_GetG(*dst) + div255((255 - GPixel_GetA(*dst)) * GPixel_GetG(src)),
                        GPixel_GetB(*dst) + div255((255 - GPixel_GetA(*dst)) * GPixel_GetB(src)));
}

inline void blendSrcIn(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(
        div255(GPixel_GetA(*dst) * GPixel_GetA(src)), div255(GPixel_GetA(*dst) * GPixel_GetR(src)),
        div255(GPixel_GetA(*dst) * GPixel_GetG(src)), div255(GPixel_GetA(*dst) * GPixel_GetB(src)));
}

inline void blendDstIn(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(
        div255(GPixel_GetA(src) * GPixel_GetA(*dst)), div255(GPixel_GetA(src) * GPixel_GetR(*dst)),
        div255(GPixel_GetA(src) * GPixel_GetG(*dst)), div255(GPixel_GetA(src) * GPixel_GetB(*dst)));
}

inline void blendSrcOut(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(div255((255 - GPixel_GetA(*dst)) * GPixel_GetA(src)),
                           div255((255 - GPixel_GetA(*dst)) * GPixel_GetR(src)),
                           div255((255 - GPixel_GetA(*dst)) * GPixel_GetG(src)),
                           div255((255 - GPixel_GetA(*dst)) * GPixel_GetB(src)));
}

inline void blendDstOut(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(div255((255 - GPixel_GetA(src)) * GPixel_GetA(*dst)),
                           div255((255 - GPixel_GetA(src)) * GPixel_GetR(*dst)),
                           div255((255 - GPixel_GetA(src)) * GPixel_GetG(*dst)),
                           div255((255 - GPixel_GetA(src)) * GPixel_GetB(*dst)));
}

inline void blendSrcATop(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(
        div255(GPixel_GetA(*dst) * GPixel_GetA(src) + (255 - GPixel_GetA(src)) * GPixel_GetA(*dst)),
        div255(GPixel_GetA(*dst) * GPixel_GetR(src) + (255 - GPixel_GetA(src)) * GPixel_GetR(*dst)),
        div255(GPixel_GetA(*dst) * GPixel_GetG(src) + (255 - GPixel_GetA(src)) * GPixel_GetG(*dst)),
        div255(GPixel_GetA(*dst) * GPixel_GetB(src) +
               (255 - GPixel_GetA(src)) * GPixel_GetB(*dst)));
}

inline void blendDstATop(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(
        div255(GPixel_GetA(src) * GPixel_GetA(*dst) + (255 - GPixel_GetA(*dst)) * GPixel_GetA(src)),
        div255(GPixel_GetA(src) * GPixel_GetR(*dst) + (255 - GPixel_GetA(*dst)) * GPixel_GetR(src)),
        div255(GPixel_GetA(src) * GPixel_GetG(*dst) + (255 - GPixel_GetA(*dst)) * GPixel_GetG(src)),
        div255(GPixel_GetA(src) * GPixel_GetB(*dst) +
               (255 - GPixel_GetA(*dst)) * GPixel_GetB(src)));
}

inline void blendXor(GPixel src, GPixel* dst) {
    *dst = GPixel_PackARGB(div255((255 - GPixel_GetA(src)) * GPixel_GetA(*dst) +
                                  (255 - GPixel_GetA(*dst)) * GPixel_GetA(src)),
                           div255((255 - GPixel_GetA(src)) * GPixel_GetR(*dst) +
                                  (255 - GPixel_GetA(*dst)) * GPixel_GetR(src)),
                           div255((255 - GPixel_GetA(src)) * GPixel_GetG(*dst) +
                                  (255 - GPixel_GetA(*dst)) * GPixel_GetG(src)),
                           div255((255 - GPixel_GetA(src)) * GPixel_GetB(*dst) +
                                  (255 - GPixel_GetA(*dst)) * GPixel_GetB(src)));
}

// rewrite using function pointer
const auto blendFuncs = std::vector<std::function<void(GPixel, GPixel*)>>{
    blendClear, blendSrc,    blendDst,    blendSrcOver, blendDstOver, blendSrcIn,
    blendDstIn, blendSrcOut, blendDstOut, blendSrcATop, blendDstATop, blendXor};

inline void blend_row(int x, int y, int count, GPaint paint, GBitmap fDevice, GMatrix ctm) {

    auto row_ptr = fDevice.getAddr(x, y);
    auto mode = paint.getBlendMode();
    if (paint.peekShader()) {
        // std::cout << "shader" << std::endl;
        auto shader = paint.shareShader();
        // shader->setContext(ctm);
        GPixel pixelsToFill[count];
        shader->shadeRow(x, y, count, pixelsToFill);
        if (shader->isOpaque()) {
            if (mode == GBlendMode::kSrcOver) {
                mode = GBlendMode::kSrc;
            }
            if (mode == GBlendMode::kDstIn) {
                mode = GBlendMode::kDst;
            }
            if (mode == GBlendMode::kDstOut) {
                mode = GBlendMode::kClear;
            }
            if (mode == GBlendMode::kSrcATop) {
                mode = GBlendMode::kSrcIn;
            }
            if (mode == GBlendMode::kDstATop) {
                mode = GBlendMode::kDstOver;
            }
        }
        auto modeIdx = static_cast<int>(mode);

        for (auto i = 0; i < count; i++) {
            blendFuncs[modeIdx](pixelsToFill[i], &row_ptr[i]);
        }
    } else {
        // std::cout << "no shader" << std::endl;
        auto color = paint.getColor();
        auto pixelToFill = colorToPixel(color);
        if (color.a == 1) {
            switch (mode) {
            case GBlendMode::kSrcOver:
                mode = GBlendMode::kSrc;
                break;
            case GBlendMode::kDstIn:
                mode = GBlendMode::kDst;
                break;
            case GBlendMode::kDstOut:
                mode = GBlendMode::kClear;
                break;
            case GBlendMode::kSrcATop:
                mode = GBlendMode::kSrcIn;
                break;
            case GBlendMode::kDstATop:
                mode = GBlendMode::kDstOver;
                break;
            default:
                break;
            }

        } else if (color.a == 0) {
            switch (mode) {
            case GBlendMode::kSrc:
            case GBlendMode::kSrcIn:
            case GBlendMode::kDstIn:
            case GBlendMode::kSrcOut:
            case GBlendMode::kDstATop:
                mode = GBlendMode::kClear;
                break;
            case GBlendMode::kSrcOver:
            case GBlendMode::kDstOver:
            case GBlendMode::kDstOut:
            case GBlendMode::kSrcATop:
            case GBlendMode::kXor:
                mode = GBlendMode::kDst;
                break;
            default:
                break;
            }
        }
        auto modeIdx = static_cast<int>(mode);
        for (auto i = 0; i < count; i++) {
            blendFuncs[modeIdx](pixelToFill, &row_ptr[i]);
        }
    }
}
