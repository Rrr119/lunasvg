#include <lunasvg.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace lunasvg;

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if(!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

uint32_t pixelAt(const Bitmap& bitmap, int x, int y)
{
    auto row = reinterpret_cast<const uint32_t*>(bitmap.data() + static_cast<size_t>(y) * bitmap.stride());
    return row[x];
}

int alpha(uint32_t pixel)
{
    return static_cast<int>((pixel >> 24) & 0xFF);
}

int red(uint32_t pixel)
{
    return static_cast<int>((pixel >> 16) & 0xFF);
}

int green(uint32_t pixel)
{
    return static_cast<int>((pixel >> 8) & 0xFF);
}

int blue(uint32_t pixel)
{
    return static_cast<int>((pixel >> 0) & 0xFF);
}

Bitmap render(const std::string& svg, int width = 20, int height = 20)
{
    auto document = Document::loadFromData(svg);
    check(document != nullptr, "SVG document should parse");
    if(document == nullptr)
        return Bitmap();
    return document->renderToBitmap(width, height);
}

void testNoFilterCompatibility()
{
    auto document = Document::loadFromData(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <rect x="5" y="5" width="10" height="10" fill="#ff0000"/>
        </svg>
    )SVG");
    check(document != nullptr, "SVG document should parse");
    if(document == nullptr)
        return;

    auto bitmap = document->renderToBitmap(20, 20);

    auto center = pixelAt(bitmap, 10, 10);
    check(alpha(center) == 255, "unfiltered center should remain opaque");
    check(red(center) == 255, "unfiltered center should remain red");
    check(alpha(pixelAt(bitmap, 1, 1)) == 0, "unfiltered outside should remain transparent");

    auto bounds = document->boundingBox();
    check(bounds.x == 5.f && bounds.y == 5.f && bounds.w == 10.f && bounds.h == 10.f,
          "unfiltered document bounds should not change");
}

void testOffset()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <defs>
                <filter id="offset" filterUnits="userSpaceOnUse" x="0" y="0" width="20" height="20">
                    <feOffset dx="5" dy="3"/>
                </filter>
            </defs>
            <rect x="0" y="0" width="5" height="5" fill="#ff0000" filter="url(#offset)"/>
        </svg>
    )SVG");

    auto moved = pixelAt(bitmap, 7, 5);
    check(alpha(moved) == 255, "offset result should be opaque at the shifted position");
    check(red(moved) == 255, "offset result should preserve its color");
    check(alpha(pixelAt(bitmap, 1, 1)) == 0, "offset should clear the source position");
}

void testGaussianBlur()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <defs>
                <filter id="blur" filterUnits="userSpaceOnUse" x="0" y="0" width="20" height="20">
                    <feGaussianBlur stdDeviation="1"/>
                </filter>
            </defs>
            <rect x="8" y="8" width="4" height="4" fill="#ff0000" filter="url(#blur)"/>
        </svg>
    )SVG");

    auto center = pixelAt(bitmap, 10, 10);
    auto outside = pixelAt(bitmap, 6, 10);
    check(alpha(center) > 0 && alpha(center) < 255, "blur should soften the source edge");
    check(alpha(outside) > 0, "blur should spread outside the original shape");
    check(red(outside) > 0, "blur should preserve the source color");
}

void testFilterChain()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="24" height="20">
            <defs>
                <filter id="chain" filterUnits="userSpaceOnUse" x="0" y="0" width="24" height="20">
                    <feGaussianBlur in="SourceGraphic" stdDeviation="0.8" result="soft"/>
                    <feOffset in="soft" dx="8" dy="2"/>
                </filter>
            </defs>
            <rect x="2" y="5" width="5" height="5" fill="#00ff00" filter="url(#chain)"/>
        </svg>
    )SVG", 24, 20);

    auto moved = pixelAt(bitmap, 12, 9);
    check(alpha(moved) > 0, "chained blur and offset should reach the shifted position");
    check(red(moved) == 0 && green(moved) > 0, "chained result should keep the green color");
    check(alpha(pixelAt(bitmap, 3, 6)) == 0, "offset in a chain should move the blurred source");
}

void testSourceAlpha()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <defs>
                <filter id="alpha" filterUnits="userSpaceOnUse" x="0" y="0" width="20" height="20">
                    <feOffset in="SourceAlpha" dx="10"/>
                </filter>
            </defs>
            <rect x="2" y="2" width="4" height="4" fill="#ff0000" filter="url(#alpha)"/>
        </svg>
    )SVG");

    auto shifted = pixelAt(bitmap, 13, 4);
    check(alpha(shifted) == 255, "SourceAlpha should preserve alpha");
    check(red(shifted) == 0, "SourceAlpha should not preserve source color");
}

void testObjectBoundingBoxAndCssFilter()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <style>
                rect { filter: url(#offset); }
            </style>
            <defs>
                <filter id="offset">
                    <feOffset dx="2" dy="2"/>
                </filter>
            </defs>
            <rect x="4" y="4" width="6" height="6" fill="#0000ff"/>
        </svg>
    )SVG");

    auto moved = pixelAt(bitmap, 8, 8);
    check(alpha(moved) == 255, "CSS filter should render with the default object bounding box region");
    check(blue(moved) == 255, "CSS filter should preserve the blue color");
}

void testObjectBoundingBoxPrimitiveUnits()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <defs>
                <filter id="offset" primitiveUnits="objectBoundingBox">
                    <feOffset dx="0.5" dy="0.5"/>
                </filter>
            </defs>
            <rect x="2" y="2" width="8" height="8" fill="#ff0000" filter="url(#offset)"/>
        </svg>
    )SVG");

    check(alpha(pixelAt(bitmap, 7, 7)) == 255,
          "objectBoundingBox primitive units should scale offsets by the object bounds");
    check(alpha(pixelAt(bitmap, 3, 3)) == 0,
          "offset should clear the source position when primitive units use the object bounds");
}

void testFilterWithTransform()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24">
            <defs>
                <filter id="offset" filterUnits="userSpaceOnUse" x="0" y="0" width="24" height="24">
                    <feOffset dx="2"/>
                </filter>
            </defs>
            <rect x="2" y="5" width="4" height="4" fill="#ff0000"
                  transform="scale(2)" filter="url(#offset)"/>
        </svg>
    )SVG", 24, 24);

    check(alpha(pixelAt(bitmap, 10, 12)) == 255,
          "offset should be transformed into device space");
    check(alpha(pixelAt(bitmap, 5, 12)) == 0,
          "transformed offset should clear the original device position");
}

void testFilterOnGroup()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <defs>
                <filter id="offset" filterUnits="userSpaceOnUse" x="0" y="0" width="20" height="20">
                    <feOffset dx="4" dy="2"/>
                </filter>
            </defs>
            <g filter="url(#offset)">
                <rect x="2" y="3" width="4" height="4" fill="#ff0000"/>
            </g>
        </svg>
    )SVG");

    check(alpha(pixelAt(bitmap, 8, 7)) == 255,
          "filters on groups should process the rendered group layer");
    check(alpha(pixelAt(bitmap, 3, 4)) == 0,
          "group offset should clear the original group position");
}

void testFilterRegionClipsOutput()
{
    auto bitmap = render(R"SVG(
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20">
            <defs>
                <filter id="region" filterUnits="userSpaceOnUse" x="0" y="0" width="10" height="10">
                    <feOffset dx="8"/>
                </filter>
            </defs>
            <rect x="0" y="2" width="5" height="5" fill="#ff0000" filter="url(#region)"/>
        </svg>
    )SVG");

    check(alpha(pixelAt(bitmap, 13, 4)) == 0, "filter output should be clipped to the filter region");
    check(alpha(pixelAt(bitmap, 8, 4)) > 0, "the filter region should retain overlapping output");
}

} // namespace

int main()
{
    testNoFilterCompatibility();
    testOffset();
    testGaussianBlur();
    testFilterChain();
    testSourceAlpha();
    testObjectBoundingBoxAndCssFilter();
    testObjectBoundingBoxPrimitiveUnits();
    testFilterWithTransform();
    testFilterOnGroup();
    testFilterRegionClipsOutput();

    if(failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    std::printf("All filter tests passed\n");
    return 0;
}
