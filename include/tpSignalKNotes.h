/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   SignalK note object definitions and data structures
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2026 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/
#ifndef _TPSIGNALKNOTES_H_
#define _TPSIGNALKNOTES_H_

#include "ocpn_plugin.h"
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/protocol/http.h>
#include <wx/sstream.h>
#include <wx/jsonval.h>
#include <wx/filename.h>
#include <vector>
#include <map>
#include <wx/string.h>
#include <set>

// Forward declaration
class signalk_notes_opencpn_pi;


class SignalKNote {
public:
  wxString id;
  wxString name;
  wxString description;
  double latitude;
  double longitude;
  wxString iconName;
  wxString url;
  wxString source;
  wxString GUID;
  bool isDisplayed;

  SignalKNote() : latitude(0.0), longitude(0.0), isDisplayed(false) {}
};

class tpSignalKNotesManager {
public:
  tpSignalKNotesManager(signalk_notes_opencpn_pi* parent);
  ~tpSignalKNotesManager() = default;  // Default-Destruktor
  
  // Server settings
  wxString GetServerHost() const { return m_serverHost; }
  int GetServerPort() const { return m_serverPort; }
  void SetServerDetails(const wxString& host, int port);

  // Notes
  void UpdateDisplayedIcons(double centerLat, double centerLon,
                            double maxDistance, signalk_notes_opencpn_pi::CanvasState& state);

  SignalKNote* GetNoteByGUID(signalk_notes_opencpn_pi::CanvasState& state, const wxString& guid);
  void GetVisibleNotes(signalk_notes_opencpn_pi::CanvasState& state,std::vector<const SignalKNote*>& outNotes);
  bool GetIconBitmapForNote(const SignalKNote& note, wxBitmap& bmp, bool forGL);


  void OnIconClick(const wxString& guid, signalk_notes_opencpn_pi::CanvasState& state, int canvasIndex);
  void InvalidateIconCache(const wxString& iconName);

  // Provider & Icon mappings
  void SetProviderSettings(const std::map<wxString, bool>& settings);
  void SetIconMappings(const std::map<wxString, wxString>& mappings);

  std::map<wxString, bool> GetProviderSettings() const {
    return m_providerSettings;
  }
  std::map<wxString, wxString> GetIconMappings() const {
    return m_iconMappings;
  }

  std::set<wxString> GetDiscoveredProviders() const {
    return m_discoveredProviders;
  }
  std::set<wxString> GetDiscoveredIcons() const { return m_discoveredIcons; }

  wxString RenderHTMLDescription(const wxString& htmlText);

  // ----------------------
  // Authentication Handling
  // ----------------------
  bool RequestAuthorization();
  bool CheckAuthorizationStatus();
  bool ValidateToken();

  wxString GetAuthToken() const { return m_authToken; }
  void SetAuthToken(const wxString& token);

  wxString GetAuthRequestHref() const { return m_authRequestHref; }
  void SetAuthRequestHref(const wxString& href);

  bool IsAuthPending() const {
    return m_authPending && !m_authRequestHref.IsEmpty();
  }

  void ClearAuthRequest();

  bool FetchInstalledPlugins(std::map<wxString, bool>& plugins);
  void CleanupDisabledProviders();

  struct ProviderInfo {
    wxString id;
    wxString name;
    wxString description;
  };

  std::vector<ProviderInfo> GetProviderInfos() const;

  bool LoadIconSmart(const wxString& basePathWithoutExt, int size,
                     wxBitmap& outBmp);
  wxDateTime GetAuthRequestTime() const { return m_authRequestTime; }
  wxDateTime GetAuthTokenReceivedTime() const {
    return m_authTokenReceivedTime;
  }
// Resourceset-Unterstützung
bool FetchAvailableResourceSets(std::set<wxString>& outResourceSets);
bool FetchResourceSet(const wxString& resourceSetName,
                      std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>& subConfigs,
                      signalk_notes_opencpn_pi::CanvasState& state,
                      const std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>& configuredSubs);
bool DiscoverSubResourceSets(const wxString& resourceSetName,
    std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>& outSubs);
// private:
int ParseFlatResourceSetJSON(const wxString& json,
                             const wxString& resourceSetName,
                             signalk_notes_opencpn_pi::CanvasState& state,
                             const signalk_notes_opencpn_pi::SubResourceSetConfig& config);    
private:
  signalk_notes_opencpn_pi* m_parent = nullptr;

  int FetchNotesListForCanvas(double centerLat, double centerLon, double maxDistance, signalk_notes_opencpn_pi::CanvasState& state);
  bool FetchNoteDetails(const wxString& noteId, SignalKNote& note);

  wxString ResolveIconPath(const wxString& skIconName);
  bool DownloadIcon(const wxString& iconName, wxBitmap& bitmap);
  bool CreateNoteIcon(SignalKNote& note);
  bool DeleteNoteIcon(const wxString& guid);

  int ParseNotesListJSON(const wxString& json, signalk_notes_opencpn_pi::CanvasState& state);
  bool ParseNoteDetailsJSON(const wxString& json, SignalKNote& note);

  // Server data
  wxString m_serverHost;
  int m_serverPort;

  // Authentication data

  wxString m_authRequestHref;  // vom Server geliefertes href
  wxString m_authToken;
  bool m_authPending = false;
  wxDateTime m_authRequestTime;
  wxDateTime m_authTokenReceivedTime;

  // Notes
  std::map<wxString, SignalKNote> m_notes;
  std::vector<wxString> m_displayedGUIDs;

  std::map<wxString, wxBitmap> m_iconCache;

  std::map<wxString, bool> m_providerSettings;
  std::map<wxString, wxString> m_iconMappings;     // iconName -> filePath

  std::set<wxString> m_discoveredProviders;
  std::set<wxString> m_discoveredIcons;

  struct ProviderDetails {
    wxString id;
    wxString name;
    wxString description;
    bool enabled;
  };
  std::map<wxString, ProviderDetails> m_providerDetails;

  // Helper-Methoden für Note-Details Dialog
  wxString PrepareHTMLContent(const wxString& description, const wxString& url);
  bool RenderWithWebView(wxDialog* dlg, wxBoxSizer* sizer,
                         const wxString& htmlContent);
  void RenderWithHtmlWindow(wxDialog* dlg, wxBoxSizer* sizer,
                            const wxString& htmlContent);
  wxString FixBrokenLinksInDescription(const wxString& html);
  bool IsValidResourceSet(wxJSONValue rsJson); 
  int ParseResourceSetJSON(const wxString& json,
                          const wxString& resourceSetName,
                          signalk_notes_opencpn_pi::CanvasState& state,
                          const std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>& configuredSubs,
                          std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>& outDiscoveredSubs);
};
#endif  // _TPSIGNALKNOTES_H_