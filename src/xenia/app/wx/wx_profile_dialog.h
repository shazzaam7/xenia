/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_PROFILE_DIALOG_H_
#define XENIA_APP_WX_PROFILE_DIALOG_H_

#include <cstdint>

class wxWindow;

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
namespace app {
namespace wx_ui {

// Modal wxWidgets port of kernel::xam::ui::GamercardUI ("Modify" profile).
// Runs entirely on the UI thread; returns true when the user saved (the
// caller should refresh, e.g. the toolbar gamertag may have changed).
bool ShowGamercardDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                         uint64_t xuid);

// Modal wxWidgets port of kernel::xam::ui::CreateProfileUI. Returns true when
// a profile was created (the caller should refresh). With with_migration,
// runs Emulator::DataMigration after creation like the ImGui variant.
bool ShowCreateProfileDialog(wxWindow* parent,
                             kernel::KernelState* kernel_state,
                             bool with_migration);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_PROFILE_DIALOG_H_
