#include "svgfilterelement.h"
#include "svgrenderstate.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace lunasvg {

namespace {

struct FilterImage {
    int width{0};
    int height{0};
    std::vector<uint32_t> pixels;
};

FilterImage copySurface(const Canvas& canvas)
{
    FilterImage image;
    image.width = canvas.width();
    image.height = canvas.height();
    image.pixels.resize(static_cast<size_t>(image.width) * image.height);
    if(image.pixels.empty())
        return image;

    auto surface = canvas.surface();
    auto data = plutovg_surface_get_data(surface);
    auto stride = plutovg_surface_get_stride(surface);
    for(int y = 0; y < image.height; ++y) {
        auto row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
        std::copy(row, row + image.width, image.pixels.begin() + static_cast<size_t>(y) * image.width);
    }

    return image;
}

void copyToSurface(const FilterImage& image, Canvas& canvas)
{
    if(image.width != canvas.width() || image.height != canvas.height())
        return;

    auto surface = canvas.surface();
    auto data = plutovg_surface_get_data(surface);
    auto stride = plutovg_surface_get_stride(surface);
    for(int y = 0; y < image.height; ++y) {
        auto row = reinterpret_cast<uint32_t*>(data + static_cast<size_t>(y) * stride);
        std::copy(image.pixels.begin() + static_cast<size_t>(y) * image.width,
                  image.pixels.begin() + static_cast<size_t>(y + 1) * image.width,
                  row);
    }
}

FilterImage transparentImage(const FilterImage& source)
{
    FilterImage image = source;
    std::fill(image.pixels.begin(), image.pixels.end(), 0);
    return image;
}

FilterImage sourceAlphaImage(const FilterImage& source)
{
    FilterImage image = source;
    for(auto& pixel : image.pixels) {
        pixel = pixel & 0xFF000000;
    }

    return image;
}

inline uint8_t channel(uint32_t pixel, int shift)
{
    return static_cast<uint8_t>((pixel >> shift) & 0xFF);
}

inline uint32_t makePixel(float alpha, float red, float green, float blue)
{
    auto a = static_cast<uint32_t>(std::clamp(std::lround(alpha), 0l, 255l));
    auto r = static_cast<uint32_t>(std::clamp(std::lround(red), 0l, 255l));
    auto g = static_cast<uint32_t>(std::clamp(std::lround(green), 0l, 255l));
    auto b = static_cast<uint32_t>(std::clamp(std::lround(blue), 0l, 255l));
    return a << 24 | r << 16 | g << 8 | b;
}

std::vector<float> gaussianKernel(float sigma)
{
    constexpr int kMaxRadius = 1024;
    if(!(sigma > 0.f) || !std::isfinite(sigma))
        return {};
    auto radius = sigma >= kMaxRadius / 3.f ? kMaxRadius : static_cast<int>(std::ceil(3.f * sigma));
    std::vector<float> kernel(static_cast<size_t>(radius) * 2 + 1);
    auto denominator = 2.f * sigma * sigma;
    float total = 0.f;
    for(int i = -radius; i <= radius; ++i) {
        auto value = std::exp(-static_cast<float>(i * i) / denominator);
        kernel[static_cast<size_t>(i + radius)] = value;
        total += value;
    }

    for(auto& value : kernel)
        value /= total;
    return kernel;
}

FilterImage blurImage(const FilterImage& source, float sigmaX, float sigmaY)
{
    if(source.pixels.empty() || (sigmaX <= 0.f && sigmaY <= 0.f))
        return source;

    FilterImage horizontal = source;
    FilterImage result = source;
    if(sigmaX > 0.f) {
        const auto kernel = gaussianKernel(sigmaX);
        if(kernel.empty())
            return source;
        const auto radius = static_cast<int>(kernel.size() / 2);
        for(int y = 0; y < source.height; ++y) {
            for(int x = 0; x < source.width; ++x) {
                float alpha = 0.f;
                float red = 0.f;
                float green = 0.f;
                float blue = 0.f;
                for(int offset = -radius; offset <= radius; ++offset) {
                    auto sx = x + offset;
                    if(sx < 0 || sx >= source.width)
                        continue;
                    auto pixel = source.pixels[static_cast<size_t>(y) * source.width + sx];
                    auto weight = kernel[static_cast<size_t>(offset + radius)];
                    alpha += channel(pixel, 24) * weight;
                    red += channel(pixel, 16) * weight;
                    green += channel(pixel, 8) * weight;
                    blue += channel(pixel, 0) * weight;
                }

                horizontal.pixels[static_cast<size_t>(y) * source.width + x] = makePixel(alpha, red, green, blue);
            }
        }
    }

    if(sigmaY > 0.f) {
        const auto kernel = gaussianKernel(sigmaY);
        if(kernel.empty())
            return source;
        const auto radius = static_cast<int>(kernel.size() / 2);
        for(int y = 0; y < source.height; ++y) {
            for(int x = 0; x < source.width; ++x) {
                float alpha = 0.f;
                float red = 0.f;
                float green = 0.f;
                float blue = 0.f;
                for(int offset = -radius; offset <= radius; ++offset) {
                    auto sy = y + offset;
                    if(sy < 0 || sy >= source.height)
                        continue;
                    auto pixel = horizontal.pixels[static_cast<size_t>(sy) * source.width + x];
                    auto weight = kernel[static_cast<size_t>(offset + radius)];
                    alpha += channel(pixel, 24) * weight;
                    red += channel(pixel, 16) * weight;
                    green += channel(pixel, 8) * weight;
                    blue += channel(pixel, 0) * weight;
                }

                result.pixels[static_cast<size_t>(y) * source.width + x] = makePixel(alpha, red, green, blue);
            }
        }
    } else {
        result.pixels = horizontal.pixels;
    }

    return result;
}

uint32_t samplePixel(const FilterImage& image, int x, int y)
{
    if(x < 0 || x >= image.width || y < 0 || y >= image.height)
        return 0;
    return image.pixels[static_cast<size_t>(y) * image.width + x];
}

FilterImage offsetImage(const FilterImage& source, float dx, float dy)
{
    FilterImage result = source;
    for(int y = 0; y < source.height; ++y) {
        for(int x = 0; x < source.width; ++x) {
            auto sx = static_cast<float>(x) - dx;
            auto sy = static_cast<float>(y) - dy;
            auto x0 = static_cast<int>(std::floor(sx));
            auto y0 = static_cast<int>(std::floor(sy));
            auto fx = sx - x0;
            auto fy = sy - y0;
            auto p00 = samplePixel(source, x0, y0);
            auto p10 = samplePixel(source, x0 + 1, y0);
            auto p01 = samplePixel(source, x0, y0 + 1);
            auto p11 = samplePixel(source, x0 + 1, y0 + 1);

            float alpha[4];
            float red[4];
            float green[4];
            float blue[4];
            const uint32_t pixels[4] = {p00, p10, p01, p11};
            const float weights[4] = {
                (1.f - fx) * (1.f - fy),
                fx * (1.f - fy),
                (1.f - fx) * fy,
                fx * fy
            };

            for(int i = 0; i < 4; ++i) {
                alpha[i] = channel(pixels[i], 24);
                red[i] = channel(pixels[i], 16);
                green[i] = channel(pixels[i], 8);
                blue[i] = channel(pixels[i], 0);
            }

            result.pixels[static_cast<size_t>(y) * source.width + x] = makePixel(
                alpha[0] * weights[0] + alpha[1] * weights[1] + alpha[2] * weights[2] + alpha[3] * weights[3],
                red[0] * weights[0] + red[1] * weights[1] + red[2] * weights[2] + red[3] * weights[3],
                green[0] * weights[0] + green[1] * weights[1] + green[2] * weights[2] + green[3] * weights[3],
                blue[0] * weights[0] + blue[1] * weights[1] + blue[2] * weights[2] + blue[3] * weights[3]);
        }
    }

    return result;
}

const SVGFilterPrimitiveElement* toFilterPrimitive(const SVGElement* element)
{
    if(element == nullptr)
        return nullptr;
    switch(element->id()) {
    case ElementID::FeGaussianBlur:
    case ElementID::FeOffset:
        return static_cast<const SVGFilterPrimitiveElement*>(element);
    default:
        return nullptr;
    }
}

Rect objectBoundingBox(const SVGElement* element)
{
    auto boundingBox = element->fillBoundingBox();
    if(boundingBox.isEmpty())
        boundingBox = element->strokeBoundingBox();
    return boundingBox;
}

} // namespace

SVGFilterElement::SVGFilterElement(Document* document)
    : SVGElement(document, ElementID::Filter)
    , m_x(PropertyID::X, LengthDirection::Horizontal, LengthNegativeMode::Allow, -10.f, LengthUnits::Percent)
    , m_y(PropertyID::Y, LengthDirection::Vertical, LengthNegativeMode::Allow, -10.f, LengthUnits::Percent)
    , m_width(PropertyID::Width, LengthDirection::Horizontal, LengthNegativeMode::Forbid, 120.f, LengthUnits::Percent)
    , m_height(PropertyID::Height, LengthDirection::Vertical, LengthNegativeMode::Forbid, 120.f, LengthUnits::Percent)
    , m_filterUnits(PropertyID::FilterUnits, Units::ObjectBoundingBox)
    , m_primitiveUnits(PropertyID::PrimitiveUnits, Units::UserSpaceOnUse)
{
    addProperty(m_x);
    addProperty(m_y);
    addProperty(m_width);
    addProperty(m_height);
    addProperty(m_filterUnits);
    addProperty(m_primitiveUnits);
}

Rect SVGFilterElement::filterBoundingBox(const SVGElement* element) const
{
    LengthContext lengthContext(this, m_filterUnits.value());
    Rect boundingBox = {
        lengthContext.valueForLength(m_x),
        lengthContext.valueForLength(m_y),
        lengthContext.valueForLength(m_width),
        lengthContext.valueForLength(m_height)
    };

    if(m_filterUnits.value() == Units::ObjectBoundingBox) {
        auto bbox = objectBoundingBox(element);
        boundingBox.x = boundingBox.x * bbox.w + bbox.x;
        boundingBox.y = boundingBox.y * bbox.h + bbox.y;
        boundingBox.w *= bbox.w;
        boundingBox.h *= bbox.h;
    }

    return boundingBox.isValid() ? boundingBox : Rect::Empty;
}

void SVGFilterElement::applyFilter(const SVGRenderState& state) const
{
    auto canvas = state.canvas();
    if(!canvas)
        return;

    auto sourceGraphic = copySurface(*canvas);
    auto sourceAlpha = sourceAlphaImage(sourceGraphic);
    std::map<std::string, FilterImage, std::less<>> results;
    results.emplace("SourceGraphic", sourceGraphic);
    results.emplace("SourceAlpha", sourceAlpha);

    auto previous = sourceGraphic;
    auto element = state.element();
    auto boundingBox = objectBoundingBox(element);
    auto primitiveUnits = m_primitiveUnits.value();
    auto xScale = std::abs(state.currentTransform().xScale());
    auto yScale = std::abs(state.currentTransform().yScale());

    for(const auto& child : children()) {
        auto primitive = toFilterPrimitive(toSVGElement(child));
        if(primitive == nullptr)
            continue;

        const FilterImage* input = nullptr;
        if(primitive->input().empty()) {
            input = &previous;
        } else {
            auto it = results.find(primitive->input());
            if(it != results.end())
                input = &it->second;
        }

        FilterImage output = input ? *input : transparentImage(sourceGraphic);
        if(primitive->id() == ElementID::FeGaussianBlur) {
            const auto& values = static_cast<const SVGFEGaussianBlurElement*>(primitive)->stdDeviation();
            auto sigmaX = values.empty() ? 0.f : std::max(0.f, values[0]);
            auto sigmaY = values.empty() ? 0.f : std::max(0.f, values.size() > 1 ? values[1] : values[0]);
            if(primitiveUnits == Units::ObjectBoundingBox) {
                sigmaX *= boundingBox.w;
                sigmaY *= boundingBox.h;
            }

            output = blurImage(output, sigmaX * xScale, sigmaY * yScale);
        } else if(primitive->id() == ElementID::FeOffset) {
            auto offset = static_cast<const SVGFEOffsetElement*>(primitive);
            auto dx = offset->dx();
            auto dy = offset->dy();
            if(primitiveUnits == Units::ObjectBoundingBox) {
                dx *= boundingBox.w;
                dy *= boundingBox.h;
            }

            auto origin = state.currentTransform().mapPoint(0.f, 0.f);
            auto mappedOffset = state.currentTransform().mapPoint(dx, dy);
            output = offsetImage(output, mappedOffset.x - origin.x, mappedOffset.y - origin.y);
        }

        if(!primitive->result().empty())
            results[primitive->result()] = output;
        previous = std::move(output);
    }

    copyToSurface(previous, *canvas);
}

SVGFilterPrimitiveElement::SVGFilterPrimitiveElement(Document* document, ElementID id)
    : SVGElement(document, id)
    , m_in(PropertyID::In)
    , m_result(PropertyID::Result)
{
    addProperty(m_in);
    addProperty(m_result);
}

SVGFEGaussianBlurElement::SVGFEGaussianBlurElement(Document* document)
    : SVGFilterPrimitiveElement(document, ElementID::FeGaussianBlur)
    , m_stdDeviation(PropertyID::StdDeviation)
{
    addProperty(m_stdDeviation);
}

SVGFEOffsetElement::SVGFEOffsetElement(Document* document)
    : SVGFilterPrimitiveElement(document, ElementID::FeOffset)
    , m_dx(PropertyID::Dx, 0.f)
    , m_dy(PropertyID::Dy, 0.f)
{
    addProperty(m_dx);
    addProperty(m_dy);
}

} // namespace lunasvg
