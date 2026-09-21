/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_provider.h"

#include <string>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"

namespace {

// Dropdown contents for the config editor. Enumerating devices needs a live
// instance, so a throwaway one is created on first use and the result is kept
// for the process lifetime; if Vulkan is unavailable the list stays empty and
// the editor falls back to a plain field.
const std::vector<cvar::ConfigVarEditorInfo::Choice>& VulkanDeviceChoices() {
  static const std::vector<cvar::ConfigVarEditorInfo::Choice>* choices = [] {
    auto* labels = new std::vector<std::string>();
    auto* values = new std::vector<std::string>();
    labels->push_back("Any compatible device");
    values->push_back("-1");
    std::unique_ptr<xe::ui::vulkan::VulkanInstance> instance =
        xe::ui::vulkan::VulkanInstance::Create(false, false);
    if (instance) {
      std::vector<VkPhysicalDevice> physical_devices;
      instance->EnumeratePhysicalDevices(physical_devices);
      const xe::ui::vulkan::VulkanInstance::Functions& ifn =
          instance->functions();
      for (size_t i = 0; i < physical_devices.size(); ++i) {
        VkPhysicalDeviceProperties properties;
        ifn.vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
        labels->push_back(fmt::format("{}: {}", i, properties.deviceName));
        values->push_back(std::to_string(i));
      }
    }
    auto* result = new std::vector<cvar::ConfigVarEditorInfo::Choice>();
    result->reserve(labels->size());
    for (size_t i = 0; i < labels->size(); ++i) {
      result->push_back({(*labels)[i].c_str(), (*values)[i].c_str()});
    }
    return result;
  }();
  return *choices;
}

}  // namespace

DEFINE_bool(
    vulkan_validation, false,
    "Enable the Vulkan validation layer (VK_LAYER_KHRONOS_validation). "
    "Messages will be written to the Xenia log if 'vulkan_log_debug_messages' "
    "is enabled, or to the OS debug output otherwise.",
    "Vulkan");
DEFINE_CVar_DisplayName(vulkan_validation, "Vulkan validation layers");

DEFINE_int32_dynamic_choices(
    vulkan_device, -1,
    "Index of the preferred Vulkan physical device, or -1 to use any "
    "compatible device.",
    "Vulkan", "GPU device", &VulkanDeviceChoices);

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanProvider> VulkanProvider::Create(
    const bool with_gpu_emulation, const bool with_presentation) {
  std::unique_ptr<VulkanProvider> provider(new VulkanProvider());

  provider->vulkan_instance_ =
      VulkanInstance::Create(with_presentation, cvars::vulkan_validation);
  if (!provider->vulkan_instance_) {
    return nullptr;
  }

  std::vector<VkPhysicalDevice> physical_devices;
  provider->vulkan_instance_->EnumeratePhysicalDevices(physical_devices);

  if (physical_devices.empty()) {
    XELOGW("No Vulkan physical devices available");
    return nullptr;
  }

  const VulkanInstance::Functions& ifn =
      provider->vulkan_instance_->functions();

  XELOGW(
      "Available Vulkan physical devices (use the 'vulkan_device' "
      "configuration variable to force a specific device):");
  for (size_t physical_device_index = 0;
       physical_device_index < physical_devices.size();
       ++physical_device_index) {
    VkPhysicalDeviceProperties physical_device_properties;
    ifn.vkGetPhysicalDeviceProperties(physical_devices[physical_device_index],
                                      &physical_device_properties);
    XELOGW("* {}: {}", physical_device_index,
           physical_device_properties.deviceName);
  }

  const int32_t preferred_physical_device_index = cvars::vulkan_device;
  if (preferred_physical_device_index >= 0 &&
      uint32_t(preferred_physical_device_index) < physical_devices.size()) {
    provider->vulkan_device_ = VulkanDevice::CreateIfSupported(
        provider->vulkan_instance_.get(),
        physical_devices[preferred_physical_device_index], with_gpu_emulation,
        with_presentation);
  }

  if (!provider->vulkan_device_) {
    for (const VkPhysicalDevice physical_device : physical_devices) {
      provider->vulkan_device_ = VulkanDevice::CreateIfSupported(
          provider->vulkan_instance_.get(), physical_device, with_gpu_emulation,
          with_presentation);
      if (provider->vulkan_device_) {
        break;
      }
    }

    if (!provider->vulkan_device_) {
      XELOGW(
          "Couldn't choose a compatible Vulkan physical device or initialize a "
          "Vulkan logical device");
      return nullptr;
    }
  }

  if (with_presentation) {
    provider->ui_samplers_ = UISamplers::Create(provider->vulkan_device_.get());
    if (!provider->ui_samplers_) {
      return nullptr;
    }
  }

  return provider;
}

std::unique_ptr<Presenter> VulkanProvider::CreatePresenter(
    Presenter::HostGpuLossCallback host_gpu_loss_callback) {
  return VulkanPresenter::Create(host_gpu_loss_callback, vulkan_device(),
                                 ui_samplers());
}

std::unique_ptr<ImmediateDrawer> VulkanProvider::CreateImmediateDrawer() {
  return VulkanImmediateDrawer::Create(vulkan_device(), ui_samplers());
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
