/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Icon handling and icon resource management
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2026 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/

#include "signalk_notes_opencpn_pi.h"
#include "ocpn_plugin.h"
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "tpicons.h"
#include <wx/mstream.h>
#include <wx/filename.h>

tpicons::tpicons(signalk_notes_opencpn_pi* plugin) : m_plugin(plugin) {
  m_dScaleFactor = 1.0;
  m_iDisplayScaleFactor = 32;
  m_iToolScaleFactor = GetOCPNGUIToolScaleFactor_PlugIn();
  m_iImageRefSize = m_iDisplayScaleFactor * m_iToolScaleFactor;
  m_bUpdateIcons = false;
  m_ColourScheme = PI_GLOBAL_COLOR_SCHEME_RGB;

  initialize_images();
}

tpicons::~tpicons() {
  // nichts mehr zu löschen
}

void tpicons::initialize_images(void) {
  wxFileName fn;

  // Plugin-Datenverzeichnis
  fn.SetPath(m_plugin->m_pluginDataDir);
  fn.AppendDir("data");

  SKN_LOG(m_plugin,"signalk_notes_opencpn_pi data location: %s", fn.GetFullPath());

  m_failedBitmapLoad = false;

#ifdef PLUGIN_USE_SVG
  fn.SetFullName("signalk_notes_opencpn.svg");
  m_s_signalk_notes_opencpn_pi = fn.GetFullPath();
  m_bm_signalk_notes_opencpn_pi = LoadSVG(fn.GetFullPath());

  fn.SetFullName("signalk_notes_opencpngrey.svg");
  m_s_signalk_notes_opencpn_grey_pi = fn.GetFullPath();
  m_bm_signalk_notes_opencpn_grey_pi = LoadSVG(fn.GetFullPath());

  fn.SetFullName("signalk_notes_opencpn-toggled.svg");
  m_s_signalk_notes_opencpn_toggled_pi = fn.GetFullPath();
  m_bm_signalk_notes_opencpn_toggled_pi = LoadSVG(fn.GetFullPath());
#else
  fn.SetFullName("signalk_notes_opencpn.png");
  m_p_bm_signalk_notes_opencpn_pi =
      new wxBitmap(fn.GetFullPath(), wxBITMAP_TYPE_PNG);
  if (!m_p_bm_signalk_notes_opencpn_pi->IsOk()) m_failedBitmapLoad = true;
#endif

  if (m_failedBitmapLoad) {
    OCPNMessageBox_PlugIn(NULL,
                          _("Failed to load all signalk_notes_opencpn_pi "
                            "icons, check OCPN log for details"),
                          _("OpenCPN Alert"), wxOK);
  } else {
    CreateSchemeIcons();
    ScaleIcons();
  }
}

#ifdef PLUGIN_USE_SVG
wxBitmap tpicons::LoadSVG(const wxString filename, int width, int height) {
  if (width == -1) width = m_iImageRefSize;
  if (height == -1) height = m_iImageRefSize;

  wxBitmap bmp = GetBitmapFromSVGFile(filename, width, height);
  if (!bmp.IsOk()) m_failedBitmapLoad = true;

  return bmp;
}

wxBitmap tpicons::ScaleIcon(wxBitmap bitmap, const wxString filename,
                            double sf) {
  int w = bitmap.GetWidth() * sf;
  int h = bitmap.GetHeight() * sf;

  wxBitmap scaled = GetBitmapFromSVGFile(filename, w, h);
  if (scaled.IsOk()) return scaled;

  return wxBitmap(32 * sf, 32 * sf);
}
#endif

wxBitmap* tpicons::ScaleIcon(wxBitmap bitmap, double sf) {
  wxImage img = bitmap.ConvertToImage();
  return new wxBitmap(img.Scale(img.GetWidth() * sf, img.GetHeight() * sf,
                                wxIMAGE_QUALITY_HIGH));
}

bool tpicons::ScaleIcons() {
  if (!SetScaleFactor()) return false;

  CreateSchemeIcons();
  return true;
}

bool tpicons::SetScaleFactor() {
  double newScale = GetOCPNGUIToolScaleFactor_PlugIn();
  if (m_dScaleFactor != newScale) {
    m_dScaleFactor = newScale;
    return true;
  }
  return false;
}

void tpicons::SetColourScheme(PI_ColorScheme cs) {
  if (m_ColourScheme != cs) {
    m_ColourScheme = cs;
    m_bUpdateIcons = true;
    ChangeScheme();
  } else {
    m_bUpdateIcons = false;
  }
}

void tpicons::ChangeScheme(void) {
  switch (m_ColourScheme) {
    case PI_GLOBAL_COLOR_SCHEME_RGB:
    case PI_GLOBAL_COLOR_SCHEME_DAY:
      m_bm_signalk_notes_opencpn_grey_pi =
          m_bm_day_signalk_notes_opencpn_grey_pi;
      break;
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
      m_bm_signalk_notes_opencpn_grey_pi =
          m_bm_dusk_signalk_notes_opencpn_grey_pi;
      break;
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
      m_bm_signalk_notes_opencpn_grey_pi =
          m_bm_night_signalk_notes_opencpn_grey_pi;
      break;
    default:
      break;
  }
}

void tpicons::CreateSchemeIcons() {
  m_bm_day_signalk_notes_opencpn_grey_pi = m_bm_signalk_notes_opencpn_grey_pi;
  m_bm_day_signalk_notes_opencpn_toggled_pi =
      m_bm_signalk_notes_opencpn_toggled_pi;
  m_bm_day_signalk_notes_opencpn_pi = m_bm_signalk_notes_opencpn_pi;

  m_bm_dusk_signalk_notes_opencpn_grey_pi =
      BuildDimmedToolBitmap(m_bm_signalk_notes_opencpn_grey_pi, 128);
  m_bm_dusk_signalk_notes_opencpn_pi =
      BuildDimmedToolBitmap(m_bm_signalk_notes_opencpn_pi, 128);
  m_bm_dusk_signalk_notes_opencpn_toggled_pi =
      BuildDimmedToolBitmap(m_bm_signalk_notes_opencpn_toggled_pi, 128);

  m_bm_night_signalk_notes_opencpn_grey_pi =
      BuildDimmedToolBitmap(m_bm_signalk_notes_opencpn_grey_pi, 32);
  m_bm_night_signalk_notes_opencpn_pi =
      BuildDimmedToolBitmap(m_bm_signalk_notes_opencpn_pi, 32);
  m_bm_night_signalk_notes_opencpn_toggled_pi =
      BuildDimmedToolBitmap(m_bm_signalk_notes_opencpn_toggled_pi, 32);
}

wxBitmap tpicons::BuildDimmedToolBitmap(wxBitmap bmp_normal,
                                        unsigned char dim_ratio) {
  wxImage img = bmp_normal.ConvertToImage();
  if (!img.IsOk()) return bmp_normal;

  if (dim_ratio < 200) {
    int w = img.GetWidth();
    int h = img.GetHeight();
    double factor = double(dim_ratio) / 256.0;

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        if (!img.IsTransparent(x, y)) {
          wxImage::RGBValue rgb(img.GetRed(x, y), img.GetGreen(x, y),
                                img.GetBlue(x, y));
          wxImage::HSVValue hsv = wxImage::RGBtoHSV(rgb);
          hsv.value *= factor;
          wxImage::RGBValue nrgb = wxImage::HSVtoRGB(hsv);
          img.SetRGB(x, y, nrgb.red, nrgb.green, nrgb.blue);
        }
      }
    }
  }

  return wxBitmap(img);
}
