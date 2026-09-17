#ifndef XENIA_APP_WX_WINDOW_H_
#define XENIA_APP_WX_WINDOW_H_

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/app/wx/wx_library_view.h"
#include "xenia/base/platform.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"

#if XE_PLATFORM_WIN32
// Must be included before Windows headers for things like NOMINMAX.
#include "xenia/base/platform_win.h"
#endif

#include <wx/arrstr.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/menu.h>
#include <wx/simplebook.h>

namespace xe {
namespace app {
namespace wx_ui {

// Forward declarations for the native controls owned by WxWindow (defined in
// wx_window.cc).
class WxViewPanel;
class WxHostFrame;

class WxMenuItem : public ui::MenuItem {
 public:
  WxMenuItem(Type type, const std::string& text, const std::string& hotkey,
             std::function<void()> callback);

  void SetEnabled(bool enabled) override { enabled_ = enabled; }
  bool enabled() const { return enabled_; }
  const std::vector<WxMenuItem*>& wx_children() const { return wx_children_; }

  using MenuItem::OnSelected;

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  bool enabled_ = true;
  std::vector<WxMenuItem*> wx_children_;
};

// wxWidgets backend for xe::ui::Window, mirroring Win32Window/GTKWindow.
// Platform-specific behavior lives in wx_window_win.cc / wx_window_linux.cc;
// this file and wx_window.cc hold the shared plumbing.
class WxWindow : public ui::Window, public WxLibraryView::Delegate {
  using super = ui::Window;

 public:
  WxWindow(ui::WindowedAppContext& app_context, const std::string_view title,
           uint32_t desired_logical_width, uint32_t desired_logical_height);
  ~WxWindow() override;

#if XE_PLATFORM_WIN32
  void* GetNativeHandle() const override { return view_hwnd_; }
#endif
  WxHostFrame* frame() const { return frame_; }
  WxViewPanel* view() const { return view_; }

  uint32_t GetMediumDpi() const override;

  // Game library. AttachLibrary builds a book (library page + game view page)
  // once the frame exists; all other calls are no-ops until attached.
  using LibraryBootCallback = std::function<void(
      size_t index, int disc_number, const std::filesystem::path& path)>;
  void AttachLibrary(LibraryBootCallback on_boot,
                     const std::filesystem::path& storage_root);
  void ShowLibrary();
  void ShowGame();
  bool IsLibraryAttached() const { return library_view_ != nullptr; }
  size_t LibraryEntryCount() const { return library_entries_.size(); }
  const GameEntry* LibraryEntry(size_t index) const;
  void NoteGameBooted(size_t index, int disc_number);
  void ImportLibraryPaths(const std::vector<std::filesystem::path>& paths);
  void ScanLibraryFolder(const std::filesystem::path& dir);
  void RemoveLibraryEntry(size_t index);

  // WxLibraryView::Delegate (all on the UI thread).
  void OnBootGame(size_t index, int disc_number) override;
  void OnRemoveGame(size_t index) override;
  void OnShowInFolder(size_t index) override;
  void OnAddGame() override;
  void OnScanFolder() override;

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;

  uint32_t GetLatestDpiImpl() const override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void LoadAndApplyIcon(const void* buffer, size_t size,
                        bool can_apply_state_in_current_phase) override;
  void ApplyNewMainMenu(ui::MenuItem* old_main_menu) override;
  void CompleteMainMenuItemsUpdateImpl() override;
  void ApplyNewMouseCapture() override;
  void ApplyNewMouseRelease() override;
  void ApplyNewCursorVisibility(
      CursorVisibility old_cursor_visibility) override;
  void FocusImpl() override;

  std::unique_ptr<ui::Surface> CreateSurfaceImpl(
      ui::Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  friend class WxViewPanel;
  friend class WxHostFrame;
  friend class WxDropTarget;

  void SaveLibraryEntries();

  // Shared close path for the frame close event.
  void CloseWindowNow();
  // Shared tail of OpenImpl: initial state reports that need no native calls
  // beyond the view (mouse capture, cursor visibility, keyboard focus).
  void CompleteOpen();
  void RebuildMenuBar();
  wxMenu* BuildPopupMenu(WxMenuItem* popup_item);
  // View client area size in physical pixels; false if not queryable.
  bool PlatformClientSize(uint32_t& width_out, uint32_t& height_out);
  // Polls the current DPI and reports changes (for ports without reliable
  // DPI-change notifications; a no-op elsewhere).
  void PlatformDpiRefresh(WindowDestructionReceiver& destruction_receiver);

  // Event plumbing called by WxViewPanel / WxHostFrame (all on the UI thread).
  void OnWxMenu(int menu_id);
  void OnWxClose();
  void OnWxSize();
  void OnWxPaint();
  void OnWxFocus(bool focused);
  void OnWxKeyDown(wxKeyEvent& event);
  void OnWxKeyUp(wxKeyEvent& event);
  void OnWxKeyChar(wxKeyEvent& event);
  void OnWxMouseDown(wxMouseEvent& event);
  void OnWxMouseUp(wxMouseEvent& event);
  void OnWxMouseMove(wxMouseEvent& event);
  void OnWxMouseWheel(wxMouseEvent& event);
  void OnWxDropFiles(const wxArrayString& files);
  void OnWxDpiChanged(const wxSize& new_dpi);
  void OnWxCursorTimer();
  void OnWxDeviceChange(bool is_arrival);

  void ForwardWxMouse(wxMouseEvent& event, int kind);
  void SetCursorIfFocusedOnView(bool hide_cursor) const;
  void HandleWxSizeUpdate(WindowDestructionReceiver& destruction_receiver);

  uint32_t dpi_ = 96;
  WxHostFrame* frame_ = nullptr;
  WxViewPanel* view_ = nullptr;
#if XE_PLATFORM_WIN32
  HWND view_hwnd_ = nullptr;
  HICON icon_ = nullptr;
  // Shared default application icon (MAINICON resource); never destroyed.
  HICON default_icon_ = nullptr;
  HDEVNOTIFY usb_device_notify_ = nullptr;
  // Sends the current (custom or default) icon to the frame, if any.
  void ApplyFrameIcons();
#else
  // Opaque xcb_connection_t* / xcb_window_t for the view (X11 only; the
  // process forces GDK_BACKEND=x11 like the GTK backend does).
  void* view_xcb_connection_ = nullptr;
  uint32_t view_xid_ = 0;
#endif

  // wxMenu item ID to MenuItem mapping for the currently attached menu bar.
  // Menu events are bound once on the frame and dispatched through this map,
  // so rebuilds never leave stale bindings behind.
  std::unordered_map<int, WxMenuItem*> menu_items_by_id_;

  wxPoint cursor_auto_hide_last_screen_pos_ = wxDefaultPosition;
  bool cursor_currently_auto_hidden_ = false;

  // Game library (only when AttachLibrary ran).
  wxSimplebook* book_ = nullptr;
  WxLibraryView* library_view_ = nullptr;
  LibraryBootCallback library_on_boot_;
  std::filesystem::path library_storage_root_;
  std::vector<GameEntry> library_entries_;
};

class WxFilePicker : public ui::FilePicker {
 public:
  bool Show(ui::Window* parent_window) override;
};

class WxWindowedAppContext final : public ui::WindowedAppContext {
 public:
#if XE_PLATFORM_WIN32
  explicit WxWindowedAppContext(HINSTANCE hinstance, int show_cmd);
  HINSTANCE hinstance() const { return hinstance_; }
  int show_cmd() const { return show_cmd_; }
#else
  WxWindowedAppContext() = default;
#endif

  // Nothing platform-specific to initialize; wxWidgets sets itself up in
  // wxEntry before the app's OnInit runs.
  bool Initialize() { return true; }

  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

 private:
#if XE_PLATFORM_WIN32
  HINSTANCE hinstance_;
  int show_cmd_;
#endif
};

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_WINDOW_H_
