/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Modeless wxWidgets mirror of EmulatorWindow::ContentInstallDialog (which is
// ImGui-based and hidden behind the library view when the library is
// attached). Polls the worker-owned entries on a UI-thread timer.

#include "xenia/app/wx/wx_content_install_dialog.h"

#include <wx/button.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/gauge.h>
#include <wx/hyperlink.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include <algorithm>
#include <cstdio>

#include "xenia/app/wx/wx_util.h"
#include "xenia/app/wx/wx_window.h"
#include "xenia/app/wx/wx_window_priv.h"
#include "xenia/base/filesystem.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

constexpr int kIconPx = 64;
constexpr int kRefreshMs = 150;

bool IsTerminal(Emulator::InstallState state) {
  return state == Emulator::InstallState::installed ||
         state == Emulator::InstallState::failed;
}

std::string StatusText(const Emulator::ContentInstallEntry& entry) {
  std::string result =
      "Status: " +
      std::string(Emulator::installStateStringName[static_cast<uint8_t>(
          entry.installation_state_)]);
  if (entry.installation_state_ == Emulator::InstallState::failed) {
    char code[16];
    std::snprintf(code, sizeof(code), "%08X", entry.installation_result_);
    result += " - " + entry.installation_error_message_ + " (" + code + ")";
  }
  return result;
}

wxBitmap IconBitmap(const std::vector<uint8_t>& bytes) {
  if (bytes.empty()) {
    return wxNullBitmap;
  }
  wxMemoryInputStream stream(bytes.data(), bytes.size());
  wxImage image(stream, wxBITMAP_TYPE_ANY);
  if (!image.IsOk()) {
    return wxNullBitmap;
  }
  if (image.GetWidth() > kIconPx || image.GetHeight() > kIconPx) {
    const double scale = std::min(double(kIconPx) / image.GetWidth(),
                                  double(kIconPx) / image.GetHeight());
    image.Rescale(int(image.GetWidth() * scale), int(image.GetHeight() * scale),
                  wxIMAGE_QUALITY_HIGH);
  }
  return wxBitmap(image);
}

// Backup for packages without a decodable thumbnail: theme-colored tile with
// a "?" so the row keeps its layout instead of collapsing to blank space.
wxBitmap PlaceholderBitmap() {
  wxBitmap bitmap(kIconPx, kIconPx);
  wxMemoryDC dc(bitmap);
  dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
  dc.Clear();
  wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
  font.SetPointSize(font.GetPointSize() * 2);
  font.MakeBold();
  dc.SetFont(font);
  dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  dc.DrawLabel("?", wxRect(0, 0, kIconPx, kIconPx),
               wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
  dc.SelectObject(wxNullBitmap);
  return bitmap;
}

class WxContentInstallDialog : public wxDialog {
 public:
  WxContentInstallDialog(
      wxWindow* parent,
      std::shared_ptr<std::vector<Emulator::ContentInstallEntry>> entries,
      const std::filesystem::path& content_root, bool is_extract)
      : wxDialog(parent, wxID_ANY,
                 is_extract ? "Extraction Progress" : "Installation Progress",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        entries_(std::move(entries)),
        content_root_(content_root) {
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL | wxHSCROLL);
    scrolled->SetScrollRate(0, 10);
    auto* rows = new wxBoxSizer(wxVERTICAL);
    for (size_t i = 0; i < entries_->size(); i++) {
      if (i > 0) {
        rows->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxTOP | wxBOTTOM,
                  6);
      }
      RowWidgets widgets;
      rows->Add(BuildRow(scrolled, (*entries_)[i], widgets), 0, wxEXPAND);
      row_widgets_.push_back(widgets);
    }
    scrolled->SetSizer(rows);
    outer->Add(scrolled, 1, wxEXPAND | wxALL, 8);
    close_button_ = new wxButton(this, wxID_CLOSE, "Close");
    close_button_->Disable();
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(close_button_, 0, wxRIGHT | wxBOTTOM, 8);
    outer->Add(buttons, 0, wxEXPAND);
    SetSizer(outer);
    // wxScrolledWindow reports a tiny best size on its own, so size it from
    // the content instead. Width is capped since the unwrapped path link can
    // be arbitrarily long (horizontal scroll covers the rest).
    const wxSize content = rows->CalcMin();
    const int width =
        content.GetWidth() + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) + 32;
    scrolled->SetMinSize(
        wxSize(std::min(width, 720), std::min(content.GetHeight() + 16, 600)));
    Fit();

    Bind(wxEVT_BUTTON, &WxContentInstallDialog::OnCloseButton, this,
         wxID_CLOSE);
    Bind(wxEVT_CLOSE_WINDOW, &WxContentInstallDialog::OnCloseWindow, this);
    Bind(wxEVT_TIMER, &WxContentInstallDialog::OnPoll, this);
    poll_timer_.SetOwner(this);
    poll_timer_.Start(kRefreshMs);
    RefreshRows();
  }

 private:
  struct RowWidgets {
    wxStaticText* status = nullptr;
    wxGauge* gauge = nullptr;
  };

  wxSizer* BuildRow(wxWindow* parent,
                    const Emulator::ContentInstallEntry& entry,
                    RowWidgets& out) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    wxBitmap icon_art = IconBitmap(entry.icon_bytes_);
    if (!icon_art.IsOk()) {
      icon_art = PlaceholderBitmap();
    }
    auto* icon = new wxStaticBitmap(parent, wxID_ANY, icon_art);
    icon->SetMinSize(wxSize(kIconPx, kIconPx));
    row->Add(icon, 0, wxRIGHT, 8);

    auto* col = new wxBoxSizer(wxVERTICAL);
    auto* name =
        new wxStaticText(parent, wxID_ANY, WxLabel("Name: " + entry.name_));
    name->Wrap(460);
    col->Add(name, 0, wxEXPAND);
    std::string install_path =
        xe::path_to_utf8(content_root_ / entry.data_installation_path_);
    std::string install_url = install_path;
    std::replace(install_url.begin(), install_url.end(), '\\', '/');
    auto* link =
        new wxHyperlinkCtrl(parent, wxID_ANY, WxLabel("Path: " + install_path),
                            WxLabel("file:///" + install_url));
    col->Add(link, 0, wxEXPAND);
    if (entry.content_type_ != xe::XContentType::kInvalid) {
      col->Add(new wxStaticText(
                   parent, wxID_ANY,
                   WxLabel("Content Type: " +
                           xe::XContentTypeMap.at(entry.content_type_))),
               0, wxEXPAND);
    }
    auto* status =
        new wxStaticText(parent, wxID_ANY, WxLabel(StatusText(entry)));
    col->Add(status, 0, wxEXPAND);
    auto* gauge =
        new wxGauge(parent, wxID_ANY, 1000, wxDefaultPosition, wxSize(460, -1));
    col->Add(gauge, 0, wxEXPAND | wxTOP, 4);
    row->Add(col, 1, wxEXPAND);
    out.status = status;
    out.gauge = gauge;
    return row;
  }

  void RefreshRows() {
    bool all_done = true;
    for (size_t i = 0; i < entries_->size(); i++) {
      const auto& entry = (*entries_)[i];
      row_widgets_[i].status->SetLabel(WxLabel(StatusText(entry)));
      int permille = 0;
      if (entry.content_size_ > 0) {
        permille =
            int(1000 * entry.currently_installed_size_ / entry.content_size_);
      }
      row_widgets_[i].gauge->SetValue(permille);
      if (!IsTerminal(entry.installation_state_)) {
        all_done = false;
      }
    }
    Layout();
    if (all_done) {
      poll_timer_.Stop();
      close_button_->Enable();
    }
  }

  void OnPoll(wxTimerEvent&) { RefreshRows(); }
  void OnCloseButton(wxCommandEvent&) { Close(); }
  void OnCloseWindow(wxCloseEvent& event) {
    poll_timer_.Stop();
    event.Skip();  // Default handler destroys this modeless dialog.
  }

  std::shared_ptr<std::vector<Emulator::ContentInstallEntry>> entries_;
  std::filesystem::path content_root_;
  std::vector<RowWidgets> row_widgets_;
  wxButton* close_button_ = nullptr;
  wxTimer poll_timer_;
};

}  // namespace

void ShowContentInstallDialog(
    WxWindow* window,
    std::shared_ptr<std::vector<Emulator::ContentInstallEntry>> entries,
    const std::filesystem::path& content_root, bool is_extract) {
  if (!window || !entries || entries->empty()) {
    return;
  }
  wxWindow* parent = window->view() ? static_cast<wxWindow*>(window->view())
                                    : static_cast<wxWindow*>(window->frame());
  if (!parent) {
    return;
  }
  wxInitAllImageHandlers();
  // Modeless and self-owning: destroys itself on close, like the ImGui
  // dialog's Close(). The worker thread keeps its own shared_ptr.
  auto* dialog = new WxContentInstallDialog(parent, std::move(entries),
                                            content_root, is_extract);
  dialog->Show();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
