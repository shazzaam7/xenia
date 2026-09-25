#include "xenia/app/wx/wx_app.h"

#include "xenia/app/wx/wx_host.h"
#include "xenia/app/wx/wx_window.h"
#include "xenia/base/console.h"
#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"
#include "xenia/ui/windowed_app.h"

#if XE_PLATFORM_WIN32
// Defined in windowed_app_main_win.cc.
DECLARE_bool(enable_console);
#endif

namespace xe {
namespace app {
namespace wx_ui {

namespace {

WxHostParams* g_host_params = nullptr;

}  // namespace

void SetWxHostParams(WxHostParams* params) { g_host_params = params; }
WxHostParams* GetWxHostParams() { return g_host_params; }

bool WxApp::OnInit() {
  if (!g_host_params || !g_host_params->app) {
    return false;
  }
  // Theme before any window exists so everything is born themed.
  ApplyUiTheme();
  if (!g_host_params->app->OnInitialize()) {
    g_host_params->result = EXIT_FAILURE;
    return false;
  }
#if XE_PLATFORM_WIN32
  // TODO(Triang3l): Rework this, need to initialize the console properly,
  // disable has_console_attached_ by default in windowed apps, and attach
  // only if needed.
  if (cvars::enable_console) {
    xe::AttachConsole();
  }
#endif
  // Window closing is fully explicit (OnClosing quits the loop); don't let
  // frame deletion implicitly exit as well.
  SetExitOnFrameDelete(false);
  return true;
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

// No main() here on purpose: the platform entry point stays authoritative
// and runs the loop via wxEntry().
wxIMPLEMENT_APP_NO_MAIN(xe::app::wx_ui::WxApp);
