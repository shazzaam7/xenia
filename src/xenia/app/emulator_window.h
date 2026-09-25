/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_EMULATOR_WINDOW_H_
#define XENIA_APP_EMULATOR_WINDOW_H_

#include <memory>
#include <string>

#include "xenia/app/profile_dialogs.h"
#include "xenia/emulator.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {

class ConsoleSettingsDialog;
class ContentListDialog;

struct RecentTitleEntry {
  std::string title_name;
  std::filesystem::path path_to_file;
  std::time_t last_run_time;
};

class EmulatorWindow {
 public:
  using steady_clock = std::chrono::steady_clock;  // stdlib steady clock

  enum : size_t {
    // The UI is on top of the game and is open in special cases, so
    // lowest-priority.
    kZOrderHidInput,
    kZOrderImGui,
    kZOrderProfiler,
    // Emulator window controls are expected to be always accessible by the
    // user, so highest-priority.
    kZOrderEmulatorWindowInput,
  };

  virtual ~EmulatorWindow();

  static std::unique_ptr<EmulatorWindow> Create(
      Emulator* emulator, ui::WindowedAppContext& app_context, uint32_t width,
      uint32_t height);

  std::unique_ptr<xe::threading::Thread> Gamepad_HotKeys_Listener;

  int32_t selected_title_index = -1;

  static constexpr int64_t diff_in_ms(
      const steady_clock::time_point t1,
      const steady_clock::time_point t2) noexcept {
    using ms = std::chrono::milliseconds;
    return std::chrono::duration_cast<ms>(t1 - t2).count();
  }

  steady_clock::time_point last_mouse_up = steady_clock::now();
  steady_clock::time_point last_mouse_down = steady_clock::now();

  Emulator* emulator() const { return emulator_; }
  ui::WindowedAppContext& app_context() const { return app_context_; }
  ui::Window* window() const { return window_.get(); }
  ui::ImGuiDrawer* imgui_drawer() const { return imgui_drawer_.get(); }

  ui::Presenter* GetGraphicsSystemPresenter() const;
  void SetupGraphicsSystemPresenterPainting();
  void ShutdownGraphicsSystemPresenterPainting();

  void OnEmulatorInitialized();

  xe::X_STATUS RunTitle(const std::filesystem::path& path_to_file);
  void UpdateTitle();
  void ShowLibrary();
  void ShowGame();
  // Spawns a fresh emulator process for the given title and quits this one.
  // Used when per-game backend overrides differ from the live backends, or
  // when in-process relaunch is disabled. Forwards the parent's CLI flags
  // and appends --return_to_ui so the child returns to the library.
  void LaunchTitleInNewProcess(const std::filesystem::path& path_to_file);
  void SetFullscreen(bool fullscreen);
  void ToggleFullscreen();
  void SetInitializingShaderStorage(bool initializing);

  void TakeScreenshot();
  void ExportScreenshot(const xe::ui::RawImage& image);
  void SaveImage(const std::filesystem::path& path,
                 const xe::ui::RawImage& image);

  // Shows the profile popup (same menu as the library toolbar button).
  void ShowProfileMenu();
  // Switches cvars::ui_theme (Display menu) and applies it live when the
  // platform allows; otherwise the change takes effect on restart.
  void SetUiTheme(const std::string& theme);
  // Shows the console settings dialog (wxWidgets in wx builds).
  void ShowConsoleSettingsDialog();
  // Shows the config.toml editor (wxWidgets only).
  void ShowConfigEditorDialog();
  // Shows the per-title override editor for one library entry (wxWidgets
  // only). Refuses to open while a title is running, like the global editor.
  void ShowGameConfigEditorDialog(const std::string& title_id,
                                  const std::string& title_name);
  void ToggleProfilesConfigDialog();
  void ToggleXMPConfigDialog();
  void ToggleConsoleSettingsDialog();
  void ToggleContentListDialog();

  void SetHotkeysState(bool enabled) { disable_hotkeys_ = !enabled; }

  void ExtractContent(const std::filesystem::path file = "");

  // Types of button functions for hotkeys.
  enum class ButtonFunctions {
    ToggleFullscreen,
    RunTitle,
    CpuTimeScalarSetHalf,
    CpuTimeScalarSetDouble,
    CpuTimeScalarReset,
    ClearGPUCache,
    ToggleControllerVibration,
    ClearMemoryPageState,
    ReadbackResolve,
    ToggleLogging,
    IncTitleSelect,
    DecTitleSelect,
    Unknown
  };

  class ControllerHotKey {
   public:
    // If true the hotkey can be activated while a title is running, otherwise
    // false.
    bool title_passthru;

    // If true vibrate the controller after activating the hotkey, otherwise
    // false.
    bool rumble;
    std::string pretty;
    ButtonFunctions function;

    ControllerHotKey(ButtonFunctions fn = ButtonFunctions::Unknown,
                     std::string pretty = "", bool rumble = false,
                     bool active = true) {
      function = fn;
      this->pretty = pretty;
      title_passthru = active;
      this->rumble = rumble;
    }
  };

 private:
  class EmulatorWindowListener final : public ui::WindowListener,
                                       public ui::WindowInputListener {
   public:
    explicit EmulatorWindowListener(EmulatorWindow& emulator_window)
        : emulator_window_(emulator_window) {}

    void OnClosing(ui::UIEvent& e) override;
    void OnFileDrop(ui::FileDropEvent& e) override;

    void OnKeyDown(ui::KeyEvent& e) override;

    void OnMouseDown(ui::MouseEvent& e) override;
    void OnMouseUp(ui::MouseEvent& e) override;

    void OnUsbDeviceChanged(bool is_arrival) override;

   private:
    EmulatorWindow& emulator_window_;
  };

  class DisplayConfigGameConfigLoadCallback
      : public Emulator::GameConfigLoadCallback {
   public:
    DisplayConfigGameConfigLoadCallback(Emulator& emulator,
                                        EmulatorWindow& emulator_window)
        : Emulator::GameConfigLoadCallback(emulator),
          emulator_window_(emulator_window) {}

    void PostGameConfigLoad() override;

   private:
    EmulatorWindow& emulator_window_;
  };

  class ContentInstallDialog final : public ui::ImGuiDialog {
   public:
    ContentInstallDialog(
        ui::ImGuiDrawer* imgui_drawer, EmulatorWindow& emulator_window,
        std::shared_ptr<std::vector<Emulator::ContentInstallEntry>> entries)
        : ui::ImGuiDialog(imgui_drawer),
          emulator_window_(emulator_window),
          installation_entries_(entries) {
      window_id_ = GetWindowId();
    }

    ~ContentInstallDialog() {
      for (auto& entry : *installation_entries_) {
        entry.icon_.release();
      }
    }

   protected:
    void OnDraw(ImGuiIO& io) override;

   private:
    uint64_t window_id_;

    EmulatorWindow& emulator_window_;
    std::shared_ptr<std::vector<Emulator::ContentInstallEntry>>
        installation_entries_;
  };

  class DisplayConfigDialog final : public ui::ImGuiDialog {
   public:
    DisplayConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                        EmulatorWindow& emulator_window)
        : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

   protected:
    void OnDraw(ImGuiIO& io) override;

   private:
    EmulatorWindow& emulator_window_;
  };

  class XMPConfigDialog final : public ui::ImGuiDialog {
   public:
    XMPConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                    EmulatorWindow& emulator_window)
        : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
      if (emulator_window_.emulator_->audio_media_player()) {
        volume_ = emulator_window_.emulator_->audio_media_player()
                      ->GetVolume()
                      ->load();
      }
    }

   protected:
    void OnDraw(ImGuiIO& io) override;

   private:
    EmulatorWindow& emulator_window_;
    float volume_ = 0.0f;
  };

  explicit EmulatorWindow(Emulator* emulator,
                          ui::WindowedAppContext& app_context, uint32_t width,
                          uint32_t height);

  bool Initialize();

  // For comparisons, use GetSwapPostEffectForCvarValue instead as the default
  // fallback may be used for multiple values.
  static const char* GetCvarValueForSwapPostEffect(
      gpu::CommandProcessor::SwapPostEffect effect);
  static gpu::CommandProcessor::SwapPostEffect GetSwapPostEffectForCvarValue(
      const std::string& cvar_value);
  // For comparisons, use GetGuestOutputPaintEffectForCvarValue instead as the
  // default fallback may be used for multiple values.
  static const char* GetCvarValueForGuestOutputPaintEffect(
      ui::Presenter::GuestOutputPaintConfig::Effect effect);
  static ui::Presenter::GuestOutputPaintConfig::Effect
  GetGuestOutputPaintEffectForCvarValue(const std::string& cvar_value);
  static ui::Presenter::GuestOutputPaintConfig
  GetGuestOutputPaintConfigForCvars();
  void ApplyDisplayConfigForCvars();

  void OnKeyDown(ui::KeyEvent& e);
  void OnMouseDown(const ui::MouseEvent& e);
  void ToggleFullscreenOnDoubleClick();
  void FileDrop(const std::filesystem::path& filename);
  void OnMouseUp(const ui::MouseEvent& e);
  void FileOpen();
  void FileClose();
  void StopTitle();
  // Guest-thread entry for game-requested exits: detaches presentation on
  // the UI thread, resets the title on a detached thread (relaunching into
  // host_path when non-empty), and returns true when handled in-process.
  // The calling guest thread is terminated by ResetTitle and must park.
  bool StopTitleFromGuestThread(std::string host_path, std::string launch_path,
                                uint32_t launch_flags,
                                std::vector<uint8_t> launch_data);
  void LibraryBoot(size_t index, int disc_number,
                   const std::filesystem::path& path);
  // Applies the title-open state to the menu items that depend on it (Stop,
  // and Open config editor, which is off-limits while a title is running).
  void UpdateTitleDependentMenuItems();
  void InstallContent();
  void ExtractZarchive();
  void CreateZarchive();
  // Adds freshly installed/extracted titles to the game library. Runs on any
  // thread; the library import itself hops to the UI thread. When
  // only_inside_content is set, paths outside the content tree are skipped.
  void AddInstalledContentToLibrary(
      const std::shared_ptr<std::vector<Emulator::ContentInstallEntry>>&
          entries,
      bool only_inside_content);
  void ShowContentDirectory();
  void CpuTimeScalarReset();
  void CpuTimeScalarSetHalf();
  void CpuTimeScalarSetDouble();
  void CpuBreakIntoDebugger();
  void CpuBreakIntoHostDebugger();
  void GpuTraceFrame();
  void GpuClearCaches();
  void ToggleDisplayConfigDialog();
  void ToggleControllerVibration();
  void ShowFAQ();
  // Refreshes game compatibility ratings (Help menu). No-op without wx UI.
  void RefreshCompatData();
  // Updates game patches from upstream (Help menu). No-op without wx UI.
  void UpdateGamePatches();
  void ShowBuildCommit();

  EmulatorWindow::ControllerHotKey ProcessControllerHotkey(int buttons);
  void VibrateController(xe::hid::InputSystem* input_sys, uint32_t user_index,
                         bool vibrate = true);
  void GamepadHotKeys();
  void ToggleGPUSetting(gpu::GPUSetting setting);
  void CycleReadbackResolve();
  void DisplayHotKeysConfig();

  static std::string CanonicalizeFileExtension(
      const std::filesystem::path& path);

  // Shared post-launch UI handling for all RunTitle paths. Must run on the
  // UI thread.
  void FinishTitleLaunch(const std::filesystem::path& path_to_file,
                         const std::filesystem::path& abs_path,
                         xe::X_STATUS result);

  // Reapplies the "game view visible iff title open or launch pending"
  // invariant. Canary uses a wxSimplebook (library vs game), not AUI panes:
  // this keeps ShowLibrary/ShowGame in sync with target_pending_launch_ and
  // refreshes title-dependent menu states.
  void ApplyContentVisibility();

  void RunPreviouslyPlayedTitle();
  void FillRecentlyLaunchedTitlesMenu(xe::ui::MenuItem* recent_menu);
  void LoadRecentlyLaunchedTitles();
  void AddRecentlyLaunchedTitle(std::filesystem::path path_to_file,
                                std::string title_name);

  void ClearDialogs();

  Emulator* emulator_;
  ui::WindowedAppContext& app_context_;
  EmulatorWindowListener window_listener_;
  std::unique_ptr<ui::Window> window_;
  std::unique_ptr<ui::ImGuiDrawer> imgui_drawer_;
  std::unique_ptr<DisplayConfigGameConfigLoadCallback>
      display_config_game_config_load_callback_;
  // Creation may fail, in this case immediate drawer UI must not be drawn.
  std::unique_ptr<ui::ImmediateDrawer> immediate_drawer_;

  bool emulator_initialized_ = false;
  std::atomic<bool> disable_hotkeys_ = false;

  std::string base_title_;
  bool initializing_shader_storage_ = false;

  std::unique_ptr<DisplayConfigDialog> display_config_dialog_;
  std::unique_ptr<ConsoleSettingsDialog> console_settings_dialog_;
  std::unique_ptr<ContentListDialog> content_list_dialog_;
  // Storing pointers and toggling dialog state is useful for broadcasting
  // messages back to guest.
  std::unique_ptr<ProfileConfigDialog> profile_config_dialog_;

  std::unique_ptr<XMPConfigDialog> xmp_config_dialog_;

  std::vector<RecentTitleEntry> recently_launched_titles_;

  // Game library state (wx backend only; the window owns the entries).
  ui::MenuItem* stop_item_ = nullptr;
  ui::MenuItem* config_editor_item_ = nullptr;
  bool has_library_boot_ = false;
  size_t library_boot_index_ = 0;
  int library_boot_disc_ = 1;
  // True while an async launch is in flight (worker thread running
  // LaunchPath). Guards against double-click re-entry and keeps the game
  // view visible until the launch settles. Always touched on the UI thread.
  bool target_pending_launch_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_EMULATOR_WINDOW_H_
