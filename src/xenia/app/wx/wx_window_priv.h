#ifndef XENIA_APP_WX_WINDOW_PRIV_H_
#define XENIA_APP_WX_WINDOW_PRIV_H_

// Internal native controls of the wxWidgets backend, shared by the common
// (wx_window.cc) and platform (wx_window_win.cc / wx_window_linux.cc)
// implementation files. Not for use outside src/xenia/app/wx/.

#include <wx/arrstr.h>
#include <wx/dnd.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/timer.h>

namespace xe {
namespace app {
namespace wx_ui {

class WxWindow;

// View panel filling the frame client area; presents the guest output and
// forwards native control events to the owning WxWindow.
class WxViewPanel : public wxPanel {
 public:
  WxViewPanel(WxWindow* owner, wxWindow* parent);

  void DetachOwner();
  void StartCursorTimer();
  void StopCursorTimer();

#if defined(__WXMSW__)
  WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam,
                          WXLPARAM lParam) override;
#endif

 private:
  enum : int { kCursorTimerId = wxID_HIGHEST + 1 };

  void OnPaint(wxPaintEvent& event);
  void OnEraseBackground(wxEraseEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnFocus(wxFocusEvent& event);
  void OnKeyDown(wxKeyEvent& event);
  void OnKeyUp(wxKeyEvent& event);
  void OnKeyChar(wxKeyEvent& event);
  void OnMouseDown(wxMouseEvent& event);
  void OnMouseUp(wxMouseEvent& event);
  void OnMouseMove(wxMouseEvent& event);
  void OnMouseWheel(wxMouseEvent& event);
  void OnDpiChanged(wxDPIChangedEvent& event);
  void OnCursorTimer(wxTimerEvent& event);

  WxWindow* owner_;
  wxTimer cursor_timer_;
};

// Top-level frame hosting the menu bar and the view panel.
class WxHostFrame : public wxFrame {
 public:
  WxHostFrame(WxWindow* owner, const std::string& title);

  void DetachOwner();

 private:
  void OnClose(wxCloseEvent& event);
  void OnMenu(wxCommandEvent& event);
  void OnKeyDown(wxKeyEvent& event);
  void OnKeyUp(wxKeyEvent& event);
  void OnKeyChar(wxKeyEvent& event);

  WxWindow* owner_;
};

class WxDropTarget : public wxFileDropTarget {
 public:
  explicit WxDropTarget(WxWindow* owner);

  bool OnDropFiles(wxCoord x, wxCoord y,
                   const wxArrayString& filenames) override;

 private:
  WxWindow* owner_;
};

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_WINDOW_PRIV_H_
