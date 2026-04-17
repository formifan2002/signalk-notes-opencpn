/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Configuration dialog class definitions
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2026 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/
#ifndef _TP_CONFIG_DIALOG_H_
#define _TP_CONFIG_DIALOG_H_

#include <wx/wx.h>
#include <wx/checklst.h>
#include <wx/bmpcbox.h>
#include <wx/scrolwin.h>
#include <map>
#include <set>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/clrpicker.h>

class signalk_notes_opencpn_pi;

class tpConfigDialog : public wxDialog {
public:
  tpConfigDialog(signalk_notes_opencpn_pi* parent, wxWindow* winparent);
  ~tpConfigDialog();
  void UpdateProviders(const std::set<wxString>& providers);
  void UpdateIconMappings(const std::set<wxString>& skIcons);

  std::map<wxString, bool> GetProviderSettings() const;
  std::map<wxString, wxString> GetIconMappings() const;

  void LoadSettings(const std::map<wxString, bool>& providers,
                    const std::map<wxString, wxString>& iconMappings);
  void UpdateVisibleCount(int count);
  void UpdateVisibleCount(int left, int right);
  void LoadDisplaySettings(int iconSize, int clusterSize, int clusterRadius,
                           const wxColour& clusterColor,
                           const wxColour& textColor, int fontSize,
                           int clusterMaxScale, int clusterMinScale);

  // Getter für Einstellungen
  int GetIconSize() const {
    return m_iconSizeCtrl ? m_iconSizeCtrl->GetValue() : DEFAULT_ICON_SIZE;
  }
  int GetClusterSize() const {
    return m_clusterSizeCtrl ? m_clusterSizeCtrl->GetValue()
                             : DEFAULT_CLUSTER_SIZE;
  }
  int GetClusterRadius() const {
    return m_clusterRadiusCtrl ? m_clusterRadiusCtrl->GetValue()
                               : DEFAULT_CLUSTER_RADIUS;
  }
  wxColour GetClusterColor() const {
    return m_clusterColorCtrl ? m_clusterColorCtrl->GetColour()
                              : DEFAULT_CLUSTER_COLOR;
  }
  wxColour GetClusterTextColor() const {
    return m_clusterTextColorCtrl ? m_clusterTextColorCtrl->GetColour()
                                  : DEFAULT_CLUSTER_TEXT_COLOR;
  }
  int GetClusterFontSize() const {
    return m_clusterFontSizeCtrl ? m_clusterFontSizeCtrl->GetValue()
                                 : DEFAULT_CLUSTER_FONT_SIZE;
  }

  int GetClusterMaxScale() const {
    return m_clusterMaxScaleCtrl ? m_clusterMaxScaleCtrl->GetValue()
                                 : DEFAULT_CLUSTER_MAX_SCALE;
  }
  int GetClusterMinScale() const {
    return m_clusterMinScaleCtrl ? m_clusterMinScaleCtrl->GetValue()
                                 : DEFAULT_CLUSTER_MIN_SCALE;
  }
  int GetFetchInterval() const {
    return m_fetchIntervalCtrl ? m_fetchIntervalCtrl->GetValue()
                               : DEFAULT_FETCH_INTERVAL;
  }

  // Default-Werte
  static const int DEFAULT_ICON_SIZE = 24;
  static const int DEFAULT_CLUSTER_SIZE = 24;
  static const int DEFAULT_CLUSTER_RADIUS = 10;
  static const wxColour DEFAULT_CLUSTER_COLOR;
  static const wxColour DEFAULT_CLUSTER_TEXT_COLOR;
  static const int DEFAULT_CLUSTER_FONT_SIZE = 8;
  static const int DEFAULT_CLUSTER_MAX_SCALE = 800;
  static const int DEFAULT_CLUSTER_MIN_SCALE = 0;
  static const int DEFAULT_FETCH_INTERVAL = 1;

  void CreateResourceSetTab();
  void UpdateResourceSetTab(
      const std::map<wxString, signalk_notes_opencpn_pi::ResourceSetConfig>&
          configs);
  std::map<wxString, signalk_notes_opencpn_pi::ResourceSetConfig>
  GetResourceSetConfigs() const;

private:
  bool m_settingsLoaded = false;
  void InitializeAuthUI();
  void ShowAuthenticatedState();
  void ShowPendingState();
  void ShowInitialState();
  void CreateControls();
  void LoadPluginIcons();
  wxBitmap LoadSvgBitmap(const wxString& path, int size);

  // Event-Handler
  void SaveProviderSettings();
  void OnOK(wxCommandEvent& event);
  void OnCancel(wxCommandEvent& event);

  // UI-Elemente
  wxStaticText* m_countLabel;       // Anzeige bei 1 Canvas
  wxStaticText* m_countLabelLeft;   // "Icons im Kartenausschnitt links:"
  wxStaticText* m_countLabelRight;  // "Icons im Kartenausschnitt rechts:"
  wxStaticText* m_countLabelTotal;  // "Icons gesamt:"
  wxStaticText* m_infoLabel;
  wxCheckListBox* m_providerList;

  // Icon-Mapping UI
  wxScrolledWindow* m_iconMappingPanel;
  wxFlexGridSizer* m_iconMappingSizer;

  // Map: skIconName -> BitmapComboBox*
  std::map<wxString, wxBitmapComboBox*> m_iconCombos;

  // Icon-Daten
  wxArrayString m_pluginIcons;                       // Dateinamen ohne .svg
  std::map<wxString, wxBitmap> m_pluginIconBitmaps;  // Cache für Icons
  wxString m_iconDir;  // Absoluter Pfad zu data/icons/

  // Einstellungen
  std::set<wxString> m_enabledProviders;
  std::map<wxString, wxString> m_currentIconMappings;

  signalk_notes_opencpn_pi* m_parent;

  wxButton* m_authButton;
  wxStaticText* m_authStatusLabel;
  wxStaticBitmap* m_authStatusIcon;
  wxTimer* m_authCheckTimer;

  void OnAuthButtonClick(wxCommandEvent& event);
  void OnAuthCheckTimer(wxTimerEvent& event);

  void OnCancelAuthRequest(wxCommandEvent& event);
  wxButton* m_cancelAuthButton;

  struct ProviderDisplayInfo {
    wxString id;
    wxString name;
    wxString description;
  };
  std::map<wxString, ProviderDisplayInfo> m_providerDisplayInfos;

  // Neue Member-Variablen
  wxNotebook* m_notebook;
  wxPanel* m_displayPanel;

  // Display-Einstellungen
  wxSpinCtrl* m_iconSizeCtrl;
  wxSpinCtrl* m_clusterSizeCtrl;
  wxSpinCtrl* m_clusterRadiusCtrl;
  wxColourPickerCtrl* m_clusterColorCtrl;
  wxColourPickerCtrl* m_clusterTextColorCtrl;
  wxSpinCtrl* m_clusterFontSizeCtrl;
  wxCheckBox* m_debugCheckbox;
  wxStaticBitmap* m_iconPreview;
  wxStaticBitmap* m_clusterPreview;
  wxSpinCtrl* m_clusterMaxScaleCtrl;  // "Maximaler Maßstab für Cluster 1:"
  wxSpinCtrl* m_clusterMinScaleCtrl;  // "Minimaler Maßstab für Cluster 1:"
  wxStaticText* m_scaleErrorLabel;    // Fehlermeldung für Maßstab-Validierung
  wxSpinCtrl* m_fetchIntervalCtrl;  // "Intervall API Aktualisierung (Minuten)"

  void CreateDisplayTab();
  void UpdateIconPreview();
  void UpdateClusterPreview();
  void OnDisplaySettingChanged(wxSpinEvent& event);
  void OnColorChanged(wxColourPickerEvent& event);
  void ValidateScaleSettings();
  void OnScaleSettingChanged(wxSpinEvent& event);
  DECLARE_EVENT_TABLE()

  // Resourceset UI
  wxPanel* m_resourceSetPanel = nullptr;
  wxScrolledWindow* m_resourceSetScrollWin = nullptr;

  struct SubRSRow {
    wxString rsName;
    wxString subName;
    wxCheckBox* enabledCheck = nullptr;
    wxBitmapComboBox* iconCombo = nullptr;
  };
  struct MainRSRow {
    wxString rsName;
    wxCheckBox* enabledCheck = nullptr;
    std::vector<SubRSRow> subRows;
  };
  std::vector<MainRSRow> m_rsRows;
  void OnMainRSToggled(wxCommandEvent& event);
};

#endif