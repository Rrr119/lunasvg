#ifndef LUNASVG_SVGFILTERELEMENT_H
#define LUNASVG_SVGFILTERELEMENT_H

#include "svgelement.h"

namespace lunasvg {

class SVGFilterElement final : public SVGElement {
public:
    SVGFilterElement(Document* document);

    const SVGLength& x() const { return m_x; }
    const SVGLength& y() const { return m_y; }
    const SVGLength& width() const { return m_width; }
    const SVGLength& height() const { return m_height; }
    const SVGEnumeration<Units>& filterUnits() const { return m_filterUnits; }
    const SVGEnumeration<Units>& primitiveUnits() const { return m_primitiveUnits; }

    Rect filterBoundingBox(const SVGElement* element) const;
    void applyFilter(const SVGRenderState& state) const;

private:
    SVGLength m_x;
    SVGLength m_y;
    SVGLength m_width;
    SVGLength m_height;
    SVGEnumeration<Units> m_filterUnits;
    SVGEnumeration<Units> m_primitiveUnits;
};

class SVGFilterPrimitiveElement : public SVGElement {
public:
    SVGFilterPrimitiveElement(Document* document, ElementID id);

    const std::string& input() const { return m_in.value(); }
    const std::string& result() const { return m_result.value(); }

private:
    SVGString m_in;
    SVGString m_result;
};

class SVGFEGaussianBlurElement final : public SVGFilterPrimitiveElement {
public:
    SVGFEGaussianBlurElement(Document* document);

    const NumberList& stdDeviation() const { return m_stdDeviation.values(); }

private:
    SVGNumberList m_stdDeviation;
};

class SVGFEOffsetElement final : public SVGFilterPrimitiveElement {
public:
    SVGFEOffsetElement(Document* document);

    float dx() const { return m_dx.value(); }
    float dy() const { return m_dy.value(); }

private:
    SVGNumber m_dx;
    SVGNumber m_dy;
};

} // namespace lunasvg

#endif // LUNASVG_SVGFILTERELEMENT_H
