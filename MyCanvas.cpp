/*
 *  Copyright 2024 <me>
 */

#include "MyCanvas.h"
#include "include/GBitmap.h"
#include "include/GColor.h"
#include "include/GMath.h"
#include "include/GMatrix.h"
#include "include/GPaint.h"
#include "include/GPath.h"
#include "include/GPathBuilder.h"
#include "include/GPoint.h"
#include "include/GRect.h"
#include "include/GShader.h"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

void MyCanvas::clear(const GColor& color) {
    // your code here
    //* the order traversing the memory matters because of caching
    for (auto h = 0; h < fDevice.height(); h++) {
        auto pixel = fDevice.getAddr(0, h);
        src_row(pixel, fDevice.width(), color); //* speed up
    }
}

void MyCanvas::drawRect(const GRect& rect, const GPaint& paint) {
    auto roundedRect = rect.round();
    auto l = std::max(0, roundedRect.left);
    auto r = std::min(fDevice.width(), roundedRect.right);

    if (l >= r)
        return;
    if (paint.peekShader() || ctm != GMatrix()) {
        auto points =
            std::vector<GPoint>{{static_cast<float>(l), static_cast<float>(roundedRect.top)},
                                {static_cast<float>(r), static_cast<float>(roundedRect.top)},
                                {static_cast<float>(r), static_cast<float>(roundedRect.bottom)},
                                {static_cast<float>(l), static_cast<float>(roundedRect.bottom)}};
        drawConvexPolygon(points.data(), 4, paint);
        return;
    }

    for (auto h = std::max(0, roundedRect.top); h < std::min(fDevice.height(), roundedRect.bottom);
         h++) {
        blend_row(l, h, r - l, paint, fDevice, ctm);
    }
}

void MyCanvas::drawConvexPolygon(const GPoint points[], int count, const GPaint& paint) {

    if (paint.peekShader()) {

        paint.peekShader()->setContext(ctm);
    }
    GPoint newPoints[count];
    auto edges = std::vector<GEdge>();
    if (ctm != GMatrix()) {
        ctm.mapPoints(newPoints, points, count);

        for (auto i = 0; i < count; i += 1) {
            auto edge = createEdge(newPoints[i], newPoints[i + 1 == count ? 0 : i + 1]);
            if (!edge.horizontal()) {
                clipEdgeTo(edges, fDevice, edge);
            }
        }
    } else {
        for (auto i = 0; i < count; i += 1) {
            auto edge = createEdge(points[i], points[i + 1 == count ? 0 : i + 1]);
            if (!edge.horizontal()) {
                clipEdgeTo(edges, fDevice, edge);
            }
        }
    }

    if (edges.size() < 2)
        return;

    std::sort(edges.begin(), edges.end(), [](GEdge a, GEdge b) { return a.top.y < b.top.y; });

    auto i = 0;
    auto j = 1;
    auto nextIdx = 2;

    for (auto r = edges[0].top.y + 0.5; r < edges[edges.size() - 1].bottom.y + 0.5; ++r) {
        if (i < edges.size() && r >= edges[i].bottom.y) {
            i = nextIdx++;
        }
        if (j < edges.size() && r >= edges[j].bottom.y) {
            j = nextIdx++;
        }
        if (r < edges[i].top.y || r < edges[j].top.y)
            continue;
        if (i >= edges.size() || j >= edges.size())
            break;
        auto left = GRoundToInt(std::min(edges[i].getX(r), edges[j].getX(r)));
        // auto left = GFloorToInt(std::min(edges[i].getX(r), edges[j].getX(r)));
        auto right = GRoundToInt(std::max(edges[i].getX(r), edges[j].getX(r)));

        blend_row(left, GFloorToInt(r), right - left, paint, fDevice, ctm);
    }
    if (paint.peekShader()) {
        auto inv = ctm.invert();
        if (inv.has_value()) {
            paint.peekShader()->setContext(*ctm.invert());
        }
    }
}

void MyCanvas::save() { copies.push_back(GMatrix(ctm)); }

void MyCanvas::restore() {
    assert(!copies.empty());
    ctm = GMatrix(copies.back());
    copies.pop_back();
}

void MyCanvas::concat(const GMatrix& matrix) { ctm = ctm * matrix; }

void createQuadEdgesTo(std::vector<GEdge>& edges, const GPoint src[3], int numToChop,
                       const GBitmap fDevice) {
    if (numToChop == 0) {
        auto edge = createEdge(src[0], src[2]);
        if (!edge.horizontal()) {
            clipEdgeTo(edges, fDevice, edge);
        }
        return;
    }
    GPoint dst[5];
    GPath::ChopQuadAt(src, dst, 0.5);
    createQuadEdgesTo(edges, dst, numToChop - 1, fDevice);
    createQuadEdgesTo(edges, dst + 2, numToChop - 1, fDevice);
}

void createCubicEdgesTo(std::vector<GEdge>& edges, const GPoint src[4], int numToChop,
                        const GBitmap fDevice) {
    if (numToChop == 0) {
        auto edge = createEdge(src[0], src[3]);
        if (!edge.horizontal()) {
            clipEdgeTo(edges, fDevice, edge);
        }
        return;
    }
    GPoint dst[7];
    GPath::ChopCubicAt(src, dst, 0.5);
    createCubicEdgesTo(edges, dst, numToChop - 1, fDevice);
    createCubicEdgesTo(edges, dst + 3, numToChop - 1, fDevice);
}

void MyCanvas::drawPath(const GPath& path, const GPaint& paint) {

    if (paint.peekShader()) {
        paint.peekShader()->setContext(ctm);
    }
    auto transformedPath = path.transform(ctm);
    GPath::Edger edger(*transformedPath);
    GPoint pts[GPath::kMaxNextPoints];
    auto edges = std::vector<GEdge>();
    while (auto v = edger.next(pts)) {
        switch (v.value()) {
        case GPathVerb::kLine: {
            auto edge = createEdge(pts[0], pts[1]);
            if (!edge.horizontal()) {
                clipEdgeTo(edges, fDevice, edge);
            }
            break;
        }
        case GPathVerb::kQuad: {
            auto A = pts[0];
            auto B = pts[1];
            auto C = pts[2];
            auto E = A - 2 * B + C;
            auto err = std::abs(E.length() / 4);

            int numSegs = GCeilToInt(std::sqrt(err * 4));
            int numToChop = GCeilToInt(std::log2(numSegs));
            createQuadEdgesTo(edges, pts, numToChop, fDevice);
            break;
        }
        case GPathVerb::kCubic: {
            auto A = pts[0];
            auto B = pts[1];
            auto C = pts[2];
            auto D = pts[3];
            auto E0 = A - 2 * B + C;
            auto E1 = B - 2 * C + D;
            GPoint E = {std::max(E0.x, E1.x), std::max(E0.y, E1.y)};
            auto err = std::abs(E.length());
            int numSegs = GCeilToInt(std::sqrt(3 * err));
            int numToChop = GCeilToInt(std::log2(numSegs));
            createCubicEdgesTo(edges, pts, numToChop, fDevice);
        }
        default:
            break;
        }
    }
    if (edges.size() < 2)
        return;
    // std::cout << edges.size() << std::endl;
    // for (int i = 0; i < edges.size(); i++) {
    //     if (edges[i].horizontal()) {
    //         edges.erase(edges.begin() + i);
    //         i--;
    //     }
    // }

    std::sort(edges.begin(), edges.end(), [](GEdge a, GEdge b) {
        if (GRoundToInt(a.top.y) == GRoundToInt(b.top.y)) {
            return a.getX(a.top.y + 0.5) < b.getX(b.top.y + 0.5);
        }
        return GRoundToInt(a.top.y) < GRoundToInt(b.top.y);
    });

    // TODO: Handle quadratic and cubic curves in the path

    auto yUpper = GRoundToInt(edges[0].top.y) + 0.5;
    float yLower = 0;
    for (auto edge : edges) {
        yLower = std::max(yLower, edge.bottom.y);
    }
    yLower = GRoundToInt(yLower) + 0.5;

    for (auto y = yUpper; y < yLower; ++y) {
        size_t i = 0;
        int w = 0;
        int L;

        while (i < edges.size() && edges[i].valid(y)) {
            int x = GFloorToInt(edges[i].getX(y));
            if (x < 0) {
                x = 0;
            }
            if (x >= fDevice.width()) {
                x = fDevice.width() - 1;
            }
            if (w == 0) {
                L = x;
            }
            w += edges[i].winding;
            if (w == 0) {
                blend_row(L, GRoundToInt(y), x - L, paint, fDevice, ctm);
            }

            if (edges[i].valid(y + 1)) {
                ++i;
            } else {
                edges.erase(edges.begin() + i);
            }
        }
        // assert(w == 0);

        while (i < edges.size() && edges[i].valid(y + 1)) {
            ++i;
        }
        std::sort(edges.begin(), edges.begin() + i,
                  [y](auto a, auto b) { return a.getX(y + 1) < b.getX(y + 1); });
    }
    if (paint.peekShader()) {
        auto inv = ctm.invert();
        if (inv.has_value()) {
            paint.peekShader()->setContext(*ctm.invert());
        }
    }
}

std::unique_ptr<GCanvas> GCreateCanvas(const GBitmap& device) {
    return std::unique_ptr<GCanvas>(new MyCanvas(device));
}

std::string GDrawSomething(GCanvas* canvas, GISize dim) {
    canvas->translate(-40, 10);
    canvas->scale(3, 3);
    GPathBuilder bu;

    auto draw = [&](const GPoint pts[], size_t count, const GPaint& paint) {
        bu.addPolygon(pts, count);
        canvas->drawPath(*bu.detach(), paint);
    };
    GPaint paint({0, 0, 0, 1});
    const GPoint pts[] = {
        {41, 94}, {32, 110}, {23, 132}, {12, 163}, {6, 190},  {7, 217},  {5, 236}, {3, 247},
        {9, 230}, {12, 211}, {12, 185}, {18, 160}, {26, 134}, {35, 110}, {43, 99}, {41, 94},
    };
    draw(pts, GARRAY_COUNT(pts), paint);

    return "tears in rain";
}
