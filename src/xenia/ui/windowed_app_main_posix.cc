/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gtk/gtk.h>
#include <cstdio>
#include <cstdlib>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/ui/windowed_app.h"

#ifdef XENIA_HAS_WX_UI
#include "xenia/app/wx/wx_host.h"
#include "xenia/app/wx/wx_window.h"

#include <wx/app.h>
#else
#include "xenia/ui/windowed_app_context_gtk.h"
#endif

int main(int argc_pre_gtk, char** argv_pre_gtk) {
  // Before touching anything GTK+, make sure that when running on Wayland,
  // we'll still get an X11 (Xwayland) window
  // also allow users to override this
  if (!secure_getenv("GDK_BACKEND")) {
    setenv("GDK_BACKEND", "x11", 1);
  }

  // Initialize GTK+, which will handle and remove its own arguments from argv.
  // Both GTK+ and Xenia use --option=value argument format (see man
  // gtk-options), however, it's meaningless to try to parse the same argument
  // both as a GTK+ one and as a cvar. Make GTK+ options take precedence in case
  // of a name collision, as there's an alternative way of setting Xenia options
  // (the config).
  int argc_post_gtk = argc_pre_gtk;
  char** argv_post_gtk = argv_pre_gtk;
  if (!gtk_init_check(&argc_post_gtk, &argv_post_gtk)) {
    // Logging has not been initialized yet.
    std::fputs("Failed to initialize GTK+\n", stderr);
    return EXIT_FAILURE;
  }

  int result;

  {
#ifdef XENIA_HAS_WX_UI
    xe::app::wx_ui::WxWindowedAppContext app_context;
#else
    xe::ui::GTKWindowedAppContext app_context;
#endif

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    cvar::ParseLaunchArguments(argc_post_gtk, argv_post_gtk,
                               app->GetPositionalOptionsUsage(),
                               app->GetPositionalOptions());

    // Initialize logging. Needs parsed cvars.
    xe::InitializeLogging(app->GetName());

#ifdef XENIA_HAS_WX_UI
    // Hand the app to the wxWidgets application object; wxEntry() runs
    // WxApp::OnInit (which calls app->OnInitialize()) and then the wxWidgets
    // main loop, replacing GTKWindowedAppContext::RunMainGTKLoop.
    xe::app::wx_ui::WxHostParams host_params;
    host_params.app = app.get();
    host_params.result = EXIT_SUCCESS;
    xe::app::wx_ui::SetWxHostParams(&host_params);
    wxEntry(argc_post_gtk, argv_post_gtk);
    result = host_params.result;
#else
    if (app->OnInitialize()) {
      app_context.RunMainGTKLoop();
      result = EXIT_SUCCESS;
    } else {
      result = EXIT_FAILURE;
    }
#endif

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  xe::ShutdownLogging();

  return result;
}
