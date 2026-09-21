/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "config.h"

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/system.h"

toml::parse_result ParseFile(const std::filesystem::path& filename) {
  return toml::parse_file(xe::path_to_utf8(filename));
}

CmdVar(config, "", "Specifies the target config to load.");

DEFINE_uint32(
    defaults_date, 0,
    "Do not modify - internal version of the default values in the config, for "
    "seamless updates if default value of any option is changed.",
    "Config");

namespace config {
std::string config_name = "xenia-canary.config.toml";
std::filesystem::path config_folder;
std::filesystem::path config_path;
std::string game_config_suffix = ".config.toml";

bool sortCvar(cvar::IConfigVar* a, cvar::IConfigVar* b) {
  if (a->category() < b->category()) {
    return true;
  }
  if (a->category() > b->category()) {
    return false;
  }
  if (a->name() < b->name()) {
    return true;
  }
  return false;
}

toml::parse_result ParseConfig(const std::filesystem::path& config_path) {
  try {
    return ParseFile(config_path);
  } catch (toml::parse_error& e) {
    xe::FatalError(fmt::format("Failed to parse config file '{}':\n\n{}",
                               config_path, e.what()));
    return toml::parse_result();
  }
}

void PrintConfigToLog(const std::filesystem::path& file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return;
  }

  std::string config_dump = "----------- CONFIG DUMP -----------\n";
  std::string config_line = "";
  while (std::getline(file, config_line)) {
    if (config_line.empty()) {
      continue;
    }

    // Find place where comment begins and cut that part.
    const size_t comment_mark_position = config_line.find_first_of("#");
    if (comment_mark_position != std::string::npos) {
      config_line.erase(comment_mark_position, config_line.length());
    }

    // Check if remaining part of line is empty.
    if (std::ranges::all_of(std::as_const(config_line), isspace)) {
      continue;
    }
    // Check if line is a category mark. If it is add new line on start for
    // improved visibility.
    const bool category_mark = config_line.at(0) == '[';
    config_dump += (category_mark ? "\n" : "") + config_line + "\n";
  }
  config_dump += "----------- END OF CONFIG DUMP ----";
  XELOGI("{}", config_dump);

  file.close();
}

void ReadConfig(const std::filesystem::path& file_path,
                bool update_if_no_version_stored) {
  if (!cvar::ConfigVars) {
    return;
  }

  const auto config = ParseConfig(file_path);

  PrintConfigToLog(file_path);

  // Loading an actual global config file that exists - if there's no
  // defaults_date in it, it's very old (before updating was added at all, thus
  // all defaults need to be updated).
  auto defaults_date_cvar =
      dynamic_cast<cvar::ConfigVar<uint32_t>*>(cv::cv_defaults_date);
  assert_not_null(defaults_date_cvar);
  defaults_date_cvar->SetConfigValue(0);
  for (auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    toml::path config_key =
        toml::path(config_var->category() + "." + config_var->name());

    const auto config_key_node = config.at_path(config_key);
    if (config_key_node) {
      config_var->LoadConfigValue(config_key_node.node());
    }
  }
  uint32_t config_defaults_date = defaults_date_cvar->GetTypedConfigValue();
  if (update_if_no_version_stored || config_defaults_date) {
    cvar::IConfigVarUpdate::ApplyUpdates(config_defaults_date);
  }

  // Check for type mismatch warnings
  if (cvar::config_type_mismatch_warnings &&
      !cvar::config_type_mismatch_warnings->empty()) {
    std::string warning_message =
        "The following config values had invalid types and have been reset to "
        "defaults:\n\n";
    for (const auto& name : *cvar::config_type_mismatch_warnings) {
      warning_message += "  - " + name + "\n";
    }
    warning_message +=
        "\nPlease check your config file. The config will be saved with the "
        "correct types.";

    xe::ShowSimpleMessageBox(xe::SimpleMessageBoxType::Warning,
                             warning_message);

    // Clear warnings
    cvar::config_type_mismatch_warnings->clear();
  }

  XELOGI("Loaded config: {}", file_path);
}

void ClearGameConfig() {
  if (!cvar::ConfigVars) {
    return;
  }
  for (auto& it : *cvar::ConfigVars) {
    it.second->ResetGameConfigValue();
  }
}

namespace {

std::string_view TrimAscii(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// Collects the keys of a parsed override file that the cvar registry cannot
// read back: settings removed or renamed since the file was written, or written
// under a section their variable doesn't use. `ReadGameConfig` walks the
// registry rather than the file, so such a key is dead weight no editor would
// ever show or remove. Dotted sections are nested tables in TOML, so the
// section name is rebuilt while descending.
void CollectUnknownGameConfigKeys(const toml::table& table,
                                  std::string_view section,
                                  std::set<std::string>& unknown) {
  if (!cvar::ConfigVars) {
    return;
  }
  for (const auto& [key, node] : table) {
    std::string name(key.str());
    if (const auto* nested = node.as_table()) {
      CollectUnknownGameConfigKeys(
          *nested, section.empty() ? name : std::string(section) + "." + name,
          unknown);
      continue;
    }
    auto it = cvar::ConfigVars->find(name);
    if (it == cvar::ConfigVars->end() ||
        it->second->category() != std::string(section)) {
      unknown.insert(std::move(name));
    }
  }
}

}  // namespace

void PruneGameConfigFile(const std::filesystem::path& file_path,
                         const std::set<std::string>& keys) {
  if (keys.empty()) {
    return;
  }
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return;
  }
  std::vector<std::string> kept_lines;
  std::string line;
  bool dropped_any = false;
  while (std::getline(file, line)) {
    const std::string_view trimmed = TrimAscii(line);
    // Only assignments are considered: the header comments and the section
    // headers survive verbatim, so the rest of the file keeps its shape.
    if (!trimmed.empty() && trimmed.front() != '#' && trimmed.front() != '[') {
      const auto equals = trimmed.find('=');
      if (equals != std::string_view::npos &&
          keys.contains(std::string(TrimAscii(trimmed.substr(0, equals))))) {
        dropped_any = true;
        continue;
      }
    }
    kept_lines.push_back(line);
  }
  file.close();
  if (!dropped_any) {
    return;
  }
  auto handle = xe::filesystem::OpenFile(file_path, "wb");
  if (!handle) {
    XELOGE("Failed to open '{}' for rewriting.", file_path);
    return;
  }
  for (const auto& kept : kept_lines) {
    fputs((kept + "\n").c_str(), handle);
  }
  fclose(handle);
  XELOGI("Pruned game config: {}", file_path);
}

void ReadGameConfig(const std::filesystem::path& file_path) {
  if (!cvar::ConfigVars) {
    return;
  }
  // Start from the global values: the file lists only the settings it
  // overrides, and a key that is absent from it has to fall back to the
  // global value rather than keep whatever the previously launched title set.
  ClearGameConfig();
  const auto config = ParseConfig(file_path);
  std::set<std::string> unknown_keys;
  CollectUnknownGameConfigKeys(config, "", unknown_keys);
  std::set<std::string> mismatched_keys;
  for (auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    toml::path config_key =
        toml::path(config_var->category() + "." + config_var->name());

    const auto config_key_node = config.at_path(config_key);
    if (config_key_node) {
      // The per-title setter, not the global one: this file sits on top of the
      // global config, so writing it into the global layer would both change
      // what the global editor shows and copy the override into the global
      // file on the next save.
      if (!config_var->LoadGameConfigValue(config_key_node.node())) {
        mismatched_keys.insert(config_var->name());
      }
    }
  }
  if (!unknown_keys.empty() || !mismatched_keys.empty()) {
    // An override this build cannot read - the setting was removed or renamed,
    // or changed type since the file was written (a text option becoming a
    // boolean, say) - is dropped here and removed from the file. Keeping it
    // would pin the title to a value that is never applied, with nothing in
    // the UI able to show or clear it.
    auto log_names = [&](const char* reason,
                         const std::set<std::string>& keys) {
      if (keys.empty()) {
        return;
      }
      std::string names;
      for (const auto& name : keys) {
        names += (names.empty() ? "" : ", ") + name;
      }
      XELOGW("Game config '{}': dropping {} ({}).", file_path, reason, names);
    };
    log_names("unknown settings", unknown_keys);
    log_names("overrides that no longer fit their type", mismatched_keys);

    std::set<std::string> stale_keys = std::move(unknown_keys);
    stale_keys.insert(mismatched_keys.begin(), mismatched_keys.end());
    PruneGameConfigFile(file_path, stale_keys);
  }
  XELOGI("Loaded game config: {}", file_path);
}

void SaveConfig() {
  if (config_path.empty()) {
    return;
  }

  // All cvar defaults have been updated on loading - store the current date.
  auto defaults_date_cvar =
      dynamic_cast<cvar::ConfigVar<uint32_t>*>(cv::cv_defaults_date);
  assert_not_null(defaults_date_cvar);
  defaults_date_cvar->SetConfigValue(
      cvar::IConfigVarUpdate::GetLastUpdateDate());

  std::vector<cvar::IConfigVar*> vars;
  if (cvar::ConfigVars) {
    for (const auto& s : *cvar::ConfigVars) {
      vars.push_back(s.second);
    }
  }
  std::ranges::sort(vars, sortCvar);

  // we use our own write logic because cpptoml doesn't
  // allow us to specify comments :(
  std::string last_category;
  bool last_multiline_description = false;
  xe::StringBuffer sb;
  for (auto config_var : vars) {
    if (config_var->is_transient()) {
      continue;
    }

    if (last_category != config_var->category()) {
      if (!last_category.empty()) {
        sb.Append('\n', 2);
      }
      last_category = config_var->category();
      last_multiline_description = false;
      sb.AppendFormat("[{}]\n", last_category);
    } else if (last_multiline_description) {
      last_multiline_description = false;
      sb.Append('\n');
    }

    auto value = config_var->config_value();
    size_t line_count;
    if (xe::utf8::find_any_of(value, "\n") == std::string_view::npos) {
      auto line = fmt::format("{} = {}", config_var->name(),
                              config_var->config_value());
      sb.Append(line);
      line_count = xe::utf8::count(line);
    } else {
      auto lines = xe::utf8::split(value, "\n");
      auto first = lines.cbegin();
      sb.AppendFormat("{} = {}\n", config_var->name(), *first);
      auto last = std::prev(lines.cend());
      for (auto it = std::next(first); it != last; ++it) {
        sb.Append(*it);
        sb.Append('\n');
      }
      sb.Append(*last);
      line_count = xe::utf8::count(*last);
    }

    constexpr size_t value_alignment = 50;
    const auto& description = config_var->description();
    if (!description.empty()) {
      if (line_count < value_alignment) {
        sb.Append(' ', value_alignment - line_count);
      }
      if (xe::utf8::find_any_of(description, "\n") == std::string_view::npos) {
        sb.AppendFormat("\t# {}\n", config_var->description());
      } else {
        auto lines = xe::utf8::split(description, "\n");
        auto first = lines.cbegin();
        sb.Append("\t# ");
        sb.Append(*first);
        sb.Append('\n');
        for (auto it = std::next(first); it != lines.cend(); ++it) {
          sb.Append(' ', value_alignment);
          sb.Append("\t# ");
          sb.Append(*it);
          sb.Append('\n');
        }
        last_multiline_description = true;
      }
    }
  }

  // save the config file
  xe::filesystem::CreateParentFolder(config_path);

  auto handle = xe::filesystem::OpenFile(config_path, "wb");
  if (!handle) {
    XELOGE("Failed to open '{}' for writing.", config_path);
  } else {
    fwrite(sb.buffer(), 1, sb.length(), handle);
    fclose(handle);
  }
}

void SetupConfig(const std::filesystem::path& config_folder) {
  config::config_folder = config_folder;
  // check if the user specified a specific config to load
  if (!cvars::config.empty()) {
    config_path = xe::to_path(cvars::config);
    if (std::filesystem::exists(config_path)) {
      // An external config file may contain only explicit overrides - in this
      // case, it will likely not contain the defaults version; don't update
      // from the version 0 in this case. Or, it may be a full config - in this
      // case, if it's recent enough (created at least in 2021), it will contain
      // the version number - updates the defaults in it.
      ReadConfig(config_path, false);
      return;
    }
  }
  // if the user specified a --config argument, but the file doesn't exist,
  // let's also load the default config
  if (!config_folder.empty()) {
    config_path = config_folder / config_name;
    if (std::filesystem::exists(config_path)) {
      ReadConfig(config_path, true);
    }
    // Re-save the loaded config to present the most up-to-date list of
    // parameters to the user, if new options were added, descriptions were
    // updated, or default values were changed.
    SaveConfig();
  }
}

std::filesystem::path GameConfigPath(const std::string& title_id) {
  if (config_folder.empty()) {
    return std::filesystem::path();
  }
  return config_folder / "config" / (title_id + game_config_suffix);
}

void LoadGameConfig(const std::string_view title_id) {
  const auto game_config_path = GameConfigPath(std::string(title_id));
  if (std::filesystem::exists(game_config_path)) {
    ReadGameConfig(game_config_path);
    return;
  }
  // This title has no overrides of its own - the previous title's, which are
  // still in the cvars, must not carry over to it.
  ClearGameConfig();
}

bool SaveGameConfigSparse(const std::string& title_id,
                          const std::string& title_name,
                          const std::map<std::string, std::string>* reasons) {
  const auto game_config_path = GameConfigPath(title_id);
  if (game_config_path.empty()) {
    return false;
  }

  std::vector<cvar::IConfigVar*> vars;
  if (cvar::ConfigVars) {
    for (const auto& it : *cvar::ConfigVars) {
      auto* config_var = it.second;
      // Transient variables are runtime state, and a variable without a
      // per-game value was never overridden - writing it would turn this file
      // into a copy of the global config.
      if (!config_var->is_transient() && config_var->has_game_config_value()) {
        vars.push_back(config_var);
      }
    }
  }
  std::ranges::sort(vars, sortCvar);

  if (vars.empty()) {
    // Nothing is overridden any more: the title falls back to the global
    // config entirely, so the file itself goes away.
    std::error_code ec;
    const bool removed = std::filesystem::remove(game_config_path, ec);
    if (ec) {
      XELOGE("Failed to remove '{}': {}", game_config_path, ec.message());
      return false;
    }
    if (removed) {
      XELOGI("Removed game config: {}", game_config_path);
    }
    return true;
  }

  xe::StringBuffer sb;
  sb.AppendFormat("# Title Name: {}\n", title_name);
  sb.AppendFormat("# Title ID: {}\n", title_id);
  std::string last_category;
  for (auto* config_var : vars) {
    if (last_category != config_var->category()) {
      last_category = config_var->category();
      sb.AppendFormat("\n[{}]\n", last_category);
    }
    // game_config_value() is already the TOML text (quoted and escaped for
    // strings and paths), exactly like config_value() in SaveConfig.
    const std::optional<std::string> value = config_var->game_config_value();
    assert_true(value.has_value());
    sb.AppendFormat("{} = {}", config_var->name(), *value);
    if (reasons) {
      auto it = reasons->find(config_var->name());
      if (it != reasons->end() && !it->second.empty()) {
        // The reason is a single-line TOML comment, so line breaks in it would
        // split the line into something that no longer parses as a comment.
        std::string reason = it->second;
        std::ranges::replace(reason, '\n', ' ');
        std::ranges::replace(reason, '\r', ' ');
        sb.AppendFormat(" # {}", reason);
      }
    }
    sb.Append('\n');
  }

  xe::filesystem::CreateParentFolder(game_config_path);
  auto handle = xe::filesystem::OpenFile(game_config_path, "wb");
  if (!handle) {
    XELOGE("Failed to open '{}' for writing.", game_config_path);
    return false;
  }
  fwrite(sb.buffer(), 1, sb.length(), handle);
  fclose(handle);
  XELOGI("Saved game config: {}", game_config_path);
  return true;
}

}  // namespace config
