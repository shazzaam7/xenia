/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CVAR_H_
#define XENIA_CVAR_H_

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "third_party/cxxopts/include/cxxopts.hpp"
#include "third_party/fmt/include/fmt/format.h"
#include "third_party/tomlplusplus/include/toml++/toml.hpp"
#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/platform.h"
#include "xenia/base/string_util.h"

#if XE_PLATFORM_ANDROID
#include <jni.h>
#endif  // XE_PLATFORM_ANDROID

namespace cvar {

namespace toml_internal {
std::string EscapeString(const std::string_view str);
}

// Track config values that had type mismatches during loading
extern std::vector<std::string>* config_type_mismatch_warnings;

// Optional, presentation-only metadata for a config variable, consumed by UI
// editors (currently the config editor dialog). Registered next to the variable
// definition through the DEFINE_*_choices / DEFINE_*_range /
// DEFINE_CVar_DisplayName macros; the engine itself never reads it.
struct ConfigVarEditorInfo {
  // One entry in a fixed set of allowed values (an enum-like variable). The
  // label is what the editor shows, the value is what gets written and parsed.
  struct Choice {
    const char* label;
    const char* value;
  };
  // Friendly name shown for the setting itself instead of the raw cvar name -
  // the key written to the config file is unchanged. Null => use the cvar name.
  const char* display_name = nullptr;
  // Non-empty => the editor shows a dropdown. The value strings are passed to
  // SetFromString, so pairs also work for integer variables.
  std::vector<Choice> choices;
  // Inclusive numeric slider bounds. When has_range is false the editor falls
  // back to a plain text field.
  bool has_range = false;
  double range_min = 0.0;
  double range_max = 0.0;
  // Granularity of the slider / spin box; 0 means an integer step of 1.
  double range_step = 0.0;
  // Bit-flag variable: the editor shows one checkbox per Choice and writes the
  // bitwise OR of the checked values, so each Choice::value holds the numeric
  // value of a single bit ("1", "2", "4", ...).
  bool is_flags = false;
  // Dropdown contents computed at runtime (GPU adapters, devices) instead of
  // registered statically. Called on the UI thread while the editor is being
  // built; the returned list must stay alive for the process lifetime.
  // Returning an empty list makes the editor fall back to a text field.
  const std::vector<Choice>& (*dynamic_choices)() = nullptr;
  // For path variables: show a browse button next to the text field, opening
  // the directory chooser when set instead of the file chooser.
  bool path_is_directory = false;
};

// Value type tag for editors, consumed through IConfigVar::value_kind().
// An enum rather than a string so dispatch sites are compile-time checked.
enum class ConfigVarValueKind {
  kBool,
  kInt32,
  kInt64,
  kUint32,
  kUint64,
  kDouble,
  kString,
  kPath,
};

// Strict "could this text be parsed into a variable of this kind" test.
// Shared by the config editors, which refuse to apply the text, and the config
// layer, which uses it to spot a stored value that no longer fits the variable
// it belongs to. The engine's from_string is lenient - it truncates "12ab" to
// 12 and degrades garbage to T() instead of reporting an error - so this has to
// be stricter than parsing.
bool IsValidConfigValueText(ConfigVarValueKind kind, std::string_view text);

class ICommandVar {
 public:
  virtual ~ICommandVar() = default;
  virtual const std::string& name() const = 0;
  virtual const std::string& description() const = 0;
  virtual void UpdateValue() = 0;
  virtual void AddToLaunchOptions(cxxopts::Options* options) = 0;
  virtual void LoadFromLaunchOptions(cxxopts::ParseResult* result) = 0;
};

class IConfigVar : virtual public ICommandVar {
 public:
  virtual const std::string& category() const = 0;
  virtual bool is_transient() const = 0;
  // Optional editor metadata (dropdown contents and/or slider bounds); null
  // when the variable has no special presentation needs.
  virtual const ConfigVarEditorInfo* editor_info() const { return nullptr; }
  // Exact value type tag for editors. Enum, not a string: typo-proof at
  // compile time, and switch statements over it are exhaustiveness-checked.
  virtual ConfigVarValueKind value_kind() const = 0;
  // Current file value (config override or default) as raw text for editing -
  // unlike config_value() this is never TOML-escaped.
  virtual std::string display_value() const = 0;
  // Parses editor text into the config override and applies it live. Numerics
  // must be pre-validated by the caller (garbage hits assert_always inside
  // from_string and degrades to T()). Returns false only for string/path
  // conversion failures (never for the types used today).
  virtual bool SetFromString(const std::string& text) = 0;
  virtual std::string config_value() const = 0;
  virtual void LoadConfigValue(const toml::node* result) = 0;
  // Applies a value from a per-title override file. Returns false, leaving the
  // variable without an override, when the stored TOML value does not fit the
  // variable's current type - which happens when a setting changes type between
  // builds (a text option becoming a boolean, say). The caller drops such
  // settings, since keeping them would pin the title to a value it can no
  // longer parse.
  virtual bool LoadGameConfigValue(const toml::node* result) = 0;
  virtual void ResetConfigValueToDefault() = 0;
  // Per-game (per-title) layer, stored in
  // <storage_root>/config/<TITLEID>.config.toml on top of the global config.
  // Both accessors return nullopt when the variable has no override and thus
  // inherits the global value; game_config_value() is the TOML text to write
  // (like config_value()), game_config_display_value() the raw text an editor
  // edits (like display_value()).
  virtual std::optional<std::string> game_config_value() const = 0;
  virtual std::optional<std::string> game_config_display_value() const = 0;
  virtual bool has_game_config_value() const = 0;
  // Applies raw editor text as a per-game override - the counterpart of
  // SetFromString, which writes the global layer instead. Text that does not
  // fit the variable's type is rejected (false) rather than degraded, since it
  // would otherwise be written to the override file as a value that can never
  // be read back.
  virtual bool SetGameConfigValueFromString(const std::string& text) = 0;
  // Drops the override so the global value applies again.
  virtual void ResetGameConfigValue() = 0;
};

template <class T>
class CommandVar : virtual public ICommandVar {
 public:
  CommandVar<T>(const char* name, T* default_value, const char* description);
  const std::string& name() const override;
  const std::string& description() const override;
  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadFromLaunchOptions(cxxopts::ParseResult* result) override;
  void SetCommandLineValue(T val);
  T* current_value() { return current_value_; }

 protected:
  std::string name_;
  T default_value_;
  T* current_value_;
  std::unique_ptr<T> commandline_value_;
  std::string description_;
  T Convert(std::string val);
  static std::string ToString(T val);
  void SetValue(T val);
  void UpdateValue() override;
};
#pragma warning(push)
#pragma warning(disable : 4250)
template <class T>
class ConfigVar : public CommandVar<T>, virtual public IConfigVar {
 public:
  ConfigVar<T>(const char* name, T* default_value, const char* description,
               const char* category, bool is_transient);
  std::string config_value() const override;
  const T& GetTypedConfigValue() const;
  const std::string& category() const override;
  bool is_transient() const override;
  const ConfigVarEditorInfo* editor_info() const override {
    return editor_info_;
  }
  // Presentation-only; set by the DEFINE_*_choices / DEFINE_*_range macros.
  void set_editor_info(const ConfigVarEditorInfo* editor_info) {
    editor_info_ = editor_info;
  }
  ConfigVarValueKind value_kind() const override;
  std::string display_value() const override;
  bool SetFromString(const std::string& text) override;
  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadConfigValue(const toml::node* result) override;
  bool LoadGameConfigValue(const toml::node* result) override;
  std::optional<std::string> game_config_value() const override;
  std::optional<std::string> game_config_display_value() const override;
  bool has_game_config_value() const override;
  bool SetGameConfigValueFromString(const std::string& text) override;
  void ResetGameConfigValue() override;
  void SetConfigValue(T val);
  void SetGameConfigValue(T val);
  // Changes the actual value used to the one specified, and also makes it the
  // one that will be stored when the global config is written next time. After
  // overriding, however, the next game config loaded may still change it.
  void OverrideConfigValue(T val);

 private:
  std::string category_;
  bool is_transient_;
  const ConfigVarEditorInfo* editor_info_ = nullptr;
  std::unique_ptr<T> config_value_ = nullptr;
  std::unique_ptr<T> game_config_value_ = nullptr;
  void UpdateValue() override;
  void ResetConfigValueToDefault() override;
};

#pragma warning(pop)
template <class T>
const std::string& CommandVar<T>::name() const {
  return name_;
}
template <class T>
const std::string& CommandVar<T>::description() const {
  return description_;
}
template <class T>
void CommandVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options()(name_, description_, cxxopts::value<T>());
}
template <>
inline void CommandVar<std::filesystem::path>::AddToLaunchOptions(
    cxxopts::Options* options) {
  options->add_options()(name_, description_, cxxopts::value<std::string>());
}
template <class T>
void ConfigVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options(category_)(this->name_, this->description_,
                                  cxxopts::value<T>());
}
template <>
inline void ConfigVar<std::filesystem::path>::AddToLaunchOptions(
    cxxopts::Options* options) {
  options->add_options(category_)(this->name_, this->description_,
                                  cxxopts::value<std::string>());
}
template <class T>
void CommandVar<T>::LoadFromLaunchOptions(cxxopts::ParseResult* result) {
  T value = (*result)[name_].template as<T>();
  SetCommandLineValue(value);
}
template <>
inline void CommandVar<std::filesystem::path>::LoadFromLaunchOptions(
    cxxopts::ParseResult* result) {
  std::string value = (*result)[name_].template as<std::string>();
  SetCommandLineValue(xe::to_path(value));
}
template <class T>
void ConfigVar<T>::LoadConfigValue(const toml::node* result) {
  auto value_opt = result->value<T>();
  if (value_opt) {
    SetConfigValue(value_opt.value());
  } else {
    // Type mismatch - track for warning
    if (!config_type_mismatch_warnings) {
      config_type_mismatch_warnings = new std::vector<std::string>();
    }
    config_type_mismatch_warnings->push_back(this->name_);
  }
}
template <>
inline void ConfigVar<std::filesystem::path>::LoadConfigValue(
    const toml::node* result) {
  SetConfigValue(xe::to_path(
      xe::utf8::fix_path_separators(result->as_string()->value_or(""))));
}
template <class T>
bool ConfigVar<T>::LoadGameConfigValue(const toml::node* result) {
  auto value_opt = result->value<T>();
  if (!value_opt) {
    // Reported by the caller instead of through
    // config_type_mismatch_warnings: that list is shown by the global config
    // load, which has already run by the time a title's file is read.
    return false;
  }
  SetGameConfigValue(value_opt.value());
  return true;
}
template <>
inline bool ConfigVar<std::filesystem::path>::LoadGameConfigValue(
    const toml::node* result) {
  // as_string() returns null for anything that isn't a TOML string, so a path
  // option whose stored value changed type must be rejected here rather than
  // dereferenced.
  if (!result->is_string()) {
    return false;
  }
  SetGameConfigValue(xe::to_path(
      xe::utf8::fix_path_separators(result->as_string()->value_or(""))));
  return true;
}
template <class T>
CommandVar<T>::CommandVar(const char* name, T* default_value,
                          const char* description)
    : name_(name),
      default_value_(*default_value),
      current_value_(default_value),
      commandline_value_(),
      description_(description) {}

template <class T>
ConfigVar<T>::ConfigVar(const char* name, T* default_value,
                        const char* description, const char* category,
                        bool is_transient)
    : CommandVar<T>(name, default_value, description),
      category_(category),
      is_transient_(is_transient) {}

template <class T>
void CommandVar<T>::UpdateValue() {
  if (commandline_value_) {
    return SetValue(*commandline_value_);
  }
  return SetValue(default_value_);
}
template <class T>
void ConfigVar<T>::UpdateValue() {
  if (this->commandline_value_) {
    return this->SetValue(*this->commandline_value_);
  }
  if (game_config_value_) {
    return this->SetValue(*game_config_value_);
  }
  if (config_value_) {
    return this->SetValue(*config_value_);
  }
  return this->SetValue(this->default_value_);
}
template <class T>
T CommandVar<T>::Convert(std::string val) {
  return xe::string_util::from_string<T>(val);
}
template <>
inline std::string CommandVar<std::string>::Convert(std::string val) {
  return val;
}
template <>
inline std::filesystem::path CommandVar<std::filesystem::path>::Convert(
    std::string val) {
  return xe::to_path(val);
}

template <>
inline std::string CommandVar<bool>::ToString(bool val) {
  return val ? "true" : "false";
}
template <>
inline std::string CommandVar<std::string>::ToString(std::string val) {
  return toml_internal::EscapeString(val);
}
template <>
inline std::string CommandVar<std::filesystem::path>::ToString(
    std::filesystem::path val) {
  return toml_internal::EscapeString(
      xe::utf8::fix_path_separators(xe::path_to_utf8(val), '/'));
}

template <class T>
std::string CommandVar<T>::ToString(T val) {
  // Use fmt::format instead of std::to_string for locale-neutral formatting of
  // floats, always with a period rather than a comma, which is treated as an
  // unidentified trailing character by cpptoml.
  return fmt::format("{}", val);
}

template <class T>
void CommandVar<T>::SetValue(T val) {
  *current_value_ = val;
}
template <typename T>
struct ConfigVarKind;  // Intentionally undefined: unsupported T fails loudly.
template <>
struct ConfigVarKind<bool> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kBool;
};
template <>
struct ConfigVarKind<int32_t> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kInt32;
};
template <>
struct ConfigVarKind<int64_t> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kInt64;
};
template <>
struct ConfigVarKind<uint32_t> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kUint32;
};
template <>
struct ConfigVarKind<uint64_t> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kUint64;
};
template <>
struct ConfigVarKind<double> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kDouble;
};
template <>
struct ConfigVarKind<std::string> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kString;
};
template <>
struct ConfigVarKind<std::filesystem::path> {
  static constexpr ConfigVarValueKind value = ConfigVarValueKind::kPath;
};
template <class T>
ConfigVarValueKind ConfigVar<T>::value_kind() const {
  return ConfigVarKind<T>::value;
}
template <class T>
std::string ConfigVar<T>::display_value() const {
  return this->ToString(GetTypedConfigValue());
}
template <>
inline std::string ConfigVar<std::string>::display_value() const {
  return GetTypedConfigValue();
}
template <>
inline std::string ConfigVar<std::filesystem::path>::display_value() const {
  return xe::path_to_utf8(GetTypedConfigValue());
}
template <class T>
bool ConfigVar<T>::SetFromString(const std::string& text) {
  SetConfigValue(this->Convert(text));
  return true;
}
template <>
inline bool ConfigVar<std::string>::SetFromString(const std::string& text) {
  SetConfigValue(text);
  return true;
}
template <>
inline bool ConfigVar<std::filesystem::path>::SetFromString(
    const std::string& text) {
  SetConfigValue(xe::to_path(text));
  return true;
}
template <class T>
const std::string& ConfigVar<T>::category() const {
  return category_;
}
template <class T>
bool ConfigVar<T>::is_transient() const {
  return is_transient_;
}
template <class T>
std::string ConfigVar<T>::config_value() const {
  if (config_value_) {
    return this->ToString(*config_value_);
  }
  return this->ToString(this->default_value_);
}
template <class T>
const T& ConfigVar<T>::GetTypedConfigValue() const {
  return config_value_ ? *config_value_ : this->default_value_;
}
template <class T>
void CommandVar<T>::SetCommandLineValue(const T val) {
  commandline_value_ = std::make_unique<T>(val);
  UpdateValue();
}
template <class T>
void ConfigVar<T>::SetConfigValue(T val) {
  config_value_ = std::make_unique<T>(val);
  UpdateValue();
}
template <class T>
void ConfigVar<T>::SetGameConfigValue(T val) {
  game_config_value_ = std::make_unique<T>(val);
  UpdateValue();
}
template <class T>
void ConfigVar<T>::OverrideConfigValue(T val) {
  config_value_ = std::make_unique<T>(val);
  // The user explicitly changes the value at runtime and wants it to take
  // effect immediately. Drop everything with a higher priority. The next game
  // config load, however, may still change it.
  game_config_value_.reset();
  this->commandline_value_.reset();
  UpdateValue();
}
template <class T>
void ConfigVar<T>::ResetConfigValueToDefault() {
  SetConfigValue(this->default_value_);
}
// Unlike display_value(), ToString already escapes string and path values, so
// one implementation covers every type.
template <class T>
std::optional<std::string> ConfigVar<T>::game_config_value() const {
  if (!game_config_value_) {
    return std::nullopt;
  }
  return this->ToString(*game_config_value_);
}
template <class T>
std::optional<std::string> ConfigVar<T>::game_config_display_value() const {
  if (!game_config_value_) {
    return std::nullopt;
  }
  return this->ToString(*game_config_value_);
}
template <>
inline std::optional<std::string>
ConfigVar<std::string>::game_config_display_value() const {
  if (!game_config_value_) {
    return std::nullopt;
  }
  return *game_config_value_;
}
template <>
inline std::optional<std::string>
ConfigVar<std::filesystem::path>::game_config_display_value() const {
  if (!game_config_value_) {
    return std::nullopt;
  }
  return xe::path_to_utf8(*game_config_value_);
}
template <class T>
bool ConfigVar<T>::has_game_config_value() const {
  return game_config_value_ != nullptr;
}
template <class T>
bool ConfigVar<T>::SetGameConfigValueFromString(const std::string& text) {
  if (!IsValidConfigValueText(this->value_kind(), text)) {
    return false;
  }
  SetGameConfigValue(this->Convert(text));
  return true;
}
template <>
inline bool ConfigVar<std::string>::SetGameConfigValueFromString(
    const std::string& text) {
  SetGameConfigValue(text);
  return true;
}
template <>
inline bool ConfigVar<std::filesystem::path>::SetGameConfigValueFromString(
    const std::string& text) {
  SetGameConfigValue(xe::to_path(text));
  return true;
}
template <class T>
void ConfigVar<T>::ResetGameConfigValue() {
  if (!game_config_value_) {
    return;
  }
  game_config_value_.reset();
  UpdateValue();
}

// CVars can be initialized before these, thus initialized on-demand using new.
extern std::map<std::string, ICommandVar*>* CmdVars;
extern std::map<std::string, IConfigVar*>* ConfigVars;

inline void AddConfigVar(IConfigVar* cv) {
  if (!ConfigVars) {
    ConfigVars = new std::map<std::string, IConfigVar*>;
  }
  ConfigVars->emplace(cv->name(), cv);
}
inline void AddCommandVar(ICommandVar* cv) {
  if (!CmdVars) {
    CmdVars = new std::map<std::string, ICommandVar*>;
  }
  CmdVars->emplace(cv->name(), cv);
}
void ParseLaunchArguments(int& argc, char**& argv,
                          const std::string_view positional_help,
                          const std::vector<std::string>& positional_options);
#if XE_PLATFORM_ANDROID
void ParseLaunchArgumentsFromAndroidBundle(jobject bundle);
#endif  // XE_PLATFORM_ANDROID

template <typename T>
IConfigVar* define_configvar(const char* name, T* default_value,
                             const char* description, const char* category,
                             bool is_transient) {
  IConfigVar* cfgvar = new ConfigVar<T>(name, default_value, description,
                                        category, is_transient);
  AddConfigVar(cfgvar);
  return cfgvar;
}

template <typename T>
ICommandVar* define_cmdvar(const char* name, T* default_value,
                           const char* description) {
  ICommandVar* cmdvar = new CommandVar<T>(name, default_value, description);
  AddCommandVar(cmdvar);
  return cmdvar;
}

// Attaches ConfigVarEditorInfo to the concrete variable. The macros below can't
// reach it through IConfigVar (it's a virtual base and the setter is not on the
// interface), hence the dynamic_cast. The macro-generated static object runs
// after the cv_##name pointer it references, in declaration order within the
// TU.
template <typename T>
class ConfigVarEditorInfoAttacher {
 public:
  ConfigVarEditorInfoAttacher(IConfigVar* const& config_var,
                              const ConfigVarEditorInfo* editor_info) {
    dynamic_cast<ConfigVar<T>*>(config_var)->set_editor_info(editor_info);
  }
};

#define DEFINE_bool(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, bool)

#define DEFINE_int32(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, int32_t)

#define DEFINE_uint32(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, uint32_t)

#define DEFINE_uint64(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, uint64_t)
#define DEFINE_int64(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, int64_t)
#define DEFINE_double(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, double)

#define DEFINE_string(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, std::string)

#define DEFINE_path(name, default_value, description, category)  \
  DEFINE_CVar(name, default_value, description, category, false, \
              std::filesystem::path)

// One label/value pair for DEFINE_*_choices; may be repeated as arguments.
#define XE_CVAR_CHOICE(label, value) \
  cvar::ConfigVarEditorInfo::Choice { label, value }

// Friendly name for a setting row, shown instead of the raw cvar name (the key
// written to the config file is unchanged). Use this for variables that don't
// already carry metadata; variables with choices / a range take the friendly
// name as an argument to those macros instead, since only one info object can
// be attached.
#define DEFINE_CVar_DisplayName(name, display_name)                     \
  namespace cv {                                                        \
  static const cvar::ConfigVarEditorInfo display_info_##name = {        \
      display_name, {}, false, 0.0, 0.0, 0.0};                          \
  static const cvar::ConfigVarEditorInfoAttacher<decltype(cvars::name)> \
      attach_display_info_##name(cv_##name, &display_info_##name);      \
  }

// Enum-like variable: the editor shows a dropdown of label/value pairs instead
// of a free-form text field. String values are written verbatim; integer values
// are parsed by SetFromString, so DEFINE_int32_choices accepts numeric strings.
#define DEFINE_string_choices(name, default_value, description, category,     \
                              display_name, ...)                              \
  DEFINE_CVar(name, default_value, description, category, false, std::string) \
      DEFINE_CVar_CHOICES(std::string, name, display_name, __VA_ARGS__)
#define DEFINE_int32_choices(name, default_value, description, category,  \
                             display_name, ...)                           \
  DEFINE_CVar(name, default_value, description, category, false, int32_t) \
      DEFINE_CVar_CHOICES(int32_t, name, display_name, __VA_ARGS__)

// Numeric variable with a known, inclusive range: the editor shows a slider
// paired with a spin box instead of a text field. min_value / max_value must
// fit in an int (slider positions), and step_value > 0 for doubles.
#define DEFINE_int32_range(name, default_value, description, category,     \
                           display_name, min_value, max_value)             \
  DEFINE_CVar(name, default_value, description, category, false, int32_t)  \
      DEFINE_CVar_RANGE(int32_t, name, display_name, min_value, max_value, \
                        0.0)
#define DEFINE_uint32_range(name, default_value, description, category,     \
                            display_name, min_value, max_value)             \
  DEFINE_CVar(name, default_value, description, category, false, uint32_t)  \
      DEFINE_CVar_RANGE(uint32_t, name, display_name, min_value, max_value, \
                        0.0)
#define DEFINE_double_range(name, default_value, description, category,     \
                            display_name, min_value, max_value, step_value) \
  DEFINE_CVar(name, default_value, description, category, false, double)    \
      DEFINE_CVar_RANGE(double, name, display_name, min_value, max_value,   \
                        step_value)

// Remaining numeric types with the same slider treatment. The range is stored
// as doubles, so keep the bounds inside the range an int can address - the
// slider itself only has int positions.
#define DEFINE_uint64_range(name, default_value, description, category,     \
                            display_name, min_value, max_value)             \
  DEFINE_CVar(name, default_value, description, category, false, uint64_t)  \
      DEFINE_CVar_RANGE(uint64_t, name, display_name, min_value, max_value, \
                        0.0)
#define DEFINE_int64_range(name, default_value, description, category,     \
                           display_name, min_value, max_value)             \
  DEFINE_CVar(name, default_value, description, category, false, int64_t)  \
      DEFINE_CVar_RANGE(int64_t, name, display_name, min_value, max_value, \
                        0.0)

// Same as DEFINE_int32_choices for the other integer types.
#define DEFINE_uint32_choices(name, default_value, description, category,  \
                              display_name, ...)                           \
  DEFINE_CVar(name, default_value, description, category, false, uint32_t) \
      DEFINE_CVar_CHOICES(uint32_t, name, display_name, __VA_ARGS__)
#define DEFINE_int64_choices(name, default_value, description, category,  \
                             display_name, ...)                           \
  DEFINE_CVar(name, default_value, description, category, false, int64_t) \
      DEFINE_CVar_CHOICES(int64_t, name, display_name, __VA_ARGS__)
#define DEFINE_uint64_choices(name, default_value, description, category,  \
                              display_name, ...)                           \
  DEFINE_CVar(name, default_value, description, category, false, uint64_t) \
      DEFINE_CVar_CHOICES(uint64_t, name, display_name, __VA_ARGS__)

// Bit-flag variable (a mask). The editor shows one checkbox per choice and
// writes the bitwise OR of the checked ones, so each choice's value must be
// the numeric value of that single bit. This is for sets that combine, unlike
// DEFINE_*_choices where only one value can be selected at a time.
#define DEFINE_int32_flags(name, default_value, description, category,    \
                           display_name, ...)                             \
  DEFINE_CVar(name, default_value, description, category, false, int32_t) \
      DEFINE_CVar_FLAGS(int32_t, name, display_name, __VA_ARGS__)
#define DEFINE_uint32_flags(name, default_value, description, category,    \
                            display_name, ...)                             \
  DEFINE_CVar(name, default_value, description, category, false, uint32_t) \
      DEFINE_CVar_FLAGS(uint32_t, name, display_name, __VA_ARGS__)
#define DEFINE_int64_flags(name, default_value, description, category,    \
                           display_name, ...)                             \
  DEFINE_CVar(name, default_value, description, category, false, int64_t) \
      DEFINE_CVar_FLAGS(int64_t, name, display_name, __VA_ARGS__)
#define DEFINE_uint64_flags(name, default_value, description, category,    \
                            display_name, ...)                             \
  DEFINE_CVar(name, default_value, description, category, false, uint64_t) \
      DEFINE_CVar_FLAGS(uint64_t, name, display_name, __VA_ARGS__)

// Integer variable whose dropdown contents are produced by a function at
// runtime rather than registered statically - used for GPU adapters and
// devices, which depend on the machine. provider is a function returning
// `const std::vector<cvar::ConfigVarEditorInfo::Choice>&` whose result must
// outlive the call (typically a lazily created, never destroyed singleton).
#define DEFINE_int32_dynamic_choices(name, default_value, description,    \
                                     category, display_name, provider)    \
  DEFINE_CVar(name, default_value, description, category, false, int32_t) \
      DEFINE_CVar_DYNAMIC_CHOICES(int32_t, name, display_name, provider)

// Gives a DEFINE_path variable a browse button in the editor. is_directory
// selects between the directory and the file chooser.
#define DEFINE_CVar_PathPicker(name, display_name, is_directory)             \
  namespace cv {                                                             \
  static const cvar::ConfigVarEditorInfo path_info_##name = {                \
      display_name, {}, false, 0.0, 0.0, 0.0, false, nullptr, is_directory}; \
  static const cvar::ConfigVarEditorInfoAttacher<decltype(cvars::name)>      \
      attach_path_info_##name(cv_##name, &path_info_##name);                 \
  }

#define DEFINE_CVar_CHOICES(type, name, display_name, ...)       \
  namespace cv {                                                 \
  static const cvar::ConfigVarEditorInfo editor_info_##name = {  \
      display_name, {__VA_ARGS__}, false, 0.0, 0.0, 0.0};        \
  static const cvar::ConfigVarEditorInfoAttacher<type>           \
      attach_editor_info_##name(cv_##name, &editor_info_##name); \
  }
#define DEFINE_CVar_RANGE(type, name, display_name, min_value, max_value, \
                          step_value)                                     \
  namespace cv {                                                          \
  static const cvar::ConfigVarEditorInfo editor_info_##name = {           \
      display_name, {}, true, min_value, max_value, step_value};          \
  static const cvar::ConfigVarEditorInfoAttacher<type>                    \
      attach_editor_info_##name(cv_##name, &editor_info_##name);          \
  }
#define DEFINE_CVar_FLAGS(type, name, display_name, ...)         \
  namespace cv {                                                 \
  static const cvar::ConfigVarEditorInfo editor_info_##name = {  \
      display_name, {__VA_ARGS__}, false, 0.0, 0.0, 0.0, true};  \
  static const cvar::ConfigVarEditorInfoAttacher<type>           \
      attach_editor_info_##name(cv_##name, &editor_info_##name); \
  }
#define DEFINE_CVar_DYNAMIC_CHOICES(type, name, display_name, provider) \
  namespace cv {                                                        \
  static const cvar::ConfigVarEditorInfo editor_info_##name = {         \
      display_name, {}, false, 0.0, 0.0, 0.0, false, provider};         \
  static const cvar::ConfigVarEditorInfoAttacher<type>                  \
      attach_editor_info_##name(cv_##name, &editor_info_##name);        \
  }

#define DEFINE_transient_bool(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, true, bool)

#define DEFINE_transient_string(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, true, std::string)

#define DEFINE_transient_path(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, true,           \
              std::filesystem::path)

#define DEFINE_CVar(name, default_value, description, category, is_transient, \
                    type)                                                     \
  namespace cvars {                                                           \
  type name = default_value;                                                  \
  }                                                                           \
  namespace cv {                                                              \
  static cvar::IConfigVar* const cv_##name = cvar::define_configvar(          \
      #name, &cvars::name, description, category, is_transient);              \
  }

// CmdVars can only be strings for now, we don't need any others
#define CmdVar(name, default_value, description)             \
  namespace cvars {                                          \
  std::string name = default_value;                          \
  }                                                          \
  namespace cv {                                             \
  static cvar::ICommandVar* const cv_##name =                \
      cvar::define_cmdvar(#name, &cvars::name, description); \
  }

#define DECLARE_bool(name) DECLARE_CVar(name, bool)

#define DECLARE_int32(name) DECLARE_CVar(name, int32_t)

#define DECLARE_uint32(name) DECLARE_CVar(name, uint32_t)

#define DECLARE_uint64(name) DECLARE_CVar(name, uint64_t)
#define DECLARE_int64(name) DECLARE_CVar(name, int64_t)
#define DECLARE_double(name) DECLARE_CVar(name, double)

#define DECLARE_string(name) DECLARE_CVar(name, std::string)

#define DECLARE_path(name) DECLARE_CVar(name, std::filesystem::path)

#define DECLARE_CVar(name, type) \
  namespace cvars {              \
  extern type name;              \
  }

#define ACCESS_CVar(name) (*cv::cv_##name)

// dynamic_cast is needed because of virtual inheritance.
#define OVERRIDE_CVar(name, type, value)                   \
  dynamic_cast<cvar::ConfigVar<type>*>(&ACCESS_CVar(name)) \
      ->OverrideConfigValue(value);

#define OVERRIDE_bool(name, value) OVERRIDE_CVar(name, bool, value)

#define OVERRIDE_int32(name, value) OVERRIDE_CVar(name, int32_t, value)

#define OVERRIDE_uint32(name, value) OVERRIDE_CVar(name, uint32_t, value)

#define OVERRIDE_uint64(name, value) OVERRIDE_CVar(name, uint64_t, value)

#define OVERRIDE_double(name, value) OVERRIDE_CVar(name, double, value)

#define OVERRIDE_string(name, value) OVERRIDE_CVar(name, std::string, value)

#define OVERRIDE_path(name, value) \
  OVERRIDE_CVar(name, std::filesystem::path, value)

// Interface for changing the default value of a variable with auto-upgrading of
// users' configs (to distinguish between a leftover old default and an explicit
// override), without having to rename the variable.
//
// Two types of updates are supported:
// - Changing the value of the variable (UPDATE_from_type) from an explicitly
//   specified previous default value to a new one, but keeping the
//   user-specified value if it was not the default, and thus explicitly
//   overridden.
// - Changing the meaning / domain of the variable (UPDATE_from_any), when
//   previous user-specified overrides also stop making sense. Config variable
//   type changes are also considered this type of updates (though
//   UPDATE_from_type, if the new type doesn't match the previous one, is also
//   safe to use - it behaves like UPDATE_from_any in this case).
//
// Rules of using UPDATE_:
// - Do not remove previous UPDATE_ entries (both typed and from-any) if you're
//   adding a new UPDATE_from_type.
//   This ensures that if the default was changed from 1 to 2 and then to 3,
//   both users who last launched Xenia when it was 1 and when it was 2 receive
//   the update (however, those who have explicitly changed it from 2 to 1 when
//   2 was the default will have it kept at 1).
//   It's safe to remove the history before a new UPDATE_from_any, however.
// - The date should preferably be in UTC+0 timezone.
// - No other pull recent pull requests should have the same date (since builds
//   are made after every commit).
// - IConfigVarUpdate::kLastCommittedUpdateDate must be updated - see the
//   comment near its declaration.

constexpr uint32_t MakeConfigVarUpdateDate(uint32_t year, uint32_t month,
                                           uint32_t day, uint32_t utc_hour) {
  // Written to the config as a decimal number - pack as decimal for user
  // readability.
  // Using 31 bits in the 3rd millennium already - don't add more digits.
  return utc_hour + day * 100 + month * 10000 + year * 1000000;
}

class IConfigVarUpdate {
 public:
  // This global highest version constant is used to ensure that version (which
  // is stored as one value for the whole config file) is monotonically
  // increased when commits - primarily pull requests - are pushed to the main
  // branch.
  //
  // This is to prevent the following situation:
  // - Pull request #1 created on day 1.
  // - Pull request #2 created on day 2.
  // - Pull request #2 from day 2 merged on day 3.
  // - User launches the latest version on day 4.
  //   CVar default changes from PR #2 (day 2) applied because the user's config
  //   version is day 0, which is < 2.
  //   User's config has day 2 version now.
  // - Pull request #1 from day 1 merged on day 5.
  // - User launches the latest version on day 5.
  //   CVar default changes from PR #1 (day 1) IGNORED because the user's config
  //   version is day 2, which is >= 1.
  //
  // If this constant is not updated, static_assert will be triggered for a new
  // DEFINE_, requiring this constant to be raised. But changing this will
  // result in merge conflicts in all other pull requests also changing cvar
  // defaults - before they're merged, they will need to be updated, which will
  // ensure monotonic growth of the versions of all cvars on the main branch. In
  // the example above, PR #1 will need to be updated before it's merged.
  //
  // If you've encountered a merge conflict here in your pull request:
  //   1) Update any UPDATE_s you've added in the pull request to the current
  //      date.
  //   2) Change this value to the same date.
  // If you're reviewing a pull request with a change here, check if 1) has been
  // done by the submitter before merging.
  static constexpr uint32_t kLastCommittedUpdateDate =
      MakeConfigVarUpdateDate(2026, 4, 9, 12);

  virtual ~IConfigVarUpdate() = default;

  virtual void Apply() const = 0;

  static void ApplyUpdates(uint32_t config_date) {
    if (!updates_) {
      return;
    }
    auto it_end = updates_->end();
    for (auto it = updates_->upper_bound(config_date); it != it_end; ++it) {
      it->second->Apply();
    }
  }

  // More reliable than kLastCommittedUpdateDate for actual usage
  // (kLastCommittedUpdateDate is just a pull request merge order guard), though
  // usually should be the same, but kLastCommittedUpdateDate may not include
  // removal of cvars.
  static uint32_t GetLastUpdateDate() {
    return (updates_ && !updates_->empty()) ? updates_->crbegin()->first : 0;
  }

 protected:
  IConfigVarUpdate(IConfigVar* const& config_var, uint32_t year, uint32_t month,
                   uint32_t day, uint32_t utc_hour)
      : config_var_(config_var) {
    if (!updates_) {
      updates_ = new std::multimap<uint32_t, const IConfigVarUpdate*>;
    }
    updates_->emplace(MakeConfigVarUpdateDate(year, month, day, utc_hour),
                      this);
  }

  IConfigVar& config_var() const {
    assert_not_null(config_var_);
    return *config_var_;
  }

 private:
  // Reference to pointer to loosen initialization order requirements.
  IConfigVar* const& config_var_;

  // Updates can be initialized before these, thus initialized on demand using
  // `new`.
  static std::multimap<uint32_t, const IConfigVarUpdate*>* updates_;
};

class ConfigVarUpdateFromAny : public IConfigVarUpdate {
 public:
  ConfigVarUpdateFromAny(IConfigVar* const& config_var, uint32_t year,
                         uint32_t month, uint32_t day, uint32_t utc_hour)
      : IConfigVarUpdate(config_var, year, month, day, utc_hour) {}
  void Apply() const override { config_var().ResetConfigValueToDefault(); }
};

template <typename T>
class ConfigVarUpdate : public IConfigVarUpdate {
 public:
  ConfigVarUpdate(IConfigVar* const& config_var, uint32_t year, uint32_t month,
                  uint32_t day, uint32_t utc_hour, const T& old_default_value)
      : IConfigVarUpdate(config_var, year, month, day, utc_hour),
        old_default_value_(old_default_value) {}
  void Apply() const override {
    IConfigVar& config_var_untyped = config_var();
    ConfigVar<T>* config_var_typed =
        dynamic_cast<ConfigVar<T>*>(&config_var_untyped);
    // Update only from the previous default value if the same type,
    // unconditionally reset if the type has been changed.
    if (!config_var_typed ||
        config_var_typed->GetTypedConfigValue() == old_default_value_) {
      config_var_untyped.ResetConfigValueToDefault();
    }
  }

 private:
  T old_default_value_;
};

#define UPDATE_from_any(name, year, month, day, utc_hour)                     \
  static_assert(                                                              \
      cvar::MakeConfigVarUpdateDate(year, month, day, utc_hour) <=            \
          cvar::IConfigVarUpdate::kLastCommittedUpdateDate,                   \
      "A new config variable default value update was added - raise "         \
      "cvar::IConfigVarUpdate::kLastCommittedUpdateDate to the same date in " \
      "base/cvar.h to ensure coherence between different pull requests "      \
      "updating config variable defaults.");                                  \
  namespace cv {                                                              \
  static const cvar::ConfigVarUpdateFromAny                                   \
      update_##name_##year_##month_##day_##utc_hour(cv_##name, year, month,   \
                                                    day, utc_hour);           \
  }

#define UPDATE_CVar(name, year, month, day, utc_hour, old_default_value, type) \
  static_assert(                                                               \
      cvar::MakeConfigVarUpdateDate(year, month, day, utc_hour) <=             \
          cvar::IConfigVarUpdate::kLastCommittedUpdateDate,                    \
      "A new config variable default value update was added - raise "          \
      "cvar::IConfigVarUpdate::kLastCommittedUpdateDate to the same date in "  \
      "base/cvar.h to ensure coherence between different pull requests "       \
      "updating config variable defaults.");                                   \
  namespace cv {                                                               \
  static const cvar::ConfigVarUpdate<type>                                     \
      update_##name_##year_##month_##day_##utc_hour(cv_##name, year, month,    \
                                                    day, utc_hour,             \
                                                    old_default_value);        \
  }

#define UPDATE_from_bool(name, year, month, day, utc_hour, old_default_value) \
  UPDATE_CVar(name, year, month, day, utc_hour, old_default_value, bool)

#define UPDATE_from_int32(name, year, month, day, utc_hour, old_default_value) \
  UPDATE_CVar(name, year, month, day, utc_hour, old_default_value, int32_t)

#define UPDATE_from_uint32(name, year, month, day, utc_hour, \
                           old_default_value)                \
  UPDATE_CVar(name, year, month, day, utc_hour, old_default_value, uint32_t)

#define UPDATE_from_uint64(name, year, month, day, utc_hour, \
                           old_default_value)                \
  UPDATE_CVar(name, year, month, day, utc_hour, old_default_value, uint64_t)

#define UPDATE_from_double(name, year, month, day, utc_hour, \
                           old_default_value)                \
  UPDATE_CVar(name, year, month, day, utc_hour, old_default_value, double)

#define UPDATE_from_string(name, year, month, day, utc_hour, \
                           old_default_value)                \
  UPDATE_CVar(name, year, month, day, utc_hour, old_default_value, std::string)

#define UPDATE_from_path(name, year, month, day, utc_hour, old_default_value) \
  UPDATE_CVar(name, year, month, day, utc_hour, old_default_value,            \
              std::filesystem::path)

}  // namespace cvar

#endif  // XENIA_CVAR_H_
