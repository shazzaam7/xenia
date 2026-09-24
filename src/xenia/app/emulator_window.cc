/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/emulator_window.h"

#include "third_party/imgui/imgui.h"
#include "third_party/stb/stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wabsolute-value"
#endif
#include "third_party/tomlplusplus/toml.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <thread>

#include "xenia/app/console_settings_dialog.h"
#include "xenia/app/content_list_dialog.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"
#include "xenia/base/threading.h"
#include "xenia/config.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/kernel/xconfig.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_host_notification.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/ui_event.h"
#include "xenia/ui/virtual_key.h"

#ifdef XENIA_HAS_WX_UI
#include "xenia/app/wx/wx_config_editor_dialog.h"
#include "xenia/app/wx/wx_console_settings_dialog.h"
#include "xenia/app/wx/wx_content_install_dialog.h"
#include "xenia/app/wx/wx_game_config_dialog.h"
#include "xenia/app/wx/wx_game_scan.h"
#include "xenia/app/wx/wx_profile_dialog.h"
#include "xenia/app/wx/wx_window.h"
#endif

#include "version.h"
#include "xenia/app/discord/discord_presence.h"

#include <cstdlib>

#if XE_PLATFORM_WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

DECLARE_bool(debug);

DECLARE_string(hid);
DECLARE_string(gpu);
DECLARE_string(apu);
DECLARE_bool(discord);
DECLARE_bool(in_process_title_relaunch);

DECLARE_bool(guide_button);

DECLARE_bool(clear_memory_page_state);

DECLARE_string(readback_resolve);

DECLARE_bool(readback_memexport);

DEFINE_bool(fullscreen, false, "Whether to launch the emulator in fullscreen.",
            "Display");
DEFINE_CVar_DisplayName(fullscreen, "Fullscreen");

DEFINE_transient_bool(return_to_ui, false,
                      "Return to UI process when game exits. Set automatically "
                      "when launching from UI.",
                      "General");

DEFINE_bool(controller_hotkeys, false, "Hotkeys for Xbox and PS controllers.",
            "General");
DEFINE_CVar_DisplayName(controller_hotkeys, "Controller hotkeys");

DEFINE_string_choices(
    postprocess_antialiasing, "",
    "Post-processing anti-aliasing effect to apply to the image output of the "
    "game.\n"
    "Using post-process anti-aliasing is heavily recommended when AMD "
    "FidelityFX Contrast Adaptive Sharpening or Super Resolution 1.0 is "
    "active.\n"
    "Use: [none, fxaa, fxaa_extreme]\n"
    " none (or any value not listed here):\n"
    "  Don't alter the original image.\n"
    " fxaa:\n"
    "  NVIDIA Fast Approximate Anti-Aliasing 3.11, normal quality preset (12)."
    "\n"
    " fxaa_extreme:\n"
    "  NVIDIA Fast Approximate Anti-Aliasing 3.11, extreme quality preset "
    "(39).",
    "Display", "Anti-aliasing", XE_CVAR_CHOICE("None", ""),
    XE_CVAR_CHOICE("FXAA", "fxaa"),
    XE_CVAR_CHOICE("FXAA Extreme", "fxaa_extreme"));
DEFINE_string_choices(
    postprocess_scaling_and_sharpening, "",
    "Post-processing effect to use for resampling and/or sharpening of the "
    "final display output.\n"
    "Use: [bilinear, cas, fsr]\n"
    " bilinear (or any value not listed here):\n"
    "  Original image at 1:1, simple bilinear stretching for resampling.\n"
    " cas:\n"
    "  Use AMD FidelityFX Contrast Adaptive Sharpening (CAS) for sharpening "
    "at scaling factors of up to 2x2, with additional bilinear stretching for "
    "larger factors.\n"
    " fsr:\n"
    "  Use AMD FidelityFX Super Resolution 1.0 (FSR) for highest-quality "
    "upscaling, or AMD FidelityFX Contrast Adaptive Sharpening for sharpening "
    "while not scaling or downsampling.\n"
    "  For scaling by factors of more than 2x2, multiple FSR passes are done.",
    "Display", "Scaling and sharpening", XE_CVAR_CHOICE("Bilinear", ""),
    XE_CVAR_CHOICE("CAS", "cas"), XE_CVAR_CHOICE("FSR", "fsr"));
DEFINE_double_range(
    postprocess_ffx_cas_additional_sharpness,
    xe::ui::Presenter::GuestOutputPaintConfig::kCasAdditionalSharpnessDefault,
    "Additional sharpness for AMD FidelityFX Contrast Adaptive Sharpening "
    "(CAS), from 0 to 1.\n"
    "Higher is sharper.",
    "Display", "CAS additional sharpness",
    xe::ui::Presenter::GuestOutputPaintConfig::kCasAdditionalSharpnessMin,
    xe::ui::Presenter::GuestOutputPaintConfig::kCasAdditionalSharpnessMax,
    0.01);
DEFINE_uint32_range(
    postprocess_ffx_fsr_max_upsampling_passes,
    xe::ui::Presenter::GuestOutputPaintConfig::kFsrMaxUpscalingPassesMax,
    "Maximum number of upsampling passes performed in AMD FidelityFX Super "
    "Resolution 1.0 (FSR) before falling back to bilinear stretching after the "
    "final pass.\n"
    "Each pass upscales only to up to 2x2 the previous size. If the game "
    "outputs a 1280x720 image, 1 pass will upscale it to up to 2560x1440 "
    "(below 4K), after 2 passes it will be upscaled to a maximum of 5120x2880 "
    "(including 3840x2160 for 4K), and so on.\n"
    "This variable has no effect if the display resolution isn't very high, "
    "but may be reduced on resolutions like 4K or 8K in case the performance "
    "impact of multiple FSR upsampling passes is too high, or if softer edges "
    "are desired.\n"
    "The default value is the maximum internally supported by Xenia.",
    "Display", "FSR max upsampling passes", 1,
    xe::ui::Presenter::GuestOutputPaintConfig::kFsrMaxUpscalingPassesMax);
DEFINE_double_range(
    postprocess_ffx_fsr_sharpness_reduction,
    xe::ui::Presenter::GuestOutputPaintConfig::kFsrSharpnessReductionDefault,
    "Sharpness reduction for AMD FidelityFX Super Resolution 1.0 (FSR), in "
    "stops.\n"
    "Lower is sharper.",
    "Display", "FSR sharpness reduction",
    xe::ui::Presenter::GuestOutputPaintConfig::kFsrSharpnessReductionMin,
    xe::ui::Presenter::GuestOutputPaintConfig::kFsrSharpnessReductionMax, 0.05);
// Dithering to 8bpc is enabled by default since the effect is minor, only
// effects what can't be shown normally by host displays, and nothing is changed
// by it for 8bpc source without resampling.
DEFINE_bool(
    postprocess_dither, true,
    "Dither the final image output from the internal precision to 8 bits per "
    "channel so gradients are smoother.\n"
    "On a 10bpc display, the lower 2 bits will still be kept, but noise will "
    "be added to them - disabling may be recommended for 10bpc, but it "
    "depends on the 10bpc displaying capabilities of the actual display used.",
    "Display");
DEFINE_CVar_DisplayName(postprocess_dither, "Dither output");

DEFINE_int32_range(recent_titles_entry_amount, 10,
                   "Allows user to define how many titles is saved in list of "
                   "recently played titles.",
                   "General", "Recent titles to keep", 0, 50);
DEFINE_bool(disable_doubleclick_fullscreen, false,
            "Allows the user to disable the behavior where a fast double-click "
            "causes Xenia to enter fullscreen mode.",
            "General");
DEFINE_CVar_DisplayName(disable_doubleclick_fullscreen,
                        "Disable double-click fullscreen");

namespace xe {
namespace app {

using xe::ui::FileDropEvent;
using xe::ui::KeyEvent;
using xe::ui::MenuItem;
using xe::ui::UIEvent;

using namespace xe::hid;
using namespace xe::gpu;

constexpr std::string_view kRecentlyPlayedTitlesFilename = "recent.toml";
constexpr std::string_view kBaseTitle = "Xenia-canary";

EmulatorWindow::EmulatorWindow(Emulator* emulator,
                               ui::WindowedAppContext& app_context,
                               uint32_t width, uint32_t height)
    : emulator_(emulator),
      app_context_(app_context),
      window_listener_(*this),
      window_(ui::Window::Create(app_context, kBaseTitle, width, height)),
      imgui_drawer_(
          std::make_unique<ui::ImGuiDrawer>(window_.get(), kZOrderImGui)),
      display_config_game_config_load_callback_(
          new DisplayConfigGameConfigLoadCallback(*emulator, *this)) {
  base_title_ = std::string(kBaseTitle) +
#ifdef DEBUG
#if _NO_DEBUG_HEAP == 1
                " DEBUG"
#else
                " CHECKED"
#endif
#endif
                " ("
#ifdef XE_BUILD_IS_PR
                "PR#" XE_BUILD_PR_NUMBER " - "
#endif
                XE_BUILD_BRANCH "@" XE_BUILD_COMMIT_SHORT " on " XE_BUILD_DATE
                ")";

  LoadRecentlyLaunchedTitles();

  // Guest-initiated exits (XamLoaderTerminateTitle and friends) route here so
  // the title is stopped via ResetTitle instead of the legacy suicide path.
  emulator_->set_on_guest_title_exit(
      [this](std::string host_path, std::string launch_path,
             uint32_t launch_flags, std::vector<uint8_t> launch_data) {
        return StopTitleFromGuestThread(std::move(host_path),
                                        std::move(launch_path), launch_flags,
                                        std::move(launch_data));
      });
}

std::unique_ptr<EmulatorWindow> EmulatorWindow::Create(
    Emulator* emulator, ui::WindowedAppContext& app_context, uint32_t width,
    uint32_t height) {
  assert_true(app_context.IsInUIThread());
  std::unique_ptr<EmulatorWindow> emulator_window(
      new EmulatorWindow(emulator, app_context, width, height));
  if (!emulator_window->Initialize()) {
    return nullptr;
  }
  return emulator_window;
}

EmulatorWindow::~EmulatorWindow() {
  // Notify the ImGui drawer that the immediate drawer is being destroyed.
  ShutdownGraphicsSystemPresenterPainting();
}

ui::Presenter* EmulatorWindow::GetGraphicsSystemPresenter() const {
  gpu::GraphicsSystem* graphics_system = emulator_->graphics_system();
  return graphics_system ? graphics_system->presenter() : nullptr;
}

void EmulatorWindow::SetupGraphicsSystemPresenterPainting() {
  ShutdownGraphicsSystemPresenterPainting();

  if (!window_) {
    return;
  }

  ui::Presenter* presenter = GetGraphicsSystemPresenter();
  if (!presenter) {
    XELOGE(
        "SetupGraphicsSystemPresenterPainting: no presenter - titles will "
        "launch without video. Graphics system was likely not set up.");
    return;
  }

  ApplyDisplayConfigForCvars();

  window_->SetPresenter(presenter);

  immediate_drawer_ =
      emulator_->graphics_system()->provider()->CreateImmediateDrawer();
  if (immediate_drawer_) {
    immediate_drawer_->SetPresenter(presenter);
    imgui_drawer_->SetPresenterAndImmediateDrawer(presenter,
                                                  immediate_drawer_.get());
    Profiler::SetUserIO(kZOrderProfiler, window_.get(), presenter,
                        immediate_drawer_.get());
  }
}

void EmulatorWindow::ShutdownGraphicsSystemPresenterPainting() {
  Profiler::SetUserIO(kZOrderProfiler, window_.get(), nullptr, nullptr);
  imgui_drawer_->SetPresenterAndImmediateDrawer(nullptr, nullptr);
  immediate_drawer_.reset();
  if (window_) {
    window_->SetPresenter(nullptr);
  }
}

void EmulatorWindow::OnEmulatorInitialized() {
#ifdef XENIA_HAS_WX_UI
  // The ImGui dialog renders behind the library view when attached.
  if (auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
      wx_window->IsLibraryAttached()) {
    if (!emulator_->kernel_state()
             ->xam_state()
             ->profile_manager()
             ->GetAccountCount()) {
      disable_hotkeys_ = true;
      if (wx_ui::ShowNoProfileDialog(wx_window, emulator_->kernel_state(),
                                     emulator_->content_root())) {
        wx_window->ShowLibrary();
      }
      disable_hotkeys_ = false;
    }
  } else
#endif
  {
    if (!emulator_->kernel_state()
             ->xam_state()
             ->profile_manager()
             ->GetAccountCount()) {
      new NoProfileDialog(imgui_drawer_.get(), this);
      disable_hotkeys_ = true;
    }
  }

  // Title systems (audio, graphics) are created per title after
  // its game config loads. Wire the fresh graphics system to the window's
  // presenter the moment it exists - SetupTitleSystems invokes this on its
  // calling (worker) thread, so marshal to the UI thread for the UI-owned
  // wiring.
  emulator_->SetGraphicsReadyHook([this]() {
    if (app_context_.IsInUIThread()) {
      SetupGraphicsSystemPresenterPainting();
    } else {
      app_context_.CallInUIThreadSynchronous(
          [this]() { SetupGraphicsSystemPresenterPainting(); });
    }
  });

  // Detach the presenter before GPU teardown during relaunch. Fired inside
  // Emulator::Shutdown while subsystems are still alive.
  emulator_->on_before_shutdown.AddListener([this]() {
    app_context_.CallInUIThreadSynchronous(
        [this]() { ShutdownGraphicsSystemPresenterPainting(); });
  });

  // Out-of-process title-to-title launches from the kernel (when
  // in_process_title_relaunch is off) and backend-switch respawns land here.
  // Spawns a fresh process carrying the loader data, then quits this one.
  // NOTE: Canary has no --log_append/--launch_flags/--launch_data cvars, so
  // only --return_to_ui/--fullscreen/--launch_module (all defined) are
  // forwarded. launch_flags/launch_data are logged and dropped.
  emulator_->set_on_launch_new_title(
      [this](const std::string& host_path, const std::string& launch_module,
             uint32_t launch_flags, const std::vector<uint8_t>& launch_data) {
        XELOGI("Launching new title process: host_path={}, module={}, flags={}",
               host_path, launch_module, launch_flags);
        if (!launch_data.empty()) {
          XELOGW(
              "on_launch_new_title: dropping {} bytes of launch_data (no "
              "--launch_data cvar on Canary)",
              launch_data.size());
        }
        if (launch_flags != 0) {
          XELOGW(
              "on_launch_new_title: dropping launch_flags={} (no "
              "--launch_flags cvar on Canary)",
              launch_flags);
        }
        std::filesystem::path executable_path =
            xe::filesystem::GetExecutablePath();
#if XE_PLATFORM_WIN32
        auto exe_path_u16 = xe::path_to_utf16(executable_path);
        std::u16string cmd_line = u"\"" + exe_path_u16 + u"\"";
        int parent_argc = 0;
        wchar_t** parent_argv =
            CommandLineToArgvW(GetCommandLineW(), &parent_argc);
        if (parent_argv) {
          for (int i = 1; i < parent_argc; ++i) {
            std::u16string a(reinterpret_cast<const char16_t*>(parent_argv[i]));
            if (a.empty() || a[0] != u'-') {
              continue;
            }
            // Skip flags we set fresh below to avoid stale duplicates.
            auto is_prefix = [&a](const char16_t* p) {
              size_t n = std::char_traits<char16_t>::length(p);
              return a.size() >= n && a.compare(0, n, p) == 0;
            };
            if (is_prefix(u"--launch_module") || is_prefix(u"--fullscreen") ||
                is_prefix(u"--return_to_ui") || is_prefix(u"--log_append") ||
                is_prefix(u"--launch_flags") || is_prefix(u"--launch_data")) {
              continue;
            }
            cmd_line += u" \"" + a + u"\"";
          }
          LocalFree(parent_argv);
        }
        // Returning to library or already in the return chain: child must
        // not try to launch a title on its own.
        if (cvars::return_to_ui || host_path.empty()) {
          cmd_line += u" --return_to_ui=true";
        }
        if (window_->IsFullscreen() && !host_path.empty()) {
          cmd_line += u" --fullscreen=true";
        }
        if (!launch_module.empty()) {
          cmd_line +=
              u" --launch_module=\"" + xe::to_utf16(launch_module) + u"\"";
        }
        if (!host_path.empty()) {
          cmd_line += u" \"" + xe::to_utf16(host_path) + u"\"";
        }
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(
                nullptr,
                const_cast<wchar_t*>(
                    reinterpret_cast<const wchar_t*>(cmd_line.c_str())),
                nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr,
                &si, &pi)) {
          XELOGE("Failed to launch new process: {}", GetLastError());
          return;
        }
        AllowSetForegroundWindow(pi.dwProcessId);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
#else
        pid_t pid = fork();
        if (pid == 0) {
          std::vector<std::string> arg_storage;
          arg_storage.push_back(executable_path.string());
          if (cvars::return_to_ui || host_path.empty()) {
            arg_storage.push_back("--return_to_ui=true");
          }
          if (window_->IsFullscreen() && !host_path.empty()) {
            arg_storage.push_back("--fullscreen=true");
          }
          if (!launch_module.empty()) {
            arg_storage.push_back("--launch_module=" + launch_module);
          }
          if (!host_path.empty()) {
            arg_storage.push_back(host_path);
          }
          std::vector<const char*> argv;
          for (const auto& a : arg_storage) {
            argv.push_back(a.c_str());
          }
          argv.push_back(nullptr);
          execv(executable_path.c_str(), const_cast<char**>(argv.data()));
          std::exit(1);
        } else if (pid < 0) {
          XELOGE("Failed to fork process");
          return;
        }
#endif
        xe::FlushLog();
        // May run on a guest thread (kernel-initiated title switch), so use
        // the thread-safe deferred quit rather than QuitFromUIThread.
        app_context_.RequestDeferredQuit();
      });

  emulator_initialized_ = true;
  window_->SetMainMenuEnabled(true);
  // When the user can see that the emulator isn't initializing anymore (the
  // menu isn't disabled), enter fullscreen if requested.
  if (cvars::fullscreen) {
    SetFullscreen(true);
  }

  if (IsUseNexusForGameBarEnabled()) {
    XELOGE(
        "Xbox Gamebar Enabled, using BACK button instead of GUIDE for "
        "controller hotkeys!!!");
  }

  // Create a thread to listen for controller hotkeys.
  if (cvars::controller_hotkeys) {
    Gamepad_HotKeys_Listener =
        threading::Thread::Create({}, [&] { GamepadHotKeys(); });
    Gamepad_HotKeys_Listener->set_name("Gamepad HotKeys Listener");
  }
}

void EmulatorWindow::EmulatorWindowListener::OnClosing(ui::UIEvent& e) {
  emulator_window_.app_context_.QuitFromUIThread();
}

void EmulatorWindow::EmulatorWindowListener::OnFileDrop(ui::FileDropEvent& e) {
  emulator_window_.FileDrop(e.filename());
}

void EmulatorWindow::EmulatorWindowListener::OnKeyDown(ui::KeyEvent& e) {
  emulator_window_.OnKeyDown(e);
}

void EmulatorWindow::EmulatorWindowListener::OnMouseDown(ui::MouseEvent& e) {
  emulator_window_.OnMouseDown(e);
}

void EmulatorWindow::EmulatorWindowListener::OnMouseUp(ui::MouseEvent& e) {
  emulator_window_.OnMouseUp(e);
}

void EmulatorWindow::EmulatorWindowListener::OnUsbDeviceChanged(
    bool is_arrival) {
  if (!emulator_window_.emulator()) {
    return;
  }

  if (!emulator_window_.emulator()->input_system()) {
    return;
  }

  auto* portal = emulator_window_.emulator()->input_system()->GetPortal();
  if (!portal) {
    return;
  }

  if (is_arrival) {
    portal->OnDeviceArrival();
  } else {
    portal->OnDeviceRemoval();
  }
}

void EmulatorWindow::DisplayConfigGameConfigLoadCallback::PostGameConfigLoad() {
  emulator_window_.ApplyDisplayConfigForCvars();
}

void EmulatorWindow::DisplayConfigDialog::OnDraw(ImGuiIO& io) {
  gpu::GraphicsSystem* graphics_system =
      emulator_window_.emulator_->graphics_system();
  if (!graphics_system) {
    return;
  }

  // In the top-left corner so it's close to the menu bar from where it was
  // opened.
  // Origin Y coordinate 20 was taken from the Dear ImGui demo.
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  // Alpha from Dear ImGui tooltips (0.35 from the overlay provides too low
  // visibility). Translucent so some effect of the changes can still be seen
  // through it.
  ImGui::SetNextWindowBgAlpha(0.6f);
  bool dialog_open = true;
  if (!ImGui::Begin("Post-processing", &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    Close();
    return;
  }

  // Even if the close button has been pressed, still paint everything not to
  // have one frame with an empty window.

  // Prevent user confusion which has been reported multiple times.
  ImGui::TextUnformatted("All effects can be used on GPUs of any brand.");
  ImGui::Spacing();

  gpu::CommandProcessor* command_processor =
      graphics_system->command_processor();
  if (command_processor) {
    if (ImGui::TreeNodeEx(
            "Anti-aliasing",
            ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
      gpu::CommandProcessor::SwapPostEffect current_swap_post_effect =
          command_processor->GetDesiredSwapPostEffect();
      int new_swap_post_effect_index = int(current_swap_post_effect);
      ImGui::RadioButton("None", &new_swap_post_effect_index,
                         int(gpu::CommandProcessor::SwapPostEffect::kNone));
      ImGui::RadioButton(
          "NVIDIA Fast Approximate Anti-Aliasing (FXAA) [Normal Quality]",
          &new_swap_post_effect_index,
          int(gpu::CommandProcessor::SwapPostEffect::kFxaa));
      ImGui::RadioButton(
          "NVIDIA Fast Approximate Anti-Aliasing (FXAA) [Extreme Quality]",
          &new_swap_post_effect_index,
          int(gpu::CommandProcessor::SwapPostEffect::kFxaaExtreme));
      gpu::CommandProcessor::SwapPostEffect new_swap_post_effect =
          gpu::CommandProcessor::SwapPostEffect(new_swap_post_effect_index);
      if (current_swap_post_effect != new_swap_post_effect) {
        command_processor->SetDesiredSwapPostEffect(new_swap_post_effect);
      }

      // Override the values in the cvars to save them to the config at exit if
      // the user has set them to anything new.
      if (GetSwapPostEffectForCvarValue(cvars::postprocess_antialiasing) !=
          new_swap_post_effect) {
        OVERRIDE_string(postprocess_antialiasing,
                        GetCvarValueForSwapPostEffect(new_swap_post_effect));
      }

      ImGui::TreePop();
    }
  }

  ui::Presenter* presenter = graphics_system->presenter();
  if (presenter) {
    const ui::Presenter::GuestOutputPaintConfig& current_presenter_config =
        presenter->GetGuestOutputPaintConfigFromUIThread();
    ui::Presenter::GuestOutputPaintConfig new_presenter_config =
        current_presenter_config;

    if (ImGui::TreeNodeEx(
            "Resampling and sharpening",
            ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
      // Filtering effect.
      int new_effect_index = int(new_presenter_config.GetEffect());
      ImGui::RadioButton(
          "None / Bilinear", &new_effect_index,
          int(ui::Presenter::GuestOutputPaintConfig::Effect::kBilinear));
      ImGui::RadioButton(
          "AMD FidelityFX Contrast Adaptive Sharpening (CAS)",
          &new_effect_index,
          int(ui::Presenter::GuestOutputPaintConfig::Effect::kCas));
      ImGui::RadioButton(
          "AMD FidelityFX Super Resolution 1.0 (FSR)", &new_effect_index,
          int(ui::Presenter::GuestOutputPaintConfig::Effect::kFsr));
      new_presenter_config.SetEffect(
          ui::Presenter::GuestOutputPaintConfig::Effect(new_effect_index));

      // effect_description must be one complete, but short enough, sentence per
      // line, as TextWrapped doesn't work correctly in auto-resizing windows
      // (in the initial frames, the window becomes extremely tall, and widgets
      // added after the wrapped text have no effect on the width of the text).
      const char* effect_description = nullptr;
      switch (new_presenter_config.GetEffect()) {
        case ui::Presenter::GuestOutputPaintConfig::Effect::kBilinear:
          effect_description =
              "Simple bilinear filtering is done if resampling is needed.\n"
              "Otherwise, only anti-aliasing is done if enabled, or displaying "
              "as is.";
          break;
        case ui::Presenter::GuestOutputPaintConfig::Effect::kCas:
          effect_description =
              "Sharpening and resampling to up to 2x2 to improve the fidelity "
              "of details.\n"
              "For scaling by more than 2x2, bilinear stretching is done "
              "afterwards.";
          break;
        case ui::Presenter::GuestOutputPaintConfig::Effect::kFsr:
          effect_description =
              "High-quality edge-preserving upscaling to arbitrary target "
              "resolutions.\n"
              "For scaling by more than 2x2, multiple upsampling passes are "
              "done.\n"
              "If not upscaling, Contrast Adaptive Sharpening (CAS) is used "
              "instead.";
          break;
      }
      if (effect_description) {
        ImGui::TextUnformatted(effect_description);
      }

      if (new_presenter_config.GetEffect() ==
              ui::Presenter::GuestOutputPaintConfig::Effect::kCas ||
          new_presenter_config.GetEffect() ==
              ui::Presenter::GuestOutputPaintConfig::Effect::kFsr) {
        if (effect_description) {
          ImGui::Spacing();
        }

        ImGui::TextUnformatted(
            "FXAA is highly recommended when using CAS or FSR.");

        ImGui::Spacing();

        // 2 decimal places is more or less enough precision for the sharpness
        // given the minor visual effect of small changes, the width of the
        // slider, and readability convenience (2 decimal places is like an
        // integer percentage). However, because Dear ImGui parses the string
        // representation of the number and snaps the value to it internally,
        // 2 decimal places actually offer less precision than the slider itself
        // does. This is especially prominent in the low range of the non-linear
        // FSR sharpness reduction slider. 3 decimal places are optimal in this
        // case.

        if (new_presenter_config.GetEffect() ==
            ui::Presenter::GuestOutputPaintConfig::Effect::kFsr) {
          float fsr_sharpness_reduction =
              new_presenter_config.GetFsrSharpnessReduction();
          ImGui::TextUnformatted(
              "FSR sharpness reduction when upscaling (lower is sharper):");
          const auto label = fmt::format(
              "{} %%", static_cast<int>(fsr_sharpness_reduction * 100));
          // Power 2.0 scaling as the reduction is in stops, used in exp2.
          fsr_sharpness_reduction = sqrt(2.f * fsr_sharpness_reduction);
          ImGui::SliderFloat(
              "##FSRSharpnessReduction", &fsr_sharpness_reduction,
              ui::Presenter::GuestOutputPaintConfig::kFsrSharpnessReductionMin,
              ui::Presenter::GuestOutputPaintConfig::kFsrSharpnessReductionMax,
              label.c_str(), ImGuiSliderFlags_NoInput);
          fsr_sharpness_reduction =
              .5f * fsr_sharpness_reduction * fsr_sharpness_reduction;
          ImGui::SameLine();
          if (ImGui::Button("Reset##ResetFSRSharpnessReduction")) {
            fsr_sharpness_reduction = ui::Presenter::GuestOutputPaintConfig ::
                kFsrSharpnessReductionDefault;
          }
          new_presenter_config.SetFsrSharpnessReduction(
              fsr_sharpness_reduction);
        }

        float cas_additional_sharpness =
            new_presenter_config.GetCasAdditionalSharpness();
        ImGui::TextUnformatted(
            new_presenter_config.GetEffect() ==
                    ui::Presenter::GuestOutputPaintConfig::Effect::kFsr
                ? "CAS additional sharpness when not upscaling (higher is "
                  "sharper):"
                : "CAS additional sharpness (higher is sharper):");
        const auto label = fmt::format(
            "{} %%", static_cast<int>(cas_additional_sharpness * 100));
        ImGui::SliderFloat(
            "##CASAdditionalSharpness", &cas_additional_sharpness,
            ui::Presenter::GuestOutputPaintConfig::kCasAdditionalSharpnessMin,
            ui::Presenter::GuestOutputPaintConfig::kCasAdditionalSharpnessMax,
            label.c_str(), ImGuiSliderFlags_NoInput);
        ImGui::SameLine();
        if (ImGui::Button("Reset##ResetCASAdditionalSharpness")) {
          cas_additional_sharpness = ui::Presenter::GuestOutputPaintConfig ::
              kCasAdditionalSharpnessDefault;
        }
        new_presenter_config.SetCasAdditionalSharpness(
            cas_additional_sharpness);

        // There's no need to expose the setting for the maximum number of FSR
        // EASU passes as it's largely meaningless if the user doesn't have a
        // very high-resolution monitor compared to the original image size as
        // most of the values of the slider will have no effect, and that's just
        // very fine-grained performance control for a fixed-overhead pass only
        // for huge screen resolutions.
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Dithering", ImGuiTreeNodeFlags_Framed |
                                           ImGuiTreeNodeFlags_DefaultOpen)) {
      bool dither = current_presenter_config.GetDither();
      ImGui::Checkbox(
          "Dither the final output to 8bpc to make gradients smoother",
          &dither);
      new_presenter_config.SetDither(dither);

      ImGui::TreePop();
    }

    presenter->SetGuestOutputPaintConfigFromUIThread(new_presenter_config);

    // Override the values in the cvars to save them to the config at exit if
    // the user has set them to anything new.
    ui::Presenter::GuestOutputPaintConfig cvars_presenter_config =
        GetGuestOutputPaintConfigForCvars();
    if (cvars_presenter_config.GetEffect() !=
        new_presenter_config.GetEffect()) {
      OVERRIDE_string(postprocess_scaling_and_sharpening,
                      GetCvarValueForGuestOutputPaintEffect(
                          new_presenter_config.GetEffect()));
    }
    if (cvars_presenter_config.GetCasAdditionalSharpness() !=
        new_presenter_config.GetCasAdditionalSharpness()) {
      OVERRIDE_double(postprocess_ffx_cas_additional_sharpness,
                      new_presenter_config.GetCasAdditionalSharpness());
    }
    if (cvars_presenter_config.GetFsrSharpnessReduction() !=
        new_presenter_config.GetFsrSharpnessReduction()) {
      OVERRIDE_double(postprocess_ffx_fsr_sharpness_reduction,
                      new_presenter_config.GetFsrSharpnessReduction());
    }
    if (cvars_presenter_config.GetDither() !=
        new_presenter_config.GetDither()) {
      OVERRIDE_bool(postprocess_dither, new_presenter_config.GetDither());
    }
  }

  ImGui::End();

  if (!dialog_open) {
    Close();
    emulator_window_.ToggleDisplayConfigDialog();
    // `this` might have been destroyed by ToggleDisplayConfigDialog.
    return;
  }
}

void EmulatorWindow::ContentInstallDialog::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(20, 20), ImGuiCond_FirstUseEver);

  bool dialog_open = true;
  if (!ImGui::Begin(
          fmt::format("Installation Progress###{}", window_id_).c_str(),
          &dialog_open,
          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
              ImGuiWindowFlags_HorizontalScrollbar)) {
    Close();
    ImGui::End();
    return;
  }

  bool is_everything_installed = true;
  for (const auto& entry : *installation_entries_) {
    ImGui::BeginTable(fmt::format("table_{}", entry.name_).c_str(), 2);
    ImGui::TableNextRow(0);
    ImGui::TableSetColumnIndex(0);
    if (entry.icon_) {
      ImGui::Image(reinterpret_cast<ImTextureID>(entry.icon_.get()),
                   ui::default_image_icon_size);
    } else {
      ImGui::Dummy(ui::default_image_icon_size);
    }
    ImGui::TableNextColumn();

    ImGui::Text("Name: %s", entry.name_.c_str());
    ImGui::Text("Installation Path:");
    ImGui::SameLine();
    if (ImGui::TextLink(
            xe::path_to_utf8(entry.data_installation_path_).c_str())) {
      LaunchFileExplorer(emulator_window_.emulator_->content_root() /
                         entry.data_installation_path_);
    }

    if (entry.content_type_ != xe::XContentType::kInvalid) {
      ImGui::Text("Content Type: %s",
                  XContentTypeMap.at(entry.content_type_).c_str());
    }

    std::string result = fmt::format(
        "Status: {}", xe::Emulator::installStateStringName[static_cast<uint8_t>(
                          entry.installation_state_)]);

    if (entry.installation_state_ == xe::Emulator::InstallState::failed) {
      result += fmt::format(" - {} ({:08X})",
                            entry.installation_error_message_.c_str(),
                            entry.installation_result_);
    }

    ImGui::Text("%s", result.c_str());
    ImGui::EndTable();

    if (entry.content_size_ > 0) {
      ImGui::ProgressBar(static_cast<float>(entry.currently_installed_size_) /
                         entry.content_size_);

      if (entry.installation_state_ == Emulator::InstallState::installing ||
          entry.installation_state_ == Emulator::InstallState::pending ||
          entry.installation_state_ == Emulator::InstallState::preparing) {
        is_everything_installed = false;
      }
    } else {
      ImGui::ProgressBar(0.0f);
    }

    if (installation_entries_->size() > 1) {
      ImGui::Separator();
    }
  }
  ImGui::Spacing();

  ImGui::BeginDisabled(!is_everything_installed);
  if (ImGui::Button("Close")) {
    ImGui::EndDisabled();
    Close();
    ImGui::End();
    return;
  }
  ImGui::EndDisabled();

  if (!dialog_open && is_everything_installed) {
    Close();
    ImGui::End();
    return;
  }
  ImGui::End();
}

void EmulatorWindow::XMPConfigDialog::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(20, 20), ImGuiCond_FirstUseEver);

  bool dialog_open = true;
  if (!ImGui::Begin("Audio Player Menu", &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    Close();
    ImGui::End();
    return;
  }

  auto audio_player = emulator_window_.emulator_->audio_media_player();
  using xmp_state = kernel::xam::apps::XmpApp::State;
  if (audio_player) {
    ImGui::Text("Audio player status:");
    ImGui::SameLine();
    switch (audio_player->GetState()) {
      case xmp_state::kIdle:
        ImGui::Text("Idle");
        break;
      case xmp_state::kPaused:
        ImGui::Text("Paused");
        break;
      case xmp_state::kPlaying:
        ImGui::Text("Playing");
        break;
      default:
        break;
    }

    if (audio_player->IsPlaying()) {
      if (ImGui::Button("Pause")) {
        audio_player->Pause();
      }
    } else if (audio_player->IsPaused()) {
      if (ImGui::Button("Resume")) {
        audio_player->Continue();
      }
    }

    volume_ =
        emulator_window_.emulator_->audio_media_player()->GetVolume()->load();

    if (ImGui::SliderFloat("Audio player volume", &volume_, 0.0f, 1.0f,
                           "%.2f")) {
      audio_player->SetVolume(volume_);
    }
  }

  ImGui::End();

  if (!dialog_open) {
    Close();
    emulator_window_.xmp_config_dialog_.release();
    return;
  }
}

bool EmulatorWindow::Initialize() {
  window_->AddListener(&window_listener_);
  window_->AddInputListener(&window_listener_, kZOrderEmulatorWindowInput);

  // Main menu.
  // FIXME: This code is really messy.
  auto main_menu = MenuItem::Create(MenuItem::Type::kNormal);
  auto file_menu = MenuItem::Create(MenuItem::Type::kPopup, "&File");
  auto recent_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Open Recent");
  FillRecentlyLaunchedTitlesMenu(recent_menu.get());
  {
    file_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Open...", "Ctrl+O",
                         std::bind(&EmulatorWindow::FileOpen, this)));
    file_menu->AddChild(std::move(recent_menu));
    file_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
    {
      auto stop = MenuItem::Create(MenuItem::Type::kString, "Stop",
                                   std::bind(&EmulatorWindow::FileClose, this));
      stop->SetEnabled(false);
      stop_item_ = stop.get();
      file_menu->AddChild(std::move(stop));
    }
    file_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
    file_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "E&xit", "Alt+F4",
                         [this]() { window_->RequestClose(); }));
  }
  main_menu->AddChild(std::move(file_menu));

  // Profile Menu
  auto profile_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Profile");
  {
    profile_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Profile", "",
                         std::bind(&EmulatorWindow::ShowProfileMenu, this)));
  }
  main_menu->AddChild(std::move(profile_menu));

  // Content Menu
  auto content_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Content");
  auto zar_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Zar Package");
  {
    content_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "Install Content",
                         std::bind(&EmulatorWindow::InstallContent, this)));
    content_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "Extract Content",
                         std::bind(&EmulatorWindow::ExtractContent, this, "")));
    zar_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "Create",
                         std::bind(&EmulatorWindow::CreateZarchive, this)));
    zar_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "Extract",
                         std::bind(&EmulatorWindow::ExtractZarchive, this)));
    content_menu->AddChild(std::move(zar_menu));
    content_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "Show content directory...",
        std::bind(&EmulatorWindow::ShowContentDirectory, this)));
  }
  main_menu->AddChild(std::move(content_menu));

  // Console menu
  auto console_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Console");
  {
    console_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Open console settings", "",
        std::bind(&EmulatorWindow::ShowConsoleSettingsDialog, this)));
  }
  main_menu->AddChild(std::move(console_menu));

  // Config menu
  auto config_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Config");
  {
    auto open_config = MenuItem::Create(
        MenuItem::Type::kString, "&Open config editor", "",
        std::bind(&EmulatorWindow::ShowConfigEditorDialog, this));
    config_editor_item_ = open_config.get();
    config_menu->AddChild(std::move(open_config));
  }
  main_menu->AddChild(std::move(config_menu));

  // Debug menu (CPU + GPU moved here).
  auto debug_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Debug");
  // CPU menu.
  auto cpu_menu = MenuItem::Create(MenuItem::Type::kPopup, "&CPU");
  {
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Reset Time Scalar", "Numpad *",
        std::bind(&EmulatorWindow::CpuTimeScalarReset, this)));
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "Time Scalar /= 2", "Numpad -",
        std::bind(&EmulatorWindow::CpuTimeScalarSetHalf, this)));
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "Time Scalar *= 2", "Numpad +",
        std::bind(&EmulatorWindow::CpuTimeScalarSetDouble, this)));
  }
#if XE_OPTION_PROFILING
  cpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
  {
    cpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                        "Toggle Profiler &Display", "F3",
                                        []() { Profiler::ToggleDisplay(); }));
    cpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                        "&Pause/Resume Profiler", "`",
                                        []() { Profiler::TogglePause(); }));
  }
#endif
  cpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
  {
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Break and Show Guest Debugger",
        "Pause/Break", std::bind(&EmulatorWindow::CpuBreakIntoDebugger, this)));
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Break into Host Debugger",
        "Ctrl+Pause/Break",
        std::bind(&EmulatorWindow::CpuBreakIntoHostDebugger, this)));
  }
  debug_menu->AddChild(std::move(cpu_menu));

  // GPU menu.
  auto gpu_menu = MenuItem::Create(MenuItem::Type::kPopup, "&GPU");
  {
    gpu_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Trace Frame", "F4",
                         std::bind(&EmulatorWindow::GpuTraceFrame, this)));
  }
  gpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
  {
    gpu_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Clear Runtime Caches", "F5",
                         std::bind(&EmulatorWindow::GpuClearCaches, this)));
  }
  debug_menu->AddChild(std::move(gpu_menu));
  main_menu->AddChild(std::move(debug_menu));

  // Display menu.
  auto display_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Display");
  {
    display_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Post-processing settings", "F6",
        std::bind(&EmulatorWindow::ToggleDisplayConfigDialog, this)));
  }
  display_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
  {
    display_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Fullscreen", "F11",
                         std::bind(&EmulatorWindow::ToggleFullscreen, this)));
    display_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Take Screenshot", "F12",
                         std::bind(&EmulatorWindow::TakeScreenshot, this)));
  }
  main_menu->AddChild(std::move(display_menu));

  // HID menu.
  auto hid_menu = MenuItem::Create(MenuItem::Type::kPopup, "&HID");
  {
    hid_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Toggle controller vibration", "",
        std::bind(&EmulatorWindow::ToggleControllerVibration, this)));
    hid_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Display controller hotkeys", "",
        std::bind(&EmulatorWindow::DisplayHotKeysConfig, this)));
  }
  main_menu->AddChild(std::move(hid_menu));

  // XMP menu
  auto xmp_menu = MenuItem::Create(MenuItem::Type::kPopup, "&XMP");
  {
    xmp_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&Show XMP Menu", "",
        std::bind(&EmulatorWindow::ToggleXMPConfigDialog, this)));
  }
  main_menu->AddChild(std::move(xmp_menu));

  // Help menu.
  auto help_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Help");
  {
    help_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "FA&Q...", "F1",
                         std::bind(&EmulatorWindow::ShowFAQ, this)));
    help_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
    help_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "Refresh game &compatibility...",
        std::bind(&EmulatorWindow::RefreshCompatData, this)));
    help_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "Update game &patches...",
                         std::bind(&EmulatorWindow::UpdateGamePatches, this)));
    help_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
    help_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "Build commit on GitHub...", "F2",
        std::bind(&EmulatorWindow::ShowBuildCommit, this)));
    help_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "Recent changes on GitHub...", []() {
          LaunchWebBrowser(
              "https://github.com/xenia-canary/xenia-canary/"
              "compare/" XE_BUILD_COMMIT "..." XE_BUILD_BRANCH);
        }));
    help_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
    help_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, "&About...",
        []() { LaunchWebBrowser("https://xenia.jp/about/"); }));
  }
  main_menu->AddChild(std::move(help_menu));

  window_->SetMainMenu(std::move(main_menu));

  window_->SetMainMenuEnabled(false);

  UpdateTitle();

  if (!window_->Open()) {
    XELOGE("Failed to open the platform window");
    return false;
  }

#ifdef XENIA_HAS_WX_UI
  static_cast<wx_ui::WxWindow*>(window_.get())
      ->AttachLibrary(
          [this](size_t index, int disc, const std::filesystem::path& path) {
            LibraryBoot(index, disc, path);
          },
          [this](const std::string& title_id, const std::string& title_name) {
            ShowGameConfigEditorDialog(title_id, title_name);
          },
          emulator_->storage_root(), emulator_->content_root(),
          [this]() { return emulator_->kernel_state(); });
#endif

  Profiler::SetUserIO(kZOrderProfiler, window_.get(), nullptr, nullptr);

  return true;
}

const char* EmulatorWindow::GetCvarValueForSwapPostEffect(
    gpu::CommandProcessor::SwapPostEffect effect) {
  switch (effect) {
    case gpu::CommandProcessor::SwapPostEffect::kFxaa:
      return "fxaa";
    case gpu::CommandProcessor::SwapPostEffect::kFxaaExtreme:
      return "fxaa_extreme";
    default:
      return "";
  }
}

gpu::CommandProcessor::SwapPostEffect
EmulatorWindow::GetSwapPostEffectForCvarValue(const std::string& cvar_value) {
  if (cvar_value == GetCvarValueForSwapPostEffect(
                        gpu::CommandProcessor::SwapPostEffect::kFxaa)) {
    return gpu::CommandProcessor::SwapPostEffect::kFxaa;
  }
  if (cvar_value == GetCvarValueForSwapPostEffect(
                        gpu::CommandProcessor::SwapPostEffect::kFxaaExtreme)) {
    return gpu::CommandProcessor::SwapPostEffect::kFxaaExtreme;
  }
  return gpu::CommandProcessor::SwapPostEffect::kNone;
}

const char* EmulatorWindow::GetCvarValueForGuestOutputPaintEffect(
    ui::Presenter::GuestOutputPaintConfig::Effect effect) {
  switch (effect) {
    case ui::Presenter::GuestOutputPaintConfig::Effect::kCas:
      return "cas";
    case ui::Presenter::GuestOutputPaintConfig::Effect::kFsr:
      return "fsr";
    default:
      return "";
  }
}

ui::Presenter::GuestOutputPaintConfig::Effect
EmulatorWindow::GetGuestOutputPaintEffectForCvarValue(
    const std::string& cvar_value) {
  if (cvar_value == GetCvarValueForGuestOutputPaintEffect(
                        ui::Presenter::GuestOutputPaintConfig::Effect::kCas)) {
    return ui::Presenter::GuestOutputPaintConfig::Effect::kCas;
  }
  if (cvar_value == GetCvarValueForGuestOutputPaintEffect(
                        ui::Presenter::GuestOutputPaintConfig::Effect::kFsr)) {
    return ui::Presenter::GuestOutputPaintConfig::Effect::kFsr;
  }
  return ui::Presenter::GuestOutputPaintConfig::Effect::kBilinear;
}

ui::Presenter::GuestOutputPaintConfig
EmulatorWindow::GetGuestOutputPaintConfigForCvars() {
  ui::Presenter::GuestOutputPaintConfig paint_config;
  paint_config.SetAllowOverscanCutoff(true);
  paint_config.SetEffect(GetGuestOutputPaintEffectForCvarValue(
      cvars::postprocess_scaling_and_sharpening));
  paint_config.SetCasAdditionalSharpness(
      float(cvars::postprocess_ffx_cas_additional_sharpness));
  paint_config.SetFsrMaxUpsamplingPasses(
      cvars::postprocess_ffx_fsr_max_upsampling_passes);
  paint_config.SetFsrSharpnessReduction(
      float(cvars::postprocess_ffx_fsr_sharpness_reduction));
  paint_config.SetDither(cvars::postprocess_dither);
  return paint_config;
}

void EmulatorWindow::ApplyDisplayConfigForCvars() {
  gpu::GraphicsSystem* graphics_system = emulator_->graphics_system();
  if (!graphics_system) {
    return;
  }

  gpu::CommandProcessor* command_processor =
      graphics_system->command_processor();
  if (command_processor) {
    command_processor->SetDesiredSwapPostEffect(
        GetSwapPostEffectForCvarValue(cvars::postprocess_antialiasing));
  }

  ui::Presenter* presenter = graphics_system->presenter();
  if (presenter) {
    presenter->SetGuestOutputPaintConfigFromUIThread(
        GetGuestOutputPaintConfigForCvars());
  }
}

void EmulatorWindow::OnKeyDown(ui::KeyEvent& e) {
  if (!emulator_initialized_) {
    return;
  }

  switch (e.virtual_key()) {
    case ui::VirtualKey::kO: {
      if (!e.is_ctrl_pressed()) {
        return;
      }
      FileOpen();
    } break;
    case ui::VirtualKey::kMultiply: {
      CpuTimeScalarReset();
    } break;
    case ui::VirtualKey::kSubtract: {
      CpuTimeScalarSetHalf();
    } break;
    case ui::VirtualKey::kAdd: {
      CpuTimeScalarSetDouble();
    } break;

    case ui::VirtualKey::kF3: {
      Profiler::ToggleDisplay();
    } break;

    case ui::VirtualKey::kF4: {
      GpuTraceFrame();
    } break;
    case ui::VirtualKey::kF5: {
      GpuClearCaches();
    } break;

    case ui::VirtualKey::kF6: {
      ToggleDisplayConfigDialog();
    } break;
    case ui::VirtualKey::kF11: {
      ToggleFullscreen();
    } break;
    case ui::VirtualKey::kF12: {
      TakeScreenshot();
    } break;

    case ui::VirtualKey::kEscape: {
      // Allow users to escape fullscreen (but not enter it).
      if (!window_->IsFullscreen()) {
        return;
      }
      SetFullscreen(false);
    } break;

#ifdef DEBUG
    case ui::VirtualKey::kF7: {
      // Save to file
      // TODO: Choose path based on user input, or from options
      // TODO: Spawn a new thread to do this.
      emulator()->SaveToFile("test.sav");
    } break;
    case ui::VirtualKey::kF8: {
      // Restore from file
      // TODO: Choose path from user
      // TODO: Spawn a new thread to do this.
      emulator()->RestoreFromFile("test.sav");
    } break;
#endif  // #ifdef DEBUG

    case ui::VirtualKey::kPause: {
      CpuBreakIntoDebugger();
    } break;
    case ui::VirtualKey::kCancel: {
      CpuBreakIntoHostDebugger();
    } break;

    case ui::VirtualKey::kF1: {
      ShowFAQ();
    } break;

    case ui::VirtualKey::kF2: {
      ShowBuildCommit();
    } break;

    case ui::VirtualKey::kF9: {
      RunPreviouslyPlayedTitle();
    } break;

    default:
      return;
  }

  e.set_handled(true);
}

void EmulatorWindow::OnMouseDown(const ui::MouseEvent& e) {
  if (imgui_drawer_->IsAnyDialogOpen()) {
    return;
  }

  if (e.button() == ui::MouseEvent::Button::kLeft) {
    ToggleFullscreenOnDoubleClick();
  }
}

void EmulatorWindow::OnMouseUp(const ui::MouseEvent& e) {
  last_mouse_up = steady_clock::now();
}

void EmulatorWindow::TakeScreenshot() {
  xe::ui::RawImage image;

  imgui_drawer_->EnableNotifications(false);

  if (!GetGraphicsSystemPresenter()->CaptureGuestOutput(image) ||
      GetGraphicsSystemPresenter() == nullptr) {
    XELOGE("Failed to capture guest output for screenshot");
    return;
  }

  imgui_drawer_->EnableNotifications(true);
  ExportScreenshot(image);
}

void EmulatorWindow::ExportScreenshot(const xe::ui::RawImage& image) {
  auto t = std::time(nullptr);

  // The format is: Year-Month-DayTHours-Minutes-Seconds based off ISO 8601
  std::string datetime =
      fmt::format("{:%Y-%m-%dT%H-%M-%S}", *std::localtime(&t));

  // Get the title id of the game because some titles contain characters that
  // cannot be used as a directory
  std::string title_id;
  if (emulator()->title_id()) {
    title_id = fmt::format("{:08X}", emulator()->title_id());
  } else {
    XELOGE("Failed to get the current title id");
    return;
  }

  // Find where xenia.exe or xenia_canary.exe is located and create a
  // screenshots folder
  auto screenshot_path =
      (xe::filesystem::GetExecutableFolder() / "screenshots" / title_id);

  if (!std::filesystem::exists(screenshot_path)) {
    std::filesystem::create_directories(screenshot_path);
  }

  std::string filename = fmt::format("{} - {}.png", title_id, datetime);
  SaveImage(screenshot_path / filename, image);

  const std::string notification_text =
      fmt::format("Screenshot saved: {}", filename);

  app_context_.CallInUIThread([&, notification_text]() {
    new xe::ui::HostNotificationWindow(imgui_drawer(), "Screenshot Created!",
                                       notification_text, 0);
  });
}

// Converts a RawImage into a PNG file
void EmulatorWindow::SaveImage(const std::filesystem::path& filepath,
                               const xe::ui::RawImage& image) {
  auto file = std::ofstream(filepath, std::ios::binary);
  if (!file.is_open()) {
    XELOGE("Failed to open file for writing: {}", filepath);
    return;
  }

  auto result = stbi_write_png_to_func(
      [](void* context, void* data, int size) {
        auto file = reinterpret_cast<std::ofstream*>(context);
        file->write(reinterpret_cast<const char*>(data), size);
      },
      &file, image.width, image.height, 4, image.data.data(),
      (int)image.stride);
  if (result == 0) {
    XELOGE("Failed to write PNG to file: {}", filepath);
    return;
  }
}

void EmulatorWindow::ToggleFullscreenOnDoubleClick() {
  if (cvars::disable_doubleclick_fullscreen) {
    return;
  }

  // this function tests if user has double clicked.
  // if double click was achieved the fullscreen gets toggled
  const auto now = steady_clock::now();  // current mouse event time
  constexpr int16_t mouse_down_max_threshold = 250;
  constexpr int16_t mouse_up_max_threshold = 250;
  constexpr int16_t mouse_up_down_max_delta = 100;
  // max delta to prevent 'chaining' of double clicks with next mouse events

  const auto last_mouse_down_delta = diff_in_ms(now, last_mouse_down);
  if (last_mouse_down_delta >= mouse_down_max_threshold) {
    last_mouse_down = now;
    return;
  }

  const auto last_mouse_up_delta = diff_in_ms(now, last_mouse_up);
  const auto mouse_event_deltas = diff_in_ms(last_mouse_up, last_mouse_down);
  if (last_mouse_up_delta >= mouse_up_max_threshold) {
    return;
  }

  if (mouse_event_deltas < mouse_up_down_max_delta) {
    ToggleFullscreen();
  }
}

void EmulatorWindow::FileDrop(const std::filesystem::path& path) {
  if (!emulator_initialized_) {
    return;
  }

  RunTitle(path);
}

void EmulatorWindow::FileOpen() {
  std::filesystem::path path;

  auto file_picker = xe::ui::FilePicker::Create();
  file_picker->set_mode(ui::FilePicker::Mode::kOpen);
  file_picker->set_type(ui::FilePicker::Type::kFile);
  file_picker->set_multi_selection(false);
  file_picker->set_title("Select Content Package");
  file_picker->set_extensions({
      {"Supported Files", "*.iso;*.xex;*.zar;*.*"},
      {"Disc Image (*.iso)", "*.iso"},
      {"Disc Archive (*.zar)", "*.zar"},
      {"Xbox Executable (*.xex)", "*.xex"},
      //{"Content Package (*.xcp)", "*.xcp" },
      {"All Files (*.*)", "*.*"},
  });
  if (file_picker->Show(window_.get())) {
    auto selected_files = file_picker->selected_files();
    if (!selected_files.empty()) {
      path = selected_files[0];
    }
    // Only run the title if a file is selected
    RunTitle(path);
  }
}

void EmulatorWindow::FileClose() { StopTitle(); }

void EmulatorWindow::ApplyContentVisibility() {
  const bool title_open = emulator_ && emulator_->is_title_open();
  const bool render_active = title_open || target_pending_launch_;
  if (render_active) {
    ShowGame();
  } else {
    ShowLibrary();
  }
  UpdateTitleDependentMenuItems();
}

void EmulatorWindow::StopTitle() {
  if (!emulator_->is_title_open()) {
    return;
  }
  // Tear down Discord presence as the game stops, before returning to the
  // library.
  if (cvars::discord) {
    discord::DiscordPresence::Shutdown();
  }
  // NOTE: target_pending_launch_ is intentionally not cleared here. If a
  // relaunch is in flight, clearing it would defeat the RunTitle re-entry
  // guard and allow a second teardown mid-launch. The completion lambda
  // below clears it on every path.
  // When in-process relaunch is off, spawn a fresh process with no target
  // (return to library) instead of resetting in-process. Falls through to
  // the in-process stop when no spawn handler is wired.
  if (!cvars::in_process_title_relaunch) {
    if (auto cb = emulator_->on_launch_new_title()) {
      cb(/*host_path=*/{}, /*launch_module=*/{}, /*launch_flags=*/0,
         /*launch_data=*/{});
      return;
    }
    XELOGW(
        "StopTitle: out-of-process stop requested but no spawn handler "
        "is wired; stopping in-process");
  }
  // Detach the presenter first, on the UI thread: the paint loop drives the
  // GPU completion timelines, so it must not touch GPU objects while the
  // background thread below tears them down.
  ShutdownGraphicsSystemPresenterPainting();
  window_->SetIcon(nullptr, 0);
  ClearDialogs();
  // ResetTitle terminates guest threads and tears down subsystems, so it must
  // run off the UI thread.
  std::thread([this]() {
    emulator_->ResetTitle();
    app_context_.CallInUIThread([this]() {
      // Nothing is running any more, so the overrides that title loaded must
      // stop deciding the application's values: a per-title file is only meant
      // to apply to its own title, and the global config is what governs
      // between titles (and what the config editor edits).
      config::ClearGameConfig();
      UpdateTitle();
      UpdateTitleDependentMenuItems();
      ShowLibrary();
      // Drop fullscreen back to windowed now that the presenter is gone, so
      // the user lands on the library at the default size.
      if (window_->IsFullscreen()) {
        SetFullscreen(false);
      }
      target_pending_launch_ = false;
      ApplyContentVisibility();
    });
  }).detach();
}

bool EmulatorWindow::StopTitleFromGuestThread(
    std::string host_path, std::string launch_path, uint32_t launch_flags,
    std::vector<uint8_t> launch_data) {
  if (!emulator_->is_title_open()) {
    return false;
  }
  // Out-of-process relaunch: hand the captured loader data to a fresh
  // process and let the kernel fall through to TerminateTitle (return
  // false). The new process replays the launch; this one exits.
  if (!cvars::in_process_title_relaunch) {
    if (auto cb = emulator_->on_launch_new_title()) {
      // host_path empty = plain dashboard exit back to library.
      if (host_path.empty()) {
        cb(/*host_path=*/{}, /*launch_module=*/{}, /*launch_flags=*/0,
           /*launch_data=*/{});
      } else {
        // NOTE: the guest launch_path is passed through the launch_module
        // slot: Canary has no --launch_flags/--launch_data cvars, so the
        // spawn handler forwards it as --launch_module (best effort).
        cb(host_path, launch_path, launch_flags, launch_data);
      }
    }
    return false;
  }
  // Runs on a guest thread: hop presentation teardown through the UI thread
  // first so the paint loop can't touch GPU objects during teardown.
  // on_before_shutdown (fired inside RelaunchTitle->Shutdown) also detaches,
  // but this covers the gap before the worker starts.
  app_context_.CallInUIThreadSynchronous([this]() {
    ShutdownGraphicsSystemPresenterPainting();
    window_->SetIcon(nullptr, 0);
    ClearDialogs();
  });
  // Detached non-guest thread: RelaunchTitle terminates all guest threads
  // (including this caller, which parks in KernelState::ExitToDashboard) and
  // performs the full Shutdown/Setup/Launch cycle under launch_mutex_.
  Emulator* emulator = emulator_;
  std::thread([this, emulator, host_path = std::move(host_path),
               launch_path = std::move(launch_path), launch_flags,
               launch_data = std::move(launch_data)]() mutable {
    if (host_path.empty()) {
      // Plain dashboard exit: reset to idle and return to the library.
      X_STATUS reset_result = emulator->ResetTitle();
      app_context_.CallInUIThread([this, reset_result]() {
        if (XFAILED(reset_result)) {
          xe::ui::ImGuiDialog::ShowMessageBox(
              imgui_drawer_.get(), "Title Stop Failed!",
              "Failed to stop the running title cleanly.\n\nCheck xenia.log "
              "for technical details.");
        }
        // Same as StopTitle: the finished title's overrides must not outlive
        // it. Also clear any pending-launch guard so future opens aren't
        // stuck ignored.
        target_pending_launch_ = false;
        config::ClearGameConfig();
        UpdateTitle();
        UpdateTitleDependentMenuItems();
        ShowLibrary();
      });
      return;
    }
    emulator->RelaunchTitle(host_path, launch_path, launch_flags,
                            std::move(launch_data));
    if (emulator->is_title_open()) {
      // Consumed in-process: drop the persisted request so a later restart
      // doesn't replay it. Kept on failure so the next boot can retry.
      auto xam_after =
          emulator->kernel_state()->GetKernelModule<kernel::xam::XamModule>(
              "xam.xex");
      if (xam_after) {
        xam_after->ClearSavedLoaderData();
      }
    }
    std::filesystem::path target = xe::to_path(host_path);
    auto abs_path = std::filesystem::absolute(target);
    // RelaunchTitle already ran LaunchPath; FinishTitleLaunch only does the
    // UI half (recent list, ShowGame, sizing). Derive the status from
    // whether a title is open now.
    X_STATUS result =
        emulator->is_title_open() ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
    app_context_.CallInUIThread([this, result, target, abs_path]() mutable {
      FinishTitleLaunch(target, abs_path, result);
    });
  }).detach();
  return true;
}

void EmulatorWindow::LibraryBoot(size_t index, int disc_number,
                                 const std::filesystem::path& path) {
  has_library_boot_ = true;
  library_boot_index_ = index;
  library_boot_disc_ = disc_number;
  if (XFAILED(RunTitle(path))) {
    has_library_boot_ = false;
  }
}

void EmulatorWindow::UpdateTitleDependentMenuItems() {
  if (!stop_item_) {
    return;
  }
  const bool title_open = emulator_->is_title_open();
  stop_item_->SetEnabled(title_open);
  // The config editor applies changes live to subsystems (GPU and audio
  // backends, mounts, the window) that a running title owns, so it is only
  // offered with no title loaded - a restart of the app is not enough to make
  // those edits safe, the guest has to be gone.
  if (config_editor_item_) {
    config_editor_item_->SetEnabled(!title_open);
  }
  window_->CompleteMainMenuItemsUpdate();
}

void EmulatorWindow::ShowLibrary() {
#ifdef XENIA_HAS_WX_UI
  static_cast<wx_ui::WxWindow*>(window_.get())->ShowLibrary();
#endif
}

void EmulatorWindow::ShowGame() {
#ifdef XENIA_HAS_WX_UI
  static_cast<wx_ui::WxWindow*>(window_.get())->ShowGame();
#endif
}

void EmulatorWindow::ShowProfileMenu() {
#ifdef XENIA_HAS_WX_UI
  static_cast<wx_ui::WxWindow*>(window_.get())->OnProfileMenu();
#else
  ToggleProfilesConfigDialog();
#endif
}

void EmulatorWindow::ShowConsoleSettingsDialog() {
#ifdef XENIA_HAS_WX_UI
  auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
  wx_ui::ShowConsoleSettingsDialog(wx_window, emulator_->kernel_state());
#else
  ToggleConsoleSettingsDialog();
#endif
}

void EmulatorWindow::ShowGameConfigEditorDialog(const std::string& title_id,
                                                const std::string& title_name) {
  // The per-game editor writes the same cvar layer and applies it live, so it
  // carries the config editor's no-running-title restriction (see
  // UpdateTitleDependentMenuItems). The library's context menu has no enabled
  // state, so this is the only gate for it.
  if (emulator_->is_title_open()) {
    XELOGW("Game config editor is unavailable while a title is running.");
    return;
  }
#ifdef XENIA_HAS_WX_UI
  auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
  wx_ui::ShowGameConfigDialog(wx_window, title_id, title_name);
#else
  XELOGW("Game config editor requires the wxWidgets UI.");
#endif
}

void EmulatorWindow::ShowConfigEditorDialog() {
  // The menu item is disabled while a title is running (see
  // UpdateTitleDependentMenuItems); this keeps any other path into the editor
  // from opening it too.
  if (emulator_->is_title_open()) {
    return;
  }
#ifdef XENIA_HAS_WX_UI
  auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
  wx_ui::ShowConfigEditorDialog(wx_window);
#else
  XELOGW("Config editor requires the wxWidgets UI.");
#endif
}

namespace {

bool IsPathInside(const std::filesystem::path& path,
                  const std::filesystem::path& root) {
  std::error_code ec = {};
  auto relative = std::filesystem::relative(path, root, ec);
  if (ec || relative.empty() || relative.is_absolute()) {
    return false;
  }
  return *relative.begin() != std::filesystem::path("..");
}

}  // namespace

void EmulatorWindow::AddInstalledContentToLibrary(
    const std::shared_ptr<std::vector<Emulator::ContentInstallEntry>>& entries,
    bool only_inside_content) {
  std::vector<std::filesystem::path> scan;
  const auto content_root = emulator_->content_root();
  for (auto& entry : *entries) {
    if (entry.installation_state_ != Emulator::InstallState::installed) {
      continue;
    }
    if (!only_inside_content &&
        entry.content_type_ != xe::XContentType::kInstalledGame &&
        entry.content_type_ != xe::XContentType::kArcadeTitle &&
        entry.content_type_ != xe::XContentType::kXbox360Title &&
        entry.content_type_ != xe::XContentType::kGameDemo) {
      continue;
    }
    auto final_path = entry.data_installation_path_ / entry.filename_;
    if (only_inside_content && !IsPathInside(final_path, content_root)) {
      continue;
    }
    std::error_code ec = {};
    if (std::filesystem::is_directory(final_path, ec)) {
      // Extracted package: launchable default.xex in file form.
      bool found = false;
      for (const auto& e :
           std::filesystem::directory_iterator(final_path, ec)) {
        if (ec) {
          break;
        }
        std::error_code ec2 = {};
        if (e.is_regular_file(ec2) &&
            xe::utf8::lower_ascii(e.path().filename().string()) ==
                "default.xex") {
          final_path = e.path();
          found = true;
          break;
        }
      }
      if (!found) {
        continue;
      }
    }
    scan.push_back(final_path);
  }
  if (scan.empty()) {
    return;
  }
  // Same full scan as manual Add/Scan (metadata + icon search + artwork).
  app_context_.CallInUIThread([this, scan]() {
#ifdef XENIA_HAS_WX_UI
    auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
    if (!wx_window->IsLibraryAttached()) {
      return;
    }
    wx_window->ImportLibraryPaths(scan);
#endif
  });
}

void EmulatorWindow::InstallContent() {
  std::vector<std::filesystem::path> paths;

  auto file_picker = xe::ui::FilePicker::Create();
  file_picker->set_mode(ui::FilePicker::Mode::kOpen);
  file_picker->set_type(ui::FilePicker::Type::kFile);
  file_picker->set_multi_selection(true);
  file_picker->set_title("Select Content Package");
  file_picker->set_extensions({
      {"All Files (*.*)", "*.*"},
  });
  if (file_picker->Show(window_.get())) {
    paths = file_picker->selected_files();
  }

  if (paths.empty()) {
    return;
  }

  std::shared_ptr<std::vector<Emulator::ContentInstallEntry>>
      content_installation_status =
          std::make_shared<std::vector<Emulator::ContentInstallEntry>>();

  for (const auto& path : paths) {
    content_installation_status->push_back({path});
  }

  for (auto& entry : *content_installation_status) {
    emulator_->ProcessContentPackageHeader(entry.path_, entry);
  }

  auto installationThread = std::thread([this, content_installation_status] {
    for (auto& entry : *content_installation_status) {
      emulator_->InstallContentPackage(entry.path_, entry);
    }
    AddInstalledContentToLibrary(content_installation_status, false);
  });
  installationThread.detach();

#ifdef XENIA_HAS_WX_UI
  // The ImGui dialog renders behind the library view when attached.
  if (auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
      wx_window->IsLibraryAttached()) {
    wx_ui::ShowContentInstallDialog(wx_window, content_installation_status,
                                    emulator_->content_root(), false);
  } else
#endif
  {
    new ContentInstallDialog(imgui_drawer_.get(), *this,
                             content_installation_status);
  }
}

void EmulatorWindow::ExtractContent(const std::filesystem::path file) {
  std::vector<std::filesystem::path> package_files;
  std::filesystem::path extract_dir;

  if (!file.empty()) {
    package_files.push_back(file);
  } else {
    auto file_picker = xe::ui::FilePicker::Create();
    file_picker->set_mode(ui::FilePicker::Mode::kOpen);
    file_picker->set_type(ui::FilePicker::Type::kFile);
    file_picker->set_multi_selection(true);
    file_picker->set_title("Select Content Package");
    file_picker->set_extensions({
        {"All Files (*.*)", "*.*"},
    });

    if (file_picker->Show(window_.get())) {
      package_files = file_picker->selected_files();
    }

    if (package_files.empty()) {
      return;
    }
  }
  auto save_file_picker = xe::ui::FilePicker::Create();
  save_file_picker->set_mode(ui::FilePicker::Mode::kOpen);
  save_file_picker->set_type(ui::FilePicker::Type::kDirectory);
  save_file_picker->set_title("Select Directory to Extract");

  if (save_file_picker->Show(window_.get())) {
    extract_dir = save_file_picker->selected_files().front();
  }

  if (extract_dir.empty()) {
    return;
  }

  std::shared_ptr<std::vector<Emulator::ContentInstallEntry>>
      content_installation_status =
          std::make_shared<std::vector<Emulator::ContentInstallEntry>>();

  for (const auto& path : package_files) {
    content_installation_status->push_back({path});
  }

  for (auto& entry : *content_installation_status) {
    emulator_->ProcessContentPackageHeader(entry.path_, entry);
    entry.data_installation_path_ = extract_dir;
    entry.header_installation_path_ = "";
  }

  auto installationThread = std::thread([this, content_installation_status] {
    for (auto& entry : *content_installation_status) {
      emulator_->ExtractContentPackage(entry.path_, entry);
    }
    AddInstalledContentToLibrary(content_installation_status, true);
  });
  installationThread.detach();

#ifdef XENIA_HAS_WX_UI
  // The ImGui dialog renders behind the library view when attached.
  if (auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
      wx_window->IsLibraryAttached()) {
    wx_ui::ShowContentInstallDialog(wx_window, content_installation_status,
                                    emulator_->content_root(), true);
  } else
#endif
  {
    new ContentInstallDialog(imgui_drawer_.get(), *this,
                             content_installation_status);
  }
}

void EmulatorWindow::ExtractZarchive() {
  std::vector<std::filesystem::path> zarchive_files;
  std::filesystem::path extract_dir;

  auto file_picker = xe::ui::FilePicker::Create();
  file_picker->set_mode(ui::FilePicker::Mode::kOpen);
  file_picker->set_type(ui::FilePicker::Type::kFile);
  file_picker->set_multi_selection(true);
  file_picker->set_title("Select Zar Package");
  file_picker->set_extensions({
      {"Zarchive Files (*.zar)", "*.zar"},
  });

  if (file_picker->Show(window_.get())) {
    zarchive_files = file_picker->selected_files();
  }

  if (zarchive_files.empty()) {
    return;
  }

  file_picker->set_type(ui::FilePicker::Type::kDirectory);
  file_picker->set_title("Select Directory to Extract");

  if (file_picker->Show(window_.get())) {
    extract_dir = file_picker->selected_files().front();
  }

  if (extract_dir.empty()) {
    return;
  }

  std::string extract_overview = "";

  for (auto& zarchive_file_path : zarchive_files) {
    extract_overview += "\n" + path_to_utf8(zarchive_file_path);
  }

  app_context_.CallInUIThread([&]() {
    new xe::ui::HostNotificationWindow(imgui_drawer(), "Extracting...",
                                       string_util::trim(extract_overview), 0);
  });

  auto run = [this, extract_dir, zarchive_files]() -> void {
    std::string summary = "";

    for (auto& zarchive_file_path : zarchive_files) {
      // Normalize the path and make absolute.
      auto abs_path = std::filesystem::absolute(zarchive_file_path);
      std::filesystem::path abs_extract_dir;

      if (zarchive_files.size() > 1) {
        abs_extract_dir =
            std::filesystem::absolute((extract_dir / abs_path.stem()));
      } else {
        abs_extract_dir = std::filesystem::absolute(extract_dir);
      }

      XELOGI("Extracting zar package: {}\n",
             zarchive_file_path.filename().string());

      auto result =
          emulator_->ExtractZarchivePackage(abs_path, abs_extract_dir);

      if (result != X_STATUS_SUCCESS) {
        std::error_code ec;

        if (!std::filesystem::is_empty(abs_extract_dir)) {
          std::filesystem::remove(abs_extract_dir, ec);
        }

        summary += fmt::format("\nFailed: {}", zarchive_file_path);

        XELOGE("Failed to extract Zarchive package.", result);
      } else {
        summary += fmt::format("\nSuccess: {}", abs_extract_dir);
      }
    }

    new xe::ui::HostNotificationWindow(imgui_drawer(), "Zar Extraction Summary",
                                       string_util::trim(summary), 0);
  };

  auto zarThread = std::thread(run);
  zarThread.detach();
}

void EmulatorWindow::CreateZarchive() {
  std::vector<std::filesystem::path> content_dirs;
  std::filesystem::path zarchive_dir;

  auto file_picker = xe::ui::FilePicker::Create();
  file_picker->set_mode(ui::FilePicker::Mode::kOpen);
  file_picker->set_type(ui::FilePicker::Type::kDirectory);
  file_picker->set_multi_selection(true);
  file_picker->set_title("Select Contents");

  if (file_picker->Show(window_.get())) {
    content_dirs = file_picker->selected_files();
  }

  if (content_dirs.empty()) {
    return;
  }

  if (content_dirs.size() == 1) {
    file_picker->set_mode(ui::FilePicker::Mode::kSave);
    file_picker->set_type(ui::FilePicker::Type::kFile);
    file_picker->set_multi_selection(false);
    file_picker->set_file_name(content_dirs.front().filename().string());
    file_picker->set_default_extension("zar");
    file_picker->set_title("Zarchive File");
    file_picker->set_extensions({
        {"Zarchive File (*.zar)", "*.zar"},
    });
  } else {
    file_picker->set_title("Output Directory");
  }

  if (file_picker->Show(window_.get())) {
    zarchive_dir = file_picker->selected_files().front();
  }

  if (zarchive_dir.empty()) {
    return;
  }

  std::string create_overview = "";

  std::map<std::filesystem::path, std::filesystem::path> zarchive_files{};

  for (auto& content_path : content_dirs) {
    // Normalize the path and make absolute.
    auto abs_content_dir = std::filesystem::absolute(content_path);
    std::filesystem::path abs_zarchive_file;

    if (content_dirs.size() > 1) {
      abs_zarchive_file = std::filesystem::absolute(
          (zarchive_dir / abs_content_dir.filename().concat(".zar")));
    } else {
      abs_zarchive_file = std::filesystem::absolute(zarchive_dir);
    }

    zarchive_files[content_path] = abs_zarchive_file;

    create_overview += "\n" + path_to_utf8(abs_zarchive_file);
  }

  app_context_.CallInUIThread([&]() {
    new xe::ui::HostNotificationWindow(imgui_drawer(), "Creating...",
                                       string_util::trim(create_overview), 0);
  });

  auto run = [this, zarchive_files]() -> void {
    std::string summary = "";

    for (auto const& [content_path, zarchive_file] : zarchive_files) {
      // Normalize the path and make absolute.
      auto abs_content_dir = std::filesystem::absolute(content_path);

      XELOGI("Creating zar package: {}\n", zarchive_file.filename().string());

      auto result =
          emulator_->CreateZarchivePackage(abs_content_dir, zarchive_file);

      if (result != X_ERROR_SUCCESS) {
        std::error_code ec;

        // delete incomplete output file
        std::filesystem::remove(zarchive_file, ec);

        summary += fmt::format("\nFailed: {}", abs_content_dir);

        XELOGE("Failed to create Zarchive package.", result);
      } else {
        summary += fmt::format("\nSuccess: {}", zarchive_file);
      }
    }

    new xe::ui::HostNotificationWindow(imgui_drawer(), "Zar Creation Summary",
                                       string_util::trim(summary), 0);
  };

  auto zarThread = std::thread(run);
  zarThread.detach();
}

void EmulatorWindow::ShowContentDirectory() {
  auto content_root = emulator_->content_root();

  if (!std::filesystem::exists(content_root)) {
    std::filesystem::create_directories(content_root);
  }

  LaunchFileExplorer(content_root);
}

void EmulatorWindow::CpuTimeScalarReset() {
  Clock::set_guest_time_scalar(1.0);
  UpdateTitle();
}

void EmulatorWindow::CpuTimeScalarSetHalf() {
  Clock::set_guest_time_scalar(Clock::guest_time_scalar() / 2.0);
  UpdateTitle();
}

void EmulatorWindow::CpuTimeScalarSetDouble() {
  Clock::set_guest_time_scalar(Clock::guest_time_scalar() * 2.0);
  UpdateTitle();
}

void EmulatorWindow::CpuBreakIntoDebugger() {
  if (!cvars::debug) {
    xe::ui::ImGuiDialog::ShowMessageBox(imgui_drawer_.get(), "Xenia Debugger",
                                        "Xenia must be launched with the "
                                        "--debug flag in order to enable "
                                        "debugging.");
    return;
  }
  auto processor = emulator()->processor();
  // Always present after Setup; kept as belt and braces.
  if (!processor) {
    return;
  }
  if (processor->execution_state() == cpu::ExecutionState::kRunning) {
    // Currently running, so interrupt (and show the debugger).
    processor->Pause();
  } else {
    // Not running, so just bring the debugger into focus.
    processor->ShowDebugger();
  }
}

void EmulatorWindow::CpuBreakIntoHostDebugger() { xe::debugging::Break(); }

void EmulatorWindow::GpuTraceFrame() {
  // Title systems only exist while a title is loaded.
  if (auto* graphics_system = emulator()->graphics_system()) {
    graphics_system->RequestFrameTrace();
  }
}

void EmulatorWindow::GpuClearCaches() {
  // Title systems only exist while a title is loaded.
  if (auto* graphics_system = emulator()->graphics_system()) {
    graphics_system->ClearCaches();
  }
}

void EmulatorWindow::SetFullscreen(bool fullscreen_) {
  if (window_->IsFullscreen() == fullscreen_) {
    return;
  }

  OVERRIDE_bool(fullscreen, fullscreen_);

  window_->SetFullscreen(fullscreen_);
  window_->SetCursorVisibility(fullscreen_
                                   ? ui::Window::CursorVisibility::kAutoHidden
                                   : ui::Window::CursorVisibility::kVisible);
}

void EmulatorWindow::ToggleFullscreen() {
  SetFullscreen(!window_->IsFullscreen());
}

void EmulatorWindow::ToggleDisplayConfigDialog() {
  if (!display_config_dialog_) {
    display_config_dialog_ =
        std::make_unique<DisplayConfigDialog>(imgui_drawer_.get(), *this);
  } else {
    if (display_config_dialog_->IsClosing()) {
      display_config_dialog_.release();
    } else {
      display_config_dialog_.reset();
    }
  }
}

void EmulatorWindow::ToggleProfilesConfigDialog() {
  if (!profile_config_dialog_) {
    disable_hotkeys_ = true;

    if (emulator_->kernel_state()->xam_state()->IsUIActive()) {
      return;
    }

    emulator_->kernel_state()->BroadcastNotification(kXNotificationSystemUI,
                                                     true);
    emulator_->kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

    profile_config_dialog_ =
        std::make_unique<ProfileConfigDialog>(imgui_drawer_.get(), this);
  } else {
    disable_hotkeys_ = false;
    emulator_->kernel_state()->BroadcastNotification(kXNotificationSystemUI,
                                                     false);
    if (profile_config_dialog_->IsClosing()) {
      profile_config_dialog_.release();
    } else {
      profile_config_dialog_.reset();
    }
    emulator_->kernel_state()->xam_state()->is_xam_dialog_present_.store(false);
  }
}

void EmulatorWindow::ToggleXMPConfigDialog() {
  if (!xmp_config_dialog_) {
    xmp_config_dialog_ = std::unique_ptr<XMPConfigDialog>(
        new XMPConfigDialog(imgui_drawer_.get(), *this));
  } else {
    xmp_config_dialog_.reset();
  }
}

void EmulatorWindow::ToggleConsoleSettingsDialog() {
  if (!console_settings_dialog_) {
    console_settings_dialog_ =
        std::unique_ptr<ConsoleSettingsDialog>(new ConsoleSettingsDialog(
            imgui_drawer_.get(), *this, emulator_->kernel_state()->xconfig()));
  } else {
    if (console_settings_dialog_->IsClosing()) {
      console_settings_dialog_.release();
    } else {
      console_settings_dialog_.reset();
    }
  }
}

void EmulatorWindow::ToggleContentListDialog() {
  if (!content_list_dialog_) {
    content_list_dialog_ = std::unique_ptr<ContentListDialog>(
        new ContentListDialog(imgui_drawer_.get(), *this,
                              emulator_->kernel_state()->content_manager()));
  } else {
    if (content_list_dialog_->IsClosing()) {
      content_list_dialog_.release();
    } else {
      content_list_dialog_.reset();
    }
  }
}

void EmulatorWindow::ToggleControllerVibration() {
  auto input_sys = emulator()->input_system();
  if (input_sys) {
    auto input_lock = input_sys->lock();

    input_sys->ToggleVibration();

    if (emulator_->kernel_state()) {
      emulator_->kernel_state()->BroadcastNotification(
          kXNotificationSystemProfileSettingChanged,
          static_cast<uint32_t>(input_sys->GetConnectedSlots().count()));
    }
  }
}

void EmulatorWindow::ShowFAQ() {
  LaunchWebBrowser("https://github.com/xenia-canary/xenia-canary/wiki/FAQ");
}

void EmulatorWindow::RefreshCompatData() {
#ifdef XENIA_HAS_WX_UI
  auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
  if (wx_window && wx_window->IsLibraryAttached()) {
    wx_window->RefreshCompat(true);
  } else {
    XELOGW("Compatibility refresh needs the game library.");
  }
#else
  XELOGW("Compatibility refresh requires the wxWidgets UI.");
#endif
}

void EmulatorWindow::UpdateGamePatches() {
#ifdef XENIA_HAS_WX_UI
  auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
  if (wx_window && wx_window->IsLibraryAttached()) {
    wx_window->UpdateGamePatches();
  } else {
    XELOGW("Game patch update needs the game library.");
  }
#else
  XELOGW("Game patch update requires the wxWidgets UI.");
#endif
}

void EmulatorWindow::ShowBuildCommit() {
#ifdef XE_BUILD_IS_PR
  LaunchWebBrowser(
      "https://github.com/xenia-canary/xenia-canary/pull/" XE_BUILD_PR_NUMBER);
#else
  LaunchWebBrowser(
      "https://github.com/xenia-canary/xenia-canary/commit/" XE_BUILD_COMMIT);
#endif
}

void EmulatorWindow::UpdateTitle() {
  xe::StringBuffer sb;
  sb.Append(base_title_);

  // Title information, if available
  if (emulator()->is_title_open()) {
    sb.AppendFormat(" | [{:08X}", emulator()->title_id());
    auto title_version = emulator()->title_version();
    if (!title_version.empty()) {
      sb.Append(" v");
      sb.Append(title_version);
    }
    sb.Append("]");

    auto title_name = emulator()->title_name();
    if (!title_name.empty()) {
      sb.Append(" ");
      sb.Append(title_name);
    }
  }

  // Graphics system name, if available
  auto graphics_system = emulator()->graphics_system();
  if (graphics_system) {
    auto graphics_name = graphics_system->name();
    if (!graphics_name.empty()) {
      sb.Append(" <");
      sb.Append(graphics_name);
      sb.Append(">");
    }
  }

  if (Clock::guest_time_scalar() != 1.0) {
    sb.AppendFormat(" (@{:.2f}x)", Clock::guest_time_scalar());
  }

  if (initializing_shader_storage_) {
    sb.Append(" (Preloading shaders\u2026)");
  }

  patcher::Patcher* patcher = emulator()->patcher();
  if (patcher && patcher->IsAnyPatchApplied()) {
    sb.Append(" [Patches Applied]");
  }

  patcher::PluginLoader* pluginloader = emulator()->plugin_loader();
  if (pluginloader && pluginloader->IsAnyPluginLoaded()) {
    sb.Append(" [Plugins Loaded]");
  }

  window_->SetTitle(sb.to_string_view());
}

void EmulatorWindow::SetInitializingShaderStorage(bool initializing) {
  if (initializing_shader_storage_ == initializing) {
    return;
  }
  initializing_shader_storage_ = initializing;
  UpdateTitle();
}

// Notes:
// SDL and XInput both support the guide button
//
// Assumes titles do not use the guide button.
// For titles that do such as dashboards these titles could be excluded based on
// their title ID.
//
// Xbox Gamebar:
// If the Xbox Gamebar overlay is enabled Windows will consume the guide
// button's input, this can be seen using hid-demo.
//
// Workaround: Detect if the Xbox Gamebar overlay is enabled then use the BACK
// button instead of the GUIDE button. Therefore BACK and GUIDE are reserved
// buttons for hotkeys.
//
// This is not an issue with DualShock controllers because Windows will not
// open the gamebar overlay using the PlayStation menu button.
//
// Xbox One S Controller:
// The guide button on this controller is very buggy no idea why.
// Using xinput usually registers after a double tap.
// Doesn't work at all using SDL.
// Needs more testing.
//
// Steam:
// If guide button focus is enabled steam will open.
// Steam uses BACK + GUIDE to open an On-Screen keyboard, however this is not a
// problem since both these buttons are reserved.
const std::map<int, EmulatorWindow::ControllerHotKey> controller_hotkey_map = {
    // Must use the Guide Button for all pass through hotkeys
    {X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::ReadbackResolve,
         "A + Guide = Toggle Readback Resolve", true)},
    {X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::ToggleLogging,
         "B + Guide = Toggle between loglevel set in config and the 'Disabled' "
         "loglevel.",
         true, true)},
    {X_INPUT_GAMEPAD_Y | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::ToggleFullscreen,
         "Y + Guide = Toggle Fullscreen", true)},
    {X_INPUT_GAMEPAD_X | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::ClearMemoryPageState,
         "X + Guide = Toggle Clear Memory Page State", true)},

    {X_INPUT_GAMEPAD_RIGHT_SHOULDER | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::ClearGPUCache,
         "Right Shoulder + Guide = Clear GPU Cache", true)},
    {X_INPUT_GAMEPAD_LEFT_SHOULDER | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::ToggleControllerVibration,
         "Left Shoulder + Guide = Toggle Controller Vibration", true)},

    // CPU Time Scalar with no rumble feedback
    {X_INPUT_GAMEPAD_DPAD_DOWN | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::CpuTimeScalarSetHalf,
         "D-PAD Down + Guide = Half CPU Scalar")},
    {X_INPUT_GAMEPAD_DPAD_UP | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::CpuTimeScalarSetDouble,
         "D-PAD Up + Guide = Double CPU Scalar")},
    {X_INPUT_GAMEPAD_DPAD_RIGHT | X_INPUT_GAMEPAD_GUIDE,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::CpuTimeScalarReset,
         "D-PAD Right + Guide = Reset CPU Scalar")},

    // non-pass through hotkeys
    {X_INPUT_GAMEPAD_Y, EmulatorWindow::ControllerHotKey(
                            EmulatorWindow::ButtonFunctions::ToggleFullscreen,
                            "Y = Toggle Fullscreen", true, false)},
    {X_INPUT_GAMEPAD_START, EmulatorWindow::ControllerHotKey(
                                EmulatorWindow::ButtonFunctions::RunTitle,
                                "Start = Run Selected Title", false, false)},
    {X_INPUT_GAMEPAD_BACK | X_INPUT_GAMEPAD_START,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::ToggleLogging,
         "Back + Start = Toggle between loglevel set in config and the "
         "'Disabled' loglevel.",
         false, false)},
    {X_INPUT_GAMEPAD_DPAD_DOWN,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::IncTitleSelect,
         "D-PAD Down = Title Selection +1", true, false)},
    {X_INPUT_GAMEPAD_DPAD_UP,
     EmulatorWindow::ControllerHotKey(
         EmulatorWindow::ButtonFunctions::DecTitleSelect,
         "D-PAD Up = Title Selection -1", true, false)}};

EmulatorWindow::ControllerHotKey EmulatorWindow::ProcessControllerHotkey(
    int buttons) {
  // Default return value
  EmulatorWindow::ControllerHotKey Unknown_hotkey = {};

  if (buttons == 0) {
    return Unknown_hotkey;
  }

  if (disable_hotkeys_.load()) {
    return Unknown_hotkey;
  }

  // Hotkey cool-down to prevent toggling too fast
  constexpr std::chrono::milliseconds delay(75);

  // If the Xbox Gamebar is enabled or the Guide button is disabled then
  // replace the Guide button with the Back button without redeclaring the key
  // mappings
  if (IsUseNexusForGameBarEnabled() || !cvars::guide_button) {
    if ((buttons & X_INPUT_GAMEPAD_BACK) == X_INPUT_GAMEPAD_BACK) {
      buttons &= ~X_INPUT_GAMEPAD_BACK;
      buttons |= X_INPUT_GAMEPAD_GUIDE;
    }
  }

  auto it = controller_hotkey_map.find(buttons);
  if (it == controller_hotkey_map.end()) {
    return Unknown_hotkey;
  }

  // Do not activate hotkeys that are not intended for activation during
  // gameplay
  if (emulator_->is_title_open()) {
    // If non-pass through (menu hoykeys) or hotkeys disabled then return
    if (!it->second.title_passthru || !cvars::controller_hotkeys) {
      return Unknown_hotkey;
    }
  }

  std::string notificationTitle = "";
  std::string notificationDesc = "";

  EmulatorWindow::ControllerHotKey button_combination = it->second;

  switch (button_combination.function) {
    case ButtonFunctions::ToggleFullscreen:
      app_context().CallInUIThread([this]() { ToggleFullscreen(); });

      // Extra Sleep
      xe::threading::Sleep(delay);
      break;
    case ButtonFunctions::RunTitle: {
      if (selected_title_index == -1) {
        selected_title_index++;
      }

      if (selected_title_index < recently_launched_titles_.size()) {
        app_context().CallInUIThread([this]() {
          RunTitle(
              recently_launched_titles_[selected_title_index].path_to_file);
        });
      }
    } break;
    case ButtonFunctions::ClearMemoryPageState:
      ToggleGPUSetting(GPUSetting::ClearMemoryPageState);

      // Assume the user wants ClearCaches as well
      if (cvars::clear_memory_page_state) {
        GpuClearCaches();
      }

      notificationTitle = "Toggle Clear Memory Page State";
      notificationDesc =
          cvars::clear_memory_page_state ? "Enabled" : "Disabled";

      // Extra Sleep
      xe::threading::Sleep(delay);
      break;
    case ButtonFunctions::ReadbackResolve:
      CycleReadbackResolve();

      notificationTitle = "Readback Resolve Mode";
      notificationDesc = cvars::readback_resolve;

      // Extra Sleep
      xe::threading::Sleep(delay);
      break;
    case ButtonFunctions::CpuTimeScalarSetHalf:
      CpuTimeScalarSetHalf();

      notificationTitle = "Time Scalar";
      notificationDesc =
          fmt::format("Decreased to {}", Clock::guest_time_scalar());
      break;
    case ButtonFunctions::CpuTimeScalarSetDouble:
      CpuTimeScalarSetDouble();

      notificationTitle = "Time Scalar";
      notificationDesc =
          fmt::format("Increased to {}", Clock::guest_time_scalar());
      break;
    case ButtonFunctions::CpuTimeScalarReset:
      CpuTimeScalarReset();

      notificationTitle = "Time Scalar";
      notificationDesc = fmt::format("Reset to {}", Clock::guest_time_scalar());
      break;
    case ButtonFunctions::ClearGPUCache:
      GpuClearCaches();

      notificationTitle = "Clear GPU Cache";
      notificationDesc = "Complete";

      // Extra Sleep
      xe::threading::Sleep(delay);
      break;
    case ButtonFunctions::ToggleControllerVibration: {
      ToggleControllerVibration();

      bool vibration = false;

      auto input_sys = emulator()->input_system();
      if (input_sys) {
        vibration = input_sys->GetVibrationCvar();
      }

      notificationTitle = "Toggle Controller Vibration";
      notificationDesc = vibration ? "Enabled" : "Disabled";

      // Extra Sleep
      xe::threading::Sleep(delay);
    } break;
    case ButtonFunctions::IncTitleSelect:
      selected_title_index++;
      break;
    case ButtonFunctions::DecTitleSelect:
      selected_title_index--;
      break;
    case ButtonFunctions::ToggleLogging: {
      logging::ToggleLogLevel();

      notificationTitle = "Toggle Logging";

      LogLevel level = static_cast<LogLevel>(logging::internal::GetLogLevel());
      notificationDesc = level == LogLevel::Disabled ? "Disabled" : "Enabled";
    } break;
    case ButtonFunctions::Unknown:
    default:
      break;
  }

  if ((button_combination.function == ButtonFunctions::IncTitleSelect ||
       button_combination.function == ButtonFunctions::DecTitleSelect) &&
      recently_launched_titles_.size() > 0) {
    selected_title_index =
        std::clamp(selected_title_index, 0,
                   static_cast<int32_t>(recently_launched_titles_.size() - 1));

    // Must clear dialogs to prevent stacking
    ClearDialogs();

    // Titles may contain Unicode characters such as At World’s End
    // Must use ImGUI font that can render these Unicode characters
    std::string title_name;

    // Use filename if title name is empty
    if (recently_launched_titles_[selected_title_index].title_name.empty()) {
      title_name = recently_launched_titles_[selected_title_index]
                       .path_to_file.filename()
                       .string();
    } else {
      title_name = recently_launched_titles_[selected_title_index].title_name;
    }

    std::string title = fmt::format(
        "{}: {}\n\n{}", selected_title_index + 1, title_name,
        controller_hotkey_map.find(X_INPUT_GAMEPAD_START)->second.pretty);

    xe::ui::ImGuiDialog::ShowMessageBox(imgui_drawer_.get(), "Title Selection",
                                        title);
  }

  if (!notificationTitle.empty()) {
    app_context_.CallInUIThread(
        [imgui_drawer = imgui_drawer(), notificationTitle, notificationDesc]() {
          new xe::ui::HostNotificationWindow(imgui_drawer, notificationTitle,
                                             notificationDesc, 0);
        });
  }

  xe::threading::Sleep(delay);

  return it->second;
}

void EmulatorWindow::VibrateController(xe::hid::InputSystem* input_sys,
                                       uint32_t user_index,
                                       bool toggle_rumble) {
  constexpr std::chrono::milliseconds rumble_duration(100);

  // Hold lock while sleeping this thread for the duration of the rumble,
  // otherwise the rumble may fail.
  auto input_lock = input_sys->lock();

  X_INPUT_VIBRATION vibration = {};

  vibration.left_motor_speed = toggle_rumble ? UINT16_MAX : 0;
  vibration.right_motor_speed = toggle_rumble ? UINT16_MAX : 0;

  input_sys->SetState(user_index, &vibration);

  // Vibration duration
  if (toggle_rumble) {
    xe::threading::Sleep(rumble_duration);
  }
}

void EmulatorWindow::GamepadHotKeys() {
  X_INPUT_STATE state;

  constexpr std::chrono::milliseconds thread_delay(75);

  auto input_sys = emulator_->input_system();

  if (input_sys) {
    while (true) {
      // Collect controller states while holding the lock
      std::array<std::pair<bool, X_INPUT_STATE>, XUserMaxUserCount>
          controller_states;
      {
        auto input_lock = input_sys->lock();
        for (uint32_t user_index = 0; user_index < XUserMaxUserCount;
             ++user_index) {
          X_RESULT result = input_sys->GetState(
              user_index, X_INPUT_FLAG::X_INPUT_FLAG_GAMEPAD, &state);
          controller_states[user_index] = {result == X_ERROR_SUCCESS, state};
        }
      }  // Lock is released here when input_lock goes out of scope

      // Process hotkeys without holding the lock
      for (uint32_t user_index = 0; user_index < XUserMaxUserCount;
           ++user_index) {
        if (controller_states[user_index].first) {
          if (ProcessControllerHotkey(
                  controller_states[user_index].second.gamepad.buttons)
                  .rumble) {
            // Enable Vibration
            VibrateController(input_sys, user_index, true);

            // Disable Vibration
            VibrateController(input_sys, user_index, false);
          }
        }
      }

      xe::threading::Sleep(thread_delay);
    }
  }
}

void EmulatorWindow::ToggleGPUSetting(gpu::GPUSetting setting) {
  switch (setting) {
    case GPUSetting::ClearMemoryPageState:
      SaveGPUSetting(GPUSetting::ClearMemoryPageState,
                     !cvars::clear_memory_page_state);
      break;
    case GPUSetting::ReadbackMemexport:
      SaveGPUSetting(GPUSetting::ReadbackMemexport, !cvars::readback_memexport);
      break;
  }
}

void EmulatorWindow::CycleReadbackResolve() {
  const std::string& current = cvars::readback_resolve;
  if (current == "fast") {
    gpu::SetReadbackResolveMode("full");
  } else if (current == "full") {
    gpu::SetReadbackResolveMode("none");
  } else {
    gpu::SetReadbackResolveMode("fast");
  }
}

void EmulatorWindow::DisplayHotKeysConfig() {
  std::string msg = "";
  std::string msg_passthru = "";

  bool guide_enabled = !IsUseNexusForGameBarEnabled() && cvars::guide_button;

  for (auto const& [key, val] : controller_hotkey_map) {
    std::string pretty_text = val.pretty;

    if (!guide_enabled) {
      pretty_text = std::regex_replace(
          pretty_text, std::regex("Guide", std::regex_constants::icase),
          "Back");
    }

    if (emulator_->is_title_open() && !val.title_passthru) {
      pretty_text += " (Disabled)";
    }

    if (val.title_passthru && !cvars::controller_hotkeys) {
      pretty_text += " (Disabled)";
    }

    if (val.title_passthru) {
      msg += pretty_text + "\n";
    } else {
      msg_passthru += pretty_text + "\n";
    }
  }

  // Add Title
  msg.insert(0, "Gameplay Hotkeys\n");

  // Prepend non-passthru hotkeys
  msg_passthru += "\n";
  msg.insert(0, msg_passthru);
  msg += "\n";

  msg += "Readback Resolve: " + cvars::readback_resolve;
  msg += "\n";

  msg += "Clear Memory Page State: " +
         xe::string_util::BoolToString(cvars::clear_memory_page_state);
  msg += "\n";

  msg += "Controller Hotkeys: " +
         xe::string_util::BoolToString(cvars::controller_hotkeys);

  ClearDialogs();
  xe::ui::ImGuiDialog::ShowMessageBox(imgui_drawer_.get(), "Controller Hotkeys",
                                      msg);
}

std::string EmulatorWindow::CanonicalizeFileExtension(
    const std::filesystem::path& path) {
  return xe::utf8::lower_ascii(xe::path_to_utf8(path.extension()));
}

void EmulatorWindow::FinishTitleLaunch(
    const std::filesystem::path& path_to_file,
    const std::filesystem::path& abs_path, xe::X_STATUS result) {
  disable_hotkeys_ = false;
  target_pending_launch_ = false;

  ClearDialogs();

  if (result) {
    XELOGE("Failed to launch target: {:08X}", result);

    xe::ui::ImGuiDialog::ShowMessageBox(
        imgui_drawer_.get(), "Title Launch Failed!",
        "Failed to launch title.\n\nCheck xenia.log for technical details.");

    if (emulator_->file_system()) {
      emulator_->file_system()->Clear();
    }
    ApplyContentVisibility();
  } else {
    AddRecentlyLaunchedTitle(path_to_file, emulator_->title_name());

    auto xam =
        emulator_->kernel_state()->GetKernelModule<kernel::xam::XamModule>(
            "xam.xex");

    xam->loader_data().host_path = xe::path_to_utf8(abs_path);

#ifdef XENIA_HAS_WX_UI
    auto* wx_window = static_cast<wx_ui::WxWindow*>(window_.get());
    if (has_library_boot_) {
      wx_window->NoteGameBooted(library_boot_index_, library_boot_disc_);
      has_library_boot_ = false;
    }
    wx_window->ShowGame();
    // Match the game view to the XConfig (or custom override) resolution.
    if (emulator_->graphics_system()) {
      const auto resolution = emulator_->graphics_system()->GetResolution();
      wx_window->SizeGameView(resolution.first, resolution.second);
    }
#endif
    UpdateTitleDependentMenuItems();
    ApplyContentVisibility();
  }
}

namespace {

// Best-effort per-game config preload for a host file path, using the wx
// scanner's title-ID extraction (no VFS mount needed). Falls back to
// ClearGameConfig when the title ID can't be determined, so a previous
// title's overrides never leak into the next launch. Must run on the UI
// thread before subsystems are (re)created.
void LoadGameConfigForPath(const std::filesystem::path& abs_path) {
  config::ReloadConfig();
#ifdef XENIA_HAS_WX_UI
  if (!abs_path.empty()) {
    xe::app::wx_ui::GameMeta meta;
    if (xe::app::wx_ui::ReadGameMeta(abs_path, meta) && meta.ok &&
        !meta.title_id.empty() && meta.title_id != "00000000") {
      config::LoadGameConfig(meta.title_id);
      return;
    }
  }
#endif
  // No title ID (or no wx scanner): drop stale overrides. The accurate
  // per-title load still happens in CompleteLaunchLoadModule after the
  // module is loaded; this just ensures the subsystem creation below sees
  // global values instead of the previous title's.
  config::ClearGameConfig();
}

}  // namespace

void EmulatorWindow::LaunchTitleInNewProcess(
    const std::filesystem::path& path_to_file) {
  std::filesystem::path executable_path = xe::filesystem::GetExecutablePath();

  if (!path_to_file.empty() && !std::filesystem::exists(path_to_file)) {
    XELOGE("Cannot launch title - file not found: {}",
           xe::path_to_utf8(path_to_file));
    return;
  }

#if XE_PLATFORM_WIN32
  auto exe_path_u16 = xe::path_to_utf16(executable_path);
  std::u16string cmd_line = u"\"" + exe_path_u16 + u"\"";

  // Forward parent's dash-flags; the positional game file is replaced below.
  // Skip flags we set fresh to avoid stale duplicates.
  int parent_argc = 0;
  wchar_t** parent_argv = CommandLineToArgvW(GetCommandLineW(), &parent_argc);
  if (parent_argv) {
    for (int i = 1; i < parent_argc; ++i) {
      std::u16string a(reinterpret_cast<const char16_t*>(parent_argv[i]));
      if (a.empty() || a[0] != u'-') {
        continue;
      }
      auto is_prefix = [&a](const char16_t* p) {
        size_t n = std::char_traits<char16_t>::length(p);
        return a.size() >= n && a.compare(0, n, p) == 0;
      };
      if (is_prefix(u"--fullscreen") || is_prefix(u"--return_to_ui") ||
          is_prefix(u"--launch_module") || is_prefix(u"--log_append") ||
          is_prefix(u"--launch_flags") || is_prefix(u"--launch_data")) {
        continue;
      }
      cmd_line += u" \"" + a + u"\"";
    }
    LocalFree(parent_argv);
  }

  // Tell game process to return to UI when it exits.
  cmd_line += u" --return_to_ui=true";
  if (window_ && window_->IsFullscreen()) {
    cmd_line += u" --fullscreen=true";
  }
  if (!path_to_file.empty()) {
    auto game_path_u16 = xe::path_to_utf16(path_to_file);
    cmd_line += u" \"" + game_path_u16 + u"\"";
  }

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(nullptr,
                      const_cast<wchar_t*>(
                          reinterpret_cast<const wchar_t*>(cmd_line.c_str())),
                      nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                      nullptr, &si, &pi)) {
    XELOGE("Failed to launch new process: {}", GetLastError());
    return;
  }
  AllowSetForegroundWindow(pi.dwProcessId);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
#else
  pid_t pid = fork();
  if (pid == 0) {
    // Child process.
    std::vector<std::string> arg_storage;
    arg_storage.push_back(executable_path.string());
    arg_storage.push_back("--return_to_ui=true");
    if (window_ && window_->IsFullscreen()) {
      arg_storage.push_back("--fullscreen=true");
    }
    std::string target_arg;
    if (!path_to_file.empty()) {
      target_arg = path_to_file.string();
      arg_storage.push_back(target_arg);
    }
    std::vector<const char*> argv;
    for (const auto& a : arg_storage) {
      argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);
    execv(executable_path.c_str(), const_cast<char**>(argv.data()));
    XELOGE("Failed to execute: {}", executable_path.string());
    std::exit(1);
  } else if (pid < 0) {
    XELOGE("Failed to fork process");
    return;
  }
#endif

  XELOGI("Launched title in new process: {}", xe::path_to_utf8(path_to_file));
  xe::FlushLog();
  app_context_.QuitFromUIThread();
}

xe::X_STATUS EmulatorWindow::RunTitle(
    const std::filesystem::path& path_to_file) {
  std::error_code ec = {};
  bool titleExists = std::filesystem::exists(path_to_file, ec);

  if (path_to_file.empty() || !titleExists) {
    std::string log_msg =
        fmt::format("Failed to launch title path is {}.",
                    path_to_file.empty() ? "empty" : "invalid");

    if (!path_to_file.empty() && !titleExists) {
      log_msg.append(fmt::format("\nProvided Path: {}", path_to_file));
    }

    if (ec) {
      log_msg.append(fmt::format("\nExtended message info: {} ({:08X})",
                                 ec.message(), ec.value()));
    }

    XELOGE("{}", log_msg);

    ClearDialogs();

    xe::ui::ImGuiDialog::ShowMessageBox(imgui_drawer_.get(),
                                        "Title Launch Failed!", log_msg);

    return X_STATUS_NO_SUCH_FILE;
  }

  // Resolve the absolute path before branching: a title may already be
  // running (handled below via reset-and-relaunch).
  auto abs_path = std::filesystem::absolute(path_to_file);

  auto extension = CanonicalizeFileExtension(abs_path);

  if (extension == ".7z" || extension == ".zip" || extension == ".rar" ||
      extension == ".tar" || extension == ".gz") {
    xe::ShowSimpleMessageBox(
        xe::SimpleMessageBoxType::Error,
        fmt::format(
            "Unsupported format!\n"
            "Xenia does not support running software in an archived format."));

    return X_STATUS_UNSUCCESSFUL;
  }

  if (emulator_->is_title_open()) {
    // Guard against re-entry first, before touching anything else: without
    // this a second Open issued while a relaunch is in flight would tear
    // down mid-launch (RelaunchTitle releases launch_mutex_ across
    // LaunchPath). Cleared by FinishTitleLaunch in the completion below.
    if (target_pending_launch_) {
      XELOGW("RunTitle: relaunch already in progress, ignoring");
      return X_STATUS_UNSUCCESSFUL;
    }
    // A title is already running: preload the incoming title's config so
    // backend overrides are visible before deciding in- vs out-of-process.
    // (LoadGameConfigForPath reloads the global config first.)
    LoadGameConfigForPath(abs_path);
    // Backend switches leave residual driver state behind, so when the
    // gpu/apu cvar (after per-game overrides) doesn't match the live
    // backend, restart cleanly via the spawn path.
    const auto& last_gpu = emulator_->active_gpu_backend();
    const auto& last_apu = emulator_->active_apu_backend();
    if ((!last_gpu.empty() && last_gpu != cvars::gpu) ||
        (!last_apu.empty() && last_apu != cvars::apu)) {
      XELOGI(
          "RunTitle: backend changed (gpu {} -> {}, apu {} -> {}); respawning",
          last_gpu, cvars::gpu, last_apu, cvars::apu);
      LaunchTitleInNewProcess(abs_path);
      return X_STATUS_SUCCESS;
    }
    if (!cvars::in_process_title_relaunch) {
      // Spawn a fresh process (same path as the kernel relaunch). Fall
      // through to the in-process relaunch when no spawn handler is wired.
      if (auto cb = emulator_->on_launch_new_title()) {
        cb(xe::path_to_utf8(abs_path), /*launch_module=*/{},
           /*launch_flags=*/0, /*launch_data=*/{});
        return X_STATUS_UNSUCCESSFUL;
      }
      XELOGW(
          "RunTitle: out-of-process relaunch requested but no spawn "
          "handler is wired; relaunching in-process");
    }
    target_pending_launch_ = true;
    // Detach the presenter first, on the UI thread: the paint loop drives the
    // GPU completion timelines, so it must not touch GPU objects while the
    // background thread below tears them down.
    ShutdownGraphicsSystemPresenterPainting();
    // Detached non-guest, non-UI thread: RelaunchTitle terminates guest
    // threads and performs the full Shutdown/Setup/Launch cycle, which must
    // not run on the UI thread. The presenter is wired by the
    // graphics-ready hook once the title systems exist (see
    // OnEmulatorInitialized), so there is nothing to attach here.
    Emulator* emulator = emulator_;
    std::string host_path = xe::path_to_utf8(abs_path);
    std::thread([this, emulator, host_path]() mutable {
      emulator->RelaunchTitle(host_path, /*launch_path=*/{},
                              /*launch_flags=*/0, /*launch_data=*/{});
      std::filesystem::path target = xe::to_path(host_path);
      auto abs = std::filesystem::absolute(target);
      X_STATUS result =
          emulator->is_title_open() ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
      app_context_.CallInUIThread([this, result, target, abs]() mutable {
        FinishTitleLaunch(target, abs, result);
      });
    }).detach();
    return X_STATUS_SUCCESS;
  }

  // Guard against re-entry — a rapid double-click would otherwise spawn
  // two concurrent LaunchPath threads.
  if (target_pending_launch_) {
    XELOGW("RunTitle: launch already in progress, ignoring");
    return X_STATUS_UNSUCCESSFUL;
  }

  // Preload the incoming title's config (global reload + per-game overrides)
  // before touching subsystems, so the backend-switch check below sees the
  // incoming title's gpu/apu. The accurate per-title load still happens
  // inside CompleteLaunch after the module is loaded; this preload handles
  // the respawn decision without a mount.
  LoadGameConfigForPath(abs_path);
  // Backend switches in-process leave residual driver/loader state behind,
  // so when the gpu/apu cvar (after per-game overrides) doesn't match the
  // live backend, restart the process cleanly via the spawn path. Checked
  // before teardown so a respawn doesn't destroy live subsystems first.
  const auto& last_gpu = emulator_->active_gpu_backend();
  const auto& last_apu = emulator_->active_apu_backend();
  if ((!last_gpu.empty() && last_gpu != cvars::gpu) ||
      (!last_apu.empty() && last_apu != cvars::apu)) {
    XELOGI("RunTitle: backend changed (gpu {} -> {}, apu {} -> {}); respawning",
           last_gpu, cvars::gpu, last_apu, cvars::apu);
    LaunchTitleInNewProcess(abs_path);
    return X_STATUS_SUCCESS;
  }
  // Drop the previous title's subsystems (if any) before bringing
  // graphics/audio back up with the merged configuration.
  ShutdownGraphicsSystemPresenterPainting();
  emulator_->ShutdownTitleSystems();
  // Toggle before swap chain creation so it picks up the right size. Show
  // the game view immediately so the render transition paints while
  // LaunchPath blocks on the worker.
  if (cvars::fullscreen && !window_->IsFullscreen()) {
    SetFullscreen(true);
  }
  target_pending_launch_ = true;
  ApplyContentVisibility();
  // LaunchPath blocks for seconds; run it off the UI thread so the
  // toolbar/render transition paints immediately. Post-launch work goes
  // back to the UI thread.
  Emulator* emulator = emulator_;
  std::thread([this, emulator, abs_path, path_to_file]() mutable {
    auto result = emulator->LaunchPath(abs_path);
    app_context_.CallInUIThread([this, result, abs_path, path_to_file]() {
      target_pending_launch_ = false;
      FinishTitleLaunch(path_to_file, abs_path, result);
      ApplyContentVisibility();
    });
  }).detach();

  return X_STATUS_SUCCESS;
}

void EmulatorWindow::RunPreviouslyPlayedTitle() {
  if (recently_launched_titles_.size() >= 1) {
    RunTitle(recently_launched_titles_[0].path_to_file);
  }
}

void EmulatorWindow::FillRecentlyLaunchedTitlesMenu(
    xe::ui::MenuItem* recent_menu) {
  for (int i = 0; i < recently_launched_titles_.size(); ++i) {
    std::string hotkey = (i == 0) ? "F9" : "";

    const RecentTitleEntry& entry = recently_launched_titles_[i];
    const std::string item_text = entry.title_name.empty()
                                      ? entry.path_to_file.string()
                                      : entry.title_name;

    recent_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, item_text, hotkey,
        std::bind(&EmulatorWindow::RunTitle, this, entry.path_to_file)));
  }
}

void EmulatorWindow::LoadRecentlyLaunchedTitles() {
  std::ifstream file(emulator()->storage_root() /
                     kRecentlyPlayedTitlesFilename);
  if (!file.is_open()) {
    return;
  }

  toml::parse_result parsed_file;
  try {
    parsed_file = toml::parse(file);
  } catch (toml::parse_error& exception) {
    XELOGE("Cannot parse file: recent.toml. Error: {}", exception.what());
    return;
  }

  if (parsed_file.is_table()) {
    for (const auto& [index, entry] : *parsed_file.as_table()) {
      if (!entry.is_table()) {
        continue;
      }

      const toml::table* entry_table = entry.as_table();

      std::string title_name =
          entry_table->get_as<std::string>("title_name")->get();
      std::string path = entry_table->get_as<std::string>("path")->get();
      std::time_t last_run_time =
          entry_table->get_as<int64_t>("last_run_time")->get();

      std::error_code ec = {};
      if (path.empty() || !std::filesystem::exists(path, ec)) {
        continue;
      }

      recently_launched_titles_.push_back({title_name, path, last_run_time});
    }
  }
}

void EmulatorWindow::AddRecentlyLaunchedTitle(
    std::filesystem::path path_to_file, std::string title_name) {
  if (cvars::recent_titles_entry_amount <= 0) {
    return;
  }

  // Check if game is already on list and pop it to front
  auto entry_index =
      std::ranges::find_if(std::as_const(recently_launched_titles_),
                           [&title_name](const RecentTitleEntry& entry) {
                             return entry.title_name == title_name;
                           });
  if (entry_index != recently_launched_titles_.cend()) {
    recently_launched_titles_.erase(entry_index);
  }

  recently_launched_titles_.insert(recently_launched_titles_.cbegin(),
                                   {title_name, path_to_file, time(nullptr)});
  // Serialize to toml
  auto toml_table = toml::table();

  uint8_t index = 0;
  for (const RecentTitleEntry& entry : recently_launched_titles_) {
    auto entry_table = toml::table();

    // Fill entry under specific index.
    std::string str_path = xe::path_to_utf8(entry.path_to_file);
    entry_table.insert("title_name", entry.title_name);
    entry_table.insert("path", str_path);
    entry_table.insert("last_run_time", entry.last_run_time);

    toml_table.insert(std::to_string(index++), entry_table);

    if (index >= cvars::recent_titles_entry_amount) {
      break;
    }
  }
  // Open and write serialized data.
  std::ofstream file(emulator()->storage_root() / kRecentlyPlayedTitlesFilename,
                     std::ofstream::trunc);
  file << toml_table;
  file.close();
}

void EmulatorWindow::ClearDialogs() {
  if (profile_config_dialog_) {
    profile_config_dialog_.reset();
  }

  if (display_config_dialog_) {
    display_config_dialog_.reset();
  }

  if (console_settings_dialog_) {
    console_settings_dialog_.reset();
  }

  if (content_list_dialog_) {
    content_list_dialog_.reset();
  }

  if (xmp_config_dialog_) {
    xmp_config_dialog_.reset();
  }

  imgui_drawer_.get()->ClearDialogs();
  emulator_->kernel_state()->xam_state()->is_xam_dialog_present_.store(false);
}

}  // namespace app
}  // namespace xe
