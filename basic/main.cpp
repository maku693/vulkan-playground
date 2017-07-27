#define NOMINMAX
#include <windows.h>

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include "WindowsHelper.hpp"

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
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
            std::vector<const char*> wanted
                = { VK_KHR_SURFACE_EXTENSION_NAME,
                    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                    VK_EXT_DEBUG_REPORT_EXTENSION_NAME };

            const auto props = vk::enumerateInstanceExtensionProperties();

            const auto eraseFrom = std::remove_if(
                wanted.begin(), wanted.end(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.extensionName) == 0;
                        });
                });

            wanted.erase(eraseFrom, wanted.end());

            return wanted;
        }();

        const auto layers = [] {
            std::vector<const char*> wanted
                = { "VK_LAYER_LUNARG_standard_validation",
                    "VK_LAYER_RENDERDOC_capture" };

            const auto props = vk::enumerateInstanceLayerProperties();

            const auto eraseFrom = std::remove_if(
                wanted.begin(), wanted.end(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.layerName) == 0;
                        });
                });

            wanted.erase(eraseFrom, wanted.end());

            return wanted;
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
    const auto debugReportCallback = [&instance] {
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
    const auto gpu = [&instance] {
        const auto gpus = instance->enumeratePhysicalDevices();

        if (gpus.size() == 0) {
            throw std::runtime_error("No physical device");
        }

        return gpus.at(0);
    }();

    // Find appropreate queue family indices
    const auto graphicsQueueFamilyIndex = [&gpu] {
        const auto props = gpu.getQueueFamilyProperties();

        const std::uint32_t i = std::distance(props.cbegin(),
            std::find_if(props.cbegin(), props.cend(), [](const auto& prop) {
                return prop.queueFlags & vk::QueueFlagBits::eGraphics;
            }));

        if (i == props.size()) {
            throw std::runtime_error("No graphics operation support");
        }

        return i;
    }();

    const auto presentQueueFamilyIndex = [&gpu, &surface, &graphicsQueueFamilyIndex] {
        std::vector<vk::Bool32> supportPresent;

        for (std::uint32_t i = 0; i < supportPresent.size(); i++) {
            supportPresent.push_back(gpu.getSurfaceSupportKHR(i, *surface));
        }

        if (supportPresent.at(graphicsQueueFamilyIndex) == VK_TRUE) {
            return graphicsQueueFamilyIndex;
        }

        const std::uint32_t i = std::distance(supportPresent.cbegin(),
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
            std::vector<const char*> wanted
                = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

            const auto props = gpu.enumerateDeviceExtensionProperties();

            const auto eraseFrom = std::remove_if(
                wanted.begin(), wanted.end(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.extensionName) == 0;
                        });
                });

            wanted.erase(eraseFrom, wanted.end());

            return wanted;
        }();

        const auto layers = [&] {
            std::vector<const char*> wanted
                = { "VK_LAYER_LUNARG_standard_validation",
                    "VK_LAYER_RENDERDOC_capture" };

            const auto props = gpu.enumerateDeviceLayerProperties();

            const auto eraseFrom = std::remove_if(
                wanted.begin(), wanted.end(), [&](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.layerName) == 0;
                        });
                });

            wanted.erase(eraseFrom, wanted.end());

            return wanted;
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

        const auto format = std::find_if(formats.cbegin(), formats.cend(),
            [](const auto format) {
            return format.format == vk::Format::eB8G8R8A8Unorm;
            });

        if (format == formats.cend()) {
            throw std::runtime_error("No appropreate surface format");
        }

        return format;
    }();

    const auto surfaceCapabilities = gpu.getSurfaceCapabilitiesKHR(*surface);

    const auto swapchainExtent = [&] {
        if (surfaceCapabilities.currentExtent.width == -1) {
            const auto windowSize = WindowsHelper::getWindowSize(hWnd);

            return vk::Extent2D {
                static_cast<std::uint32_t>(windowSize.cx),
                    static_cast<std::uint32_t>(windowSize.cy)
            };
        }

        return surfaceCapabilities.currentExtent;
    }();

    // Create a swapchain
    const auto swapchain = [&] {
        std::vector<std::uint32_t> queueFamilyIndices = { graphicsQueueFamilyIndex };
        if (separatePresentQueue) {
            queueFamilyIndices.emplace_back(presentQueueFamilyIndex);
        }

        const std::uint32_t imageCount = std::max(
            static_cast<std::uint32_t>(3), surfaceCapabilities.maxImageCount);

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
            preTransform = surfaceCapabilities.currentTransform;
        }

        return device->createSwapchainKHRUnique(
            vk::SwapchainCreateInfoKHR()
                  .setSurface(*surface)
                  .setMinImageCount(imageCount)
                  .setImageColorSpace(surfaceFormat->colorSpace)
                  .setImageFormat(surfaceFormat->format)
                  .setImageExtent(swapchainExtent)
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
    const auto depthImages = [&] {
        std::vector<vk::UniqueImage> v(swapchainImages.size());

        std::generate(
            v.begin(), v.end(),
            [&] {
                return device->createImageUnique(vk::ImageCreateInfo()
                    .setImageType(vk::ImageType::e2D)
                    .setFormat(vk::Format::eD16Unorm)
                    .setExtent({ swapchainExtent.width, swapchainExtent.height, 1 })
                    .setMipLevels(1)
                    .setArrayLayers(1)
                    .setSamples(vk::SampleCountFlagBits::e1)
                    .setTiling(vk::ImageTiling::eOptimal)
                    .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment
                        | vk::ImageUsageFlagBits::eTransferDst)
                    .setSharingMode(vk::SharingMode::eExclusive)
                    .setQueueFamilyIndexCount(0)
                    .setPQueueFamilyIndices(nullptr)
                    .setInitialLayout(vk::ImageLayout::eUndefined));
            });

        return v;
    }();

    // TODO: shadow map?

    const auto memoryProps = gpu.getMemoryProperties();

    const auto getMemoryTypeIndex = [&](
        const vk::MemoryRequirements& requirements,
        const vk::MemoryPropertyFlags& propertyFlags) {

        const std::uint32_t typeIndex =
            std::distance(memoryProps.memoryTypes, std::find_if(
                    memoryProps.memoryTypes,
                    memoryProps.memoryTypes + VK_MAX_MEMORY_TYPES,
                    [&](const auto& memoryType) {
                        return (memoryType.propertyFlags & propertyFlags)
                            == propertyFlags;
                    }));

        if (typeIndex == VK_MAX_MEMORY_TYPES) {
            throw new std::runtime_error("No appropreate memory type");
        }

        return typeIndex;
    };

    const auto allocateMemoryForImageUnique = [&](const vk::UniqueImage& image,
        const vk::MemoryPropertyFlagBits& flagBit) {
        const auto requirements = device->getImageMemoryRequirements(*image);

        const auto memoryTypeIndex = getMemoryTypeIndex(requirements,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        return device->allocateMemoryUnique(vk::MemoryAllocateInfo()
            .setAllocationSize(requirements.size)
            .setMemoryTypeIndex(memoryTypeIndex)
        );
    };

    const auto depthMemories = [&] {
        std::vector<vk::UniqueDeviceMemory> v;

        for (const auto& image : depthImages) {
            auto memory = allocateMemoryForImageUnique(image,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
            device->bindImageMemory(*image, *memory, 0);
            v.emplace_back(std::move(memory));
        }

        return v;
    }();

    const auto depthImageViews = [&] {
        std::vector<vk::UniqueImageView> v;

        for (const auto& image : depthImages) {
            const auto subresourceRange
                = vk::ImageSubresourceRange()
                .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1);
            v.emplace_back(
                device->createImageViewUnique(vk::ImageViewCreateInfo()
                    .setImage(*image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(vk::Format::eD16Unorm)
                    .setSubresourceRange(subresourceRange)));
        }

        return v;
    }();

    ShowWindow(hWnd, SW_SHOWDEFAULT);

    WindowsHelper::mainLoop();
}
