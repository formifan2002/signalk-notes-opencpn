#pragma once

#include <wx/string.h>
#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/geometry.h>
#include <wx/affinematrix2d.h>
#include <wx/xml/xml.h>

// Graphics-Header nur für Non-Android Builds
#ifndef __OCPN__ANDROID__
  #include <wx/graphics.h>
#endif

#include <wx/image.h>

#include <map>
#include <memory>
#include <vector>

enum class SvgElementType { GROUP, LINE, RECT, POLYLINE, PATH, TEXT };

struct SvgStyle {
  // Farben
  bool hasStroke = false;
  bool hasFill = false;
  wxColour stroke;
  wxColour fill;

  // Linienbreite / Deckkraft
  double strokeWidth = 1.0;
  double opacity = 1.0;

  // Dasharray
  std::vector<double> dashArray;

  // Text
  double fontSize = 12.0;
  wxString fontFamily = "Arial";
  wxString textAnchor;  // "start", "middle", "end"

  // Abgerundete Ecken für RECT
  double rx = 0.0;
  double ry = 0.0;

  // Flags für MergeStyles / Prioritäten
  bool strokeSet = false;
  bool fillSet = false;
  bool strokeWidthSet = false;
  bool opacitySet = false;
  bool dashArraySet = false;
  bool fontSizeSet = false;
  bool fontFamilySet = false;
  bool textAnchorSet = false;
  bool cornerRadiusSet = false;
  
};

struct SvgTransform {
  wxAffineMatrix2D matrix;
};

struct SvgElement {
  SvgElementType type;
  SvgStyle style;
  SvgTransform transform;

  std::vector<std::shared_ptr<SvgElement>> children;

  // line
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0;

  // rect
  double x = 0, y = 0, width = 0, height = 0, rx = 0;

  // polyline / path
  std::vector<wxPoint2DDouble> points;

  // text
  double tx = 0, ty = 0;
  wxString text;
};

struct SvgDocument {
  double viewBoxX = 0;
  double viewBoxY = 0;
  double viewBoxWidth = 0;
  double viewBoxHeight = 0;
  int widthPx = 0;
  int heightPx = 0;

  std::shared_ptr<SvgElement> root;  // GROUP

  std::map<wxString, SvgStyle> classStyles;
};

class SvgRenderer {
public:
  SvgRenderer() = default;

  // Convenience: SVG-XML direkt nach PNG rendern
  bool FromSvgXmlToPng(const wxString& svgXml, const wxString& outputPath,
                       int targetWidth, int targetHeight);

  bool ParseSvg(const wxString& svgXml, SvgDocument& outDoc);
  bool RenderToPng(const SvgDocument& doc, const wxString& outputPath,
                   int targetWidth, int targetHeight);

private:
  bool ParseSvgRoot(wxXmlNode* root, SvgDocument& doc);
  void ParseStyleBlock(wxXmlNode* node, SvgDocument& doc);
  void ParseCssBlock(const wxString& css, SvgDocument& doc);
  SvgStyle ParseStyleAttribute(const wxString& styleAttr);
  void MergeStyles(SvgStyle& base, const SvgStyle& extra);

  std::shared_ptr<SvgElement> ParseElementRecursive(
      wxXmlNode* node, SvgDocument& doc, const SvgStyle& inheritedStyle,
      const wxAffineMatrix2D& parentTransform);

  SvgTransform ParseTransform(const wxString& transformStr);

  void ParseLineAttributes(wxXmlNode* node, SvgElement& el);
  void ParseRectAttributes(wxXmlNode* node, SvgElement& el);
  void ParsePolylineAttributes(wxXmlNode* node, SvgElement& el);
  void ParsePathAttributes(wxXmlNode* node, SvgElement& el);
  void ParseTextAttributes(wxXmlNode* node, SvgElement& el);
  void ApproximateCubicBezier(double x0, double y0, double x1, double y1,
                              double x2, double y2, double x3, double y3,
                              std::vector<wxPoint2DDouble>& outPoints);

  void ApproximateQuadraticBezier(double x0, double y0, double x1, double y1,
                                  double x2, double y2,
                                  std::vector<wxPoint2DDouble>& outPoints);

  double GetDoubleAttr(wxXmlNode* node, const wxString& name, double def = 0.0);
  wxString GetStringAttr(wxXmlNode* node, const wxString& name,
                         const wxString& def = "");
  wxColour ParseColour(const wxString& s, bool& ok);
  void ParseDashArray(const wxString& s, std::vector<double>& out);
  void ParsePathData(const wxString& d,
                     std::vector<wxPoint2DDouble>& outPoints);

  // Rendering (nur auf Non-Android)
#ifndef __OCPN__ANDROID__
  void RenderElement(wxGraphicsContext* gc, const SvgElement& el,
                     const SvgDocument& doc,
                     const wxAffineMatrix2D& parentTransform);

  void ApplyStyle(wxGraphicsContext* gc, const SvgStyle& style, bool forStroke);
#else
  // Android: NanoSVG für SVG→Image Konvertierung
  bool RenderToImageNanoSVG(const wxString& svgXml, wxImage& outImage,
                            int targetWidth, int targetHeight);
#endif
};