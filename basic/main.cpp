#include <windows.h>

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include "WindowsHelper.hpp"

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
    VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
    std::uint64_t obj, std::size_t location, std::int32_t code,
    const char* layerPrefix, const char* msg, void* userData)
{
    std::stringstream ss;
    ss << "Vulkan Validation Layer: " << msg;

    std::string s = ss.str();
    OutputDebugStringA(s.data());

    return VK_FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // Create an vulkan instance
    const auto instance = [] {
        const auto extensions = [] {
            const std::vector<const char*> wanted
                = { VK_KHR_SURFACE_EXTENSION_NAME,
                    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                    VK_EXT_DEBUG_REPORT_EXTENSION_NAME };

            const auto props = vk::enumerateInstanceExtensionProperties();

            return std::remove_if(
                wanted.cbegin(), wanted.cend(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [](const auto& p) {
                            return std::strcmp(w, p.extensionName) == 0;
                        });
                });
        }();

        const auto layers = [] {
            const std::vector<const char*> wanted
                = { "VK_LAYER_LUNARG_standard_validation",
                    "VK_LAYER_RENDERDOC_capture" };

            const auto props = vk::enumerateInstanceLayerProperties();

            return std::remove_if(
                wanted.cbegin(), wanted.cend(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [](const auto& p) {
                            return std::strcmp(w, p.layerName) == 0;
                        });
                });
        }();

        const auto appInfo
            = vk::ApplicationInfo().setApiVersion(VK_API_VERSION_1_0);

        const auto createInfo
            = vk::InstanceCreateInfo()
                  .setPApplicationInfo(&appInfo)
                  .setEnabledLayerCount(
                      static_cast<std::uint32_t>(layers.size()))
                  .setPpEnabledLayerNames(layers.data())
                  .setEnabledExtensionCount(
                      static_cast<std::uint32_t>(extensions.size()))
                  .setPpEnabledExtensionNames(extensions.data());

        return vk::createInstanceUnique(createInfo);
    }();

    // Setup debug callback
    const auto debugReportCallback = [] {
        return instance->createDebugReportCallbackEXTUnique(
            vk::DebugReportCallbackCreateInfoEXT()
                .setFlags(vk::DebugReportFlagBitsEXT::eInformation
                    | vk::DebugReportFlagBitsEXT::eWarning)
                .setPfnCallback(debugCallback));
    }();

    // Create a window
    const auto hWnd = WindowsHelper::createWindow(hInstance);

    // Create a surface
    const auto surface = instance->createWin32SurfaceKHRUnique(
        vk::Win32SurfaceCreateInfoKHR().setHinstance(hInstance).setHwnd(hWnd));

    // Select GPU and queue family index for graphics and presentation
    const auto gpu = [] {
        const auto gpus = instance->enumeratePhysicalDevices();

        if (gpus.size() == 0) {
            throw std::runtime_error("No physical device");
        }

        return gpus.at(0);
    }();

    ShowWindow(hWnd, SW_SHOWDEFAULT);

    WindowsHelper::mainLoop();
}

