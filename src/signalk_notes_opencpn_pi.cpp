/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Main plugin implementation and OpenCPN integration
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2026 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__) && !defined(__APPLE__)
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#if defined(ocpnUSE_GL) && defined(__APPLE__)
#include <OpenGL/gl.h>
#endif

#ifdef __OCPN__ANDROID__
#include <GLES2/gl2.h>
#endif

#include "ocpn_plugin.h"

#include "version.h"
#include "signalk_notes_opencpn_pi.h"
#include "tpSignalKNotes.h"
#include "tpicons.h"
#include "tpConfigDialog.h"

#include <cmath>
#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/stdpaths.h>
#include <wx/event.h>
#include <wx/sysopt.h>
#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/msgdlg.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/aui/aui.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/listctrl.h>
#include <algorithm>

#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/window.h>

#ifdef __WXMSW__
#include <rpc.h>
#pragma comment(lib, "Rpcrt4.lib")
#elif defined(__OCPN__ANDROID__)
#include "android_uuid.h"
#else
#include <uuid/uuid.h>
#endif

#include "wxWTranslateCatalog.h"
#include "wx/jsonwriter.h"

#ifndef DECL_EXP
#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
#define DECL_EXP __declspec(dllexport)
#else
#define DECL_EXP
#endif
#endif

signalk_notes_opencpn_pi* g_signalk_notes_opencpn_pi = nullptr;

// -----------------------------------------------------------------------------
// Factory
// -----------------------------------------------------------------------------

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
  return new signalk_notes_opencpn_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

extern "C" DECL_EXP int get_api_major_version() {
  return OCPN_API_VERSION_MAJOR;
}

extern "C" DECL_EXP int get_api_minor_version() {
  return OCPN_API_VERSION_MINOR;
}

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

signalk_notes_opencpn_pi::signalk_notes_opencpn_pi(void* ppimgr)
    : opencpn_plugin_119(ppimgr) {
  g_signalk_notes_opencpn_pi = this;

  wxString pluginDir = GetPluginDataDir("signalk_notes_opencpn_pi");
  if (!pluginDir.EndsWith("/")) pluginDir += "/";

  wxString dataDir = pluginDir + "data/";
  if (!wxDir::Exists(dataDir)) wxMkdir(dataDir);

  wxString iconDir = dataDir + "icons/";
  if (!wxDir::Exists(iconDir)) wxMkdir(iconDir);

  wxString layerDir = pluginDir + "Layers/";
  if (!wxDir::Exists(layerDir)) wxMkdir(layerDir);

  m_ptpicons = new tpicons(this);
  m_pSignalKNotesManager = new tpSignalKNotesManager(this);
}

signalk_notes_opencpn_pi::~signalk_notes_opencpn_pi() {
  if (m_pSignalKNotesManager) delete m_pSignalKNotesManager;
}

// -----------------------------------------------------------------------------
// Metadata
// -----------------------------------------------------------------------------

int signalk_notes_opencpn_pi::GetAPIVersionMajor() {
  return OCPN_API_VERSION_MAJOR;
}
int signalk_notes_opencpn_pi::GetAPIVersionMinor() {
  return OCPN_API_VERSION_MINOR;
}

int signalk_notes_opencpn_pi::GetPlugInVersionMajor() {
  return PLUGIN_VERSION_MAJOR;
}
int signalk_notes_opencpn_pi::GetPlugInVersionMinor() {
  return PLUGIN_VERSION_MINOR;
}
int signalk_notes_opencpn_pi::GetPlugInVersionPatch() {
  return PLUGIN_VERSION_PATCH;
}
int signalk_notes_opencpn_pi::GetPlugInVersionPost() {
  return PLUGIN_VERSION_TWEAK;
}

wxString signalk_notes_opencpn_pi::GetCommonName() {
  return _T(PLUGIN_COMMON_NAME);
}
wxString signalk_notes_opencpn_pi::GetShortDescription() {
  return _(PLUGIN_SHORT_DESCRIPTION);
}
wxString signalk_notes_opencpn_pi::GetLongDescription() {
  return _(PLUGIN_LONG_DESCRIPTION);
}

// Init / DeInit
int signalk_notes_opencpn_pi::Init(void) {
  AddLocaleCatalog(PLUGIN_CATALOG_NAME);
  // wxMessageBox("Attach debugger now! PID: " + wxString::Format("%d",
  // getpid()),"Debug", wxOK);
  m_parent_window = GetOCPNCanvasWindow();
  m_pTPConfig = GetOCPNConfigObject();

  LoadConfig();

#ifdef PLUGIN_USE_SVG
  m_signalk_notes_opencpn_button_id = InsertPlugInToolSVG(
      _("SignalK Notes"), m_ptpicons->m_s_signalk_notes_opencpn_grey_pi,
      m_ptpicons->m_s_signalk_notes_opencpn_pi,
      m_ptpicons->m_s_signalk_notes_opencpn_toggled_pi, wxITEM_CHECK,
      _("SignalK Notes"), wxS(""), nullptr, -1, 0, this);
#else
  m_signalk_notes_opencpn_button_id = InsertPlugInTool(
      _("SignalK Notes"), &m_ptpicons->m_bm_signalk_notes_opencpn_grey_pi,
      &m_ptpicons->m_bm_signalk_notes_opencpn_pi, wxITEM_CHECK,
      _("SignalK Notes"), wxS(""), nullptr, -1, 0, this);
#endif

  return (WANTS_CURSOR_LATLON | WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
          INSTALLS_TOOLBOX_PAGE | WANTS_OVERLAY_CALLBACK |
          WANTS_OPENGL_OVERLAY_CALLBACK | WANTS_PLUGIN_MESSAGING |
          WANTS_LATE_INIT | WANTS_MOUSE_EVENTS | WANTS_KEYBOARD_EVENTS |
          WANTS_ONPAINT_VIEWPORT | WANTS_PREFERENCES);
}

void signalk_notes_opencpn_pi::LateInit(void) {
  SendPluginMessage("SIGNALK_NOTES_OPENCPN_PI_READY_FOR_REQUESTS", "TRUE");
}

bool signalk_notes_opencpn_pi::DeInit(void) {
  if (m_pOverviewDialog) {
    m_pOverviewDialog->Destroy();
    m_pOverviewDialog = nullptr;
  }

  if (m_pTPConfig) SaveConfig();
  return true;
}

// -----------------------------------------------------------------------------
// Toolbar
// -----------------------------------------------------------------------------

void signalk_notes_opencpn_pi::OnToolbarToolCallback(int id) {
  if (!m_pSignalKNotesManager->GetAuthToken().IsEmpty()) {
    if (!m_pSignalKNotesManager->ValidateToken()) {
      m_pSignalKNotesManager->SetAuthToken("");
      m_pSignalKNotesManager->ClearAuthRequest();
      SaveConfig();
    }
  }

  if (m_pSignalKNotesManager) {
    m_pSignalKNotesManager->CleanupDisabledProviders();
  }

  if (m_pConfigDialog) {
    m_pConfigDialog->Destroy();
    m_pConfigDialog = nullptr;
  }

  m_pConfigDialog = new tpConfigDialog(this, GetOCPNCanvasWindow());
  m_pConfigDialog->ShowModal();

  if (m_pConfigDialog->GetReturnCode() == wxID_OK) {
    m_pSignalKNotesManager->SetProviderSettings(
        m_pConfigDialog->GetProviderSettings());

    m_pSignalKNotesManager->SetIconMappings(m_pConfigDialog->GetIconMappings());

    SaveConfig();

    // UPDATE für ALLE Canvas
    for (auto& pair : m_canvasStates) {
      CanvasState& state = pair.second;
      if (state.valid) {
        double centerLat = state.viewPort.clat;
        double centerLon = state.viewPort.clon;
        double maxDistance = CalculateMaxDistance(state);
        m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon,
                                                     maxDistance, state);
      }
    }

    RequestRefresh(m_parent_window);
  }

  m_pConfigDialog->Destroy();
  m_pConfigDialog = nullptr;

  UpdateOverviewDialog();
}

void signalk_notes_opencpn_pi::OnToolbarToolDownCallback(int id) {}
void signalk_notes_opencpn_pi::OnToolbarToolUpCallback(int id) {
  m_ptpicons->SetScaleFactor();
}

void signalk_notes_opencpn_pi::UpdateOverviewDialog() {
  // Update für alle Canvas
  for (auto& pair : m_canvasStates) {
    CanvasState& state = pair.second;
    if (state.valid) {
      double centerLat = state.viewPort.clat;
      double centerLon = state.viewPort.clon;
      double maxDistance = CalculateMaxDistance(state);

      m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon,
                                                   maxDistance, state);
    }
  }

  if (m_pOverviewDialog) {
    if (m_canvasStates.size() >= 2) {
      int left = 0, right = 0;
      auto it = m_canvasStates.begin();
      if (it != m_canvasStates.end()) {
        left = GetVisibleNoteCount(it->second);
        ++it;
      }
      if (it != m_canvasStates.end()) {
        right = GetVisibleNoteCount(it->second);
      }
      m_pOverviewDialog->UpdateVisibleCount(left, right);
    } else {
      m_pOverviewDialog->UpdateVisibleCount(GetVisibleNoteCount());
    }
  }
}

// -----------------------------------------------------------------------------
// Rendering — BLOCK 1 ends here
// -----------------------------------------------------------------------------

bool signalk_notes_opencpn_pi::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) {
  return false;
}

bool signalk_notes_opencpn_pi::RenderOverlayMultiCanvas(wxDC& dc,
                                                        PlugIn_ViewPort* vp,
                                                        int canvasIndex,
                                                        int priority) {
  return DoRenderOverlay(dc, vp, canvasIndex, priority);
}

bool signalk_notes_opencpn_pi::RenderGLOverlay(wxGLContext* pcontext,
                                               PlugIn_ViewPort* vp) {
  return false;
}

bool signalk_notes_opencpn_pi::RenderGLOverlayMultiCanvas(wxGLContext* pcontext,
                                                          PlugIn_ViewPort* vp,
                                                          int canvasIndex,
                                                          int priority) {
  return DoRenderGLOverlay(pcontext, vp, canvasIndex, priority);
}

bool signalk_notes_opencpn_pi::DoRenderCommon(PlugIn_ViewPort* vp,
                                              int canvasIndex, int priority) {
  // MinScale-Prüfung: wenn aktiviert (!=0) und aktueller Maßstab >= MinScale →
  // nichts anzeigen
  if (m_clusterMinScale != 0 && m_clusterMinScale < vp->chart_scale) {
    return false;
  }
  // CanvasState aktualisieren
  m_activeCanvasIndex = canvasIndex;
  CanvasState& state = m_canvasStates[canvasIndex];
  state.lastViewPort = state.viewPort;
  state.viewPort = *vp;
  state.valid = true;

  // Cluster-Zoom prüfen
  if (!ProcessClusterZoom(state, canvasIndex)) return false;

  // Sichtbereich bestimmen
  double centerLat = state.viewPort.clat;
  double centerLon = state.viewPort.clon;
  double maxDistance = CalculateMaxDistance(state);

  // Fetch-Update nur wenn kein Dialog offen ist
  wxLongLong now = wxGetLocalTimeMillis();
  bool updateClusters = false;
  if (!m_dialogOpen &&
      (state.lastFetchTime == 0 ||
       (now - state.lastFetchTime).ToLong() >
           (long)(m_fetchInterval * 60 * 1000) ||
       fabs(centerLat - state.lastFetchCenterLat) > 0.01 ||
       fabs(centerLon - state.lastFetchCenterLon) > 0.01 ||
       fabs(maxDistance - state.lastFetchDistance) > maxDistance * 0.2)) {
    m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon,
                                                 maxDistance, state);

    state.lastFetchCenterLat = centerLat;
    state.lastFetchCenterLon = centerLon;
    state.lastFetchDistance = maxDistance;
    state.lastFetchTime = now;
    updateClusters = true;
  } else {
    updateClusters = ViewPortsDiffer(state.viewPort, state.lastViewPort);
  }
  if (updateClusters) {
    // Cluster neu berechnen, wenn sich der ViewPort geändert hat oder neue
    // Daten geladen wurden
    // Sichtbare Notes holen
    std::vector<const SignalKNote*> visibleNotes;
    m_pSignalKNotesManager->GetVisibleNotes(state, visibleNotes);

    if (visibleNotes.empty()) return false;

    // Cluster berechnen
    state.clusters = BuildClusters(visibleNotes, state);
  }
  return !state.clusters.empty();
}

bool signalk_notes_opencpn_pi::DoRenderOverlay(wxDC& dc, PlugIn_ViewPort* vp,
                                               int canvasIndex, int priority) {
  PruneCanvasStates(canvasIndex);
  if (priority != OVERLAY_OVER_EMBOSS ||
      !DoRenderCommon(vp, canvasIndex, priority))
    return false;

  CanvasState& state = m_canvasStates[canvasIndex];
  bool drewSomething = false;

  for (const auto& cluster : state.clusters) {
    if (cluster.noteIds.size() == 1) {
      const SignalKNote* note =
          m_pSignalKNotesManager->GetNoteByGUID(state, cluster.noteIds[0]);
      if (!note) continue;

      wxBitmap bmp;
      if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp, false))
        continue;

      dc.DrawBitmap(bmp, cluster.screenPos.x - bmp.GetWidth() / 2,
                    cluster.screenPos.y - bmp.GetHeight() / 2, true);

      drewSomething = true;
    } else {
      wxBitmap clusterBmp = CreateClusterBitmap(cluster.noteIds.size());

      dc.DrawBitmap(clusterBmp, cluster.screenPos.x - clusterBmp.GetWidth() / 2,
                    cluster.screenPos.y - clusterBmp.GetHeight() / 2, true);

      drewSomething = true;
    }
  }

  return drewSomething;
}

bool signalk_notes_opencpn_pi::DoRenderGLOverlay(wxGLContext* pcontext,
                                                 PlugIn_ViewPort* vp,
                                                 int canvasIndex,
                                                 int priority) {
#ifdef __OCPN__ANDROID__
  return false;
#else
  if (priority > 0 || !m_pSignalKNotesManager) return false;
  PruneCanvasStates(canvasIndex);
  if (!DoRenderCommon(vp, canvasIndex, priority)) return false;

  CanvasState& state = m_canvasStates[canvasIndex];
  bool drewSomething = false;

  for (const auto& cluster : state.clusters) {
    if (cluster.noteIds.size() == 1) {
      const SignalKNote* note =
          m_pSignalKNotesManager->GetNoteByGUID(state, cluster.noteIds[0]);
      if (!note) continue;

      wxBitmap bmp;
      if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp, true))
        continue;

      DrawGLBitmap(bmp, cluster.screenPos.x - bmp.GetWidth() / 2,
                   cluster.screenPos.y - bmp.GetHeight() / 2);

      drewSomething = true;
    } else {
      wxBitmap clusterBmp = CreateClusterBitmap(cluster.noteIds.size());
      // Für OpenGL vertikal spiegeln
      wxImage img = clusterBmp.ConvertToImage();
      img = img.Mirror(false);  // false = vertikal spiegeln
      wxBitmap flipped(img);
      flipped.UseAlpha(true);
      DrawGLBitmap(flipped, cluster.screenPos.x - clusterBmp.GetWidth() / 2,
                   cluster.screenPos.y - clusterBmp.GetHeight() / 2);

      drewSomething = true;
    }
  }

  return drewSomething;
#endif
}

bool signalk_notes_opencpn_pi::KeyboardEventHook(wxKeyEvent& event) {
  return false;
}

bool signalk_notes_opencpn_pi::MouseEventHook(wxMouseEvent& event) {
  if (event.Dragging() && event.LeftIsDown()) {
    return false;
  }
  if (!event.LeftDown() || !m_pSignalKNotesManager) return false;

  m_activeCanvasIndex = GetCanvasIndexUnderMouse();
  // Fallback auf letzten bekannten State
  if (m_activeCanvasIndex == -1 && !m_canvasStates.empty()) {
    m_activeCanvasIndex = m_canvasStates.rbegin()->first;
  }

  if (m_activeCanvasIndex == -1) return false;

  CanvasState& state = m_canvasStates[m_activeCanvasIndex];
  if (!state.valid) return false;

  wxPoint mousePos = event.GetPosition();

  double clickLat = 0.0, clickLon = 0.0;
  PlugIn_ViewPort vpCopy = state.viewPort;
  GetCanvasLLPix(&vpCopy, mousePos, &clickLat, &clickLon);

  SKN_LOG(this, "Mouse clicked canvas=%d at lat=%.6f lon=%.6f screen(%d,%d)",
          m_activeCanvasIndex, clickLat, clickLon, mousePos.x, mousePos.y);

  struct ClickableElement {
    enum Type { NOTE, CLUSTER } type;
    double distancePixels;
    wxString noteGuid;
    size_t clusterIndex;
    wxString description;
  };

  std::vector<ClickableElement> hitElements;

  auto pixelDistance = [](wxPoint p1, wxPoint p2) {
    int dx = p2.x - p1.x;
    int dy = p2.y - p1.y;
    return std::sqrt(dx * dx + dy * dy);
  };

  auto noteIsInMultiCluster = [&](const SignalKNote* note) {
    for (const auto& c : state.clusters) {
      if (c.noteIds.size() <= 1) continue;
      for (const auto& id : c.noteIds) {
        if (id == note->id) return true;
      }
    }
    return false;
  };

  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(state, visibleNotes);

  double noteTolerance = GetIconSize() / 2;
  double clusterTolerance = GetClusterSize() / 2;

  SKN_LOG(this, "Hit-Test tolerances: note=%.1f px, cluster=%.1f px",
          noteTolerance, clusterTolerance);

  // CLUSTER-HIT-TEST
  SKN_LOG(this, "Testing %zu clusters for hit...", state.clusters.size());

  for (size_t i = 0; i < state.clusters.size(); i++) {
    const auto& cluster = state.clusters[i];
    if (cluster.noteIds.size() <= 1) continue;

    double distPx = pixelDistance(mousePos, cluster.screenPos);

    if (distPx <= clusterTolerance) {
      ClickableElement elem;
      elem.type = ClickableElement::CLUSTER;
      elem.distancePixels = distPx;
      elem.clusterIndex = i;
      elem.description = wxString::Format("Cluster %zu (%zu notes) at %.1f px",
                                          i, cluster.noteIds.size(), distPx);
      hitElements.push_back(elem);

      SKN_LOG(this, "Cluster HIT: %s (tolerance=%.1f px)",
              elem.description.mb_str(), clusterTolerance);
    }
  }

  // NOTE-HIT-TEST
  SKN_LOG(this, "Testing %zu individual notes for hit...", visibleNotes.size());

  for (const SignalKNote* note : visibleNotes) {
    if (!note) continue;
    if (noteIsInMultiCluster(note)) continue;

    wxPoint noteScreenPos;
    GetCanvasPixLL(&vpCopy, &noteScreenPos, note->latitude, note->longitude);

    double distPx = pixelDistance(mousePos, noteScreenPos);

    if (distPx <= noteTolerance) {
      ClickableElement elem;
      elem.type = ClickableElement::NOTE;
      elem.distancePixels = distPx;
      elem.noteGuid = note->id;
      elem.description =
          wxString::Format("Note '%s' at %.1f px", note->id, distPx);
      hitElements.push_back(elem);

      SKN_LOG(this, "Note HIT: %s (tolerance=%.1f px)",
              elem.description.mb_str(), noteTolerance);
    }
  }

  // NO HITS
  if (hitElements.empty()) {
    SKN_LOG(this, "No icon or cluster hit");
    return false;
  }

  // SORTING
  std::sort(hitElements.begin(), hitElements.end(),
            [](const ClickableElement& a, const ClickableElement& b) {
              return a.distancePixels < b.distancePixels;
            });

  const ClickableElement& winner = hitElements[0];

  // TREAT WINNERS
  if (winner.type == ClickableElement::CLUSTER) {
    const auto& cluster = state.clusters[winner.clusterIndex];
    SKN_LOG(this, "Cluster clicked with %zu notes", cluster.noteIds.size());
    OnClusterClick(cluster, state, m_activeCanvasIndex);
    return true;
  }

  SKN_LOG(this, "Note clicked: %s", winner.noteGuid.mb_str());
  m_pSignalKNotesManager->OnIconClick(winner.noteGuid, state,
                                      m_activeCanvasIndex);
  return true;
}

void signalk_notes_opencpn_pi::SaveConfig() {
  wxFileConfig* pConf = m_pTPConfig;
  if (!pConf) return;
  SKN_LOG(this, "SaveConfig called, token='%s'",  // ← NEU
          m_pSignalKNotesManager->GetAuthToken().Left(20));

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  pConf->Write("SignalKHost", m_pSignalKNotesManager->GetServerHost());
  pConf->Write("SignalKPort", m_pSignalKNotesManager->GetServerPort());

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/Providers");

  for (auto& it : m_pSignalKNotesManager->GetProviderSettings())
    pConf->Write(it.first, it.second);

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/IconMappings");

  for (auto& it : m_pSignalKNotesManager->GetIconMappings())
    pConf->Write(it.first, it.second);

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  pConf->Write("AuthToken", m_pSignalKNotesManager->GetAuthToken());
  pConf->Write("AuthRequestHref", m_pSignalKNotesManager->GetAuthRequestHref());
  pConf->Write("ClientUUID", m_clientUUID);

  if (m_pConfigDialog) {
    m_pTPConfig->Write("DisplaySettings/IconSize",
                       m_pConfigDialog->GetIconSize());

    m_pTPConfig->Write("DisplaySettings/ClusterSize",
                       m_pConfigDialog->GetClusterSize());

    m_pTPConfig->Write("DisplaySettings/ClusterRadius",
                       m_pConfigDialog->GetClusterRadius());

    wxColour clusterColor = m_pConfigDialog->GetClusterColor();
    m_pTPConfig->Write("DisplaySettings/ClusterColorR",
                       (int)clusterColor.Red());
    m_pTPConfig->Write("DisplaySettings/ClusterColorG",
                       (int)clusterColor.Green());
    m_pTPConfig->Write("DisplaySettings/ClusterColorB",
                       (int)clusterColor.Blue());

    wxColour textColor = m_pConfigDialog->GetClusterTextColor();
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorR",
                       (int)textColor.Red());
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorG",
                       (int)textColor.Green());
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorB",
                       (int)textColor.Blue());

    m_pTPConfig->Write("DisplaySettings/ClusterFontSize",
                       m_pConfigDialog->GetClusterFontSize());

    m_pTPConfig->Write("DisplaySettings/DebugMode", (long)m_debugMode);

    m_pTPConfig->Write("DisplaySettings/ClusterMaxScale",
                       m_pConfigDialog->GetClusterMaxScale());
    m_pTPConfig->Write("DisplaySettings/ClusterMinScale",
                       m_pConfigDialog->GetClusterMinScale());
    m_pTPConfig->Write("DisplaySettings/FetchInterval",
                       m_pConfigDialog->GetFetchInterval());
  }
  // Write changes to disk
  pConf->Flush();
  SKN_LOG(this, "SaveConfig Flush done");  // ← NEU
}

void signalk_notes_opencpn_pi::LoadConfig() {
  wxFileConfig* pConf = m_pTPConfig;
  if (!pConf) return;

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  wxString host;
  int port;

  pConf->Read("SignalKHost", &host, "192.168.188.25");
  pConf->Read("SignalKPort", &port, 4000);

  m_pSignalKNotesManager->SetServerDetails(host, port);

  std::map<wxString, bool> providers;

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/Providers");

  wxString providerName;
  long providerIndex;

  bool hasMore = pConf->GetFirstEntry(providerName, providerIndex);

  while (hasMore) {
    bool enabled;
    pConf->Read(providerName, &enabled, true);
    providers[providerName] = enabled;
    hasMore = pConf->GetNextEntry(providerName, providerIndex);
  }

  m_pSignalKNotesManager->SetProviderSettings(providers);

  std::map<wxString, wxString> iconMappings;

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/IconMappings");

  wxString iconName;
  long iconIndex;

  hasMore = pConf->GetFirstEntry(iconName, iconIndex);

  while (hasMore) {
    wxString ocpnIcon;
    pConf->Read(iconName, &ocpnIcon, wxEmptyString);
    iconMappings[iconName] = ocpnIcon;
    hasMore = pConf->GetNextEntry(iconName, iconIndex);
  }

  m_pSignalKNotesManager->SetIconMappings(iconMappings);

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  wxString authToken;
  wxString requestHref;

  pConf->Read("AuthToken", &authToken, wxEmptyString);
  pConf->Read("AuthRequestHref", &requestHref, wxEmptyString);

  m_pSignalKNotesManager->SetAuthToken(authToken);

  if (!requestHref.IsEmpty())
    m_pSignalKNotesManager->SetAuthRequestHref(requestHref);
  else
    m_pSignalKNotesManager->ClearAuthRequest();

  pConf->Read("ClientUUID", &m_clientUUID, "");

  if (m_clientUUID.IsEmpty()) {
#ifdef __WXMSW__
    UUID uuid;
    UuidCreate(&uuid);

    RPC_CSTR str = nullptr;
    UuidToStringA(&uuid, &str);

    if (str) {
      m_clientUUID = wxString(reinterpret_cast<char*>(str));
      RpcStringFreeA(&str);
    }

#else
    uuid_t binuuid;
    uuid_generate_random(binuuid);

    char uuid_str[37];
    uuid_unparse_lower(binuuid, uuid_str);

    m_clientUUID = wxString(uuid_str);
#endif

    pConf->Write("ClientUUID", m_clientUUID);
    SKN_LOG(this, "Generated new client UUID: %s", m_clientUUID);
  }

  m_iconSize = m_pTPConfig->Read("DisplaySettings/IconSize",
                                 (long)tpConfigDialog::DEFAULT_ICON_SIZE);

  m_clusterSize = m_pTPConfig->Read("DisplaySettings/ClusterSize",
                                    (long)tpConfigDialog::DEFAULT_CLUSTER_SIZE);

  m_clusterRadius =
      m_pTPConfig->Read("DisplaySettings/ClusterRadius",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_RADIUS);

  int r = m_pTPConfig->Read("DisplaySettings/ClusterColorR",
                            (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Red());

  int g =
      m_pTPConfig->Read("DisplaySettings/ClusterColorG",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Green());

  int b = m_pTPConfig->Read("DisplaySettings/ClusterColorB",
                            (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Blue());

  m_clusterColor = wxColour(r, g, b);

  r = m_pTPConfig->Read("DisplaySettings/ClusterTextColorR",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Red());

  g = m_pTPConfig->Read(
      "DisplaySettings/ClusterTextColorG",
      (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Green());

  b = m_pTPConfig->Read(
      "DisplaySettings/ClusterTextColorB",
      (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Blue());

  m_clusterTextColor = wxColour(r, g, b);

  m_clusterFontSize =
      m_pTPConfig->Read("DisplaySettings/ClusterFontSize",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_FONT_SIZE);

  m_debugMode = m_pTPConfig->Read("DisplaySettings/DebugMode", (long)0);

  m_clusterMaxScale =
      m_pTPConfig->Read("DisplaySettings/ClusterMaxScale",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_MAX_SCALE);

  m_clusterMinScale =
      m_pTPConfig->Read("DisplaySettings/ClusterMinScale",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_MIN_SCALE);

  m_fetchInterval =
      m_pTPConfig->Read("DisplaySettings/FetchInterval",
                        (long)tpConfigDialog::DEFAULT_FETCH_INTERVAL);
}

wxString signalk_notes_opencpn_pi::GetPluginIconDir() const {
  wxString dir = GetPluginDataDir("signalk_notes_opencpn_pi");
  if (!dir.EndsWith("/")) dir += "/";
  dir += "data/icons/";
  return dir;
}

wxBitmap* signalk_notes_opencpn_pi::GetPlugInBitmap() {
  return &m_ptpicons->m_bm_signalk_notes_opencpn_pi;
}

double signalk_notes_opencpn_pi::CalculateMaxDistance(
    const CanvasState& state) {
  const PlugIn_ViewPort& vp = state.viewPort;

  const double diagonalPixels = std::hypot(vp.pix_width, vp.pix_height) / 2.0;
  const double metersPerPixel = vp.view_scale_ppm;

  if (metersPerPixel > 0) {
    return std::ceil(diagonalPixels / metersPerPixel);
  }

  const double latSpan = vp.pix_height * vp.view_scale_ppm / 111000.0;
  const double maxDistanceKm = latSpan * 111.0 / 2.0;

  return std::ceil(maxDistanceKm * 1000.0);
}

// Für einen spezifischen Canvas
int signalk_notes_opencpn_pi::GetVisibleNoteCount(CanvasState& state) const {
  std::vector<const SignalKNote*> notes;
  m_pSignalKNotesManager->GetVisibleNotes(state, notes);
  return notes.size();
}

// Für alle Canvas (Summe)
int signalk_notes_opencpn_pi::GetVisibleNoteCount() const {
  int totalCount = 0;

  for (const auto& pair : m_canvasStates) {
    const CanvasState& state = pair.second;
    if (state.valid) {
      totalCount += GetVisibleNoteCount(const_cast<CanvasState&>(state));
    }
  }

  return totalCount;
}

std::vector<signalk_notes_opencpn_pi::NoteCluster>
signalk_notes_opencpn_pi::BuildClusters(
    const std::vector<const SignalKNote*>& notes, CanvasState& state,
    int clusterRadius) {
  std::vector<NoteCluster> clusters;
  std::vector<bool> clustered(notes.size(), false);

  PlugIn_ViewPort vpCopy = state.viewPort;

  for (size_t i = 0; i < notes.size(); i++) {
    if (clustered[i]) continue;

    wxPoint p1;
    GetCanvasPixLL(&vpCopy, &p1, notes[i]->latitude, notes[i]->longitude);

    NoteCluster cluster;
    cluster.noteIds.push_back(notes[i]->id);
    clustered[i] = true;

    for (size_t j = i + 1; j < notes.size(); j++) {
      if (clustered[j]) continue;

      wxPoint p2;
      GetCanvasPixLL(&vpCopy, &p2, notes[j]->latitude, notes[j]->longitude);

      int dx = p2.x - p1.x;
      int dy = p2.y - p1.y;

      double dist = std::sqrt(dx * dx + dy * dy);

      if (dist < clusterRadius) {
        cluster.noteIds.push_back(notes[j]->id);
        clustered[j] = true;
      }
    }

    double sumLat = 0, sumLon = 0;

    for (const auto& id : cluster.noteIds) {
      const SignalKNote* note =
          m_pSignalKNotesManager->GetNoteByGUID(state, id);
      if (note) {
        sumLat += note->latitude;
        sumLon += note->longitude;
      }
    }

    cluster.centerLat = sumLat / cluster.noteIds.size();
    cluster.centerLon = sumLon / cluster.noteIds.size();

    GetCanvasPixLL(&vpCopy, &cluster.screenPos, cluster.centerLat,
                   cluster.centerLon);

    clusters.push_back(cluster);
  }

  SKN_LOG(this, "BuildClusters created %zu clusters", clusters.size());
  return clusters;
}

void signalk_notes_opencpn_pi::OnClusterClick(const NoteCluster& cluster,
                                              CanvasState& state,
                                              int canvasIndex) {
  SKN_LOG(this, "OnClusterClick: Starting zoom for %zu notes",
          cluster.noteIds.size());

  if (cluster.noteIds.size() <= 1) return;

  const int MAX_SCALE_LIMIT = m_clusterMaxScale;
  int currentScale = (int)std::round(state.viewPort.chart_scale);

  if (currentScale <= MAX_SCALE_LIMIT) {
    ShowClusterSelectionDialog(cluster, state, canvasIndex);
    return;
  }

  // Cluster-Zoom am STATE aktivieren
  state.clusterZoom.active = true;
  state.clusterZoom.noteIds.clear();
  state.clusterZoom.noteIds = cluster.noteIds;
  state.clusterZoom.targetLat = cluster.centerLat;
  state.clusterZoom.targetLon = cluster.centerLon;

  wxWindow* canvas = GetCanvasByIndex(m_activeCanvasIndex);

  if (canvas && state.valid) {
    double factor = 1.4;
    double newScale = state.viewPort.view_scale_ppm * factor;

    SKN_LOG(this,
            "OnClusterClick: CanvasJumpToPosition canvas=%d lat=%.6f lon=%.6f "
            "scale=%.3f",
            m_activeCanvasIndex, state.clusterZoom.targetLat,
            state.clusterZoom.targetLon, newScale);

    CanvasJumpToPosition(canvas, state.clusterZoom.targetLat,
                         state.clusterZoom.targetLon, newScale);
  }

  return;
}

void signalk_notes_opencpn_pi::SetDisplaySettings(
    int iconSize, int clusterSize, int clusterRadius,
    const wxColour& clusterColor, const wxColour& textColor, int fontSize,
    int clusterMaxScale, int clusterMinScale) {
  m_iconSize = iconSize;
  m_clusterSize = clusterSize;
  m_clusterRadius = clusterRadius;
  m_clusterColor = clusterColor;
  m_clusterTextColor = textColor;
  m_clusterFontSize = fontSize;
  m_clusterMaxScale = clusterMaxScale;
  m_clusterMinScale = clusterMinScale;
  InvalidateAllBmpCaches();
}

wxBitmap signalk_notes_opencpn_pi::CreateClusterBitmap(size_t count) {
  int size = m_clusterSize;
  int radius = m_clusterRadius;
  wxColour circleColor = m_clusterColor;
  wxColour textColor = m_clusterTextColor;
  int fontSize = m_clusterFontSize;

  const int centerX = size / 2;
  const int centerY = size / 2;

#ifdef __OCPN__ANDROID__
  // Android: ohne wxGraphicsContext, nur wxDC
  wxBitmap bmp(size, size, 32);
  wxMemoryDC dc;
  dc.SelectObject(bmp);

  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  dc.SetBrush(wxBrush(circleColor));
  dc.SetPen(*wxBLACK_PEN);
  dc.DrawCircle(centerX, centerY, radius);

  wxFont font(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
              wxFONTWEIGHT_BOLD, false, "Arial");
  dc.SetFont(font);
  dc.SetTextForeground(textColor);

  wxString text = wxString::Format("%zu", count);
  wxSize ts = dc.GetTextExtent(text);
  dc.DrawText(text, centerX - ts.GetWidth() / 2, centerY - ts.GetHeight() / 2);

  dc.SelectObject(wxNullBitmap);
  return bmp;

#else
  wxBitmap bmp(size, size, 32);
  bmp.UseAlpha(true);

  wxMemoryDC dc;
  dc.SelectObject(bmp);

  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
  if (!gc) {
    dc.SelectObject(wxNullBitmap);
    return bmp;
  }

#if defined(wxANTIALIAS_DEFAULT)
  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
#endif

  wxColour fill(circleColor.Red(), circleColor.Green(), circleColor.Blue(),
                200);
  wxColour border(0, 0, 0, 220);

  gc->SetBrush(wxBrush(fill));
  gc->SetPen(wxPen(border, 1));
  gc->DrawEllipse(centerX - radius, centerY - radius, radius * 2, radius * 2);

  wxFont font(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
              wxFONTWEIGHT_BOLD, false, "Arial");
  gc->SetFont(font, textColor);

  wxString text = wxString::Format("%zu", count);
  double tw, th;
  gc->GetTextExtent(text, &tw, &th);
  gc->DrawText(text, centerX - tw / 2, centerY - th / 2);

  delete gc;
  dc.SelectObject(wxNullBitmap);

  wxImage img = bmp.ConvertToImage();
  return img;
#endif
}

wxBitmap signalk_notes_opencpn_pi::PrepareIconBitmapForGL(const wxBitmap& src,
                                                          int targetSize) {
#ifdef __OCPN__ANDROID__
  // Android: kein GL/Y-Flip, nur skalieren
  if (targetSize <= 0 ||
      (src.GetWidth() == targetSize && src.GetHeight() == targetSize)) {
    return src;
  }

  wxImage img = src.ConvertToImage();
  img.InitAlpha();
  wxImage scaled = img.Scale(targetSize, targetSize, wxIMAGE_QUALITY_HIGH);
  return wxBitmap(scaled);

#else
  // Desktop GL: Spiegeln für OpenGL Koordinatensystem
  wxBitmap bmp(targetSize, targetSize, 32);
  bmp.UseAlpha(true);

  wxMemoryDC dc;
  dc.SelectObject(bmp);

  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
  if (!gc) {
    dc.SelectObject(wxNullBitmap);
    return src;
  }

#if defined(wxANTIALIAS_DEFAULT)
  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
#endif

  wxImage img = src.ConvertToImage();
  img.InitAlpha();
  wxImage scaled = img.Scale(targetSize, targetSize, wxIMAGE_QUALITY_HIGH);

  wxBitmap scaledBmp(scaled);
  gc->DrawBitmap(scaledBmp, 0, 0, targetSize, targetSize);

  delete gc;
  dc.SelectObject(wxNullBitmap);

  // Y-Achsen-Spiegelung für OpenGL
  wxImage flipped = bmp.ConvertToImage().Mirror(false);
  wxBitmap finalBmp(flipped);
  finalBmp.UseAlpha(true);

  return finalBmp;
#endif
}

#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)

// Desktop OpenGL
void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap& bmp, int x, int y) {
  wxImage img = bmp.ConvertToImage();
  img.InitAlpha();

  int w = img.GetWidth();
  int h = img.GetHeight();

  unsigned char* rgb = img.GetData();
  unsigned char* alpha = img.GetAlpha();

  if (!rgb) return;

  std::vector<unsigned char> buffer(w * h * 4);
  for (int i = 0; i < w * h; i++) {
    buffer[i * 4 + 0] = rgb[i * 3 + 0];
    buffer[i * 4 + 1] = rgb[i * 3 + 1];
    buffer[i * 4 + 2] = rgb[i * 3 + 2];
    buffer[i * 4 + 3] = alpha ? alpha[i] : 255;
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glRasterPos2i(x, y + h);
  glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
}

#elif defined(__OCPN__ANDROID__)

// Android: kein GL-Rendering
void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap&, int, int) {
  // noop
}

#else

void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap&, int, int) {
  // noop
}

#endif

void signalk_notes_opencpn_pi::ShowPreferencesDialog(wxWindow* parent) {
  tpConfigDialog dlg(this, parent);
  dlg.ShowModal();
}

wxWindow* signalk_notes_opencpn_pi::GetParentWindow() {
  return GetOCPNCanvasWindow() ? GetOCPNCanvasWindow() : m_parent_window;
}

void signalk_notes_opencpn_pi::ShowClusterSelectionDialog(NoteCluster cluster,
                                                          CanvasState& state,
                                                          int canvasIndex) {
  m_dialogOpen = true;
  wxDialog* dlg =
      new wxDialog(GetParentWindow(), wxID_ANY, _("Notes at this position"),
                   wxDefaultPosition, wxSize(600, 400),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  dlg->CenterOnScreen();

  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxListCtrl* listCtrl =
      new wxListCtrl(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
  listCtrl->AppendColumn("", wxLIST_FORMAT_LEFT, 560);

  wxImageList* imgList = new wxImageList(24, 24, true);
  listCtrl->AssignImageList(imgList, wxIMAGE_LIST_SMALL);

  for (size_t i = 0; i < cluster.noteIds.size(); i++) {
    const SignalKNote* note =
        m_pSignalKNotesManager->GetNoteByGUID(state, cluster.noteIds[i]);
    if (!note) continue;
    wxString label = note->name.IsEmpty() ? note->id : note->name;

    int imgIdx = -1;
    wxBitmap bmp;

    // Hole Bitmap OHNE GL-Präparation (forGL=false)
    if (m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp, false) &&
        bmp.IsOk()) {
      // Skaliere falls nötig (24x24 für Dialog)
      if (bmp.GetWidth() != 24 || bmp.GetHeight() != 24) {
        wxImage img = bmp.ConvertToImage();
        img = img.Scale(24, 24, wxIMAGE_QUALITY_HIGH);
        bmp = wxBitmap(img);
      }
      imgIdx = imgList->Add(bmp);
    }

    listCtrl->InsertItem(i, label, imgIdx);
  }

  sizer->Add(listCtrl, 1, wxALL | wxEXPAND, 10);

  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
  wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
  btnSizer->Add(okBtn, 0, wxALL, 5);
  sizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 5);
  dlg->SetSizer(sizer);

  wxString selectedNoteId;

  auto handler = [&](wxListEvent& evt) {
    long sel = evt.GetIndex();
    if (sel < 0 || sel >= (long)cluster.noteIds.size()) return;
    selectedNoteId = cluster.noteIds[sel];
    dlg->EndModal(wxID_OK);
  };
  listCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, handler);

  dlg->ShowModal();
  dlg->Destroy();
  dlg = nullptr;
  m_dialogOpen = false;
  wxWindow* canvas = GetCanvasByIndex(canvasIndex);
  if (canvas) {
    wxMouseEvent upEvent(wxEVT_LEFT_UP);
    upEvent.SetPosition(wxGetMousePosition());
    canvas->GetEventHandler()->ProcessEvent(upEvent);
  }

  if (!selectedNoteId.IsEmpty()) {
    m_pSignalKNotesManager->OnIconClick(selectedNoteId, state, canvasIndex);
    return;
  }
}

void signalk_notes_opencpn_pi::SetCurrentViewPort(PlugIn_ViewPort& vp) {
  return;
}

int ComputeScale(const PlugIn_ViewPort& vp) {
  if (vp.view_scale_ppm <= 0) {
    return 1;
  }
  double scale = 1.0 / vp.view_scale_ppm;
  return (int)scale;
}

bool signalk_notes_opencpn_pi::ProcessClusterZoom(CanvasState& state,
                                                  int canvasIndex) {
  // Prüfe ob Cluster-Zoom aktiv und Scale sich geändert hat
  if (!state.clusterZoom.active ||
      state.viewPort.chart_scale == state.lastViewPort.chart_scale) {
    return true;  // Kein aktiver Zoom → normal weiter rendern
  }

  const double zoomFactor = 1.4;
  const int MAX_SCALE_LIMIT = m_clusterMaxScale;
  int currentScale = (int)std::round(state.viewPort.chart_scale);

  SKN_LOG(this, "ClusterZoom canvas=%d: current scale 1:%d", canvasIndex,
          currentScale);

  // FIND THE NOTES USING THE IDS
  std::vector<const SignalKNote*> originalNotes;
  for (const auto& id : state.clusterZoom.noteIds) {
    SignalKNote* note = m_pSignalKNotesManager->GetNoteByGUID(state, id);
    if (note) {
      originalNotes.push_back(note);
    }
  }

  // LIMIT REACHED?
  if (currentScale <= MAX_SCALE_LIMIT) {
    SKN_LOG(this, "ClusterZoom canvas=%d: Scale limit reached, showing dialog",
            canvasIndex);
    state.clusterZoom.active = false;

    NoteCluster dialogCluster;
    dialogCluster.noteIds = state.clusterZoom.noteIds;
    dialogCluster.centerLat = state.clusterZoom.targetLat;
    dialogCluster.centerLon = state.clusterZoom.targetLon;

    ShowClusterSelectionDialog(dialogCluster, state, canvasIndex);
    return true;  // Zoom beendet → normal weiter rendern
  }

  // CALCULATE NEW CLUSTERS
  std::vector<NoteCluster> newClusters = BuildClusters(originalNotes, state);

  // ARE THE NOTES STILL TOGETHER?
  bool stillTogether = false;
  for (const auto& nc : newClusters) {
    if (nc.noteIds.size() == state.clusterZoom.noteIds.size()) {
      stillTogether = true;
      break;
    }
  }

  // CLUSTER BLOWN OPEN!
  if (!stillTogether) {
    SKN_LOG(this, "ClusterZoom canvas=%d: Cluster successfully separated",
            canvasIndex);
    state.clusterZoom.active = false;
    return true;  // Zoom beendet → normal weiter rendern
  }

  // PERFORM THE NEXT ZOOM - nur auf diesem Canvas
  // wxWindow* canvas = *PluginGetFocusCanvas();
  wxWindow* canvas = GetCanvasByIndex(canvasIndex);
  if (!canvas) return true;

  double newScale = state.viewPort.view_scale_ppm * zoomFactor;

  SKN_LOG(this, "ClusterZoom canvas=%d: Next zoom to scale=%.3f", canvasIndex,
          newScale);

  CanvasJumpToPosition(canvas, state.clusterZoom.targetLat,
                       state.clusterZoom.targetLon, newScale);

  return false;  // Zoom läuft noch → NICHT rendern
}

bool signalk_notes_opencpn_pi::GetCachedIconBitmap(const wxString& iconName,
                                                   wxBitmap& bmp, bool forGL) {
  auto it = m_iconBitmapCache.find(iconName);
  if (it != m_iconBitmapCache.end() && it->second.IsOk()) {
    if (forGL) {
      bmp = PrepareIconBitmapForGL(it->second, it->second.GetWidth());
    } else {
      bmp = it->second;
    }
    return true;
  }
  return false;
}

void signalk_notes_opencpn_pi::CacheIconBitmap(const wxString& iconName,
                                               const wxBitmap& rawBitmap,
                                               bool forGL, wxBitmap& outBmp) {
  m_iconBitmapCache[iconName] = rawBitmap;  // Speichere Roh-Bitmap

  if (forGL) {
    outBmp = PrepareIconBitmapForGL(rawBitmap, rawBitmap.GetWidth());
  } else {
    outBmp = rawBitmap;
  }
}

void signalk_notes_opencpn_pi::InvalidateBmpIconCache() {
  m_iconBitmapCache.clear();
}

void signalk_notes_opencpn_pi::InvalidateBmpClusterCache() {
  m_clusterBitmapCache.clear();
}

void signalk_notes_opencpn_pi::InvalidateAllBmpCaches() {
  InvalidateBmpIconCache();
  InvalidateBmpClusterCache();
}

void signalk_notes_opencpn_pi::PruneCanvasStates(int canvasIndex) {
  if (m_canvasStates.size() > 1 &&
      m_canvasStates.size() != static_cast<size_t>(GetCanvasCount())) {
    for (auto it = m_canvasStates.begin(); it != m_canvasStates.end();) {
      if (it->first != canvasIndex)
        it = m_canvasStates.erase(it);
      else
        ++it;
    }
  }
}

bool signalk_notes_opencpn_pi::ViewPortsDiffer(const PlugIn_ViewPort& a,
                                               const PlugIn_ViewPort& b) {
  return a.clat != b.clat || a.clon != b.clon ||
         a.view_scale_ppm != b.view_scale_ppm || a.skew != b.skew ||
         a.rotation != b.rotation || a.chart_scale != b.chart_scale ||
         a.pix_width != b.pix_width || a.pix_height != b.pix_height ||
         a.rv_rect != b.rv_rect || a.b_quilt != b.b_quilt ||
         a.m_projection_type != b.m_projection_type || a.lat_min != b.lat_min ||
         a.lat_max != b.lat_max || a.lon_min != b.lon_min ||
         a.lon_max != b.lon_max || a.bValid != b.bValid;
}
