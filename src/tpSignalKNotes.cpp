/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Handling of SignalK note objects and data updates
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2026 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/

#include "ocpn_plugin.h"
#include "signalk_notes_opencpn_pi.h"
#include "tpSignalKNotes.h"
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
#include <wx/uri.h>
#include <wx/regex.h>
#include <wx/base64.h>

#include <cstring>
#if defined(wxHAS_WEB_VIEW)
#include <wx/webview.h>
#endif

#ifndef __OCPN__ANDROID__
#include <wx/bmpbndl.h>
#endif

#include "svgRenderer.h"

wxString HttpGet(const wxString& url, const wxString& authHeader = "",
                 long* httpStatusOut = nullptr, wxString* errorOut = nullptr);

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__OCPN__ANDROID__)

#include <curl/curl.h>

static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb,
                                void* userp) {
  size_t total = size * nmemb;
  std::string* s = static_cast<std::string*>(userp);
  s->append(static_cast<char*>(contents), total);
  return total;
}

wxString HttpGet(const wxString& url, const wxString& authHeader,
                 long* httpStatusOut, wxString* errorOut) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "curl_easy_init failed";
    return "";
  }

  std::string response;
  struct curl_slist* headers = NULL;

  if (!authHeader.IsEmpty()) {
    headers = curl_slist_append(headers, authHeader.mb_str().data());
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

  if (httpStatusOut) *httpStatusOut = httpCode;

  if (res != CURLE_OK) {
    if (errorOut)
      *errorOut = wxString::Format("CURL error: %s", curl_easy_strerror(res));
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return "";
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (httpCode != 200) {
    if (errorOut) *errorOut = wxString::Format("HTTP error %ld", httpCode);
    return "";
  }

  return wxString::FromUTF8(response.c_str());
}

#endif

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

wxString HttpGet(const wxString& url, const wxString& authHeader,
                 long* httpStatusOut, wxString* errorOut) {
  URL_COMPONENTS uc = {0};
  uc.dwStructSize = sizeof(uc);

  wchar_t host[256];
  wchar_t path[2048];

  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;

  if (!WinHttpCrackUrl(url.wc_str(), 0, 0, &uc)) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "WinHttpCrackUrl failed";
    return "";
  }

  HINTERNET hSession =
      WinHttpOpen(L"SignalKNotes/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

  if (!hSession) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "WinHttpOpen failed";
    return "";
  }

  HINTERNET hConnect = WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0);
  if (!hConnect) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "WinHttpConnect failed";
    WinHttpCloseHandle(hSession);
    return "";
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);

  if (!hRequest) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "WinHttpOpenRequest failed";
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  if (!authHeader.IsEmpty()) {
    std::wstring hdr = std::wstring(authHeader.wc_str());
    WinHttpAddRequestHeaders(
        hRequest, hdr.c_str(), (DWORD)-1,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  }

  if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "WinHttpSendRequest failed";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "WinHttpReceiveResponse failed";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  DWORD status = 0;
  DWORD size = sizeof(status);

  if (!WinHttpQueryHeaders(
          hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
          WINHTTP_NO_HEADER_INDEX)) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "WinHttpQueryHeaders failed";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  if (httpStatusOut) *httpStatusOut = status;

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

  if (status != 200) {
    if (errorOut) *errorOut = wxString::Format("HTTP error %lu", status);
    return "";
  }

  return wxString::FromUTF8(response.c_str());
}

#endif

#ifdef __OCPN__ANDROID__

wxString HttpGet(const wxString& url, const wxString& authHeader,
                 long* httpStatusOut, wxString* errorOut) {
  wxHTTP http;
  http.SetTimeout(10);

  wxURL wxurl(url);
  if (wxurl.GetError() != wxURL_NOERR) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "wxURL parse error";
    return "";
  }

  wxString host = wxurl.GetServer();
  long port = 80;
  wxurl.GetPort().ToLong(&port);
  wxString path = "/" + wxurl.GetPath();

  if (!authHeader.IsEmpty()) {
    http.SetHeader("Authorization", authHeader.AfterFirst(' ').AfterFirst(' '));
  }

  if (!http.Connect(host, (unsigned short)port)) {
    if (httpStatusOut) *httpStatusOut = -1;
    if (errorOut) *errorOut = "HTTP connect failed";
    return "";
  }

  wxInputStream* in = http.GetInputStream(path);
  if (!in || !in->IsOk()) {
    if (httpStatusOut) *httpStatusOut = http.GetResponse();
    if (errorOut) *errorOut = "HTTP input stream failed";
    delete in;
    return "";
  }

  if (httpStatusOut) *httpStatusOut = http.GetResponse();

  wxString response;
  char buf[4096];
  while (true) {
    in->Read(buf, sizeof(buf));
    size_t read = in->LastRead();
    if (read == 0) break;
    response += wxString::FromUTF8(buf, read);
  }
  delete in;

  if (httpStatusOut && *httpStatusOut != 200) {
    if (errorOut)
      *errorOut = wxString::Format("HTTP error %ld", *httpStatusOut);
    return "";
  }

  return response;
}

#endif

// Helper: strip extension and return base path (without .svg/.png)
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

tpSignalKNotesManager::tpSignalKNotesManager(signalk_notes_opencpn_pi* parent) {
  m_parent = parent;
  m_serverHost = wxEmptyString;
  m_serverPort = 3000;
}

void tpSignalKNotesManager::SetServerDetails(const wxString& host, int port) {
  m_serverHost = host;
  m_serverPort = port;
}

void tpSignalKNotesManager::UpdateDisplayedIcons(
    double centerLat, double centerLon, double maxDistance,
    signalk_notes_opencpn_pi::CanvasState& state) {
  int ok = FetchNotesListForCanvas(centerLat, centerLon, maxDistance, state);
  if (ok < 0) {
    SKN_LOG(m_parent, "Failed to fetch notes");
    return;
  }

  // Resourcesets abrufen - nur wenn Intervall abgelaufen
  wxLongLong now = wxGetLocalTimeMillis();
  bool rsFetchDue = (state.lastRSFetchTime == 0 ||
                     (now - state.lastRSFetchTime).ToLong() >
                         (long)(m_parent->GetFetchInterval() * 60 * 1000));

  if (m_parent && rsFetchDue) {
    std::set<wxString> activeRSNames;

    for (auto& rsKv : m_parent->m_resourceSetConfigs) {
      if (!rsKv.second.enabled) continue;
      activeRSNames.insert(rsKv.first);

      std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>
          discovered;
      FetchResourceSet(rsKv.first, discovered, state, rsKv.second.subSets);

      for (auto& sub : discovered) {
        if (rsKv.second.subSets.find(sub.first) == rsKv.second.subSets.end()) {
          rsKv.second.subSets[sub.first] = sub.second;
        }
      }
    }

    // Cleanup ohne Mutex — separater Lock-Block
    {
      wxMutexLocker lock(state.notesMutex);
      std::vector<wxString> toRemove;
      for (auto& kv : state.resourceSetNotes) {
        wxString rsName = kv.second.source.AfterFirst(':').BeforeLast(':');
        if (activeRSNames.find(rsName) == activeRSNames.end()) {
          toRemove.push_back(kv.first);
        }
      }
      for (auto& guid : toRemove) state.resourceSetNotes.erase(guid);
    }  // ← Lock wird hier freigegeben

    state.lastRSFetchTime = now;
  }

  if (ok == 0) return;

  wxMutexLocker lock(state.notesMutex);  // ← jetzt kein Deadlock mehr

  bool newMappingsFound = false;

  for (auto& pair : state.notes) {
    SignalKNote& note = pair.second;

    if (!note.iconName.IsEmpty()) {
      if (m_iconMappings.find(note.iconName) == m_iconMappings.end()) {
        wxString iconPath = ResolveIconPath(note.iconName);
        m_iconMappings[note.iconName] = iconPath;
        newMappingsFound = true;
      }
    }

    bool providerEnabled = true;
    if (!note.source.IsEmpty()) {
      auto it = m_providerSettings.find(note.source);
      if (it != m_providerSettings.end()) {
        providerEnabled = it->second;
      }
    }

    note.isDisplayed = providerEnabled;
  }

  if (newMappingsFound && m_parent) {
    m_parent->SaveConfig();
  }
}

SignalKNote* tpSignalKNotesManager::GetNoteByGUID(
    signalk_notes_opencpn_pi::CanvasState& state, const wxString& guid) {
  // Normale Notes
  auto it = state.notes.find(guid);
  if (it != state.notes.end()) return &it->second;

  // Resourceset-Notes
  auto rsIt = state.resourceSetNotes.find(guid);
  if (rsIt != state.resourceSetNotes.end()) return &rsIt->second;

  return nullptr;
}

void tpSignalKNotesManager::OnIconClick(
    const wxString& guid, signalk_notes_opencpn_pi::CanvasState& state,
    int canvasIndex) {
  SKN_LOG(m_parent, "OnIconClick called with guid='%s'", guid);
  m_parent->m_dialogOpen = true;

  // --- Gemeinsamer Exit-Block ---
  auto FinishAndReleaseMouse = [&]() -> void {
    wxWindow* canvas = GetCanvasByIndex(canvasIndex);
    if (canvas) {
      wxMouseEvent upEvent(wxEVT_LEFT_UP);
      upEvent.SetPosition(wxGetMousePosition());
      canvas->GetEventHandler()->ProcessEvent(upEvent);
    }
    m_parent->m_dialogOpen = false;
  };

  // ============================================================
  // 1. ResourceSet-Notes
  // ============================================================
  {
    wxMutexLocker lock(state.notesMutex);
    auto rsIt = state.resourceSetNotes.find(guid);
    if (rsIt != state.resourceSetNotes.end()) {
      const SignalKNote& note = rsIt->second;

      wxDialog* dlg = new wxDialog(
          m_parent->GetParentWindow(), wxID_ANY, note.name, wxDefaultPosition,
          wxSize(500, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

      wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

      // Beschreibung
      wxTextCtrl* textCtrl = new wxTextCtrl(
          dlg, wxID_ANY, note.description, wxDefaultPosition, wxDefaultSize,
          wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
      sizer->Add(textCtrl, 1, wxALL | wxEXPAND, 10);

      // Buttons
      wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

      wxButton* centerBtn = new wxButton(dlg, wxID_ANY, _("Center on map"));
      centerBtn->Bind(wxEVT_BUTTON, [this, &note, canvasIndex,
                                     dlg](wxCommandEvent&) {
        wxWindow* canvas = GetCanvasByIndex(canvasIndex);
        double scale = 0.0;
        if (canvas) {
          scale = m_parent->m_canvasStates[canvasIndex].viewPort.view_scale_ppm;
        }
        dlg->EndModal(wxID_OK);
        if (canvas) {
          CanvasJumpToPosition(canvas, note.latitude, note.longitude, scale);
        }
      });
      btnSizer->Add(centerBtn, 0, wxALL, 5);

      btnSizer->AddStretchSpacer();

      wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
      btnSizer->Add(okBtn, 0, wxALL, 5);

      sizer->Add(btnSizer, 0, wxALL | wxEXPAND, 5);

      dlg->SetSizer(sizer);
      dlg->ShowModal();
      dlg->Destroy();

      FinishAndReleaseMouse();
      return;
    }
  }

  // ============================================================
  // 2. Normale Notes
  // ============================================================
  SignalKNote* note = GetNoteByGUID(state, guid);
  if (!note) {
    SKN_LOG(m_parent, "Note with guid='%s' not found!", guid);
    FinishAndReleaseMouse();
    return;
  }

  // Details ggf. nachladen
  if (note->name.IsEmpty() || note->description.IsEmpty()) {
    if (!FetchNoteDetails(note->id, *note)) {
      SKN_LOG(m_parent, "Failed to fetch details for %s", note->id);
      if (note->name.IsEmpty()) note->name = note->id;
      if (note->description.IsEmpty())
        note->description = _("Details could not be loaded.");

      // Fallback-Dialog
      wxDialog* dlg = new wxDialog(
          m_parent->GetParentWindow(), wxID_ANY, note->name, wxDefaultPosition,
          wxSize(500, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

      wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

      wxTextCtrl* textCtrl = new wxTextCtrl(
          dlg, wxID_ANY, note->description, wxDefaultPosition, wxDefaultSize,
          wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
      sizer->Add(textCtrl, 1, wxALL | wxEXPAND, 10);

      wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

      wxButton* centerBtn = new wxButton(dlg, wxID_ANY, _("Center on map"));
      centerBtn->Bind(wxEVT_BUTTON, [this, note, canvasIndex,
                                     dlg](wxCommandEvent&) {
        wxWindow* canvas = GetCanvasByIndex(canvasIndex);
        double scale = 0.0;
        if (canvas) {
          scale = m_parent->m_canvasStates[canvasIndex].viewPort.view_scale_ppm;
        }
        dlg->EndModal(wxID_OK);
        if (canvas) {
          CanvasJumpToPosition(canvas, note->latitude, note->longitude, scale);
        }
      });
      btnSizer->Add(centerBtn, 0, wxALL, 5);

      btnSizer->AddStretchSpacer();

      wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
      btnSizer->Add(okBtn, 0, wxALL, 5);

      sizer->Add(btnSizer, 0, wxALL | wxEXPAND, 5);

      dlg->SetSizer(sizer);
      dlg->ShowModal();
      dlg->Destroy();

      FinishAndReleaseMouse();
      return;
    }
  }

  // ============================================================
  // 3. Vollständiger HTML-Dialog
  // ============================================================
  SKN_LOG(m_parent, "Found note '%s'", note->name);

  int totalWidth = 0, totalHeight = 0;
  for (const auto& pair : m_parent->m_canvasStates) {
    if (pair.second.valid) {
      totalWidth = std::max(totalWidth, pair.second.viewPort.pix_width);
      totalHeight = std::max(totalHeight, pair.second.viewPort.pix_height);
    }
  }

  int dlgWidth = std::max(400, (int)std::round(totalWidth * 0.67));
  int dlgHeight = std::max(300, (int)std::round(totalHeight * 0.67));

  wxDialog* dlg = new wxDialog(m_parent->GetParentWindow(), wxID_ANY,
                               _("SignalK Note Details"), wxDefaultPosition,
                               wxSize(dlgWidth, dlgHeight),
                               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  dlg->CenterOnScreen();

  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText* title = new wxStaticText(dlg, wxID_ANY, note->name);
  wxFont font = title->GetFont();
  font.SetPointSize(font.GetPointSize() + 2);
  font.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(font);
  sizer->Add(title, 0, wxALL | wxEXPAND, 10);

  wxString htmlContent = PrepareHTMLContent(note->description, note->url);

#if defined(__WXMSW__) || defined(__WXMAC__)
  if (!RenderWithWebView(dlg, sizer, htmlContent))
    RenderWithHtmlWindow(dlg, sizer, htmlContent);
#elif defined(__OCPN__ANDROID__)
  RenderWithHtmlWindow(dlg, sizer, htmlContent);
#else
  if (!RenderWithWebView(dlg, sizer, htmlContent))
    RenderWithHtmlWindow(dlg, sizer, htmlContent);
#endif

  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

  wxButton* centerBtn = new wxButton(dlg, wxID_ANY, _("Center on map"));
  centerBtn->Bind(
      wxEVT_BUTTON, [this, note, canvasIndex, dlg](wxCommandEvent&) {
        wxWindow* canvas = GetCanvasByIndex(canvasIndex);
        double scale = 0.0;
        if (canvas) {
          scale = m_parent->m_canvasStates[canvasIndex].viewPort.view_scale_ppm;
        }
        dlg->EndModal(wxID_OK);
        if (canvas) {
          CanvasJumpToPosition(canvas, note->latitude, note->longitude, scale);
        }
      });
  btnSizer->Add(centerBtn, 0, wxALL, 5);

  btnSizer->AddStretchSpacer();

  wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
  btnSizer->Add(okBtn, 0, wxALL, 5);

  sizer->Add(btnSizer, 0, wxALL | wxEXPAND, 5);

  dlg->SetSizer(sizer);
  dlg->ShowModal();
  dlg->Destroy();

  FinishAndReleaseMouse();
}

wxString tpSignalKNotesManager::ProcessDataUrls(const wxString& htmlIn) {
  wxString html = htmlIn;

  wxRegEx reDataSvg("data:image/svg\\+xml;base64,([A-Za-z0-9+/=]+)",
                    wxRE_EXTENDED | wxRE_ICASE);

  if (!reDataSvg.IsValid()) {
    SKN_LOG(m_parent, "ProcessDataUrls: Regex invalid");
    return html;
  }

  size_t searchPos = 0;
  int index = 0;

  while (reDataSvg.Matches(html, searchPos)) {
    wxString fullMatch = reDataSvg.GetMatch(html, 0);
    wxString base64Part = reDataSvg.GetMatch(html, 1);

    // Base64 → MemoryBuffer
    wxMemoryBuffer decoded = wxBase64Decode(
        base64Part.mb_str(wxConvUTF8).data(), base64Part.length());

    if (decoded.GetDataLen() == 0) {
      SKN_LOG(m_parent, "ProcessDataUrls: Base64 decode failed");
      searchPos += fullMatch.length();
      continue;
    }

    wxString svgXml(static_cast<const char*>(decoded.GetData()), wxConvUTF8,
                    decoded.GetDataLen());

    // PNG-Pfad erzeugen
    wxString fileName = wxString::Format("note_svg_%d.png", index++);
    wxFileName fn(m_parent->m_pluginDataDir, fileName);
    wxString fullPath = fn.GetFullPath();

    // SVG → PNG
    SvgRenderer renderer;
    if (!renderer.FromSvgXmlToPng(svgXml, fullPath, 900, 400)) {
      SKN_LOG(m_parent, "ProcessDataUrls: SvgRenderer failed");
      searchPos += fullMatch.length();
      continue;
    }

    wxString fileUrl = "file://" + fullPath;

    size_t start = html.find(fullMatch, searchPos);
    if (start == wxString::npos) {
      break;
    }
    size_t end = start + fullMatch.length();

    html = html.Left(start) + fileUrl + html.Mid(end);

    searchPos = start + fileUrl.length();
  }

  return html;
}

// Helper: HTML-Content vorbereiten
wxString tpSignalKNotesManager::PrepareHTMLContent(const wxString& description,
                                                   const wxString& url) {
  wxString htmlContent;
  wxString safeDescription = FixBrokenLinksInDescription(description);
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
      << "</head><body>" << safeDescription;

  if (!url.IsEmpty()) {
    wxURI uri(url);
    wxString safeUrl = uri.BuildURI();
    htmlContent << "<hr>"
                << "<b>Link:</b> "
                << "<a href=\"" << safeUrl << "\" target=\"_blank\">" << safeUrl
                << "</a>";
  }

  htmlContent << "<hr>"
              << "<small style=\"color: #666;\">"
              << "<i>Do not use for navigation.</i>"
              << "</small>"
              << "</body></html>";

  return ProcessDataUrls(htmlContent);
}

// Helper: Rendering mit wxWebView (Windows, macOS, Linux optional)
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

// Helper: Rendering mit wxHtmlWindow (Fallback, Android)
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

int tpSignalKNotesManager::FetchNotesListForCanvas(
    double centerLat, double centerLon, double maxDistance,
    signalk_notes_opencpn_pi::CanvasState& state) {
  if (m_serverHost.IsEmpty()) {
    SKN_LOG(m_parent, _("Server host not configured"));
    return -2;
  }

  wxString path;
  path.Printf(
      "http://%s:%d/signalk/v2/api/resources/"
      "notes?position=[%f,%f]&distance=%.0f",
      m_serverHost.c_str(), m_serverPort, centerLon, centerLat, maxDistance);

  long status = 0;
  wxString err;

  wxString response = HttpGet(path, "", &status, &err);

  if (response.IsEmpty() || status != 200) {
    wxString shortResp = response.Left(200);
    if (response.Length() > 200) shortResp += "...";

    SKN_LOG(m_parent,
            wxString::Format("FetchNotesList FAILED — status=%ld error=\"%s\" "
                             "url=%s host=%s port=%d response=\"%s\"",
                             status, err, path, m_serverHost.c_str(),
                             m_serverPort, shortResp));

    return -1;
  }

  int ok = ParseNotesListJSON(response, state);

  if (ok == -1) {
    SKN_LOG(
        m_parent,
        wxString::Format(
            "FetchNotesList FAILED — JSON parse error url=%s host=%s port=%d",
            path, m_serverHost.c_str(), m_serverPort));
  }

  return ok;
}

bool tpSignalKNotesManager::FetchNoteDetails(const wxString& noteId,
                                             SignalKNote& note) {
  wxString path;
  path.Printf("http://%s:%d/signalk/v2/api/resources/notes/%s",
              m_serverHost.c_str(), m_serverPort, noteId);

  long status = 0;
  wxString err;

  wxString response = HttpGet(path, "", &status, &err);

  if (response.IsEmpty() || status != 200) {
    // Response kürzen, damit Logs nicht explodieren
    wxString shortResp = response.Left(200);
    if (response.Length() > 200) shortResp += "...";

    SKN_LOG(m_parent, wxString::Format(
                          "FetchNoteDetails FAILED — status=%ld error=\"%s\" "
                          "url=%s host=%s port=%d response=\"%s\"",
                          status, err, path, m_serverHost.c_str(), m_serverPort,
                          shortResp));

    return false;
  }

  return ParseNoteDetailsJSON(response, note);
}

int tpSignalKNotesManager::ParseNotesListJSON(
    const wxString& json, signalk_notes_opencpn_pi::CanvasState& state) {
  wxJSONReader reader;
  wxJSONValue root;

  int errors = reader.Parse(json, &root);
  if (errors > 0) {
    SKN_LOG(m_parent, _("Failed to parse notes list JSON"));
    return -1;
  }

  // Erstelle temporäre Map für neue Notes
  std::map<wxString, SignalKNote> newNotes;
  wxArrayString memberNames = root.GetMemberNames();

  // Parse alle Notes aus JSON
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

    newNotes[noteId] = note;
  }

  // Vergleiche und Update von state.notes
  {
    wxMutexLocker lock(state.notesMutex);

    // Übernehme isDisplayed Status von bestehenden Notes
    for (auto& newPair : newNotes) {
      auto existingIt = state.notes.find(newPair.first);
      if (existingIt != state.notes.end()) {
        newPair.second.isDisplayed = existingIt->second.isDisplayed;
        newPair.second.GUID = existingIt->second.GUID;
      } else {
        newPair.second.isDisplayed = false;
      }
    }

    // Vergleiche: Haben sich die Notes wirklich geändert?
    if (newNotes.size() != state.notes.size()) {
      SKN_LOG(m_parent, "Notes changed: count %zu -> %zu", state.notes.size(),
              newNotes.size());
      state.notes = std::move(newNotes);
      return 1;
    }

    // Gleiche Anzahl - prüfe ob Inhalte identisch sind
    bool hasChanges = false;
    for (const auto& newPair : newNotes) {
      const wxString& id = newPair.first;
      const SignalKNote& newNote = newPair.second;

      auto oldIt = state.notes.find(id);
      if (oldIt == state.notes.end()) {
        // Neue Note
        hasChanges = true;
        break;
      }

      const SignalKNote& oldNote = oldIt->second;

      // Feldweiser Vergleich (außer isDisplayed und GUID)
      if (newNote.name != oldNote.name ||
          newNote.description != oldNote.description ||
          newNote.latitude != oldNote.latitude ||
          newNote.longitude != oldNote.longitude ||
          newNote.iconName != oldNote.iconName || newNote.url != oldNote.url ||
          newNote.source != oldNote.source) {
        hasChanges = true;
        break;
      }
    }

    if (hasChanges) {
      SKN_LOG(m_parent, "Notes changed: content modified");
      state.notes = std::move(newNotes);
      return 1;
    } else {
      SKN_LOG(m_parent, "Notes unchanged - keeping existing data");
      // Keine Änderungen - behalte state.notes wie es ist
      return 0;
    }
  }  // Mutex wird hier automatisch freigegeben
  return 0;
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
    signalk_notes_opencpn_pi::CanvasState& state,
    std::vector<const SignalKNote*>& outNotes) {
  wxMutexLocker lock(state.notesMutex);

  // Normale Notes (bereits per isDisplayed gefiltert)
  for (auto& kv : state.notes) {
    if (kv.second.isDisplayed) outNotes.push_back(&kv.second);
  }

  // Resourceset-Notes: Viewport-Check über lat/lon Grenzen
  if (!state.valid) return;
  const PlugIn_ViewPort& vp = state.viewPort;
  for (auto& kv : state.resourceSetNotes) {
    const SignalKNote& note = kv.second;
    if (note.latitude >= vp.lat_min && note.latitude <= vp.lat_max &&
        note.longitude >= vp.lon_min && note.longitude <= vp.lon_max) {
      outNotes.push_back(&note);
    }
  }
}

bool tpSignalKNotesManager::GetIconBitmapForNote(const SignalKNote& note,
                                                 wxBitmap& bmp, bool forGL) {
  wxString skIcon = note.iconName;

  // 0. Cache-Check im PLUGIN
  if (m_parent->GetCachedIconBitmap(skIcon, bmp, forGL)) {
    return true;
  }

  // 1. Mapping aus Config?
  auto it = m_iconMappings.find(skIcon);
  if (it != m_iconMappings.end()) {
    wxString mappedPath = it->second;
    if (wxFileExists(mappedPath)) {
      wxString base = GetBasePathWithoutExt(mappedPath);
      wxBitmap raw;
      if (LoadIconSmart(base, 24, raw) && raw.IsOk()) {
        m_parent->CacheIconBitmap(skIcon, raw, forGL, bmp);  // Cache im Plugin
        return true;
      }
    }
  }

  // 2. Fallback: Plugin-Icon-Verzeichnis (skIcon.*)
  wxString basePluginIcon = m_parent->GetPluginIconDir() + skIcon;
  wxBitmap raw2;
  if (LoadIconSmart(basePluginIcon, 24, raw2) && raw2.IsOk()) {
    if (forGL) {
      bmp = m_parent->PrepareIconBitmapForGL(raw2, 24);
    } else {
      bmp = raw2;
    }
    return true;
  }

  // 3. Letzter Fallback: notice-to-mariners.*
  wxString baseFallback = m_parent->GetPluginIconDir() + "notice-to-mariners";
  wxBitmap raw3;
  if (LoadIconSmart(baseFallback, 24, raw3) && raw3.IsOk()) {
    if (forGL) {
      bmp = m_parent->PrepareIconBitmapForGL(raw3, 24);
    } else {
      bmp = raw3;
    }
    return true;
  }

  return false;
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
  fn.SetPath(m_parent->m_pluginDataDir);
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
            wxString::Format("SignalK Notes Auth: Connection failed "
                             "(RequestAuthorization) — host=%s port=%d",
                             m_serverHost, m_serverPort));
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
    SetAuthToken(access["token"].AsString());
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
    SKN_LOG(m_parent,
            wxString::Format("ValidateToken - empty or failed HTTP response — "
                             "url=%s host=%s port=%d",
                             url, m_serverHost.c_str(), m_serverPort));
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

wxString tpSignalKNotesManager::FixBrokenLinksInDescription(
    const wxString& html) {
  wxString fixed = html;

  wxRegEx reHrefNoQuotes("<a[ ]+href=([^\"'> ]+)([^>]*)>", wxRE_ICASE);

  while (reHrefNoQuotes.Matches(fixed)) {
    wxString fullMatch = reHrefNoQuotes.GetMatch(fixed, 0);
    wxString url = reHrefNoQuotes.GetMatch(fixed, 1);
    wxString rest = reHrefNoQuotes.GetMatch(fixed, 2);

    wxURI uri(url);
    wxString safeUrl = uri.BuildURI();

    wxString replacement;
    replacement << "<a href=\"" << safeUrl << "\"" << rest << ">";

    fixed.Replace(fullMatch, replacement);
  }

  wxRegEx reMissingClose("(<a[^>]+)(?<!>)$", wxRE_ICASE);
  if (reMissingClose.Matches(fixed)) {
    wxString fullMatch = reMissingClose.GetMatch(fixed, 1);
    fixed.Replace(fullMatch, fullMatch + ">");
  }

  return fixed;
}

void tpSignalKNotesManager::InvalidateIconCache(const wxString& iconName) {
  m_parent->InvalidateBmpIconCache();
  SKN_LOG(m_parent, "Icon cache invalidated for: %s", iconName);
}

// Ignorierte Standard-Resourceset-Typen
static const std::set<wxString> s_ignoredResourceSets = {
    "charts",     "routes", "notes",  "regions",
    "infolayers", "groups", "tracks", "waypoints"};

bool tpSignalKNotesManager::FetchAvailableResourceSets(
    std::set<wxString>& outResourceSets) {
  outResourceSets.clear();

  wxHTTP http;
  http.SetHeader("Content-type", "application/json");
  http.SetTimeout(10);

  if (!http.Connect(m_serverHost, m_serverPort)) {
    SKN_LOG(m_parent,
            "FetchAvailableResourceSets: Verbindung fehlgeschlagen zu %s:%d",
            m_serverHost, m_serverPort);
    return false;
  }

  wxString path = "/signalk/v2/api/resources";
  wxInputStream* stream = http.GetInputStream(path);
  if (!stream || http.GetError() != wxPROTO_NOERR) {
    wxDELETE(stream);
    return false;
  }

  wxStringOutputStream out;
  stream->Read(out);
  wxDELETE(stream);

  wxString json = out.GetString();
  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(json, &root) != 0) return false;

  wxArrayString keys = root.GetMemberNames();
  for (size_t i = 0; i < keys.GetCount(); i++) {
    wxString key = keys[i];
    if (s_ignoredResourceSets.find(key) == s_ignoredResourceSets.end()) {
      outResourceSets.insert(key);
      SKN_LOG(m_parent, "FetchAvailableResourceSets: Gefunden: %s", key);
    }
  }

  return !outResourceSets.empty();
}

bool tpSignalKNotesManager::IsValidResourceSet(wxJSONValue rsJson) {
  if (!rsJson.HasMember("type")) return false;
  if (rsJson["type"].AsString() != "ResourceSet") return false;
  if (!rsJson.HasMember("values")) return false;
  if (!rsJson["values"].HasMember("features")) return false;
  if (!rsJson["values"]["features"].IsArray()) return false;

  wxJSONValue features = rsJson["values"]["features"];
  for (int i = 0; i < features.Size(); i++) {
    wxJSONValue f = features[i];
    if (!f.HasMember("geometry")) continue;
    if (!f["geometry"].HasMember("type")) continue;
    if (f["geometry"]["type"].AsString() != "Point") continue;
    if (!f["geometry"].HasMember("coordinates")) continue;
    if (!f["geometry"]["coordinates"].IsArray()) continue;
    if (f["geometry"]["coordinates"].Size() < 2) continue;
    if (!f.HasMember("properties")) continue;
    if (!f["properties"].HasMember("name")) continue;
    return true;
  }
  return false;
}

int tpSignalKNotesManager::ParseResourceSetJSON(
    const wxString& json, const wxString& resourceSetName,
    signalk_notes_opencpn_pi::CanvasState& state,
    const std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>&
        configuredSubs,
    std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>&
        outDiscoveredSubs) {
  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(json, &root) != 0) {
    SKN_LOG(m_parent, "ParseResourceSetJSON: JSON-Fehler für %s",
            resourceSetName);
    return 0;
  }

  // Neue Notes aus diesem Abruf sammeln
  std::map<wxString, SignalKNote> newNotes;

  wxArrayString uuids = root.GetMemberNames();
  for (size_t i = 0; i < uuids.GetCount(); i++) {
    wxString uuid = uuids[i];
    wxJSONValue rsEntry = root[uuid];

    if (!IsValidResourceSet(rsEntry)) {
      SKN_LOG(m_parent,
              "ParseResourceSetJSON: Überspringe ungültiges Unter-RS %s", uuid);
      continue;
    }

    wxString subName = rsEntry["name"].AsString();

    // Unter-Resourceset als entdeckt markieren (für Config-Dialog)
    if (outDiscoveredSubs.find(subName) == outDiscoveredSubs.end()) {
      signalk_notes_opencpn_pi::SubResourceSetConfig cfg;
      cfg.name = subName;
      cfg.enabled = false;
      // Übernehme ggf. vorhandene Konfiguration
      auto it = configuredSubs.find(subName);
      if (it != configuredSubs.end()) {
        cfg.enabled = it->second.enabled;
        cfg.iconName = it->second.iconName;
      }
      outDiscoveredSubs[subName] = cfg;
    }

    // Prüfe ob dieses Unter-Resourceset aktiviert ist
    auto cfgIt = configuredSubs.find(subName);
    bool subEnabled = (cfgIt != configuredSubs.end() && cfgIt->second.enabled);
    wxString iconName =
        (cfgIt != configuredSubs.end()) ? cfgIt->second.iconName : wxString("");

    if (!subEnabled) continue;

    wxJSONValue features = rsEntry["values"]["features"];
    for (int j = 0; j < features.Size(); j++) {
      wxJSONValue feat = features[j];
      if (!feat.HasMember("geometry") || !feat.HasMember("properties"))
        continue;

      wxJSONValue geom = feat["geometry"];
      wxJSONValue props = feat["properties"];

      if (geom["type"].AsString() != "Point") continue;
      wxJSONValue coords = geom["coordinates"];
      if (coords.Size() < 2) continue;

      double lon = coords[0].AsDouble();
      double lat = coords[1].AsDouble();
      wxString name =
          props.HasMember("name") ? props["name"].AsString() : _("Unknown");
      wxString desc = props.HasMember("description")
                          ? props["description"].AsString()
                          : wxString("");

      // GUID: stabil und eindeutig pro Eintrag
      wxString guid =
          wxString::Format("RS_%s_%s_%d", resourceSetName, subName, j);

      SignalKNote note;
      note.GUID = guid;
      note.id = guid;
      note.name = name;
      note.description = desc;
      note.latitude = lat;
      note.longitude = lon;
      note.iconName = iconName;
      note.source =
          wxString::Format("resourceset:%s:%s", resourceSetName, subName);
      note.isDisplayed = true;

      newNotes[guid] = note;
    }
  }

  // Änderungscheck: Vergleiche mit bisherigen resourceSetNotes
  wxMutexLocker lock(state.notesMutex);

  // Alte RS-Notes für dieses resourceSet entfernen - NUR wenn newNotes gefüllt
  // ODER wenn das Resourceset explizit deaktiviert wurde
  std::vector<wxString> toRemove;
  for (auto& kv : state.resourceSetNotes) {
    if (kv.second.source.StartsWith(
            wxString::Format("resourceset:%s:", resourceSetName))) {
      toRemove.push_back(kv.first);
    }
  }

  // Nur löschen wenn neue Notes vorhanden ODER wenn bewusst alle deaktiviert
  if (!newNotes.empty() || configuredSubs.empty()) {
    for (auto& guid : toRemove) state.resourceSetNotes.erase(guid);
    for (auto& kv : newNotes) state.resourceSetNotes[kv.first] = kv.second;
  }
  // Wenn newNotes leer UND configuredSubs nicht leer: Fetch hat wahrscheinlich
  // gefehlt → alte Notes behalten

  bool changed = (toRemove.size() != newNotes.size());
  if (!changed) {
    for (auto& kv : newNotes) {
      if (state.resourceSetNotes.find(kv.first) ==
          state.resourceSetNotes.end()) {
        changed = true;
        break;
      }
    }
  }

  int count = (int)newNotes.size();
  SKN_LOG(m_parent, "ParseResourceSetJSON: %s → %d Notes geladen (changed=%d)",
          resourceSetName, count, (int)changed);
  return count;
}

static wxString EncodeResourceSetName(const wxString& name) {
  wxString encoded = name;
  encoded.Replace("ä", "%C3%A4");
  encoded.Replace("ö", "%C3%B6");
  encoded.Replace("ü", "%C3%BC");
  encoded.Replace("Ä", "%C3%84");
  encoded.Replace("Ö", "%C3%96");
  encoded.Replace("Ü", "%C3%9C");
  encoded.Replace("ß", "%C3%9F");
  encoded.Replace(" ", "%20");
  encoded.Replace("é", "%C3%A9");
  encoded.Replace("è", "%C3%A8");
  encoded.Replace("à", "%C3%A0");
  return encoded;
}

// Prüft ob ein JSON-Root ein "flaches" Resourceset ist (UUID → einzelne Notes)
// Rückgabe: true wenn mindestens ein Eintrag feature.geometry.type == "Point"
// hat
static bool IsFlatResourceSet(wxJSONValue root) {
  if (!root.IsObject()) return false;
  wxArrayString keys = root.GetMemberNames();
  for (size_t i = 0; i < keys.GetCount(); i++) {
    wxJSONValue entry = root[keys[i]];
    if (!entry.IsObject()) continue;
    if (!entry.HasMember("feature")) continue;
    wxJSONValue feat = entry["feature"];
    if (!feat.HasMember("geometry")) continue;
    if (!feat["geometry"].HasMember("type")) continue;
    if (feat["geometry"]["type"].AsString() != "Point") continue;
    if (!feat["geometry"].HasMember("coordinates")) continue;
    if (!feat["geometry"]["coordinates"].IsArray()) continue;
    if (feat["geometry"]["coordinates"].Size() < 2) continue;
    return true;
  }
  return false;
}

bool tpSignalKNotesManager::FetchResourceSet(
    const wxString& resourceSetName,
    std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>&
        outDiscoveredSubs,
    signalk_notes_opencpn_pi::CanvasState& state,
    const std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>&
        configuredSubs) {
  wxString url =
      wxString::Format("http://%s:%d/signalk/v2/api/resources/%s", m_serverHost,
                       m_serverPort, EncodeResourceSetName(resourceSetName));
  wxString authHeader;
  if (!m_authToken.IsEmpty())
    authHeader = "Authorization: Bearer " + m_authToken;

  wxString json = HttpGet(url, authHeader);
  if (json.IsEmpty()) {
    SKN_LOG(m_parent, "FetchResourceSet: Kein Ergebnis für %s",
            resourceSetName);
    return false;
  }

  // Struktur erkennen: hierarchisch (values.features) oder flach (UUID → Note)?
  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(json, &root) != 0) return false;

  if (IsFlatResourceSet(root)) {
    // Flaches Resourceset: das gesamte RS ist ein einzelnes "Unter-RS"
    // Eintrag mit dem RS-Namen selbst als Sub-Name anlegen
    wxString subName = resourceSetName;
    if (outDiscoveredSubs.find(subName) == outDiscoveredSubs.end()) {
      signalk_notes_opencpn_pi::SubResourceSetConfig cfg;
      cfg.name = subName;
      cfg.enabled = false;
      auto it = configuredSubs.find(subName);
      if (it != configuredSubs.end()) {
        cfg.enabled = it->second.enabled;
        cfg.iconName = it->second.iconName;
      }
      outDiscoveredSubs[subName] = cfg;
    }

    // Nur laden wenn aktiviert
    auto cfgIt = configuredSubs.find(subName);
    if (cfgIt != configuredSubs.end() && cfgIt->second.enabled) {
      ParseFlatResourceSetJSON(json, resourceSetName, state, cfgIt->second);
    }
  } else {
    // Hierarchisches Resourceset: normale Verarbeitung
    ParseResourceSetJSON(json, resourceSetName, state, configuredSubs,
                         outDiscoveredSubs);
  }

  return true;
}

bool tpSignalKNotesManager::DiscoverSubResourceSets(
    const wxString& resourceSetName,
    std::map<wxString, signalk_notes_opencpn_pi::SubResourceSetConfig>&
        outSubs) {
  wxString url =
      wxString::Format("http://%s:%d/signalk/v2/api/resources/%s", m_serverHost,
                       m_serverPort, EncodeResourceSetName(resourceSetName));
  wxString authHeader;
  if (!m_authToken.IsEmpty())
    authHeader = "Authorization: Bearer " + m_authToken;

  wxString json = HttpGet(url, authHeader);
  if (json.IsEmpty()) return false;

  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(json, &root) != 0) return false;

  if (IsFlatResourceSet(root)) {
    // Flaches RS: nur ein Eintrag mit dem RS-Namen selbst
    if (outSubs.find(resourceSetName) == outSubs.end()) {
      signalk_notes_opencpn_pi::SubResourceSetConfig cfg;
      cfg.name = resourceSetName;
      cfg.enabled = false;
      outSubs[resourceSetName] = cfg;
    }
  } else {
    // Hierarchisches RS: Unter-RS-Namen sammeln
    wxArrayString uuids = root.GetMemberNames();
    for (size_t i = 0; i < uuids.GetCount(); i++) {
      wxJSONValue entry = root[uuids[i]];
      if (!IsValidResourceSet(entry)) continue;
      wxString subName = entry["name"].AsString();
      if (outSubs.find(subName) == outSubs.end()) {
        signalk_notes_opencpn_pi::SubResourceSetConfig cfg;
        cfg.name = subName;
        cfg.enabled = false;
        outSubs[subName] = cfg;
      }
    }
  }

  return !outSubs.empty();
}

int tpSignalKNotesManager::ParseFlatResourceSetJSON(
    const wxString& json, const wxString& resourceSetName,
    signalk_notes_opencpn_pi::CanvasState& state,
    const signalk_notes_opencpn_pi::SubResourceSetConfig& config) {
  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(json, &root) != 0) return 0;

  std::map<wxString, SignalKNote> newNotes;

  wxArrayString uuids = root.GetMemberNames();
  for (size_t i = 0; i < uuids.GetCount(); i++) {
    wxJSONValue entry = root[uuids[i]];
    if (!entry.IsObject()) continue;
    if (!entry.HasMember("feature")) continue;

    wxJSONValue feat = entry["feature"];
    if (!feat.HasMember("geometry") || !feat.HasMember("properties")) continue;

    wxJSONValue geom = feat["geometry"];
    if (geom["type"].AsString() != "Point") continue;
    wxJSONValue coords = geom["coordinates"];
    if (coords.Size() < 2) continue;

    double lon = coords[0].AsDouble();
    double lat = coords[1].AsDouble();

    wxJSONValue props = feat["properties"];
    wxString name = props.HasMember("name")   ? props["name"].AsString()
                    : entry.HasMember("name") ? entry["name"].AsString()
                                              : _("Unknown");
    wxString desc =
        entry.HasMember("description")   ? entry["description"].AsString()
        : props.HasMember("description") ? props["description"].AsString()
                                         : wxString("");

    wxString guid = wxString::Format("RSF_%s_%s", resourceSetName, uuids[i]);

    SignalKNote note;
    note.GUID = guid;
    note.id = guid;
    note.name = name;
    note.description = desc;
    note.latitude = lat;
    note.longitude = lon;
    note.iconName = config.iconName;
    note.source =
        wxString::Format("resourceset:%s:%s", resourceSetName, resourceSetName);
    note.isDisplayed = true;

    newNotes[guid] = note;
  }

  // Änderungscheck und Speichern (analog zu ParseResourceSetJSON)
  wxMutexLocker lock(state.notesMutex);

  std::vector<wxString> toRemove;
  for (auto& kv : state.resourceSetNotes) {
    if (kv.second.source ==
        wxString::Format("resourceset:%s:%s", resourceSetName, resourceSetName))
      toRemove.push_back(kv.first);
  }

  // Nur löschen+ersetzen wenn tatsächlich Daten geladen wurden
  if (!newNotes.empty()) {
    for (auto& guid : toRemove) state.resourceSetNotes.erase(guid);
    for (auto& kv : newNotes) state.resourceSetNotes[kv.first] = kv.second;
  }

  SKN_LOG(m_parent, "ParseFlatResourceSetJSON: %s → %d Notes", resourceSetName,
          (int)newNotes.size());
  return (int)newNotes.size();
}
