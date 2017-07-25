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

    // Pick a GPU
    const auto gpu = [] {
        const auto gpus = instance->enumeratePhysicalDevices();

        if (gpus.size() == 0) {
            throw std::runtime_error("No physical device");
        }

        return gpus.at(0);
    }();

    // Find appropreate queue family indices
    const std::uint32_t graphicsQueueFamilyIndex = [&] {
        const auto props = gpu.getQueueFamilyProperties();
        const auto i = std::distance(props.cbegin(),
            std::find_if(props.cbegin(), props.cend(), [](const auto& prop) {
                return prop.queueFlags & vk::QueueFlagBits::eGraphics;
            }));

        if (i == props.size()) {
            throw std::runtime_error("No graphics operation support");
        }

        return i;
    }();

    const std::uint32_t presentQueueFamilyIndex = [&] {
        std::vector<vk::Bool32> supportPresent;
        for (std::uint32_t i; i < supportPresent.size(); i++) {
            supportPresent.push_back(gpu.getSurfaceSupportKHR(i, *surface));
        }

        if (supportPresent.at(graphicsQueueFamilyIndex) == VK_TRUE) {
            return graphicsQueueFamilyIndex;
        }

        const auto i = std::distance(supportPresent.cbegin(),
            std::find(supportPresent.cbegin(), supportPresent.cend(), VK_TRUE));

        if (i == supportPresent.size()) {
            throw std::runtime_error("No presentation support");
        }

        return i;
    }();

    const bool separatePresentQueue =
        graphicsQueueFamilyIndex != presentQueueFamilyIndex;

    // Pick a logical device
    const auto device = [&] {
        const float graphicsQueuePriority = 0.0f;
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos{
            vk::DeviceQueueCreateInfo()
                .setPQueuePriorities(&graphicsQueuePriority)
                .setQueueFamilyIndex(graphicsQueueFamilyIndex)
                .setQueueCount(1)
        };

        if (graphicsQueueFamilyIndex != presentQueueFamilyIndex) {
            const float presentQueuePriority = 0.0f;
            queueCreateInfos.emplace_back(
                vk::DeviceQueueCreateInfo()
                    .setPQueuePriorities(&presentQueuePriority)
                    .setQueueFamilyIndex(presentQueueFamilyIndex)
                    .setQueueCount(1));
        }

        const auto extensions = [&] {
            const std::vector<const char*> wanted
                = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

            const auto props = gpu.enumerateDeviceExtensionProperties();

            return std::remove_if(
                wanted.cbegin(), wanted.cend(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [](const auto& p) {
                            return std::strcmp(w, p.extensionName) == 0;
                        });
                });
        }();

        const auto layers = [&] {
            const std::vector<const char*> wanted
                = { "VK_LAYER_LUNARG_standard_validation",
                    "VK_LAYER_RENDERDOC_capture" };

            const auto props = gpu.enumerateDeviceLayerProperties();

            return std::remove_if(
                wanted.cbegin(), wanted.cend(), [&](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [](const auto& p) {
                            return std::strcmp(w, p.layerName) == 0;
                        });
                });
        }();

        const auto features = gpu.getFeatures();

        const auto deviceCreateInfo
            = vk::DeviceCreateInfo()
                  .setQueueCreateInfoCount(queueCreateInfos.size())
                  .setPQueueCreateInfos(queueCreateInfos.data())
                  .setEnabledLayerCount(layers.size())
                  .setPpEnabledLayerNames(layers.data())
                  .setEnabledExtensionCount(extensions.size())
                  .setPpEnabledExtensionNames(extensions.data())
                  .setPEnabledFeatures(&features);

        return gpu.createDeviceUnique(deviceCreateInfo);
    }();

    // Pick a surface format
    const auto surfaceFormat = [&] {
        const auto formats = gpu.getSurfaceFormatsKHR(*surface);

        const auto format = std::find(formats.cbegin(), formats.cend(),
            vk::surfaceFormat::eB8G8R8A8Unorm);

        if (format == formats.cend()) {
            throw std::runtime_error("No appropreate surface format");
        }

        return format;
    }();

    const auto surfaceCapabilities = gpu.getSurfaceCapabilitiesKHR(*surface);

    const auto swapchainExtent = [&] {
        if (surfaceCapabilities.currentExtent.width == -1) {
            return vk::Extent2D {
                static_cast<std::uint32_t>(windowSize.cx),
                    static_cast<std::uint32_t>(windowSize.cy)
            };
        }

        return capabilities.currentExtent;
    }();
    // Create a swapchain
    const auto swapchain = [&] {
        std::vector<std::uint32_t> queueFamilyIndices = { graphicsQueueFamilyIndex };
        if (separatePresentQueue) {
            queueFamilyIndices.emplace_back(presentQueueFamilyIndex);
        }

        const std::uint32_t imageCount =
            std::max(3u, surfaceCapabilities.maxImageCount);

        const auto windowSize = WindowsHelper::getWindowSize(hWnd);

        vk::SharingMode sharingMode;

        if (separatePresentQueue) {
            sharingMode = vk::SharingMode::eConcurrent;
        } else {
            sharingMode = vk::SharingMode::eExclusive;
        }

        vk::SurfaceTransformFlagBitsKHR preTransform;

        if (surfaceCapabilities.supportedTransforms
            & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
            preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        } else {
            preTransform = capabilities.currentTransform;
        }

        return device->createSwapchainKHRUnique(
            vk::SwapchainCreateInfoKHR()
                  .setSurface(*surface)
                  .setMinImageCount(imageCount)
                  .setImageColorSpace(surfaceFormat->colorSpace)
                  .setImageFormat(surfaceFormat->format)
                  .setImageExtent(extent)
                  .setImageArrayLayers(1)
                  .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                  .setImageSharingMode(sharingMode)
                  .setQueueFamilyIndexCount(queueFamilyIndices.size())
                  .setPQueueFamilyIndices(queueFamilyIndices.data())
                  .setPreTransform(preTransform)
                  .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                  .setPresentMode(vk::PresentModeKHR::eFifo)
                  .setClipped(true)
        );
    }();

    const auto swapchainImages = device->getSwapchainImagesKHR(*swapchain);

    const auto swapchainViews = [&] {
        std::vector<vk::UniqueImageView> views;

        views.reserve(swapchainImages.size());

        for (const auto& image : swapchainImages) {
            const auto subresourceRange
                = vk::ImageSubresourceRange()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1);
            views.emplace_back(
                device->createImageViewUnique(vk::ImageViewCreateInfo()
                .setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(surfaceFormat->format)
                .setSubresourceRange(subresourceRange)));
        }

        return views;
    }();

    // Create depth image
    const auto depthImage = device->createImageUnique(vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eD16Unorm)
            .setExtent({ swapchainExtent.width, swapchainExtent.height, 1 })
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setQueueFamilyIndexCount(0)
            .setPQueueFamilyIndices(nullptr)
            .setInitialLayout(vk::ImageLayout::eUndefined));

    const auto memoryProps = gpu.getMemoryProperties();

    const auto allocateMemoryForImageUnique = [&](const vk::ImageUnique& image,
        const vk::MemoryPropertyFlagsBits& flagBit) {
        const auto requirements = device->getImageMemoryRequirements(*image);

        const std::uint32_t typeIndex =
            std::distance(requirements.memoryTypes, std::find_if(
                    requirements.memoryTypes,
                    requirements.memoryTypes + VK_MAX_MEMORY_TYPES,
                    [](const auto& memoryType) {
                        return memoryType.propertyFlags & flagBit == flagBit;
                    }));

        device.allocateMemory(vk::MemoryAllocateInfo()
            .setAllocationSize(requirements.size)
            .setMemoryTypeIndex(typeIndex)
        );
    };

    allocateMemoryForImageUnique(depthImage);

    ShowWindow(hWnd, SW_SHOWDEFAULT);

    WindowsHelper::mainLoop();
}
