#ifndef XENIA_APP_WX_APP_H_
#define XENIA_APP_WX_APP_H_

#include <wx/app.h>

namespace xe {
namespace app {
namespace wx_ui {

// wxWidgets application object driving the real emulator app. The platform
// entry point provides the WindowedApp via SetWxHostParams before wxEntry()
// runs; OnInit initializes it and the wxWidgets main loop replaces
// Win32WindowedAppContext::RunMainMessageLoop.
class WxApp : public wxApp {
 public:
  bool OnInit() override;
};

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_APP_H_
