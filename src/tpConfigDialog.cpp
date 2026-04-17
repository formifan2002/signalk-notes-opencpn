/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Configuration dialog and user interface settings
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2026 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/
#include "signalk_notes_opencpn_pi.h"
#include "tpConfigDialog.h"
#include "tpSignalKNotes.h"
#include "ocpn_plugin.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/notebook.h>
#include <wx/filename.h>
#include <wx/dir.h>
#ifndef __OCPN__ANDROID__
#include <wx/bmpbndl.h>
#endif
#include <wx/timer.h>
#include <wx/artprov.h>

BEGIN_EVENT_TABLE(tpConfigDialog, wxDialog)
EVT_BUTTON(wxID_OK, tpConfigDialog::OnOK)
EVT_BUTTON(wxID_CANCEL, tpConfigDialog::OnCancel)
END_EVENT_TABLE()

const wxColour tpConfigDialog::DEFAULT_CLUSTER_COLOR(30, 144, 255);
const wxColour tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR(*wxWHITE);

tpConfigDialog::tpConfigDialog(signalk_notes_opencpn_pi* parent,
                               wxWindow* winparent)
    : wxDialog(winparent, wxID_ANY, _("SignalK Notes Configuration"),
               wxDefaultPosition, wxSize(800, 800),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_settingsLoaded(false) {
  m_parent = parent;

  m_iconDir = m_parent->GetPluginIconDir();
  LoadPluginIcons();
  CreateControls();
}

void tpConfigDialog::CreateControls() {
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  // --- Sichtbare Icons ---
  m_countLabel =
      new wxStaticText(this, wxID_ANY, _("Icons in the map area: 0"));

  wxBoxSizer* countSizer = new wxBoxSizer(wxHORIZONTAL);
  m_countLabelLeft = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_countLabelRight = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_countLabelTotal = new wxStaticText(this, wxID_ANY, wxEmptyString);
  countSizer->Add(m_countLabel, 0, wxRIGHT, 15);
  countSizer->Add(m_countLabelLeft, 0, wxRIGHT, 15);
  countSizer->Add(m_countLabelRight, 0, wxRIGHT, 15);
  countSizer->Add(m_countLabelTotal, 0, 0, 0);

  mainSizer->Add(countSizer, 0, wxALL | wxEXPAND, 10);

  // Initialer Aufruf — bestimmt selbst, welche Labels sichtbar sind
  {
    int canvasCount = (int)m_parent->m_canvasStates.size();
    if (canvasCount >= 2) {
      // Zähle für Canvas 0 (links) und Canvas 1 (rechts)
      int left = 0, right = 0;
      auto it = m_parent->m_canvasStates.begin();
      if (it != m_parent->m_canvasStates.end()) {
        left = m_parent->GetVisibleNoteCount(
            const_cast<signalk_notes_opencpn_pi::CanvasState&>(it->second));
        ++it;
      }
      if (it != m_parent->m_canvasStates.end()) {
        right = m_parent->GetVisibleNoteCount(
            const_cast<signalk_notes_opencpn_pi::CanvasState&>(it->second));
      }
      UpdateVisibleCount(left, right);
    } else {
      UpdateVisibleCount(m_parent->GetVisibleNoteCount());
    }
  }

  // --- Hinweis ---
  m_infoLabel = new wxStaticText(
      this, wxID_ANY, _("Note: Providers are discovered as you move the map."));
  m_infoLabel->SetForegroundColour(*wxBLUE);
  mainSizer->Add(m_infoLabel, 0, wxALL | wxEXPAND, 5);

  // --- Notebook ---
  m_notebook = new wxNotebook(this, wxID_ANY);

  // ========== Tab Provider ==========
  wxPanel* providerPanel = new wxPanel(m_notebook);
  wxBoxSizer* providerSizer = new wxBoxSizer(wxVERTICAL);

  providerSizer->Add(new wxStaticText(providerPanel, wxID_ANY,
                                      _("Select the providers to display:")),
                     0, wxALL, 5);

  m_providerList = new wxCheckListBox(providerPanel, wxID_ANY);
  providerSizer->Add(m_providerList, 1, wxALL | wxEXPAND, 5);

  // Auth-Status-Anzeige + Fetch-Intervall in einer Zeile
  wxBoxSizer* authStatusSizer = new wxBoxSizer(wxHORIZONTAL);
  m_authStatusIcon = new wxStaticBitmap(providerPanel, wxID_ANY, wxNullBitmap);
  authStatusSizer->Add(m_authStatusIcon, 0, wxALIGN_TOP | wxALL, 5);

  m_authStatusLabel =
      new wxStaticText(providerPanel, wxID_ANY, _("SignalK Connection"));
  m_authStatusLabel->SetMinSize(wxSize(-1, 45));
  authStatusSizer->Add(m_authStatusLabel, 0, wxALIGN_TOP | wxALL, 5);

  authStatusSizer->AddStretchSpacer();

  authStatusSizer->Add(new wxStaticText(providerPanel, wxID_ANY,
                                        _("API update interval (minutes)")),
                       0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

  m_fetchIntervalCtrl = new wxSpinCtrl(providerPanel, wxID_ANY);
  m_fetchIntervalCtrl->SetRange(1, 1440);  // 1 Minute bis 24 Stunden
  m_fetchIntervalCtrl->SetValue(tpConfigDialog::DEFAULT_FETCH_INTERVAL);
  authStatusSizer->Add(m_fetchIntervalCtrl, 0, wxALIGN_CENTER_VERTICAL | wxALL,
                       5);

  providerSizer->Add(authStatusSizer, 0, wxALL | wxEXPAND, 5);

  providerPanel->SetSizer(providerSizer);
  m_notebook->AddPage(providerPanel, _("Provider"));

  // ========== Tab Icon-Zuordnung ==========
  wxPanel* iconPanel = new wxPanel(m_notebook);
  wxBoxSizer* iconPanelSizer = new wxBoxSizer(wxVERTICAL);

  iconPanelSizer->Add(new wxStaticText(iconPanel, wxID_ANY,
                                       _("Map SignalK icons to plugin icons:")),
                      0, wxALL, 5);

  m_iconMappingPanel =
      new wxScrolledWindow(iconPanel, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxVSCROLL | wxBORDER_SUNKEN);
  m_iconMappingPanel->SetScrollRate(0, 20);

  m_iconMappingSizer = new wxFlexGridSizer(2, 10, 10);
  m_iconMappingSizer->AddGrowableCol(1, 1);

  m_iconMappingPanel->SetSizer(m_iconMappingSizer);
  iconPanelSizer->Add(m_iconMappingPanel, 1, wxALL | wxEXPAND, 5);

  iconPanel->SetSizer(iconPanelSizer);
  m_notebook->AddPage(iconPanel, _("Icon mapping"));

  // Resourceset-Tab (wird initial leer erstellt; Inhalt kommt via
  // UpdateResourceSetTab)
  CreateResourceSetTab();

  // ========== Tab Display Settings ==========
  CreateDisplayTab();
  m_notebook->AddPage(m_displayPanel, _("Deciption"));

  mainSizer->Add(m_notebook, 1, wxALL | wxEXPAND, 5);

  // ========== BUTTON-BEREICH ==========
  wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

  m_scaleErrorLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_scaleErrorLabel->SetForegroundColour(*wxRED);
  m_scaleErrorLabel->Hide();
  buttonSizer->Add(m_scaleErrorLabel, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

  buttonSizer->AddStretchSpacer();

  m_authButton = new wxButton(this, wxID_ANY, _("SignalK authentication"));

  m_authButton->Bind(wxEVT_BUTTON, &tpConfigDialog::OnAuthButtonClick, this);
  buttonSizer->Add(m_authButton, 0, wxALL, 5);

  m_cancelAuthButton = new wxButton(this, wxID_ANY, _("Cancel authentication"));
  m_cancelAuthButton->Bind(wxEVT_BUTTON, &tpConfigDialog::OnCancelAuthRequest,
                           this);
  m_cancelAuthButton->Hide();
  buttonSizer->Add(m_cancelAuthButton, 0, wxALL, 5);

  buttonSizer->Add(new wxButton(this, wxID_OK, _("OK")), 0, wxALL, 5);
  buttonSizer->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0, wxALL, 5);

  mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 5);

  // --- Timer ---
  m_authCheckTimer = new wxTimer(this);
  Bind(wxEVT_TIMER, &tpConfigDialog::OnAuthCheckTimer, this);

  SetSizer(mainSizer);

  // --- Settings laden ---
  auto* mgr = m_parent->m_pSignalKNotesManager;

  // --- Provider-Settings laden ---
  std::map<wxString, bool> savedProviders = mgr->GetProviderSettings();
  for (const auto& pair : savedProviders) {
    int index = m_providerList->Append(pair.first);
    m_providerList->Check(index, pair.second);
    m_providerList->SetClientData(index, new wxString(pair.first));
  }

  // --- Icon-Mappings laden ---
  m_currentIconMappings = mgr->GetIconMappings();

  std::set<wxString> skIconNames;
  for (const auto& mapping : m_currentIconMappings)
    skIconNames.insert(mapping.first);

  UpdateIconMappings(skIconNames);
  UpdateIconMappings(mgr->GetDiscoveredIcons());

  LoadDisplaySettings(
      m_parent->GetIconSize(), m_parent->GetClusterSize(),
      m_parent->GetClusterRadius(), m_parent->GetClusterColor(),
      m_parent->GetClusterTextColor(), m_parent->GetClusterFontSize(),
      m_parent->GetClusterMaxScale(), m_parent->GetClusterMinScale());

  if (m_fetchIntervalCtrl)
    m_fetchIntervalCtrl->SetValue(m_parent->GetFetchInterval());

  //  ---Auth status setzen ---
  InitializeAuthUI();
  if (!m_authCheckTimer->IsRunning()) {
    m_authCheckTimer->Start(2000);
  }
  m_settingsLoaded = true;
  Layout();
}

void tpConfigDialog::LoadPluginIcons() {
  m_pluginIcons.Clear();
  m_pluginIconBitmaps.clear();

  wxDir dir(m_iconDir);
  if (!dir.IsOpened()) {
    SKN_LOG(m_parent, "SignalK Notes Config: Icon directory not found: %s",
            m_iconDir);
    return;
  }

  wxString filename;
  bool cont = dir.GetFirst(&filename, "*.svg", wxDIR_FILES);

  while (cont) {
    wxFileName fn(filename);
    wxString name = fn.GetName();  // ohne .svg

    m_pluginIcons.Add(name);

    wxString fullPath = m_iconDir + filename;
    wxBitmap bmp = LoadSvgBitmap(fullPath, 24);

    if (bmp.IsOk()) {
      m_pluginIconBitmaps[name] = bmp;
    }

    cont = dir.GetNext(&filename);
  }

  SKN_LOG(m_parent, "SignalK Notes Config: Loaded %zu plugin icons",
          m_pluginIcons.GetCount());
}

wxBitmap tpConfigDialog::LoadSvgBitmap(const wxString& path, int size) {
#ifdef __OCPN__ANDROID__
  // Android: kein wxBitmapBundle, kein SVG-Support → PNG-Fallback
  wxString pngPath = path;
  pngPath.Replace(".svg", ".png");

  wxBitmap bmp(pngPath, wxBITMAP_TYPE_PNG);
  if (bmp.IsOk()) return bmp;

  // Fallback: leeres Bitmap
  return wxBitmap(size, size);

#else
  // Desktop: SVG normal laden
  wxBitmapBundle bundle = wxBitmapBundle::FromSVGFile(path, wxSize(size, size));
  if (bundle.IsOk()) return bundle.GetBitmap(wxSize(size, size));

  return wxBitmap(size, size);
#endif
}

void tpConfigDialog::UpdateIconMappings(const std::set<wxString>& skIcons) {
  // Für jedes neue SignalK-Icon eine Zeile hinzufügen
  for (const wxString& skIconName : skIcons) {
    // Prüfen ob schon vorhanden
    if (m_iconCombos.find(skIconName) != m_iconCombos.end()) {
      continue;  // Schon da
    }

    // Label: SignalK Icon-Name
    wxStaticText* label =
        new wxStaticText(m_iconMappingPanel, wxID_ANY, skIconName);
    m_iconMappingSizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    // Dropdown: Alle Plugin-Icons
    wxBitmapComboBox* combo = new wxBitmapComboBox(
        m_iconMappingPanel, wxID_ANY, "", wxDefaultPosition, wxSize(300, -1), 0,
        nullptr, wxCB_READONLY);

    m_pluginIcons.Sort();
    // Alle Plugin-Icons zur Dropdown hinzufügen
    for (const wxString& pluginIconName : m_pluginIcons) {
      auto it = m_pluginIconBitmaps.find(pluginIconName);
      if (it != m_pluginIconBitmaps.end()) {
        combo->Append(pluginIconName, it->second);
      } else {
        combo->Append(pluginIconName);
      }
    }

    // Vorauswahl setzen (falls Mapping existiert)
    auto mapIt = m_currentIconMappings.find(skIconName);
    if (mapIt != m_currentIconMappings.end()) {
      // Mapping existiert - extrahiere Dateinamen aus Pfad
      wxFileName fn(mapIt->second);
      wxString iconName = fn.GetName();  // ohne .svg

      int idx = combo->FindString(iconName);
      if (idx != wxNOT_FOUND) {
        combo->SetSelection(idx);
      }
    } else {
      // Auto-Mapping: skIconName.svg
      int idx = combo->FindString(skIconName);
      if (idx != wxNOT_FOUND) {
        combo->SetSelection(idx);
      } else {
        // Fallback: notice-to-mariners
        idx = combo->FindString(wxT("notice-to-mariners"));
        if (idx != wxNOT_FOUND) {
          combo->SetSelection(idx);
        }
      }
    }

    wxString selectedIconName = combo->wxChoice::GetStringSelection();

    if (!selectedIconName.IsEmpty()) {
      wxFileName iconPath(m_iconDir);
      iconPath.SetFullName(selectedIconName + ".svg");
      m_currentIconMappings[skIconName] = iconPath.GetFullPath();
    }

    m_iconMappingSizer->Add(combo, 1, wxEXPAND | wxALL, 5);

    // In Map speichern
    m_iconCombos[skIconName] = combo;
  }

  // Layout aktualisieren
  m_iconMappingPanel->FitInside();
  m_iconMappingPanel->Layout();
  Layout();
}

void tpConfigDialog::LoadSettings(
    const std::map<wxString, bool>& providers,
    const std::map<wxString, wxString>& iconMappings) {
  m_providerList->Clear();

  for (const auto& pair : providers) {
    int index = m_providerList->Append(pair.first);
    m_providerList->Check(index, pair.second);
    m_enabledProviders.insert(pair.first);
  }

  if (providers.empty()) {
    m_infoLabel->SetLabel(
        _("Note: No providers configured yet. Move the map to discover "
          "providers."));

    m_infoLabel->Show();
  } else {
    m_infoLabel->Hide();
  }

  // Icon-Mappings laden (VOLLSTÄNDIGE Pfade)
  m_currentIconMappings = iconMappings;

  Layout();
}

std::map<wxString, wxString> tpConfigDialog::GetIconMappings() const {
  std::map<wxString, wxString> mappings;

  // Alle Combos durchgehen
  for (const auto& pair : m_iconCombos) {
    const wxString& skIconName = pair.first;
    wxBitmapComboBox* combo = pair.second;

    wxString selectedIconName = combo->wxChoice::GetStringSelection();

    if (!selectedIconName.IsEmpty()) {
      // Vollständigen Pfad bauen
      wxFileName iconPath(m_iconDir);
      iconPath.SetFullName(selectedIconName + wxT(".svg"));

      mappings[skIconName] = iconPath.GetFullPath();
    }
  }

  return mappings;
}

std::map<wxString, bool> tpConfigDialog::GetProviderSettings() const {
  std::map<wxString, bool> settings;

  for (unsigned int i = 0; i < m_providerList->GetCount(); i++) {
    wxString* idPtr = (wxString*)m_providerList->GetClientData(i);
    if (idPtr) {
      settings[*idPtr] = m_providerList->IsChecked(i);
    } else {
      // Fallback: String direkt verwenden
      settings[m_providerList->GetString(i)] = m_providerList->IsChecked(i);
    }
  }

  return settings;
}

void tpConfigDialog::SaveProviderSettings() {
  m_enabledProviders.clear();

  for (unsigned int i = 0; i < m_providerList->GetCount(); i++) {
    if (m_providerList->IsChecked(i))
      m_enabledProviders.insert(m_providerList->GetString(i));
  }
}

void tpConfigDialog::OnOK(wxCommandEvent& event) {
  // Provider-Settings aus UI übernehmen
  SaveProviderSettings();

  // Provider-Settings an Manager übergeben
  if (m_parent && m_parent->m_pSignalKNotesManager) {
    m_parent->m_pSignalKNotesManager->SetProviderSettings(
        GetProviderSettings());
  }

  // Icon-Mappings an Manager übergeben
  if (m_parent && m_parent->m_pSignalKNotesManager) {
    auto newMappings = GetIconMappings();
    auto oldMappings = m_parent->m_pSignalKNotesManager->GetIconMappings();

    // Prüfe welche Mappings sich geändert haben
    for (const auto& newMapping : newMappings) {
      auto oldIt = oldMappings.find(newMapping.first);

      // Wenn Mapping sich geändert hat → Cache für dieses Icon löschen
      if (oldIt == oldMappings.end() || oldIt->second != newMapping.second) {
        m_parent->m_pSignalKNotesManager->InvalidateIconCache(newMapping.first);
      }
    }

    m_parent->m_pSignalKNotesManager->SetIconMappings(newMappings);
  }

  // Validierung Maßstäbe
  int maxScale = GetClusterMaxScale();
  int minScale = GetClusterMinScale();
  if (maxScale < 800) {
    m_notebook->SetSelection(2);  // Tab "Darstellung" aktivieren
    ValidateScaleSettings();
    return;
  }
  if (minScale != 0 && maxScale >= minScale) {
    m_notebook->SetSelection(2);
    ValidateScaleSettings();
    return;
  }

  // Display-Einstellungen an Plugin übergeben
  if (m_parent) {
    m_parent->SetDisplaySettings(
        GetIconSize(), GetClusterSize(), GetClusterRadius(), GetClusterColor(),
        GetClusterTextColor(), GetClusterFontSize(), maxScale, minScale);
    m_parent->SetFetchInterval(GetFetchInterval());
  }

  // Debug-Einstellungen an Plugin übergeben
  if (m_debugCheckbox && m_parent) {
    m_parent->SetDebugMode(m_debugCheckbox->GetValue());
  }
  // Icons neu berechnen und Karte aktualisieren
  if (m_parent) {
    RequestRefresh(m_parent->m_parent_window);
  }

  // Timer stoppen
  if (m_authCheckTimer && m_authCheckTimer->IsRunning()) {
    m_authCheckTimer->Stop();
  }

  // Plugin-Konfiguration
  if (m_parent && m_resourceSetScrollWin) {
    m_parent->m_resourceSetConfigs = GetResourceSetConfigs();
  }
  m_parent->SaveConfig();
  EndModal(wxID_OK);
}

void tpConfigDialog::OnCancel(wxCommandEvent& event) {
  if (m_authCheckTimer->IsRunning()) m_authCheckTimer->Stop();
  EndModal(wxID_CANCEL);
}

void tpConfigDialog::UpdateVisibleCount(int count) {
  m_countLabel->SetLabel(
      wxString::Format(_("Icons in the map area: %d"), count));
  m_countLabel->Show(true);
  m_countLabelLeft->Hide();
  m_countLabelRight->Hide();
  m_countLabelTotal->Hide();
  Layout();
}

void tpConfigDialog::UpdateVisibleCount(int left, int right) {
  m_countLabel->Hide();
  m_countLabelLeft->SetLabel(
      wxString::Format(_("Icons in the left map area: %d"), left));
  m_countLabelRight->SetLabel(
      wxString::Format(_("Icons in the right map area: %d"), right));
  m_countLabelTotal->SetLabel(
      wxString::Format(_("Total icons: %d"), left + right));
  m_countLabelLeft->Show(true);
  m_countLabelRight->Show(true);
  m_countLabelTotal->Show(true);
  Layout();
}

void tpConfigDialog::OnAuthButtonClick(wxCommandEvent& event) {
  if (m_parent->m_pSignalKNotesManager->RequestAuthorization()) {
    SKN_LOG(m_parent, "Auth request started");
    ShowPendingState();
    m_parent->SaveConfig();
    m_authStatusLabel->SetLabel(
        _("In SignalK, please confirm the request in the menu item 'Access "
          "Requests' "
          "with 'Permission' Admin and 'Authentication Timeout' NEVER."));

    m_authStatusLabel->Wrap(500);
    m_authStatusLabel->SetForegroundColour(*wxRED);
    Layout();
  } else {
    wxMessageBox(_("Error requesting authentication. Are there still 'access "
                   "requests' in "
                   "SignalK? If yes, delete/reject them."),
                 _("Error"), wxOK | wxICON_ERROR);
  }
}

void tpConfigDialog::OnAuthCheckTimer(wxTimerEvent& event) {
  auto* mgr = m_parent->m_pSignalKNotesManager;
  SKN_LOG(m_parent, "OnAuthCheckTimer fired, IsAuthPending=%d",
          (int)mgr->IsAuthPending());

  if (mgr->IsAuthPending()) {
    if (mgr->CheckAuthorizationStatus()) {
      m_parent->SaveConfig();
      ShowAuthenticatedState();

      std::map<wxString, bool> plugins;
      mgr->FetchInstalledPlugins(plugins);

      auto providerInfos = mgr->GetProviderInfos();
      if (providerInfos.empty()) {
        SKN_LOG(m_parent, "ProviderInfos empty → will retry on next tick");
        return;
      }

      SKN_LOG(m_parent, "ProviderInfos available → updating provider list");
      UpdateProviders(mgr->GetDiscoveredProviders());
    }
    return;
  }

  wxString token = mgr->GetAuthToken();

  if (token.IsEmpty()) {
    ShowInitialState();
    return;
  }

  wxTimeSpan tokenAge = wxDateTime::Now() - mgr->GetAuthTokenReceivedTime();

  if (tokenAge.GetSeconds() < 10) {
    SKN_LOG(m_parent, "Token just received (%lld sec ago), skipping validation",
            tokenAge.GetSeconds());
    ShowAuthenticatedState();
    return;
  }

  if (!mgr->ValidateToken()) {
    SKN_LOG(m_parent,
            "OnAuthCheckTimer: token became invalid → resetting auth");
    mgr->SetAuthToken("");
    mgr->ClearAuthRequest();
    m_parent->SaveConfig();
    ShowInitialState();
    return;
  }

  ShowAuthenticatedState();
}

tpConfigDialog::~tpConfigDialog() {
  // ClientData aufräumen
  for (unsigned int i = 0; i < m_providerList->GetCount(); i++) {
    wxString* idPtr = (wxString*)m_providerList->GetClientData(i);
    delete idPtr;
  }
}

void tpConfigDialog::OnCancelAuthRequest(wxCommandEvent& event) {
  SKN_LOG(m_parent, "User cancelled auth request");
  m_parent->m_pSignalKNotesManager->ClearAuthRequest();
  m_parent->SaveConfig();
  ShowInitialState();
}

void tpConfigDialog::InitializeAuthUI() {
  auto* mgr = m_parent->m_pSignalKNotesManager;

  wxString token = mgr->GetAuthToken();
  wxString requestHref = mgr->GetAuthRequestHref();
  bool pending = mgr->IsAuthPending();

  // 1. Token vorhanden → NUR validieren wenn KEINE Anfrage läuft
  if (!token.IsEmpty() && !pending) {
    SKN_LOG(m_parent, "Token present, validating...");

    // Kleine Verzögerung falls Token gerade erst empfangen wurde
    wxMilliSleep(500);

    if (mgr->ValidateToken()) {
      SKN_LOG(m_parent, "Token is valid → showing authenticated state");
      ShowAuthenticatedState();
      return;
    } else {
      // Token ungültig → löschen
      SKN_LOG(m_parent, "Token INVALID → clearing and showing initial state");
      mgr->SetAuthToken("");
      mgr->ClearAuthRequest();
      m_parent->SaveConfig();
      token.Clear();
      requestHref.Clear();
      pending = false;
    }
  }

  // 2. Authentifizierung läuft
  if (pending && !requestHref.IsEmpty()) {
    SKN_LOG(m_parent, "Auth pending → showing pending state");
    ShowPendingState();
    return;
  }

  // 3. Keine gültige Authentifizierung
  SKN_LOG(m_parent, "No valid auth → showing initial state");
  ShowInitialState();
}

void tpConfigDialog::ShowAuthenticatedState() {
  SKN_LOG(m_parent, "ShowAuthenticatedState() ENTER");

  m_authStatusIcon->SetBitmap(
      wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_OTHER, wxSize(16, 16)));
  m_authStatusLabel->SetForegroundColour(*wxGREEN);
  m_authStatusLabel->SetLabel(_("SignalK Connection"));
  m_authStatusLabel->Wrap(-1);

  m_authButton->Hide();
  m_cancelAuthButton->Hide();

  auto* mgr = m_parent->m_pSignalKNotesManager;

  // Pluginliste und Provider-Namen aktualisieren
  std::map<wxString, bool> plugins;
  mgr->FetchInstalledPlugins(plugins);

  auto providerInfos = mgr->GetProviderInfos();

  if (providerInfos.empty()) {
    SKN_LOG(m_parent, "providerInfos EMPTY → retrying in 500ms");
    return;
  }

  // Provider-Namen aktualisieren
  UpdateProviders(mgr->GetDiscoveredProviders());

  Layout();
  SKN_LOG(m_parent, "ShowAuthenticatedState() EXIT");
}

void tpConfigDialog::ShowPendingState() {
  m_authStatusIcon->SetBitmap(
      wxArtProvider::GetBitmap(wxART_WARNING, wxART_OTHER, wxSize(16, 16)));
  m_authStatusLabel->SetForegroundColour(*wxBLUE);

  // Auth-Button deaktiviert mit neuem Text
  m_authButton->SetLabel(_("Authentication in progress..."));
  m_authButton->Enable(false);
  m_authButton->Show();

  // Abbruch-Button anzeigen
  m_cancelAuthButton->Show();
  m_cancelAuthButton->Enable(true);

  Layout();
}

void tpConfigDialog::ShowInitialState() {
  // Timer stoppen
  m_authStatusIcon->SetBitmap(
      wxArtProvider::GetBitmap(wxART_ERROR, wxART_OTHER, wxSize(16, 16)));
  m_authStatusLabel->SetForegroundColour(*wxRED);
  m_authStatusLabel->SetLabel(_("SignalK Connection"));
  m_authStatusLabel->Wrap(-1);

  // Auth-Button aktiv
  m_authButton->SetLabel(_("SignalK authentication"));
  m_authButton->Enable(true);
  m_authButton->Show();

  // Abbruch-Button verstecken
  m_cancelAuthButton->Hide();

  Layout();
}

void tpConfigDialog::UpdateProviders(
    const std::set<wxString>& providersFromNotes) {
  SKN_LOG(m_parent, "UpdateProviders called with %zu providers from notes",
          providersFromNotes.size());

  auto* mgr = m_parent->m_pSignalKNotesManager;

  // ProviderInfos (Name + Description) holen, falls authentifiziert
  std::map<wxString, ProviderDisplayInfo> providerDisplayInfos;
  if (!mgr->GetAuthToken().IsEmpty()) {
    auto infos = mgr->GetProviderInfos();
    for (const auto& info : infos) {
      ProviderDisplayInfo d;
      d.id = info.id;
      d.name = info.name;
      d.description = info.description;
      providerDisplayInfos[info.id] = d;
    }
  }

  // Bisherige Checked-States sichern
  std::map<wxString, bool> existingChecked;
  for (unsigned int i = 0; i < m_providerList->GetCount(); i++) {
    wxString* idPtr = (wxString*)m_providerList->GetClientData(i);
    if (idPtr) {
      existingChecked[*idPtr] = m_providerList->IsChecked(i);
    }
  }

  // Provider bestimmen, die angezeigt werden sollen
  std::set<wxString> allProviders = providersFromNotes;

  for (const auto& p : mgr->GetProviderSettings()) {
    allProviders.insert(p.first);
  }

  m_providerList->Clear();

  for (const wxString& providerId : allProviders) {
    wxString displayText;

    // Falls ProviderInfos vorhanden → Name + Description
    auto it = providerDisplayInfos.find(providerId);
    if (it != providerDisplayInfos.end() && !it->second.name.IsEmpty()) {
      displayText = it->second.name;
      if (!it->second.description.IsEmpty()) {
        displayText += "\n  " + it->second.description;
      }
    } else {
      // Fallback: ID
      displayText = providerId;
    }

    int index = m_providerList->Append(displayText);

    // Provider-ID als ClientData speichern
    m_providerList->SetClientData(index, new wxString(providerId));

    // Checked-Status wiederherstellen oder Standard (true)
    bool checked = true;
    auto it2 = existingChecked.find(providerId);
    if (it2 != existingChecked.end()) {
      checked = it2->second;
    }

    m_providerList->Check(index, checked);
  }

  // Hinweis ausblenden, wenn Provider existieren
  if (m_providerList->GetCount() > 0) {
    m_infoLabel->Hide();
  }

  Layout();
}

void tpConfigDialog::CreateDisplayTab() {
  m_displayPanel = new wxPanel(m_notebook);
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  // Icon-Einstellungen
  wxStaticBoxSizer* iconBox =
      new wxStaticBoxSizer(wxVERTICAL, m_displayPanel, _("Icon settings"));

  wxFlexGridSizer* iconGrid = new wxFlexGridSizer(2, 5, 5);
  iconGrid->AddGrowableCol(1);

  iconGrid->Add(
      new wxStaticText(m_displayPanel, wxID_ANY, _("Icon size (px):")), 0,
      wxALIGN_CENTER_VERTICAL);
  m_iconSizeCtrl = new wxSpinCtrl(m_displayPanel, wxID_ANY);
  m_iconSizeCtrl->SetRange(12, 64);
  m_iconSizeCtrl->SetValue(DEFAULT_ICON_SIZE);
  iconGrid->Add(m_iconSizeCtrl, 1, wxEXPAND);

  iconBox->Add(iconGrid, 0, wxEXPAND | wxALL, 5);

  // Icon-Vorschau
  m_iconPreview = new wxStaticBitmap(m_displayPanel, wxID_ANY, wxNullBitmap);
  iconBox->Add(new wxStaticText(m_displayPanel, wxID_ANY, _("Preview:")), 0,
               wxALL, 5);
  iconBox->Add(m_iconPreview, 0, wxALL | wxALIGN_CENTER, 5);

  mainSizer->Add(iconBox, 0, wxEXPAND | wxALL, 10);

  // Cluster-Einstellungen
  wxStaticBoxSizer* clusterBox =
      new wxStaticBoxSizer(wxVERTICAL, m_displayPanel, _("Cluster settings"));

  wxFlexGridSizer* clusterGrid = new wxFlexGridSizer(2, 5, 5);
  clusterGrid->AddGrowableCol(1);

  clusterGrid->Add(
      new wxStaticText(m_displayPanel, wxID_ANY, _("Cluster size (px):")), 0,
      wxALIGN_CENTER_VERTICAL);
  m_clusterSizeCtrl = new wxSpinCtrl(m_displayPanel, wxID_ANY);
  m_clusterSizeCtrl->SetRange(16, 64);
  m_clusterSizeCtrl->SetValue(DEFAULT_CLUSTER_SIZE);
  clusterGrid->Add(m_clusterSizeCtrl, 1, wxEXPAND);

  clusterGrid->Add(
      new wxStaticText(m_displayPanel, wxID_ANY, _("Cluster radius (px):")), 0,
      wxALIGN_CENTER_VERTICAL);
  m_clusterRadiusCtrl = new wxSpinCtrl(m_displayPanel, wxID_ANY);
  m_clusterRadiusCtrl->SetRange(5, 32);
  m_clusterRadiusCtrl->SetValue(DEFAULT_CLUSTER_RADIUS);
  clusterGrid->Add(m_clusterRadiusCtrl, 1, wxEXPAND);

  clusterGrid->Add(
      new wxStaticText(m_displayPanel, wxID_ANY, _("Circle color:")), 0,
      wxALIGN_CENTER_VERTICAL);
  m_clusterColorCtrl =
      new wxColourPickerCtrl(m_displayPanel, wxID_ANY, DEFAULT_CLUSTER_COLOR);
  clusterGrid->Add(m_clusterColorCtrl, 1, wxEXPAND);

  clusterGrid->Add(
      new wxStaticText(m_displayPanel, wxID_ANY, _("Circle color:")), 0,
      wxALIGN_CENTER_VERTICAL);
  m_clusterTextColorCtrl = new wxColourPickerCtrl(m_displayPanel, wxID_ANY,
                                                  DEFAULT_CLUSTER_TEXT_COLOR);
  clusterGrid->Add(m_clusterTextColorCtrl, 1, wxEXPAND);

  clusterGrid->Add(new wxStaticText(m_displayPanel, wxID_ANY, _("Font size")),
                   0, wxALIGN_CENTER_VERTICAL);
  m_clusterFontSizeCtrl = new wxSpinCtrl(m_displayPanel, wxID_ANY);
  m_clusterFontSizeCtrl->SetRange(6, 16);
  m_clusterFontSizeCtrl->SetValue(DEFAULT_CLUSTER_FONT_SIZE);
  clusterGrid->Add(m_clusterFontSizeCtrl, 1, wxEXPAND);

  clusterBox->Add(clusterGrid, 0, wxEXPAND | wxALL, 5);

  // Cluster-Vorschau
  m_clusterPreview = new wxStaticBitmap(m_displayPanel, wxID_ANY, wxNullBitmap);
  clusterBox->Add(new wxStaticText(m_displayPanel, wxID_ANY, _("Preview:")), 0,
                  wxALL, 5);
  clusterBox->Add(m_clusterPreview, 0, wxALL | wxALIGN_CENTER, 5);

  mainSizer->Add(clusterBox, 0, wxEXPAND | wxALL, 10);

  // Maßstab-Grenzen für Cluster
  wxFlexGridSizer* scaleGrid = new wxFlexGridSizer(2, 5, 5);
  scaleGrid->AddGrowableCol(1);

  scaleGrid->Add(new wxStaticText(m_displayPanel, wxID_ANY,
                                  _("Maximum scale for cluster 1:")),
                 0, wxALIGN_CENTER_VERTICAL);
  m_clusterMaxScaleCtrl = new wxSpinCtrl(m_displayPanel, wxID_ANY);
  m_clusterMaxScaleCtrl->SetRange(1, 999999);
  m_clusterMaxScaleCtrl->SetValue(DEFAULT_CLUSTER_MAX_SCALE);
  m_clusterMaxScaleCtrl->SetIncrement(100);
  scaleGrid->Add(m_clusterMaxScaleCtrl, 1, wxEXPAND);

  scaleGrid->Add(new wxStaticText(m_displayPanel, wxID_ANY,
                                  _("Minimum scale for cluster 1:")),
                 0, wxALIGN_CENTER_VERTICAL);
  m_clusterMinScaleCtrl = new wxSpinCtrl(m_displayPanel, wxID_ANY);
  m_clusterMinScaleCtrl->SetRange(0, 9999999);
  m_clusterMinScaleCtrl->SetValue(DEFAULT_CLUSTER_MIN_SCALE);
  m_clusterMinScaleCtrl->SetIncrement(100);
  scaleGrid->Add(m_clusterMinScaleCtrl, 1, wxEXPAND);
  m_clusterMaxScaleCtrl->Bind(wxEVT_SPINCTRL,
                              &tpConfigDialog::OnScaleSettingChanged, this);
  m_clusterMinScaleCtrl->Bind(wxEVT_SPINCTRL,
                              &tpConfigDialog::OnScaleSettingChanged, this);
  mainSizer->Add(scaleGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

  // Debug-Checkbox ("Erweitertes Logging")
  m_debugCheckbox = new wxCheckBox(m_displayPanel, wxID_ANY,
                                   _("Advanced debug logging in opencpn.log"));
  m_debugCheckbox->SetValue(m_parent->IsDebugMode());
  mainSizer->Add(m_debugCheckbox, 0, wxALL, 10);

  m_displayPanel->SetSizer(mainSizer);

  // Event-Handler
  m_iconSizeCtrl->Bind(wxEVT_SPINCTRL, &tpConfigDialog::OnDisplaySettingChanged,
                       this);
  m_clusterSizeCtrl->Bind(wxEVT_SPINCTRL,
                          &tpConfigDialog::OnDisplaySettingChanged, this);
  m_clusterRadiusCtrl->Bind(wxEVT_SPINCTRL,
                            &tpConfigDialog::OnDisplaySettingChanged, this);
  m_clusterFontSizeCtrl->Bind(wxEVT_SPINCTRL,
                              &tpConfigDialog::OnDisplaySettingChanged, this);
  m_clusterColorCtrl->Bind(wxEVT_COLOURPICKER_CHANGED,
                           &tpConfigDialog::OnColorChanged, this);
  m_clusterTextColorCtrl->Bind(wxEVT_COLOURPICKER_CHANGED,
                               &tpConfigDialog::OnColorChanged, this);

  // Initiale Vorschau
  UpdateIconPreview();
  UpdateClusterPreview();
}

void tpConfigDialog::UpdateIconPreview() {
  wxFileName fn;
  fn.SetPath(m_parent->GetPluginIconDir());
  fn.SetName("notice-to-mariners");
  fn.SetExt("svg");

  int size = GetIconSize();
  wxBitmap bmp = LoadSvgBitmap(fn.GetFullPath(), size);

  if (bmp.IsOk()) {
    m_iconPreview->SetBitmap(bmp);
  }
}

void tpConfigDialog::UpdateClusterPreview() {
  int size = GetClusterSize();
  int radius = GetClusterRadius();
  wxColour circleColor = GetClusterColor();
  wxColour textColor = GetClusterTextColor();
  int fontSize = GetClusterFontSize();

  const wxColour maskColor(255, 0, 255);

  wxBitmap bmp(size, size);
  wxMemoryDC dc;
  dc.SelectObject(bmp);

  dc.SetBackground(wxBrush(maskColor));
  dc.Clear();

  int centerX = size / 2;
  int centerY = size / 2;

  dc.SetBrush(wxBrush(circleColor));
  dc.SetPen(wxPen(*wxBLACK, 1));
  dc.DrawCircle(centerX, centerY, radius);

  dc.SetTextForeground(textColor);
  wxFont font(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
              wxFONTWEIGHT_BOLD);
  dc.SetFont(font);

  wxString text = "5";
  wxSize ts = dc.GetTextExtent(text);
  dc.DrawText(text, centerX - ts.x / 2, centerY - ts.y / 2);

  dc.SelectObject(wxNullBitmap);
  bmp.SetMask(new wxMask(bmp, maskColor));

  m_clusterPreview->SetBitmap(bmp);
}

void tpConfigDialog::OnDisplaySettingChanged(wxSpinEvent& event) {
  UpdateIconPreview();
  UpdateClusterPreview();
}

void tpConfigDialog::OnColorChanged(wxColourPickerEvent& event) {
  UpdateClusterPreview();
}

void tpConfigDialog::LoadDisplaySettings(int iconSize, int clusterSize,
                                         int clusterRadius,
                                         const wxColour& clusterColor,
                                         const wxColour& textColor,
                                         int fontSize, int clusterMaxScale,
                                         int clusterMinScale) {
  if (m_iconSizeCtrl) m_iconSizeCtrl->SetValue(iconSize);
  if (m_clusterSizeCtrl) m_clusterSizeCtrl->SetValue(clusterSize);
  if (m_clusterRadiusCtrl) m_clusterRadiusCtrl->SetValue(clusterRadius);
  if (m_clusterColorCtrl) m_clusterColorCtrl->SetColour(clusterColor);
  if (m_clusterTextColorCtrl) m_clusterTextColorCtrl->SetColour(textColor);
  if (m_clusterFontSizeCtrl) m_clusterFontSizeCtrl->SetValue(fontSize);
  if (m_clusterMaxScaleCtrl) m_clusterMaxScaleCtrl->SetValue(clusterMaxScale);
  if (m_clusterMinScaleCtrl) m_clusterMinScaleCtrl->SetValue(clusterMinScale);

  UpdateIconPreview();
  UpdateClusterPreview();
}

void tpConfigDialog::ValidateScaleSettings() {
  if (!m_scaleErrorLabel) return;

  int maxScale = m_clusterMaxScaleCtrl ? m_clusterMaxScaleCtrl->GetValue() : 0;
  int minScale = m_clusterMinScaleCtrl ? m_clusterMinScaleCtrl->GetValue() : 0;

  wxString error;

  if (maxScale < 800) {
    error = _("Maximum scale must be at least 800.");
  } else if (minScale != 0 && maxScale >= minScale) {
    error =
        _("Maximum scale must be smaller than minimum scale (or minimum scale "
          "= 0).");
  }

  m_scaleErrorLabel->SetLabel(error);
  m_scaleErrorLabel->Show(!error.IsEmpty());
  Layout();
}

void tpConfigDialog::OnScaleSettingChanged(wxSpinEvent& event) {
  ValidateScaleSettings();
  event.Skip();
}

void tpConfigDialog::CreateResourceSetTab() {
  if (!m_notebook) return;

  m_resourceSetPanel = new wxPanel(m_notebook);
  m_notebook->AddPage(m_resourceSetPanel, _("Resourcesets"));

  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText* hint = new wxStaticText(
      m_resourceSetPanel, wxID_ANY,
      _("Enable resource sets and choose an icon for the chart display."));

  mainSizer->Add(hint, 0, wxALL, 5);

  m_resourceSetScrollWin =
      new wxScrolledWindow(m_resourceSetPanel, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxVSCROLL);
  m_resourceSetScrollWin->SetScrollRate(0, 10);
  mainSizer->Add(m_resourceSetScrollWin, 1, wxEXPAND | wxALL, 5);

  m_resourceSetPanel->SetSizer(mainSizer);
}

void tpConfigDialog::UpdateResourceSetTab(
    const std::map<wxString, signalk_notes_opencpn_pi::ResourceSetConfig>&
        configs) {
  if (!m_resourceSetScrollWin) return;

  m_resourceSetScrollWin->DestroyChildren();
  m_rsRows.clear();

  wxFlexGridSizer* grid = new wxFlexGridSizer(
      3, 5, 5);  // 3 Spalten: Checkbox | Icon-Combo | (leer)
  grid->AddGrowableCol(1, 1);
  m_resourceSetScrollWin->SetSizer(grid);

  for (auto& rsKv : configs) {
    const wxString& rsName = rsKv.first;
    const auto& rsCfg = rsKv.second;

    MainRSRow mainRow;
    mainRow.rsName = rsName;

    // Haupt-Resourceset-Zeile (fett, über volle Breite)
    wxCheckBox* mainCheck = new wxCheckBox(m_resourceSetScrollWin, wxID_ANY,
                                           wxString::Format("── %s", rsName));
    wxFont boldFont = mainCheck->GetFont();
    boldFont.SetWeight(wxFONTWEIGHT_BOLD);
    mainCheck->SetFont(boldFont);
    mainCheck->SetValue(rsCfg.enabled);
    mainCheck->SetClientData(new wxString(rsName));
    mainCheck->Bind(wxEVT_CHECKBOX, &tpConfigDialog::OnMainRSToggled, this);

    mainRow.enabledCheck = mainCheck;
    grid->Add(mainCheck, 0, wxALL | wxALIGN_CENTER_VERTICAL, 3);
    grid->AddSpacer(0);
    grid->AddSpacer(0);

    // Unter-Resourcesets
    for (auto& subKv : rsCfg.subSets) {
      const wxString& subName = subKv.first;
      const auto& subCfg = subKv.second;

      SubRSRow row;
      row.rsName = rsName;
      row.subName = subName;

      wxCheckBox* subCheck =
          new wxCheckBox(m_resourceSetScrollWin, wxID_ANY, "    " + subName);
      subCheck->SetValue(subCfg.enabled);
      subCheck->Enable(rsCfg.enabled);
      row.enabledCheck = subCheck;

      wxBitmapComboBox* combo = new wxBitmapComboBox(
          m_resourceSetScrollWin, wxID_ANY, wxEmptyString, wxDefaultPosition,
          wxSize(200, -1), 0, nullptr, wxCB_READONLY);
      // Icons befüllen (gleiche Icons wie im Icon-Zuordnung-Tab)
      int selectedIdx = 0;
      for (size_t k = 0; k < m_pluginIcons.GetCount(); k++) {
        wxString iconName = m_pluginIcons[k];
        auto bmpIt = m_pluginIconBitmaps.find(iconName);
        if (bmpIt != m_pluginIconBitmaps.end())
          combo->Append(iconName, bmpIt->second);
        else
          combo->Append(iconName);
        if (iconName == subCfg.iconName) selectedIdx = (int)k;
      }
      if (combo->GetCount() > 0) combo->SetSelection(selectedIdx);
      combo->Enable(rsCfg.enabled && subCfg.enabled);
      row.iconCombo = combo;

      // Wenn Unter-Check geändert wird, Combo enablen/disablen
      subCheck->Bind(wxEVT_CHECKBOX, [combo](wxCommandEvent& evt) {
        combo->Enable(evt.IsChecked());
      });

      grid->Add(subCheck, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
      grid->Add(combo, 0, wxALL | wxEXPAND, 2);
      grid->AddSpacer(0);

      mainRow.subRows.push_back(row);
    }

    m_rsRows.push_back(mainRow);
  }

  m_resourceSetScrollWin->FitInside();
  m_resourceSetScrollWin->Layout();
}

void tpConfigDialog::OnMainRSToggled(wxCommandEvent& event) {
  wxCheckBox* mainCheck = dynamic_cast<wxCheckBox*>(event.GetEventObject());
  if (!mainCheck) return;

  wxString* rsNamePtr = static_cast<wxString*>(mainCheck->GetClientData());
  if (!rsNamePtr) return;
  wxString rsName = *rsNamePtr;
  bool enabled = mainCheck->GetValue();

  for (auto& mainRow : m_rsRows) {
    if (mainRow.rsName != rsName) continue;
    for (auto& subRow : mainRow.subRows) {
      subRow.enabledCheck->Enable(enabled);
      if (!enabled) {
        subRow.enabledCheck->SetValue(false);
        subRow.iconCombo->Enable(false);
      } else {
        subRow.iconCombo->Enable(subRow.enabledCheck->GetValue());
      }
    }
    break;
  }
}

std::map<wxString, signalk_notes_opencpn_pi::ResourceSetConfig>
tpConfigDialog::GetResourceSetConfigs() const {
  std::map<wxString, signalk_notes_opencpn_pi::ResourceSetConfig> result;

  for (auto& mainRow : m_rsRows) {
    signalk_notes_opencpn_pi::ResourceSetConfig cfg;
    cfg.name = mainRow.rsName;
    cfg.enabled = mainRow.enabledCheck->GetValue();

    for (auto& subRow : mainRow.subRows) {
      signalk_notes_opencpn_pi::SubResourceSetConfig subCfg;
      subCfg.name = subRow.subName;
      subCfg.enabled = subRow.enabledCheck->GetValue();
      int sel = subRow.iconCombo->GetSelection();
      if (sel >= 0 && sel < (int)m_pluginIcons.GetCount())
        subCfg.iconName = m_pluginIcons[sel];
      cfg.subSets[subRow.subName] = subCfg;
    }

    result[mainRow.rsName] = cfg;
  }

  return result;
}