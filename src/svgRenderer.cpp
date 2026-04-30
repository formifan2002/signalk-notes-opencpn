#include "svgRenderer.h"

// Nur auf Android: NanoSVG Library
#ifdef __OCPN__ANDROID__
  #define NANOSVG_IMPLEMENTATION
  #include "nanosvg.h"
#endif

#include <wx/tokenzr.h>
#include <wx/regex.h>
#include <wx/log.h>
#include <wx/sstream.h>

// Graphics-Includes nur für Non-Android Builds
#ifndef __OCPN__ANDROID__
  #include <wx/dcmemory.h>
  #include <wx/dcgraph.h>
#endif

#include <wx/pen.h>
#include <wx/brush.h>

#include <cmath>

// ---------------------------------------------------------
// SVG parsen
// ---------------------------------------------------------

bool SvgRenderer::ParseSvg(const wxString& svgXml, SvgDocument& outDoc) {
  wxStringInputStream sis(svgXml);
  wxXmlDocument xml;
  if (!xml.Load(sis)) {
    wxLogWarning("SvgRenderer: Failed to parse SVG XML");
    return false;
  }

  wxXmlNode* root = xml.GetRoot();
  if (!root || !root->GetName().IsSameAs("svg", false)) {
    wxLogWarning("SvgRenderer: Root element is not <svg>");
    return false;
  }

  if (!ParseSvgRoot(root, outDoc)) {
    wxLogWarning("SvgRenderer::ParseSvg: ParseSvgRoot FAILED");
    return false;
  }

  SvgStyle emptyStyle;
  wxAffineMatrix2D identity;
  outDoc.root = std::make_shared<SvgElement>();
  outDoc.root->type = SvgElementType::GROUP;

  for (wxXmlNode* child = root->GetChildren(); child;
       child = child->GetNext()) {
    if (child->GetType() != wxXML_ELEMENT_NODE) continue;

    if (child->GetName().IsSameAs("style", false)) {
      ParseStyleBlock(child, outDoc);
      continue;
    }

    auto el = ParseElementRecursive(child, outDoc, emptyStyle, identity);
    if (el) {
      outDoc.root->children.push_back(el);
    }
  }

  return true;
}

// ---------------------------------------------------------
// Convenience: SVG-XML → PNG
// ---------------------------------------------------------

bool SvgRenderer::FromSvgXmlToPng(const wxString& svgXml,
                                  const wxString& outputPath, int targetWidth,
                                  int targetHeight) {
  SvgDocument doc;
  if (!ParseSvg(svgXml, doc)) {
    wxLogWarning("SvgRenderer::FromSvgXmlToPng: ParseSvg FAILED");
    return false;
  }

  bool ok = RenderToPng(doc, outputPath, 0, 0);
  return ok;
}

// ---------------------------------------------------------
// RenderToPng mit automatischer Skalierung
// ---------------------------------------------------------
bool SvgRenderer::RenderToPng(const SvgDocument& doc,
                              const wxString& outputPath,
                              int targetWidth,
                              int targetHeight)
{
#ifdef __OCPN__ANDROID__
  // Android: Nutze NanoSVG
  wxImage img;
  if (!RenderToImageNanoSVG("", img, targetWidth, targetHeight)) {
    wxLogWarning("RenderToPng: NanoSVG rendering failed");
    return false;
  }
  
  if (!img.SaveFile(outputPath, wxBITMAP_TYPE_PNG)) {
    wxLogWarning("RenderToPng: Failed to save PNG '%s'", outputPath);
    return false;
  }
  
  return true;
#else
  // Desktop: wxGraphicsContext
  wxLogMessage("=== RenderToPng START ===");
  wxLogMessage("targetWidth=%d targetHeight=%d", targetWidth, targetHeight);
  wxLogMessage("SVG viewBox: x=%f y=%f w=%f h=%f",
               doc.viewBoxX, doc.viewBoxY,
               doc.viewBoxWidth, doc.viewBoxHeight);
  wxLogMessage("SVG pixel size: widthPx=%d heightPx=%d",
               doc.widthPx, doc.heightPx);

  int w = 0;
  int h = 0;

  // 1) Wenn explizite Zielgröße gesetzt ist UND kein viewBox vorhanden ist:
  if (targetWidth > 0 && targetHeight > 0 &&
      (doc.viewBoxWidth <= 0 || doc.viewBoxHeight <= 0)) {
    w = targetWidth;
    h = targetHeight;
    wxLogMessage("Using explicit target size (no viewBox): %d x %d", w, h);
  }
  // 2) Wenn viewBox vorhanden → Größe aus viewBox + Limits ableiten
  else if (doc.viewBoxWidth > 0 && doc.viewBoxHeight > 0) {
    const double maxWidth  = (targetWidth  > 0) ? targetWidth  : 900.0;
    const double maxHeight = (targetHeight > 0) ? targetHeight : 1600.0;
    const double minHeight = 300.0;

    const double vbW = doc.viewBoxWidth;
    const double vbH = doc.viewBoxHeight;
    const double aspect = vbW / vbH;

    // Erst so hoch wie möglich, aber begrenzt
    double fh = std::min(maxHeight, vbH);
    double fw = fh * aspect;

    if (fw > maxWidth) {
      fw = maxWidth;
      fh = fw / aspect;
    }

    if (fh < minHeight) {
      fh = minHeight;
      fw = fh * aspect;
      if (fw > maxWidth) {
        fw = maxWidth;
        fh = fw / aspect;
      }
    }

    w = (int)std::round(fw);
    h = (int)std::round(fh);
    wxLogMessage("Using viewBox-based size: %d x %d (aspect=%f)", w, h, aspect);
  }
  // 3) Fallback: width/height-Attribute oder Defaults
  else {
    if (doc.widthPx > 0 && doc.heightPx > 0) {
      w = doc.widthPx;
      h = doc.heightPx;
      wxLogMessage("Using pixel size: %d x %d", w, h);
    } else {
      w = (targetWidth  > 0) ? targetWidth  : 900;
      h = (targetHeight > 0) ? targetHeight : 400;
      wxLogMessage("Using default size: %d x %d", w, h);
    }
  }

  wxLogMessage("Final render size: %d x %d", w, h);

  wxBitmap bmp(w, h);
  wxMemoryDC dc(bmp);
  dc.SetBackground(*wxWHITE_BRUSH);
  dc.Clear();

  std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
  if (!gc) {
    wxLogWarning("SvgRenderer::RenderToPng: wxGraphicsContext::Create FAILED");
    return false;
  }

  if (doc.viewBoxWidth > 0 && doc.viewBoxHeight > 0) {
    const double sx = (double)w / doc.viewBoxWidth;
    const double sy = (double)h / doc.viewBoxHeight;

    // Isotrope Skalierung: immer denselben Faktor nehmen
    const double s = std::min(sx, sy);
    const double tx = (w / s - doc.viewBoxWidth) * 0.5;
    const double ty = (h / s - doc.viewBoxHeight) * 0.5;

    wxLogMessage("Applying scale: sx=%f sy=%f (iso=%f), translate=%f,%f",
                 sx, sy, s, tx, ty);

    gc->Scale(s, s);
    gc->Translate(-doc.viewBoxX + tx, -doc.viewBoxY + ty);
  }

  wxAffineMatrix2D identity;
  if (doc.root) {
    RenderElement(gc.get(), *doc.root, doc, identity);
  } else {
    wxLogWarning("SvgRenderer::RenderToPng: doc.root is NULL");
  }

  wxImage img = bmp.ConvertToImage();
  if (!img.SaveFile(outputPath, wxBITMAP_TYPE_PNG)) {
    wxLogWarning("SvgRenderer: Failed to save PNG '%s'", outputPath);
    return false;
  }

  wxLogMessage("PNG save: OK");
  wxLogMessage("=== RenderToPng END ===");
  return true;
#endif
}

// ---------------------------------------------------------
// Root / Styles / CSS
// ---------------------------------------------------------

bool SvgRenderer::ParseSvgRoot(wxXmlNode* root, SvgDocument& doc) {
  wxString wStr = GetStringAttr(root, "width", "");
  wxString hStr = GetStringAttr(root, "height", "");
  long tmp;
  if (wStr.ToLong(&tmp)) doc.widthPx = (int)tmp;
  if (hStr.ToLong(&tmp)) doc.heightPx = (int)tmp;

  wxString vb = GetStringAttr(root, "viewBox", "");
  if (!vb.IsEmpty()) {
    wxArrayString parts = wxSplit(vb, ' ', '\0');
    if (parts.size() == 4) {
      parts[0].ToDouble(&doc.viewBoxX);
      parts[1].ToDouble(&doc.viewBoxY);
      parts[2].ToDouble(&doc.viewBoxWidth);
      parts[3].ToDouble(&doc.viewBoxHeight);
    }
  }

  return true;
}

void SvgRenderer::ParseStyleBlock(wxXmlNode* node, SvgDocument& doc) {
  wxString css = node->GetNodeContent();
  ParseCssBlock(css, doc);
}

void SvgRenderer::ParseCssBlock(const wxString& css, SvgDocument& doc) {
  wxRegEx reRule("\\.([a-zA-Z0-9_-]+)\\s*\\{([^}]*)\\}",
                 wxRE_EXTENDED | wxRE_ICASE);

  wxString remaining = css;

  while (reRule.Matches(remaining)) {
    wxString fullMatch = reRule.GetMatch(remaining, 0);
    wxString className = reRule.GetMatch(remaining, 1);
    wxString body = reRule.GetMatch(remaining, 2);

    SvgStyle st;
    wxArrayString decls = wxSplit(body, ';', '\0');
    for (auto& d : decls) {
      wxString decl = d;
      decl.Trim(true).Trim(false);
      if (decl.IsEmpty()) continue;

      int colon = decl.Find(':');
      if (colon == wxNOT_FOUND) continue;

      wxString key = decl.SubString(0, colon - 1);
      wxString val = decl.SubString(colon + 1, decl.Length() - 1);
      key.Trim(true).Trim(false).MakeLower();
      val.Trim(true).Trim(false);

      if (key == "fill") {
        bool ok = false;
        wxColour c = ParseColour(val, ok);
        if (ok) {
          st.hasFill = true;
          st.fill = c;
        }
      } else if (key == "stroke") {
        bool ok = false;
        wxColour c = ParseColour(val, ok);
        if (ok) {
          st.hasStroke = true;
          st.stroke = c;
        }
      } else if (key == "stroke-width") {
        double dval;
        if (val.ToDouble(&dval)) st.strokeWidth = dval;
      } else if (key == "font-size") {
        double dval;
        if (val.ToDouble(&dval)) st.fontSize = dval;
      } else if (key == "font-family") {
        st.fontFamily = val;
      } else if (key == "opacity") {
        double dval;
        if (val.ToDouble(&dval)) st.opacity = dval;
      } else if (key == "stroke-dasharray") {
        ParseDashArray(val, st.dashArray);
      } else if (key == "text-anchor") {
        st.textAnchor = val;
      }
    }

    doc.classStyles[className] = st;
    remaining = remaining.Mid(fullMatch.length());
  }
}

SvgStyle SvgRenderer::ParseStyleAttribute(const wxString& styleAttr) {
  SvgStyle st;
  if (styleAttr.IsEmpty()) return st;

  wxArrayString decls = wxSplit(styleAttr, ';', '\0');
  for (auto& d : decls) {
    wxString decl = d;
    decl.Trim(true).Trim(false);
    if (decl.IsEmpty()) continue;
    int colon = decl.Find(':');
    if (colon == wxNOT_FOUND) continue;
    wxString key = decl.SubString(0, colon - 1);
    wxString val = decl.SubString(colon + 1, decl.Length() - 1);
    key.Trim(true).Trim(false).MakeLower();
    val.Trim(true).Trim(false);

    if (key == "fill") {
      bool ok = false;
      wxColour c = ParseColour(val, ok);
      if (ok) {
        st.hasFill = true;
        st.fill = c;
      }
    } else if (key == "stroke") {
      bool ok = false;
      wxColour c = ParseColour(val, ok);
      if (ok) {
        st.hasStroke = true;
        st.stroke = c;
      }
    } else if (key == "stroke-width") {
      double dval;
      if (val.ToDouble(&dval)) st.strokeWidth = dval;
    } else if (key == "font-size") {
      double dval;
      if (val.ToDouble(&dval)) st.fontSize = dval;
    } else if (key == "font-family") {
      st.fontFamily = val;
    } else if (key == "opacity") {
      double dval;
      if (val.ToDouble(&dval)) st.opacity = dval;
    } else if (key == "stroke-dasharray") {
      ParseDashArray(val, st.dashArray);
    } else if (key == "text-anchor") {
      st.textAnchor = val;
    }
  }

  return st;
}

void SvgRenderer::MergeStyles(SvgStyle& target, const SvgStyle& source) {
  if (source.hasFill) {
    target.hasFill = true;
    target.fill = source.fill;
  }

  if (source.hasStroke) {
    target.hasStroke = true;
    target.stroke = source.stroke;
  }

  if (source.strokeWidthSet) {
    target.strokeWidth = source.strokeWidth;
    target.strokeWidthSet = true;
  }

  if (source.opacitySet) {
    target.opacity = source.opacity;
    target.opacitySet = true;
  }

  if (source.fontSizeSet) {
    target.fontSize = source.fontSize;
    target.fontSizeSet = true;
  }

  if (!source.fontFamily.IsEmpty()) {
    target.fontFamily = source.fontFamily;
  }

  if (!source.textAnchor.IsEmpty()) {
    target.textAnchor = source.textAnchor;
  }

  if (source.dashArraySet) {
    target.dashArray = source.dashArray;
    target.dashArraySet = true;
  }
}

// ---------------------------------------------------------
// Elemente parsen
// ---------------------------------------------------------

std::shared_ptr<SvgElement> SvgRenderer::ParseElementRecursive(
    wxXmlNode* node, SvgDocument& doc, const SvgStyle& inheritedStyle,
    const wxAffineMatrix2D& parentTransform) {
  wxString name = node->GetName().Lower();

  // <style> überall
  if (name == "style") {
    ParseStyleBlock(node, doc);
    return nullptr;
  }

  // <defs> durchlaufen, aber nicht rendern
  if (name == "defs") {
    for (wxXmlNode* ch = node->GetChildren(); ch; ch = ch->GetNext()) {
      if (ch->GetType() != wxXML_ELEMENT_NODE) continue;
      wxString cname = ch->GetName().Lower();
      if (cname == "style") {
        ParseStyleBlock(ch, doc);
      }
    }
    return nullptr;
  }

  // Transform
  wxString tStr = GetStringAttr(node, "transform", "");
  SvgTransform tr;
  tr.matrix = parentTransform;
  if (!tStr.IsEmpty()) {
    SvgTransform local = ParseTransform(tStr);
    tr.matrix.Concat(local.matrix);
  }

  // Style erben
  SvgStyle style = inheritedStyle;

  // CSS-Klassen
  wxString classAttr = GetStringAttr(node, "class", "");
  if (!classAttr.IsEmpty()) {
    wxArrayString classes = wxSplit(classAttr, ' ', '\0');
    for (auto& c : classes) {
      auto it = doc.classStyles.find(c);
      if (it != doc.classStyles.end()) {
        MergeStyles(style, it->second);
      }
    }
  }

  // Inline-Attribute
  wxString strokeAttr = GetStringAttr(node, "stroke", "");
  if (!strokeAttr.IsEmpty() && strokeAttr.Lower() != "none") {
    bool ok = false;
    wxColour c = ParseColour(strokeAttr, ok);
    if (ok) {
      style.hasStroke = true;
      style.stroke = c;
      style.strokeSet = true;
    }
  }

  wxString fillAttr = GetStringAttr(node, "fill", "");
  if (!fillAttr.IsEmpty() && fillAttr.Lower() != "none") {
    bool ok = false;
    wxColour c = ParseColour(fillAttr, ok);
    if (ok) {
      style.hasFill = true;
      style.fill = c;
      style.fillSet = true;
    }
  }

  wxString strokeWidthAttr = GetStringAttr(node, "stroke-width", "");
  if (!strokeWidthAttr.IsEmpty()) {
    double dval;
    if (strokeWidthAttr.ToDouble(&dval)) {
      style.strokeWidth = dval;
      style.strokeWidthSet = true;
    }
  }

  wxString opacityAttr = GetStringAttr(node, "opacity", "");
  if (!opacityAttr.IsEmpty()) {
    double dval;
    if (opacityAttr.ToDouble(&dval)) {
      style.opacity = dval;
      style.opacitySet = true;
    }
  }

  wxString strokeOpacityAttr = GetStringAttr(node, "stroke-opacity", "");
  if (!strokeOpacityAttr.IsEmpty()) {
    double dval;
    if (strokeOpacityAttr.ToDouble(&dval)) {
      style.opacity = dval;
      style.opacitySet = true;
    }
  }

  wxString strokeDasharrayAttr = GetStringAttr(node, "stroke-dasharray", "");
  if (!strokeDasharrayAttr.IsEmpty()) {
    style.dashArray.clear();
    ParseDashArray(strokeDasharrayAttr, style.dashArray);
    style.dashArraySet = true;
  }

  wxString fontSizeAttr = GetStringAttr(node, "font-size", "");
  if (!fontSizeAttr.IsEmpty()) {
    double dval;
    if (fontSizeAttr.ToDouble(&dval)) {
      style.fontSize = dval;
      style.fontSizeSet = true;
    }
  }

  wxString fontFamilyAttr = GetStringAttr(node, "font-family", "");
  if (!fontFamilyAttr.IsEmpty()) {
    style.fontFamily = fontFamilyAttr;
    style.fontFamilySet = true;
  }

  // rx / ry für RECT
  wxString rxAttr = GetStringAttr(node, "rx", "");
  wxString ryAttr = GetStringAttr(node, "ry", "");
  if (!rxAttr.IsEmpty() || !ryAttr.IsEmpty()) {
    double rx = 0.0, ry = 0.0;
    bool rxOk = rxAttr.IsEmpty() ? false : rxAttr.ToDouble(&rx);
    bool ryOk = ryAttr.IsEmpty() ? false : ryAttr.ToDouble(&ry);
    if (rxOk || ryOk) {
      if (!rxOk) rx = ry;
      if (!ryOk) ry = rx;
      style.rx = rx;
      style.ry = ry;
      style.cornerRadiusSet = true;
    }
  }

  // style="..."
  wxString styleAttr = GetStringAttr(node, "style", "");
  if (!styleAttr.IsEmpty()) {
    SvgStyle s2 = ParseStyleAttribute(styleAttr);
    MergeStyles(style, s2);
  }

  // Gruppen-Element
  if (name == "g") {
    auto el = std::make_shared<SvgElement>();
    el->type = SvgElementType::GROUP;
    el->style = style;
    el->transform = tr;

    for (wxXmlNode* ch = node->GetChildren(); ch; ch = ch->GetNext()) {
      if (ch->GetType() != wxXML_ELEMENT_NODE) continue;
      auto childEl = ParseElementRecursive(ch, doc, style, tr.matrix);
      if (childEl) el->children.push_back(childEl);
    }

    return el;
  }

  // Einzelnes Element
  auto el = std::make_shared<SvgElement>();
  el->style = style;
  el->transform = tr;

  if (name == "line") {
    el->type = SvgElementType::LINE;
    ParseLineAttributes(node, *el);
  } else if (name == "rect") {
    el->type = SvgElementType::RECT;
    ParseRectAttributes(node, *el);
  } else if (name == "polyline") {
    el->type = SvgElementType::POLYLINE;
    ParsePolylineAttributes(node, *el);
  } else if (name == "path") {
    el->type = SvgElementType::PATH;
    ParsePathAttributes(node, *el);
  } else if (name == "text") {
    el->type = SvgElementType::TEXT;
    ParseTextAttributes(node, *el);
  } else {
    return nullptr;
  }

  return el;
}

// ---------------------------------------------------------
// Transform-Parsing
// ---------------------------------------------------------

SvgTransform SvgRenderer::ParseTransform(const wxString& transformStr) {
  SvgTransform t;

  wxString s = transformStr;
  s.Trim(true).Trim(false);

  wxRegEx reFunc("([a-zA-Z]+)\\s*\\(([^)]*)\\)", wxRE_EXTENDED | wxRE_ICASE);
  size_t pos = 0;

  while (reFunc.Matches(s, pos)) {
    wxString fullMatch = reFunc.GetMatch(s, 0);
    size_t start = s.find(fullMatch, pos);
    if (start == wxString::npos) break;

    wxString fname = reFunc.GetMatch(s, 1).Lower();
    wxString args = reFunc.GetMatch(s, 2);

    wxArrayString vals = wxSplit(args, ',', '\0');
    if (vals.size() == 1) {
      wxArrayString vals2 = wxSplit(args, ' ', '\0');
      if (vals2.size() > vals.size()) vals = vals2;
    }

    if (fname == "translate") {
      double tx = 0, ty = 0;
      if (vals.size() >= 1) vals[0].ToDouble(&tx);
      if (vals.size() >= 2) vals[1].ToDouble(&ty);

      wxAffineMatrix2D m;
      m.Translate(tx, ty);
      t.matrix.Concat(m);
    } else if (fname == "scale") {
      double sx = 1, sy = 1;
      if (vals.size() >= 1) vals[0].ToDouble(&sx);
      if (vals.size() >= 2)
        vals[1].ToDouble(&sy);
      else
        sy = sx;

      wxAffineMatrix2D m;
      m.Scale(sx, sy);
      t.matrix.Concat(m);
    } else if (fname == "rotate") {
      double angle = 0;
      vals[0].ToDouble(&angle);

      double cx = 0, cy = 0;
      if (vals.size() >= 3) {
        vals[1].ToDouble(&cx);
        vals[2].ToDouble(&cy);
      }

      wxAffineMatrix2D m;
      if (cx != 0 || cy != 0) {
        m.Translate(cx, cy);
        m.Rotate(wxDegToRad(angle));
        m.Translate(-cx, -cy);
      } else {
        m.Rotate(wxDegToRad(angle));
      }

      t.matrix.Concat(m);
    }

    pos = start + fullMatch.length();
  }

  return t;
}

// ---------------------------------------------------------
// Attribute-Parser
// ---------------------------------------------------------

void SvgRenderer::ParseLineAttributes(wxXmlNode* node, SvgElement& el) {
  el.x1 = GetDoubleAttr(node, "x1");
  el.y1 = GetDoubleAttr(node, "y1");
  el.x2 = GetDoubleAttr(node, "x2");
  el.y2 = GetDoubleAttr(node, "y2");
}

void SvgRenderer::ParseRectAttributes(wxXmlNode* node, SvgElement& el) {
  el.x = GetDoubleAttr(node, "x");
  el.y = GetDoubleAttr(node, "y");
  el.width = GetDoubleAttr(node, "width");
  el.height = GetDoubleAttr(node, "height");
  el.rx = GetDoubleAttr(node, "rx", 0.0);
}

void SvgRenderer::ParsePolylineAttributes(wxXmlNode* node, SvgElement& el) {
  wxString pts = GetStringAttr(node, "points", "");
  wxArrayString tokens = wxSplit(pts, ' ', '\0');
  for (auto& t : tokens) {
    wxArrayString xy = wxSplit(t, ',', '\0');
    if (xy.size() != 2) continue;
    double x, y;
    if (!xy[0].ToDouble(&x)) continue;
    if (!xy[1].ToDouble(&y)) continue;
    el.points.emplace_back(x, y);
  }
}

void SvgRenderer::ParsePathAttributes(wxXmlNode* node, SvgElement& el) {
  wxString d = GetStringAttr(node, "d", "");
  ParsePathData(d, el.points);
}

void SvgRenderer::ParseTextAttributes(wxXmlNode* node, SvgElement& el) {
  el.tx = GetDoubleAttr(node, "x", 0.0);
  el.ty = GetDoubleAttr(node, "y", 0.0);
  el.text = node->GetNodeContent();
  el.text.Trim(true).Trim(false);

  wxString textAnchorAttr = GetStringAttr(node, "text-anchor", "");
  if (!textAnchorAttr.IsEmpty()) {
    el.style.textAnchor = textAnchorAttr;
  } else {
    el.style.textAnchor = "start";
  }
}

double SvgRenderer::GetDoubleAttr(wxXmlNode* node, const wxString& name,
                                  double def) {
  wxString s = GetStringAttr(node, name, "");
  if (s.IsEmpty()) return def;
  double v;
  if (!s.ToDouble(&v)) return def;
  return v;
}

wxString SvgRenderer::GetStringAttr(wxXmlNode* node, const wxString& name,
                                    const wxString& def) {
  for (wxXmlAttribute* a = node->GetAttributes(); a; a = a->GetNext()) {
    if (a->GetName().IsSameAs(name, false)) return a->GetValue();
  }
  return def;
}

wxColour SvgRenderer::ParseColour(const wxString& s, bool& ok) {
  ok = false;
  wxString v = s;
  v.Trim(true).Trim(false);

  if (v.IsEmpty() || v.Lower() == "none") {
    return wxColour();
  }

  if (v.StartsWith("#")) {
    wxColour c(v);
    if (c.IsOk()) {
      ok = true;
      return c;
    } else {
      wxLogWarning("SvgRenderer::ParseColour: invalid hex color '%s'", v);
      return wxColour();
    }
  }

  if (v.StartsWith("rgb(") || v.StartsWith("rgba(")) {
    wxLogWarning("SvgRenderer::ParseColour: parsing rgb/rgba color '%s'", v);
  }

  wxColour c(v);
  if (c.IsOk()) {
    ok = true;
    return c;
  } else {
    wxLogWarning("SvgRenderer::ParseColour: unknown color '%s'", v);
    return wxColour();
  }
}

void SvgRenderer::ParseDashArray(const wxString& s, std::vector<double>& out) {
  out.clear();

  wxString input = s;
  input.Trim(true).Trim(false);

  if (input.IsEmpty() || input.Lower() == "none") {
    return;
  }

  wxArrayString parts = wxSplit(input, ',', '\0');
  if (parts.size() == 1) {
    wxArrayString parts2 = wxSplit(input, ' ', '\0');
    if (parts2.size() > 1) {
      parts = parts2;
    }
  }
  for (auto& p : parts) {
    wxString part = p;
    part.Trim(true).Trim(false);

    double v;
    if (!part.IsEmpty() && part.ToDouble(&v)) {
      out.push_back(v);
    }
  }
}

// ---------------------------------------------------------
// Path-Daten → Punkte
// ---------------------------------------------------------

void SvgRenderer::ParsePathData(const wxString& d,
                                std::vector<wxPoint2DDouble>& outPoints) {
  wxString s = d;
  s.Trim(true).Trim(false);

  double curX = 0, curY = 0;
  double startX = 0, startY = 0;
  wxChar cmd = 0;

  wxString tmp;
  wxArrayString tokens;

  for (size_t i = 0; i < s.Length(); ++i) {
    wxChar c = s[i];
    if (c == 'M' || c == 'm' || c == 'L' || c == 'l' || c == 'H' || c == 'h' ||
        c == 'V' || c == 'v' || c == 'Z' || c == 'z' || c == 'C' || c == 'c' ||
        c == 'S' || c == 's' || c == 'Q' || c == 'q' || c == 'T' || c == 't' ||
        c == 'A' || c == 'a') {
      if (!tmp.IsEmpty()) {
        tokens.push_back(tmp);
        tmp.clear();
      }
      wxString cc(c);
      tokens.push_back(cc);
    } else if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '-') {
      if (!tmp.IsEmpty()) {
        tokens.push_back(tmp);
        tmp.clear();
      }
      if (c == '-' && (i + 1 < s.Length() && isdigit(s[i + 1]))) {
        tmp.Append(c);
      }
    } else {
      tmp.Append(c);
    }
  }
  if (!tmp.IsEmpty()) tokens.push_back(tmp);

  size_t i = 0;
  while (i < tokens.size()) {
    wxString t = tokens[i];
    if (t.Length() == 1 &&
        (t[0] == 'M' || t[0] == 'm' || t[0] == 'L' || t[0] == 'l' ||
         t[0] == 'H' || t[0] == 'h' || t[0] == 'V' || t[0] == 'v' ||
         t[0] == 'Z' || t[0] == 'z' || t[0] == 'C' || t[0] == 'c' ||
         t[0] == 'S' || t[0] == 's' || t[0] == 'Q' || t[0] == 'q' ||
         t[0] == 'T' || t[0] == 't' || t[0] == 'A' || t[0] == 'a')) {
      cmd = t[0];
      ++i;
      continue;
    }

    if (cmd == 'M' || cmd == 'm') {
      if (i + 1 >= tokens.size()) break;
      double x, y;
      if (!tokens[i].ToDouble(&x) || !tokens[i + 1].ToDouble(&y)) break;
      if (cmd == 'm') {
        curX += x;
        curY += y;
      } else {
        curX = x;
        curY = y;
      }
      startX = curX;
      startY = curY;
      outPoints.emplace_back(curX, curY);
      i += 2;
      cmd = (cmd == 'm') ? 'l' : 'L';
    } else if (cmd == 'L' || cmd == 'l') {
      if (i + 1 >= tokens.size()) break;
      double x, y;
      if (!tokens[i].ToDouble(&x) || !tokens[i + 1].ToDouble(&y)) break;
      if (cmd == 'l') {
        curX += x;
        curY += y;
      } else {
        curX = x;
        curY = y;
      }
      outPoints.emplace_back(curX, curY);
      i += 2;
    } else if (cmd == 'H' || cmd == 'h') {
      double x;
      if (!tokens[i].ToDouble(&x)) break;
      if (cmd == 'h')
        curX += x;
      else
        curX = x;
      outPoints.emplace_back(curX, curY);
      ++i;
    } else if (cmd == 'V' || cmd == 'v') {
      double y;
      if (!tokens[i].ToDouble(&y)) break;
      if (cmd == 'v')
        curY += y;
      else
        curY = y;
      outPoints.emplace_back(curX, curY);
      ++i;
    } else if (cmd == 'C' || cmd == 'c') {
      if (i + 5 >= tokens.size()) break;
      double x1, y1, x2, y2, x, y;
      if (!tokens[i].ToDouble(&x1) || !tokens[i + 1].ToDouble(&y1) ||
          !tokens[i + 2].ToDouble(&x2) || !tokens[i + 3].ToDouble(&y2) ||
          !tokens[i + 4].ToDouble(&x) || !tokens[i + 5].ToDouble(&y)) {
        break;
      }
      if (cmd == 'c') {
        x1 += curX;
        y1 += curY;
        x2 += curX;
        y2 += curY;
        x += curX;
        y += curY;
      }
      ApproximateCubicBezier(curX, curY, x1, y1, x2, y2, x, y, outPoints);
      curX = x;
      curY = y;
      i += 6;
    } else if (cmd == 'Q' || cmd == 'q') {
      if (i + 3 >= tokens.size()) break;
      double x1, y1, x, y;
      if (!tokens[i].ToDouble(&x1) || !tokens[i + 1].ToDouble(&y1) ||
          !tokens[i + 2].ToDouble(&x) || !tokens[i + 3].ToDouble(&y)) {
        break;
      }
      if (cmd == 'q') {
        x1 += curX;
        y1 += curY;
        x += curX;
        y += curY;
      }
      ApproximateQuadraticBezier(curX, curY, x1, y1, x, y, outPoints);
      curX = x;
      curY = y;
      i += 4;
    } else if (cmd == 'Z' || cmd == 'z') {
      outPoints.emplace_back(startX, startY);
      curX = startX;
      curY = startY;
      ++i;
    } else {
      ++i;
    }
  }
}

// ---------------------------------------------------------
// Bezier-Approximation
// ---------------------------------------------------------

void SvgRenderer::ApproximateCubicBezier(
    double x0, double y0, double x1, double y1, double x2, double y2, double x3,
    double y3, std::vector<wxPoint2DDouble>& outPoints) {
  const int steps = 20;
  for (int i = 1; i <= steps; ++i) {
    double t = (double)i / steps;
    double t2 = t * t;
    double t3 = t2 * t;
    double mt = 1 - t;
    double mt2 = mt * mt;
    double mt3 = mt2 * mt;

    double x = mt3 * x0 + 3 * mt2 * t * x1 + 3 * mt * t2 * x2 + t3 * x3;
    double y = mt3 * y0 + 3 * mt2 * t * y1 + 3 * mt * t2 * y2 + t3 * y3;
    outPoints.emplace_back(x, y);
  }
}

void SvgRenderer::ApproximateQuadraticBezier(
    double x0, double y0, double x1, double y1, double x2, double y2,
    std::vector<wxPoint2DDouble>& outPoints) {
  const int steps = 15;
  for (int i = 1; i <= steps; ++i) {
    double t = (double)i / steps;
    double t2 = t * t;
    double mt = 1 - t;
    double mt2 = mt * mt;

    double x = mt2 * x0 + 2 * mt * t * x1 + t2 * x2;
    double y = mt2 * y0 + 2 * mt * t * y1 + t2 * y2;
    outPoints.emplace_back(x, y);
  }
}

// ---------------------------------------------------------
// Style anwenden
// ---------------------------------------------------------

#ifndef __OCPN__ANDROID__
void SvgRenderer::ApplyStyle(wxGraphicsContext* gc, const SvgStyle& style,
                             bool forStroke) {
  wxColour col = forStroke ? style.stroke : style.fill;

  if (!col.IsOk()) {
    col = forStroke ? *wxBLACK : *wxWHITE;
  }

  double alpha = style.opacity;
  if (alpha < 0.0) alpha = 0.0;
  if (alpha > 1.0) alpha = 1.0;

  wxColour c2(col.Red(), col.Green(), col.Blue(),
              (unsigned char)std::round(alpha * 255.0));

  if (forStroke) {
    int effectiveWidth = (int)std::round(style.strokeWidth);
    if (effectiveWidth <= 0) effectiveWidth = 1;

    wxPen pen(c2, effectiveWidth);

    if (!style.dashArray.empty()) {
      double scale = std::max(1.0, style.strokeWidth * 1.5);

      size_t n = style.dashArray.size();
      wxDash* dashes = new wxDash[n];

      for (size_t i = 0; i < n; ++i) {
        double v = style.dashArray[i] * scale;
        if (v < 1.0) v = 1.0;
        dashes[i] = (wxDash)std::round(v);
      }

      pen.SetStyle(wxPENSTYLE_SHORT_DASH);
      pen.SetDashes(n, dashes);

      delete[] dashes;
    }

    gc->SetPen(pen);
  } else {
    wxBrush brush(c2);
    gc->SetBrush(brush);
  }
}
#endif

// ---------------------------------------------------------
// RenderElement
// ---------------------------------------------------------

#ifndef __OCPN__ANDROID__
void SvgRenderer::RenderElement(wxGraphicsContext* gc, const SvgElement& el,
                                const SvgDocument& doc,
                                const wxAffineMatrix2D& parentTransform) {
  wxLogMessage("RenderElement: type=%d", (int)el.type);

  wxAffineMatrix2D m = parentTransform;
  m.Concat(el.transform.matrix);

  wxMatrix2D mat;
  wxPoint2DDouble tr;
  m.Get(&mat, &tr);

  wxLogMessage("  Transform: [%f %f; %f %f] + [%f %f]", mat.m_11, mat.m_12,
               mat.m_21, mat.m_22, tr.m_x, tr.m_y);

  gc->PushState();

  switch (el.type) {
    case SvgElementType::TEXT:
      wxLogMessage("  TEXT: '%s' at (%f, %f) fontSize=%f", el.text, el.tx,
                   el.ty, el.style.fontSize);
      break;

    case SvgElementType::RECT:
      wxLogMessage("  RECT: x=%f y=%f w=%f h=%f", el.x, el.y, el.width,
                   el.height);
      break;

    case SvgElementType::LINE:
      wxLogMessage("  LINE: (%f,%f) -> (%f,%f)", el.x1, el.y1, el.x2, el.y2);
      break;

    case SvgElementType::PATH:
      wxLogMessage("  PATH: %zu points", el.points.size());
      break;

    default:
      break;
  }

  switch (el.type) {
    case SvgElementType::GROUP: {
      for (auto& ch : el.children) {
        RenderElement(gc, *ch, doc, m);
      }
      break;
    }

    case SvgElementType::LINE: {
      wxMatrix2D mat;
      wxPoint2DDouble tr;
      m.Get(&mat, &tr);
      wxGraphicsMatrix gm = gc->CreateMatrix(mat.m_11, mat.m_12, mat.m_21,
                                             mat.m_22, tr.m_x, tr.m_y);
      gc->ConcatTransform(gm);

      if (el.style.hasStroke && el.style.stroke.IsOk()) {
        ApplyStyle(gc, el.style, true);
        gc->StrokeLine(el.x1, el.y1, el.x2, el.y2);
      }
      break;
    }

    case SvgElementType::RECT: {
      wxMatrix2D mat;
      wxPoint2DDouble tr;
      m.Get(&mat, &tr);
      wxGraphicsMatrix gm = gc->CreateMatrix(mat.m_11, mat.m_12, mat.m_21,
                                             mat.m_22, tr.m_x, tr.m_y);
      gc->ConcatTransform(gm);

      wxGraphicsPath p = gc->CreatePath();
      if (el.style.cornerRadiusSet &&
          (el.style.rx > 0.0 || el.style.ry > 0.0)) {
        double r = el.style.rx > 0.0 ? el.style.rx : el.style.ry;
        p.AddRoundedRectangle(el.x, el.y, el.width, el.height, r);
      } else {
        p.AddRectangle(el.x, el.y, el.width, el.height);
      }

      if (el.style.hasFill && el.style.fill.IsOk()) {
        ApplyStyle(gc, el.style, false);
        gc->FillPath(p);
      }
      if (el.style.hasStroke && el.style.stroke.IsOk()) {
        ApplyStyle(gc, el.style, true);
        gc->StrokePath(p);
      }
      break;
    }

    case SvgElementType::POLYLINE:
    case SvgElementType::PATH: {
      wxMatrix2D mat;
      wxPoint2DDouble tr;
      m.Get(&mat, &tr);
      wxGraphicsMatrix gm = gc->CreateMatrix(mat.m_11, mat.m_12, mat.m_21,
                                             mat.m_22, tr.m_x, tr.m_y);
      gc->ConcatTransform(gm);

      if (el.points.size() >= 2) {
        wxGraphicsPath path = gc->CreatePath();
        path.MoveToPoint(el.points[0]);
        for (size_t i = 1; i < el.points.size(); ++i) {
          path.AddLineToPoint(el.points[i]);
        }

        if (el.style.hasFill && el.style.fill.IsOk()) {
          ApplyStyle(gc, el.style, false);
          gc->FillPath(path);
        }
        if (el.style.hasStroke && el.style.stroke.IsOk()) {
          ApplyStyle(gc, el.style, true);
          gc->StrokePath(path);
        }
      }
      break;
    }

    case SvgElementType::TEXT: {
      //
      // 1) SVG font-size (px) → wxWidgets Punktgröße umrechnen
      //    Browser: 1px = 0.75pt (96dpi → 72dpi)
      //
      double pxSize = el.style.fontSize > 0 ? el.style.fontSize : 12.0;
      double ptSize = pxSize * 0.75;  // perfekte Browser-Entsprechung

      wxFontInfo fi(ptSize);
      fi.Family(wxFONTFAMILY_SWISS);
      if (!el.style.fontFamily.IsEmpty()) fi.FaceName(el.style.fontFamily);

      wxFont font(fi);

      wxColour textColor = el.style.hasFill ? el.style.fill : *wxBLACK;
      gc->SetFont(font, textColor);

      //
      // 2) Echte Font-Metriken holen
      //
      double tw, th, descent, externalLeading;
      gc->GetTextExtent(el.text, &tw, &th, &descent, &externalLeading);

      //
      // 3) Horizontale Ausrichtung
      //
      double x = el.tx;
      if (el.style.textAnchor == "middle")
        x -= tw / 2.0;
      else if (el.style.textAnchor == "end")
        x -= tw;

      //
      // 4) SVG y = BASELINE → wxWidgets braucht Top-Left
      //
      double baselineY = el.ty;
      double yTop = baselineY - (th - descent);

      //
      // 5) Transform anwenden (aber NICHT doppelt!)
      //
      wxMatrix2D mat;
      wxPoint2DDouble tr;
      m.Get(&mat, &tr);

      wxGraphicsMatrix gm = gc->CreateMatrix(mat.m_11, mat.m_12, mat.m_21,
                                             mat.m_22, tr.m_x, tr.m_y);
      gc->ConcatTransform(gm);

      //
      // 6) Text zeichnen
      //
      gc->DrawText(el.text, x, yTop);
      break;
    }
  }

  gc->PopState();
}
#endif

// ---------------------------------------------------------
// Android: NanoSVG Rendering
// ---------------------------------------------------------
#ifdef __OCPN__ANDROID__

bool SvgRenderer::RenderToImageNanoSVG(const wxString& svgXml, wxImage& outImage,
                                       int targetWidth, int targetHeight) {
  // Standard-Größen falls nicht angegeben
  if (targetWidth <= 0) targetWidth = 900;
  if (targetHeight <= 0) targetHeight = 400;
  
  // Konvertiere wxString zu C-String
  wxCharBuffer buf = svgXml.ToUTF8();
  char* svgStr = (char*)buf.data();
  
  // Parse SVG
  NSVGimage* image = nsvgParse(svgStr, "px", 96.0f);
  if (!image) {
    wxLogWarning("RenderToImageNanoSVG: SVG parse failed");
    return false;
  }
  
  // Berechne Skalierung
  float scale = 1.0f;
  if (image->width > 0 && image->height > 0) {
    float scaleX = (float)targetWidth / image->width;
    float scaleY = (float)targetHeight / image->height;
    scale = (scaleX < scaleY) ? scaleX : scaleY;
  }
  
  int w = (int)(image->width * scale);
  int h = (int)(image->height * scale);
  
  if (w <= 0 || h <= 0) {
    nsvgDelete(image);
    wxLogWarning("RenderToImageNanoSVG: Invalid dimensions %d x %d", w, h);
    return false;
  }
  
  // Erstelle Rasterizer
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) {
    nsvgDelete(image);
    wxLogWarning("RenderToImageNanoSVG: Rasterizer creation failed");
    return false;
  }
  
  // Allokiere Bitmap (RGBA)
  unsigned char* bitmap = new unsigned char[w * h * 4];
  
  // Rasterisiere
  nsvgRasterize(rast, image, 0, 0, scale, bitmap, w, h, w * 4);
  
  // Konvertiere zu wxImage (RGB, ignoriere Alpha)
  unsigned char* rgbData = new unsigned char[w * h * 3];
  for (int i = 0; i < w * h; i++) {
    rgbData[i * 3 + 0] = bitmap[i * 4 + 0];  // R
    rgbData[i * 3 + 1] = bitmap[i * 4 + 1];  // G
    rgbData[i * 3 + 2] = bitmap[i * 4 + 2];  // B
  }
  
  outImage = wxImage(w, h, rgbData);
  
  // Cleanup
  delete[] bitmap;
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);
  
  return true;
}

#endif