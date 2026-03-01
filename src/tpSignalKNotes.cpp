/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Handling of SignalK note objects and data updates
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2024 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/

#include "ocpn_plugin.h"
#include "tpSignalKNotes.h"
#include "signalk_notes_opencpn_pi.h"
#include "tpConfigDialog.h"

#include <wx/filename.h>
#include <wx/jsonreader.h>
#include <wx/url.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/html/htmlwin.h>
#include <wx/protocol/http.h>
#include <wx/datetime.h>
#include <wx/artprov.h>
#include <cstring>
#if defined(wxHAS_WEB_VIEW)
#include <wx/webview.h>
#endif

#ifndef __OCPN__ANDROID__
#include <wx/bmpbndl.h>
#endif

wxString HttpGet(const wxString& url, const wxString& authHeader = "");

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__OCPN__ANDROID__)

#include <curl/curl.h>

static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb,
                                void* userp) {
  size_t total = size * nmemb;
  std::string* s = static_cast<std::string*>(userp);
  s->append(static_cast<char*>(contents), total);
  return total;
}

wxString HttpGet(const wxString& url, const wxString& authHeader) {
  CURL* curl = curl_easy_init();
  if (!curl) return "";

  std::string response;
  struct curl_slist* headers = NULL;

  if (!authHeader.IsEmpty()) {
    wxString hdr = authHeader;
    headers = curl_slist_append(headers, hdr.mb_str().data());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.mb_str().data());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);

  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || httpCode != 200) return "";

  return wxString::FromUTF8(response.c_str());
}

#endif

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

wxString HttpGet(const wxString& url, const wxString& authHeader) {
  // URL parsen
  URL_COMPONENTS uc = {0};
  uc.dwStructSize = sizeof(uc);

  wchar_t host[256];
  wchar_t path[2048];

  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;

  if (!WinHttpCrackUrl(url.wc_str(), 0, 0, &uc)) return "";

  HINTERNET hSession =
      WinHttpOpen(L"SignalKNotes/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

  if (!hSession) return "";

  HINTERNET hConnect = WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return "";
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);

  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  // Authorization Header korrekt übernehmen
  if (!authHeader.IsEmpty()) {
    std::wstring hdr = std::wstring(authHeader.wc_str());
    WinHttpAddRequestHeaders(
        hRequest, hdr.c_str(), (DWORD)-1,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  }

  if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  std::string response;
  DWORD bytesAvailable = 0;

  do {
    if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) break;

    if (bytesAvailable == 0) break;

    std::vector<char> buffer(bytesAvailable);
    DWORD bytesRead = 0;

    if (!WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead))
      break;

    response.append(buffer.data(), bytesRead);

  } while (bytesAvailable > 0);

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);

  return wxString::FromUTF8(response.c_str());
}

#endif

#ifdef __OCPN__ANDROID__

wxString HttpGet(const wxString& url, const wxString& authHeader) {
  // Android: HTTP über wxHTTP
  wxHTTP http;
  http.SetTimeout(10);

  wxURL wxurl(url);
  if (wxurl.GetError() != wxURL_NOERR) return "";

  wxString host = wxurl.GetServer();
  long port = 80;
  wxurl.GetPort().ToLong(&port);
  wxString path = "/" + wxurl.GetPath();

  if (!authHeader.IsEmpty()) {
    http.SetHeader("Authorization", authHeader.AfterFirst(' ').AfterFirst(' '));
  }

  if (!http.Connect(host, (unsigned short)port)) return "";

  wxInputStream* in = http.GetInputStream(path);
  if (!in || !in->IsOk()) {
    delete in;
    return "";
  }

  wxString response;
  char buf[4096];
  while (true) {
    in->Read(buf, sizeof(buf));
    size_t read = in->LastRead();
    if (read == 0) break;
    response += wxString::FromUTF8(buf, read);
  }
  delete in;
  return response;
}

#endif

// ---------------------------------------------------------------------------
// Helper: strip extension and return base path (without .svg/.png)
// ---------------------------------------------------------------------------
static wxString GetBasePathWithoutExt(const wxString& fullPath) {
  wxFileName fn(fullPath);
  fn.ClearExt();
  return fn.GetPathWithSep() + fn.GetName();
}

// ---------------------------------------------------------------------------
// Helper: unified icon loader (Desktop: SVG+PNG, Android: PNG only)
// basePathWithoutExt: full path without extension
// size: requested size (square)
// outBmp: resulting bitmap
// ---------------------------------------------------------------------------
bool tpSignalKNotesManager::LoadIconSmart(const wxString& basePathWithoutExt,
                                          int size, wxBitmap& outBmp) {
#ifdef __OCPN__ANDROID__
  // ANDROID → PNG ONLY
  wxString pngPath = basePathWithoutExt + ".png";

  wxBitmap bmp(pngPath, wxBITMAP_TYPE_PNG);
  if (bmp.IsOk()) {
    outBmp = bmp;
    return true;
  }

  outBmp = wxBitmap(size, size);
  return false;
#else
  // DESKTOP → SVG preferred, PNG fallback
  wxString svgPath = basePathWithoutExt + ".svg";
  wxString pngPath = basePathWithoutExt + ".png";

  if (wxFileExists(svgPath)) {
    wxBitmapBundle bundle =
        wxBitmapBundle::FromSVGFile(svgPath, wxSize(size, size));
    if (bundle.IsOk()) {
      outBmp = bundle.GetBitmap(wxSize(size, size));
      return true;
    }
  }

  if (wxFileExists(pngPath)) {
    wxBitmap bmp(pngPath, wxBITMAP_TYPE_PNG);
    if (bmp.IsOk()) {
      outBmp = bmp;
      return true;
    }
  }

  outBmp = wxBitmap(size, size);
  return false;
#endif
}

// ---------------------------------------------------------------------------

tpSignalKNotesManager::tpSignalKNotesManager(signalk_notes_opencpn_pi* parent) {
  m_parent = parent;
  m_serverHost = wxEmptyString;
  m_serverPort = 3000;
}

tpSignalKNotesManager::~tpSignalKNotesManager() { ClearAllIcons(); }

void tpSignalKNotesManager::SetServerDetails(const wxString& host, int port) {
  m_serverHost = host;
  m_serverPort = port;
}

bool tpSignalKNotesManager::FetchNotesForViewport(double centerLat,
                                                  double centerLon,
                                                  double maxDistance) {
  if (m_serverHost.IsEmpty()) {
    SKN_LOG(m_parent, _("Server host not configured"));
    return false;
  }

  return FetchNotesList(centerLat, centerLon, maxDistance);
}

void tpSignalKNotesManager::UpdateDisplayedIcons(double centerLat,
                                                 double centerLon,
                                                 double maxDistance) {
  if (!FetchNotesForViewport(centerLat, centerLon, maxDistance)) {
    SKN_LOG(m_parent, "Failed to fetch notes");
    return;
  }

  bool newMappingsFound = false;

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    if (!note.iconName.IsEmpty()) {
      if (m_iconMappings.find(note.iconName) == m_iconMappings.end()) {
        wxString iconPath = ResolveIconPath(note.iconName);
        m_iconMappings[note.iconName] = iconPath;
        newMappingsFound = true;
      }
    }
  }

  if (newMappingsFound && m_parent) {
    m_parent->SaveConfig();
  }

  std::set<wxString> visibleNoteIds;

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    bool providerEnabled = true;

    if (!note.source.IsEmpty()) {
      auto it = m_providerSettings.find(note.source);
      if (it != m_providerSettings.end()) {
        providerEnabled = it->second;
      }
    }

    if (providerEnabled) {
      visibleNoteIds.insert(note.id);
    }
  }

  for (auto it = m_displayedGUIDs.begin(); it != m_displayedGUIDs.end();) {
    if (visibleNoteIds.find(*it) == visibleNoteIds.end()) {
      auto noteIt = m_notes.find(*it);
      if (noteIt != m_notes.end()) {
        noteIt->second.isDisplayed = false;
      }
      it = m_displayedGUIDs.erase(it);
    } else {
      ++it;
    }
  }

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    bool providerEnabled = true;
    if (!note.source.IsEmpty()) {
      auto it = m_providerSettings.find(note.source);
      if (it != m_providerSettings.end()) {
        providerEnabled = it->second;
      }
    }
    if (!providerEnabled) continue;

    if (note.isDisplayed) continue;

    if (CreateNoteIcon(note)) {
      note.isDisplayed = true;

      if (std::find(m_displayedGUIDs.begin(), m_displayedGUIDs.end(),
                    note.id) == m_displayedGUIDs.end()) {
        m_displayedGUIDs.push_back(note.id);
      }
    }
  }
}

void tpSignalKNotesManager::ClearAllIcons() {
  m_displayedGUIDs.clear();

  for (auto& pair : m_notes) {
    pair.second.isDisplayed = false;
  }
}

SignalKNote* tpSignalKNotesManager::GetNoteByGUID(const wxString& guid) {
  auto it = m_notes.find(guid);
  if (it != m_notes.end()) return &it->second;

  return nullptr;
}

void tpSignalKNotesManager::OnIconClick(
    const wxString& guid, const PlugIn_ViewPort& currentViewPort) {
  SKN_LOG(m_parent, "OnIconClick called with guid='%s'", guid);

  SignalKNote* note = GetNoteByGUID(guid);
  if (!note) {
    SKN_LOG(m_parent, "Note with guid='%s' not found!", guid);
    return;
  }

  if (note->name.IsEmpty() || note->description.IsEmpty()) {
    if (!FetchNoteDetails(note->id, *note)) {
      SKN_LOG(m_parent, "Failed to fetch details for %s", note->id);
      if (note->name.IsEmpty()) note->name = note->id;
      if (note->description.IsEmpty())
        note->description = _("Details konnten nicht geladen werden.");
    }
  }

  SKN_LOG(m_parent, "Found note '%s'", note->name);
  int dlgWidth =
      std::max(400, (int)std::round(currentViewPort.pix_width * 0.75));
  int dlgHeight =
      std::max(300, (int)std::round(currentViewPort.pix_height * 0.75));
  wxDialog* dlg = new wxDialog(m_parent->GetParentWindow(), wxID_ANY,
                               _("SignalK Note Details"), wxDefaultPosition,
                               wxSize(dlgWidth, dlgHeight),
                               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText* title = new wxStaticText(dlg, wxID_ANY, note->name);
  wxFont font = title->GetFont();
  font.SetPointSize(font.GetPointSize() + 2);
  font.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(font);
  sizer->Add(title, 0, wxALL | wxEXPAND, 10);

  // ============================================================
  // HTML-Content vorbereiten
  // ============================================================
  wxString htmlContent = PrepareHTMLContent(note->description, note->url);

  // ============================================================
  // Platform-spezifische Rendering
  // ============================================================
#if defined(__WXMSW__) || defined(__WXMAC__)
  // Windows & macOS: wxWebView (beste Unterstützung)
  if (!RenderWithWebView(dlg, sizer, htmlContent)) {
    SKN_LOG(m_parent,
            "wxWebView not available on this system, using wxHtmlWindow");
    RenderWithHtmlWindow(dlg, sizer, htmlContent);
  }
#elif defined(__OCPN__ANDROID__)
  // Android: wxHtmlWindow (wxWebView begrenzt)
  RenderWithHtmlWindow(dlg, sizer, htmlContent);

#else
  // Linux & sonstige: Versuche wxWebView, fallback auf wxHtmlWindow
  if (!RenderWithWebView(dlg, sizer, htmlContent)) {
    SKN_LOG(m_parent,
            "wxWebView not available on this system, using wxHtmlWindow");
    RenderWithHtmlWindow(dlg, sizer, htmlContent);
  }
#endif

  // ============================================================
  // Buttons
  // ============================================================
  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

  wxButton* centerBtn = new wxButton(dlg, wxID_ANY, _("Auf Karte zentrieren"));
  centerBtn->Bind(wxEVT_BUTTON, [note, this](wxCommandEvent&) {
    PlugIn_ViewPort vp = m_parent->m_lastViewPort;

    double lat = note->latitude;
    double lon = note->longitude;
    double scale = vp.view_scale_ppm;

    vp.clat = lat;
    vp.clon = lon;

    double latSpan = vp.lat_max - vp.lat_min;
    double lonSpan = vp.lon_max - vp.lon_min;

    vp.lat_min = lat - latSpan / 2.0;
    vp.lat_max = lat + latSpan / 2.0;
    vp.lon_min = lon - lonSpan / 2.0;
    vp.lon_max = lon + lonSpan / 2.0;

    SKN_LOG(m_parent, "Centering via VP lat=%.6f lon=%.6f scale=%.6f", lat, lon,
            scale);

    JumpToPosition(lat, lon, scale);
  });
  btnSizer->Add(centerBtn, 0, wxALL, 5);

  btnSizer->AddStretchSpacer();

  wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
  btnSizer->Add(okBtn, 0, wxALL, 5);

  sizer->Add(btnSizer, 0, wxALL | wxEXPAND, 5);

  dlg->SetSizer(sizer);

  dlg->ShowModal();
  dlg->Destroy();

  // Canvas aus potenziellem Drag-Modus befreien
  wxWindow* canvas = GetOCPNCanvasWindow();
  if (canvas) {
    wxMouseEvent upEvent(wxEVT_LEFT_UP);
    upEvent.SetPosition(wxGetMousePosition());
    canvas->GetEventHandler()->ProcessEvent(upEvent);
  }
}

// ============================================================
// Helper: HTML-Content vorbereiten
// ============================================================
wxString tpSignalKNotesManager::PrepareHTMLContent(const wxString& description,
                                                   const wxString& url) {
  wxString htmlContent;
  htmlContent
      << "<!DOCTYPE html><html><head>"
      << "<meta charset=\"UTF-8\">"
      << "<meta name=\"viewport\" content=\"width=device-width, "
         "initial-scale=1.0\">"
      << "<style>"
      << "body { "
      << "  font-family: 'Segoe UI', 'Apple Color Emoji', 'Noto Color Emoji', "
         "Arial, sans-serif; "
      << "  font-size: 14px; "
      << "  margin: 10px; "
      << "  line-height: 1.5; "
      << "  color: #333; "
      << "}"
      << "h1, h2, h3, h4, h5, h6 { color: #333; margin-top: 15px; }"
      << "table { border-collapse: collapse; width: 100%; margin: 10px 0; }"
      << "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
      << "th { background-color: #f2f2f2; font-weight: bold; }"
      << "a { color: #0066cc; text-decoration: none; }"
      << "a:hover { text-decoration: underline; }"
      << "hr { margin: 15px 0; border: none; border-top: 1px solid #ddd; }"
      << "img { max-width: 100%; height: auto; }"
      << "</style>"
      << "</head><body>" << description;

  if (!url.IsEmpty()) {
    htmlContent << "<hr>"
                << "<b>Link:</b> "
                << "<a href=\"" << url << "\" target=\"_blank\">" << url
                << "</a>";
  }

  htmlContent << "<hr>"
              << "<small style=\"color: #666;\">"
              << "<i>Do not use for navigation.</i>"
              << "</small>"
              << "</body></html>";

  return htmlContent;
}

// ============================================================
// Helper: Rendering mit wxWebView (Windows, macOS, Linux optional)
// ============================================================
#ifdef wxHAS_WEB_VIEW
bool tpSignalKNotesManager::RenderWithWebView(wxDialog* dlg, wxBoxSizer* sizer,
                                              const wxString& htmlContent) {
  try {
    wxWebView* webView =
        wxWebView::New(dlg, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);

    if (!webView) return false;

    // Link-Clicks abfangen
    webView->Bind(wxEVT_WEBVIEW_NAVIGATING, [this](wxWebViewEvent& evt) {
      wxString url = evt.GetURL();
      if (url.StartsWith("http://") || url.StartsWith("https://")) {
        evt.Veto();
        SKN_LOG(m_parent, "Opening URL: %s", url);
        wxLaunchDefaultBrowser(url);
      }
    });

    webView->SetPage(htmlContent, "");
    sizer->Add(webView, 1, wxALL | wxEXPAND, 10);

    SKN_LOG(m_parent, "Using wxWebView for rendering");
    return true;
  } catch (...) {
    SKN_LOG(m_parent, "wxWebView initialization failed, falling back");
    return false;
  }
}
#else
bool tpSignalKNotesManager::RenderWithWebView(wxDialog* dlg, wxBoxSizer* sizer,
                                              const wxString& htmlContent) {
  SKN_LOG(m_parent, "wxWebView not compiled, using wxHtmlWindow");
  return false;
}
#endif

// ============================================================
// Helper: Rendering mit wxHtmlWindow (Fallback, Android)
// ============================================================
void tpSignalKNotesManager::RenderWithHtmlWindow(wxDialog* dlg,
                                                 wxBoxSizer* sizer,
                                                 const wxString& htmlContent) {
  wxHtmlWindow* htmlWin = new wxHtmlWindow(dlg, wxID_ANY, wxDefaultPosition,
                                           wxDefaultSize, wxHW_SCROLLBAR_AUTO);

  htmlWin->Bind(wxEVT_HTML_LINK_CLICKED, [this](wxHtmlLinkEvent& evt) {
    wxString url = evt.GetLinkInfo().GetHref();
    SKN_LOG(m_parent, "Opening URL: %s", url);
    wxLaunchDefaultBrowser(url);
  });

  htmlWin->SetPage(htmlContent);
  sizer->Add(htmlWin, 1, wxALL | wxEXPAND, 10);

  SKN_LOG(m_parent, "Using wxHtmlWindow for rendering");
}

bool tpSignalKNotesManager::FetchNotesList(double centerLat, double centerLon,
                                           double maxDistance) {
  wxString path;
  path.Printf(wxT("http://%s:%d/signalk/v2/api/resources/"
                  "notes?position=[%f,%f]&distance=%.0f"),
              m_serverHost, m_serverPort, centerLon, centerLat, maxDistance);

  wxString response = HttpGet(path);

  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "FetchNotesList FAILED - empty response");
    return false;
  }

  bool ok = ParseNotesListJSON(response);

  if (!ok) {
    SKN_LOG(m_parent, "FetchNotesList FAILED - JSON parse error");
  }

  return ok;
}

bool tpSignalKNotesManager::FetchNoteDetails(const wxString& noteId,
                                             SignalKNote& note) {
  wxString path;
  path.Printf(wxT("http://%s:%d/signalk/v2/api/resources/notes/%s"),
              m_serverHost, m_serverPort, noteId);

  wxString response = HttpGet(path);

  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "FetchNoteDetails FAILED - empty response");
    return false;
  }

  return ParseNoteDetailsJSON(response, note);
}

bool tpSignalKNotesManager::ParseNotesListJSON(const wxString& json) {
  wxJSONReader reader;
  wxJSONValue root;

  int errors = reader.Parse(json, &root);
  if (errors > 0) {
    SKN_LOG(m_parent, _("Failed to parse notes list JSON"));
    return false;
  }

  m_notes.clear();

  wxArrayString memberNames = root.GetMemberNames();

  for (size_t i = 0; i < memberNames.GetCount(); i++) {
    wxString noteId = memberNames[i];
    wxJSONValue noteData = root[noteId];

    SignalKNote note;
    note.id = noteId;

    if (noteData.HasMember(wxT("name"))) {
      note.name = noteData[wxT("name")].AsString();
    }

    if (noteData.HasMember(wxT("$source"))) {
      note.source = noteData[wxT("$source")].AsString();
      m_discoveredProviders.insert(note.source);

      if (m_providerSettings.find(note.source) == m_providerSettings.end()) {
        m_providerSettings[note.source] = true;
      }
    }

    if (noteData.HasMember(wxT("position"))) {
      wxJSONValue pos = noteData[wxT("position")];
      if (pos.HasMember(wxT("latitude"))) {
        note.latitude = pos[wxT("latitude")].AsDouble();
      }
      if (pos.HasMember(wxT("longitude"))) {
        note.longitude = pos[wxT("longitude")].AsDouble();
      }
    }

    if (noteData.HasMember(wxT("url"))) {
      note.url = noteData[wxT("url")].AsString();
    }

    if (noteData.HasMember(wxT("properties"))) {
      wxJSONValue props = noteData[wxT("properties")];
      if (props.HasMember(wxT("skIcon"))) {
        note.iconName = props[wxT("skIcon")].AsString();
        m_discoveredIcons.insert(note.iconName);
      }
    }

    note.isDisplayed = false;
    m_notes[noteId] = note;
  }
  return true;
}

bool tpSignalKNotesManager::ParseNoteDetailsJSON(const wxString& json,
                                                 SignalKNote& note) {
  wxJSONReader reader;
  wxJSONValue root;

  int errors = reader.Parse(json, &root);
  if (errors > 0) {
    SKN_LOG(m_parent, _("Failed to parse note details JSON"));
    return false;
  }

  if (root.HasMember(wxT("name"))) {
    note.name = root[wxT("name")].AsString();
  }

  if (root.HasMember(wxT("description"))) {
    note.description = root[wxT("description")].AsString();
  }

  if (root.HasMember(wxT("position"))) {
    wxJSONValue pos = root[wxT("position")];
    if (pos.HasMember(wxT("latitude"))) {
      note.latitude = pos[wxT("latitude")].AsDouble();
    }
    if (pos.HasMember(wxT("longitude"))) {
      note.longitude = pos[wxT("longitude")].AsDouble();
    }
  }

  if (root.HasMember(wxT("properties"))) {
    wxJSONValue props = root[wxT("properties")];
    if (props.HasMember(wxT("skIcon"))) {
      note.iconName = props[wxT("skIcon")].AsString();
    }
  }

  return true;
}

bool tpSignalKNotesManager::DownloadIcon(const wxString& iconName,
                                         wxBitmap& bitmap) {
  auto cacheIt = m_iconCache.find(iconName);
  if (cacheIt != m_iconCache.end()) {
    if (cacheIt->second.IsOk()) {
      bitmap = cacheIt->second;
      return true;
    }
  }

  wxString iconPath;

  auto mapIt = m_iconMappings.find(iconName);
  if (mapIt != m_iconMappings.end() && !mapIt->second.IsEmpty()) {
    iconPath = mapIt->second;
  } else {
    iconPath = ResolveIconPath(iconName);
    m_iconMappings[iconName] = iconPath;
  }

  wxBitmap bmp;

  wxString basePath = GetBasePathWithoutExt(iconPath);
  if (!LoadIconSmart(basePath, 32, bmp) || !bmp.IsOk()) {
    SKN_LOG(m_parent, "Failed to load icon from: %s", iconPath);
    return false;
  }

  m_iconCache[iconName] = bmp;
  bitmap = bmp;

  return true;
}

bool tpSignalKNotesManager::CreateNoteIcon(SignalKNote& note) {
  wxString iconName = note.iconName;
  if (iconName.IsEmpty()) {
    iconName = wxT("fallback");
  }

  wxBitmap bmp;
  if (!DownloadIcon(iconName, bmp)) {
    SKN_LOG(m_parent, "CreateNoteIcon - failed to load icon '%s'", iconName);
    return false;
  }

  return true;
}

bool tpSignalKNotesManager::DeleteNoteIcon(const wxString& guid) {
  auto it = m_notes.find(guid);
  if (it == m_notes.end()) return false;

  it->second.isDisplayed = false;
  return true;
}

void tpSignalKNotesManager::GetVisibleNotes(
    std::vector<const SignalKNote*>& outNotes) {
  outNotes.clear();
  for (auto& pair : m_notes) {
    const SignalKNote& note = pair.second;
    if (note.isDisplayed) {
      outNotes.push_back(&note);
    }
  }
}

bool tpSignalKNotesManager::GetIconBitmapForNote(const SignalKNote& note,
                                                 wxBitmap& bmp) {
  wxString skIcon = note.iconName;

  // 1. Mapping aus Config?
  auto it = m_iconMappings.find(skIcon);
  if (it != m_iconMappings.end()) {
    wxString mappedPath = it->second;

    if (wxFileExists(mappedPath)) {
      wxString base = GetBasePathWithoutExt(mappedPath);
      wxBitmap raw;
      if (LoadIconSmart(base, 24, raw) && raw.IsOk()) {
        bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
        return true;
      }
    }
  }

  // 2. Fallback: Plugin-Icon-Verzeichnis (skIcon.*)
  wxString basePluginIcon = m_parent->GetPluginIconDir() + skIcon;
  wxBitmap raw2;
  if (LoadIconSmart(basePluginIcon, 24, raw2) && raw2.IsOk()) {
    bmp = m_parent->PrepareIconBitmapForGL(raw2, 24);
    return true;
  }

  // 3. Letzter Fallback: notice-to-mariners.*
  wxString baseFallback = m_parent->GetPluginIconDir() + "notice-to-mariners";
  wxBitmap raw3;
  if (LoadIconSmart(baseFallback, 24, raw3) && raw3.IsOk()) {
    bmp = m_parent->PrepareIconBitmapForGL(raw3, 24);
    return true;
  }

  return false;
}

int tpSignalKNotesManager::GetVisibleIconCount(const PlugIn_ViewPort& vp) {
  PlugIn_ViewPort vpCopy = vp;

  std::vector<const SignalKNote*> visibleNotes;
  GetVisibleNotes(visibleNotes);

  int count = 0;

  for (const SignalKNote* note : visibleNotes) {
    if (!note) continue;

    wxPoint p;
    GetCanvasPixLL(&vpCopy, &p, note->latitude, note->longitude);

    if (p.x >= 0 && p.x < vpCopy.pix_width && p.y >= 0 &&
        p.y < vpCopy.pix_height) {
      count++;
    }
  }

  return count;
}

void tpSignalKNotesManager::SetProviderSettings(
    const std::map<wxString, bool>& settings) {
  m_providerSettings = settings;
}

void tpSignalKNotesManager::SetIconMappings(
    const std::map<wxString, wxString>& mappings) {
  m_iconMappings = mappings;
}

wxString tpSignalKNotesManager::ResolveIconPath(const wxString& skIconName) {
  wxFileName fn;
  fn.SetPath(GetPluginDataDir("signalk_notes_opencpn_pi"));
  fn.AppendDir("data");
  fn.AppendDir("icons");

  auto it = m_iconMappings.find(skIconName);
  if (it != m_iconMappings.end()) {
    wxFileName mapped(it->second);
    if (mapped.FileExists()) {
      return mapped.GetFullPath();
    }
  }

  fn.SetName(skIconName);
  fn.SetExt("svg");
  if (fn.FileExists()) {
    return fn.GetFullPath();
  }

  fn.SetExt("png");
  if (fn.FileExists()) {
    return fn.GetFullPath();
  }

  fn.SetName("notice-to-mariners");
  fn.SetExt("svg");
  if (fn.FileExists()) {
    return fn.GetFullPath();
  }

  fn.SetExt("png");
  return fn.GetFullPath();
}

wxString tpSignalKNotesManager::RenderHTMLDescription(
    const wxString& htmlText) {
  wxString result = htmlText;

  if (!result.Contains("<")) {
    return result;
  }

  return result;
}

bool tpSignalKNotesManager::RequestAuthorization() {
  wxString path = "/signalk/v1/access/requests";

  wxString body = wxString::Format(
      "{"
      "  \"clientId\": \"%s\","
      "  \"description\": \"OpenCPN SignalK Notes Plugin\""
      "}",
      m_parent->m_clientUUID);

  wxHTTP http;
  http.SetTimeout(10);
  if (!http.Connect(m_serverHost, m_serverPort)) {
    SKN_LOG(m_parent,
            "SignalK Notes Auth: Connection failed (RequestAuthorization)");
    return false;
  }

  wxMemoryBuffer postData;
  wxCharBuffer utf8 = body.ToUTF8();
  postData.AppendData(utf8.data(), utf8.length());

  http.SetPostBuffer("application/json", postData);

  wxInputStream* in = http.GetInputStream(path);

  if (!in || !in->IsOk()) {
    SKN_LOG(m_parent, "SignalK Notes Auth: POST request failed (no response)");
    delete in;
    return false;
  }

  // Chunk-weises Lesen bis wirklich nichts mehr kommt
  wxString response;
  char buf[4096];
  while (true) {
    in->Read(buf, sizeof(buf));
    size_t read = in->LastRead();
    if (read == 0) break;
    response += wxString::FromUTF8(buf, read);
  }
  delete in;

  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(response, &root) > 0) {
    SKN_LOG(m_parent, "SignalK Notes Auth: Failed to parse JSON response");
    return false;
  }

  if (root.HasMember("href")) {
    wxString href = root["href"].AsString();

    SetAuthRequestHref(href);
    m_authRequestTime = wxDateTime::Now();

    return true;
  }

  SKN_LOG(m_parent, "SignalK Notes Auth: ERROR – server did not return href");
  return false;
}

bool tpSignalKNotesManager::CheckAuthorizationStatus() {
  if (m_authRequestHref.IsEmpty()) return false;

  SKN_LOG(m_parent, "CheckAuthorizationStatus: href=%s", m_authRequestHref);

  wxString url;
  url.Printf("http://%s:%d%s", m_serverHost, m_serverPort, m_authRequestHref);

  wxString response = HttpGet(url);

  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "CheckAuthorizationStatus - empty response");
    return false;
  }

  SKN_LOG(m_parent, "CheckAuthorizationStatus: response='%s'",
          response.Left(200));

  wxJSONReader reader;
  wxJSONValue root;

  if (reader.Parse(response, &root) != 0) {
    SKN_LOG(m_parent, "CheckAuthorizationStatus - invalid JSON");
    return false;
  }

  wxString state;
  if (root.HasMember("state"))
    state = root["state"].AsString();
  else if (root.HasMember("status"))
    state = root["status"].AsString();

  if (state == "PENDING") return false;

  if (!root.HasMember("accessRequest")) {
    SKN_LOG(m_parent, "AuthStatus - no accessRequest → failed");
    ClearAuthRequest();
    return false;
  }

  wxJSONValue access = root["accessRequest"];

  if (access.HasMember("permission") &&
      access["permission"].AsString() == "DENIED") {
    SKN_LOG(m_parent, "AuthStatus - denied");
    ClearAuthRequest();
    return false;
  }

  if (access.HasMember("token")) {
    SetAuthToken(access["token"].AsString());  // statt m_authToken = ...
    SKN_LOG(m_parent, "AuthStatus - token received");
    ClearAuthRequest();
    return true;
  }

  SKN_LOG(m_parent, "AuthStatus - completed without token");
  ClearAuthRequest();
  return false;
}

bool tpSignalKNotesManager::ValidateToken() {
  if (m_authToken.IsEmpty()) {
    SKN_LOG(m_parent, "ValidateToken - token is empty");
    return false;
  }

  wxString url =
      wxString::Format("http://%s:%d/plugins/", m_serverHost, m_serverPort);

  wxString response = HttpGet(url, "Authorization: Bearer " + m_authToken);

  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "ValidateToken - empty or failed HTTP response");
    return false;
  }

  SKN_LOG(m_parent, "ValidateToken: response='%s'", response.Left(200));

  wxJSONReader reader;
  wxJSONValue root;

  if (reader.Parse(response, &root) != 0) {
    SKN_LOG(m_parent, "ValidateToken - invalid JSON");
    return false;
  }

  if (!root.IsArray()) {
    SKN_LOG(m_parent, "ValidateToken - JSON is not an array");
    return false;
  }

  SKN_LOG(m_parent, "ValidateToken - token valid");
  return true;
}

static wxString HttpGetAuth(const wxString& url, const wxString& token) {
  return HttpGet(url, "Authorization: Bearer " + token);
}

bool tpSignalKNotesManager::FetchInstalledPlugins(
    std::map<wxString, bool>& plugins) {
  wxString url;
  url.Printf("http://%s:%d/plugins/", m_serverHost, m_serverPort);

  wxString response = HttpGetAuth(url, m_authToken);

  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "Failed to fetch installed plugins");
    return false;
  }

  wxJSONReader reader;
  wxJSONValue root;

  if (reader.Parse(response, &root) > 0) {
    SKN_LOG(m_parent, "Failed to parse plugins JSON");
    return false;
  }

  if (!root.IsArray()) {
    SKN_LOG(m_parent, "Expected array in plugins response");
    return false;
  }

  plugins.clear();
  m_providerDetails.clear();

  for (int i = 0; i < root.Size(); i++) {
    wxJSONValue plugin = root[i];

    if (plugin.HasMember("id")) {
      wxString id = plugin["id"].AsString();

      ProviderDetails details;
      details.id = id;
      details.name = plugin.Get("name", id).AsString();
      details.description = plugin.Get("description", "").AsString();

      bool enabled = false;
      if (plugin.HasMember("data")) {
        wxJSONValue data = plugin["data"];
        if (data.HasMember("enabled")) {
          enabled = data["enabled"].AsBool();
        }
      }

      details.enabled = enabled;
      plugins[id] = enabled;
      m_providerDetails[id] = details;
    }
  }

  return true;
}

void tpSignalKNotesManager::CleanupDisabledProviders() {
  std::map<wxString, bool> installedPlugins;

  if (!FetchInstalledPlugins(installedPlugins)) {
    SKN_LOG(m_parent, "Cannot cleanup providers - plugin fetch failed");
    return;
  }

  std::vector<wxString> providersToRemove;

  for (const auto& providerPair : m_providerSettings) {
    wxString provider = providerPair.first;

    auto it = installedPlugins.find(provider);

    if (it == installedPlugins.end()) {
      SKN_LOG(m_parent, "Removing provider '%s' - not installed", provider);
      providersToRemove.push_back(provider);
    } else if (!it->second) {
      SKN_LOG(m_parent, "Removing provider '%s' - disabled in SignalK",
              provider);
      providersToRemove.push_back(provider);
    }
  }

  for (const wxString& provider : providersToRemove) {
    m_providerSettings.erase(provider);
    m_discoveredProviders.erase(provider);
  }

  if (!providersToRemove.empty() && m_parent) {
    m_parent->SaveConfig();
  }
}

std::vector<tpSignalKNotesManager::ProviderInfo>
tpSignalKNotesManager::GetProviderInfos() const {
  std::vector<ProviderInfo> infos;

  for (const auto& pair : m_providerDetails) {
    ProviderInfo info;
    info.id = pair.second.id;
    info.name = pair.second.name;
    info.description = pair.second.description;
    infos.push_back(info);
  }

  return infos;
}

void tpSignalKNotesManager::SetAuthToken(const wxString& token) {
  m_authToken = token;
  SKN_LOG(m_parent, "SetAuthToken called, token='%s'", token.Left(20));
  if (!token.IsEmpty()) {
    m_authTokenReceivedTime = wxDateTime::Now();
  }
  if (m_parent) {
    m_parent->SaveConfig();
  }
}

void tpSignalKNotesManager::SetAuthRequestHref(const wxString& href) {
  m_authRequestHref = href;
  m_authPending = !href.IsEmpty();

  if (m_parent) {
    m_parent->SaveConfig();
  }
}

void tpSignalKNotesManager::ClearAuthRequest() {
  m_authRequestHref.Clear();
  m_authPending = false;

  if (m_parent) {
    m_parent->SaveConfig();
  }
}
