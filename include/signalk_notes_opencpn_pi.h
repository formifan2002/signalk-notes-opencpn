/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Plugin class declarations and API interface definitions
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2026 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/
#ifndef _SIGNALK_NOTES_OPENCPN_PI_H_
#define _SIGNALK_NOTES_OPENCPN_PI_H_

// GCC auf Linux/arm64 benötigt expliziten cstdint-Include
// vor ocpn_plugin.h (uint64_t/uint8_t sonst nicht verfügbar) - fehlt in API19
#include "ocpn_plugin.h"
#include <wx/string.h>
#include <vector>
#include <map>
#include <set> 

class tpicons;
class tpSignalKNotesManager;
class tpConfigDialog;
class SignalKNote;

class signalk_notes_opencpn_pi : public opencpn_plugin_119 {
public:
  signalk_notes_opencpn_pi(void* ppimgr);
  ~signalk_notes_opencpn_pi() override;

  // ---------------------------------------------------------
  // CLUSTER-STRUKTUR
  // ---------------------------------------------------------
  struct NoteCluster {
    std::vector<wxString> noteIds;
    double centerLat = 0.0;
    double centerLon = 0.0;
    wxPoint screenPos;
  };

  struct ClusterZoomState {
    bool active = false;
    std::vector<wxString> noteIds;
    double targetLat = 0.0;
    double targetLon = 0.0;
  };

  // ---------------------------------------------------------
  // RESOURCESET-STRUKTUREN
  // ---------------------------------------------------------
  struct ResourceSetEntry {
    wxString name;         // aus properties.name
    wxString description;  // aus properties.description (bereits als Detail
                           // vorhanden)
    double latitude;
    double longitude;
    wxString subResourceSetName;  // Name des Unter-Resourcesets
    wxString resourceSetName;     // Name des Haupt-Resourcesets (z.B. "Funk")
    wxString GUID;  // generiert: "RS_<resourceSetName>_<subName>_<index>"
  };

  struct SubResourceSetConfig {
    wxString name;  // Name des Unter-Resourcesets
    bool enabled = false;
    wxString iconName;  // gewähltes Icon (Dateiname ohne .svg)
  };

  struct ResourceSetConfig {
    wxString name;  // Name des Haupt-Resourcesets (z.B. "Funk")
    bool enabled = false;
    std::map<wxString, SubResourceSetConfig> subSets;  // subName -> config
  };

  // Bitmap-Erzeugung / GL-Vorbereitung
  wxBitmap PrepareIconBitmapForGL(const wxBitmap& src, int targetSize);
  wxBitmap CreateClusterBitmap(size_t count);
  void DrawGLBitmap(const wxBitmap& bmp, int x, int y);

  // Plugin-Interface
  int Init(void) override;
  bool DeInit(void) override;

  int GetAPIVersionMajor() override;
  int GetAPIVersionMinor() override;

  int GetPlugInVersionMajor() override;
  int GetPlugInVersionMinor() override;
  int GetPlugInVersionPatch() override;
  int GetPlugInVersionPost() override;

  wxString GetCommonName() override;
  wxString GetShortDescription() override;
  wxString GetLongDescription() override;

  int GetToolbarToolCount(void) override { return 1; }

  void OnToolbarToolCallback(int id) override;
  void OnToolbarToolDownCallback(int id) override;
  void OnToolbarToolUpCallback(int id) override;

  bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) override;
  bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp) override;

  bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp, int canvasIndex,
                                int priority) override;
  bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                                  int canvasIndex, int priority) override;

  bool MouseEventHook(wxMouseEvent& event) override;
  bool KeyboardEventHook(wxKeyEvent& event) override;

  void LateInit(void) override;

  wxBitmap* GetPlugInBitmap() override;

  struct CanvasState {
    std::vector<NoteCluster> clusters;
    PlugIn_ViewPort viewPort;
    PlugIn_ViewPort lastViewPort;
    bool valid = false;
    double lastFetchCenterLat = 0.0;
    double lastFetchCenterLon = 0.0;
    double lastFetchDistance = 0.0;
    wxLongLong lastFetchTime = 0;
    wxLongLong lastRSFetchTime = 0;
    std::map<wxString, SignalKNote> notes;
    mutable wxMutex notesMutex;  // Schützt notes
    ClusterZoomState clusterZoom;
    std::map<wxString, SignalKNote> resourceSetNotes;
  };
  std::map<int, CanvasState> m_canvasStates;

  // Config
  void SaveConfig();
  bool LoadConfig();

  // Utility
  double CalculateMaxDistance(const CanvasState& state);
  void UpdateOverviewDialog();
  wxString GetPluginIconDir() const;
  int GetVisibleNoteCount(CanvasState& state) const;
  int GetVisibleNoteCount() const;

  // Public state
  tpSignalKNotesManager* m_pSignalKNotesManager = nullptr;
  wxWindow* m_parent_window = nullptr;
  wxString m_clientUUID;

  void SetDisplaySettings(int iconSize, int clusterSize, int clusterRadius,
                          const wxColour& clusterColor,
                          const wxColour& textColor, int fontSize,
                          int clusterMaxScale, int clusterMinScale);

  bool GetCachedIconBitmap(const wxString& iconName, wxBitmap& bmp, bool forGL);
  void CacheIconBitmap(const wxString& iconName, const wxBitmap& rawBitmap,
                       bool forGL, wxBitmap& outBmp);
  void InvalidateBmpIconCache();
  void InvalidateBmpClusterCache();
  void InvalidateAllBmpCaches();

  int GetIconSize() const { return m_iconSize; }
  int GetClusterSize() const { return m_clusterSize; }
  int GetClusterRadius() const { return m_clusterRadius; }
  wxColour GetClusterColor() const { return m_clusterColor; }
  wxColour GetClusterTextColor() const { return m_clusterTextColor; }
  int GetClusterFontSize() const { return m_clusterFontSize; }
  int GetClusterMaxScale() const { return m_clusterMaxScale; }
  int GetClusterMinScale() const { return m_clusterMinScale; }
  int GetFetchInterval() const { return m_fetchInterval; }
  bool IsDebugMode() const { return m_debugMode; }
  void SetDebugMode(bool v) { m_debugMode = v; }
  void SetFetchInterval(int v) { m_fetchInterval = v; }
  void ShowPreferencesDialog(wxWindow* parent);
  wxWindow* GetParentWindow();
  virtual void SetCurrentViewPort(PlugIn_ViewPort& vp) override;
  int m_activeCanvasIndex = 0;
  bool m_dialogOpen = false;
  // Resourceset-Konfiguration
  std::map<wxString, ResourceSetConfig>
      m_resourceSetConfigs;                    // resourceSetName -> config
  std::map<wxString, ResourceSetConfig> m_resourceSetConfigsBackup;

  void SaveResourceSetConfig(wxFileConfig* pConf);
  void LoadResourceSetConfig(wxFileConfig* pConf);
  wxString m_pluginDataDir;


private:
  bool DoRenderCommon(PlugIn_ViewPort* vp, int canvasIndex, int priority);
  bool DoRenderOverlay(wxDC& dc, PlugIn_ViewPort* vp, int canvasIndex,
                       int priority);
  bool DoRenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                         int canvasIndex, int priority);
  void PruneCanvasStates(int canvasIndex);
  bool ViewPortsDiffer(const PlugIn_ViewPort& a, const PlugIn_ViewPort& b);
  // Config + UI
  wxFileConfig* m_pTPConfig = nullptr;
  int m_signalk_notes_opencpn_button_id = -1;

  tpicons* m_ptpicons = nullptr;
  tpConfigDialog* m_pOverviewDialog = nullptr;
  tpConfigDialog* m_pConfigDialog = nullptr;
  friend class tpSignalKNotesManager;

  // Bitmap-Caching
  std::map<wxString, wxBitmap> m_iconBitmapCache;  // iconName -> Bitmap
  std::map<int, wxBitmap> m_clusterBitmapCache;    // count -> Bitmap

  // Clustering
  std::vector<NoteCluster> BuildClusters(
      const std::vector<const SignalKNote*>& notes, CanvasState& state,
      int clusterRadius = 60);

  void OnClusterClick(const NoteCluster& cluster, CanvasState& state,
                      int canvasIndex);
  bool ProcessClusterZoom(CanvasState& state, int canvasIndex);
  void ShowClusterSelectionDialog(NoteCluster cluster, CanvasState& state,
                                  int canvasIndex);

  // Display-Einstellungen
  int m_iconSize;
  int m_clusterSize;
  int m_clusterRadius;
  wxColour m_clusterColor;
  wxColour m_clusterTextColor;
  int m_clusterFontSize;
  int m_clusterMaxScale;
  int m_clusterMinScale;
  int m_fetchInterval;
  bool m_debugMode = false;
  double m_prevChartScale = -1;
  wxPoint m_mouseDownPos;
  std::set<wxString> m_availableResourceSets;

};

int ComputeScale(const PlugIn_ViewPort& vp);

// LOGGING-MAKRO
#define SKN_LOG(plugin, fmt, ...)                                           \
  do {                                                                      \
    if ((plugin)->IsDebugMode())                                            \
      wxLogMessage(wxString::Format("SignalK Notes: %s",                    \
                                    wxString::Format(fmt, ##__VA_ARGS__))); \
  } while (0)

#endif
