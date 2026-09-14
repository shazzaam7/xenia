#ifndef XENIA_APP_WX_HOST_H_
#define XENIA_APP_WX_HOST_H_

namespace xe {
namespace ui {
class WindowedApp;
}  // namespace ui

namespace app {
namespace wx_ui {

// Parameters provided by the platform entry point
// (windowed_app_main_win.cc / windowed_app_main_posix.cc) before running the
// wxWidgets event loop. WxApp consumes them in OnInit.
struct WxHostParams {
  ui::WindowedApp* app = nullptr;
  int result = 0;
};

void SetWxHostParams(WxHostParams* params);
WxHostParams* GetWxHostParams();

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_HOST_H_
