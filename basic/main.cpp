#define NOMINMAX
#include <windows.h>

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"

#include "WindowsHelper.hpp"

class Defer {
public:
    explicit Defer(const std::function<void()>& defered) noexcept
        : m_defered(defered)
    {
    }

    explicit Defer(std::function<void()>&& defered) noexcept
        : m_defered(defered)
    {
    }

    ~Defer() { m_defered(); }

private:
    std::function<void()> m_defered;
};

struct UBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

struct Vertex {
    glm::vec4 position;
    glm::vec4 color;
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // Create an vulkan instance
    const auto instance = [] {
        const auto extensions = [] {
            std::vector<const char*> wanted = { VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

            const auto props = vk::enumerateInstanceExtensionProperties();

            const auto eraseBegin = std::remove_if(
                wanted.begin(), wanted.end(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.extensionName) == 0;
                        });
                });

            wanted.erase(eraseBegin, wanted.end());

            return wanted;
        }();

        const auto layers = [] {
            std::vector<const char*> wanted
                = { "VK_LAYER_LUNARG_standard_validation",
                    "VK_LAYER_RENDERDOC_capture" };

            const auto props = vk::enumerateInstanceLayerProperties();

            const auto eraseBegin = std::remove_if(
                wanted.begin(), wanted.end(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.layerName) == 0;
                        });
                });

            wanted.erase(eraseBegin, wanted.end());

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

        return vk::createInstance(createInfo);
    }();

    const auto destroyInstance = Defer([&] { instance.destroy(); });

    // Create a window
    const auto hWnd = WindowsHelper::createWindow(hInstance);

    // Create a surface
    const auto surface = instance.createWin32SurfaceKHR(
        vk::Win32SurfaceCreateInfoKHR().setHinstance(hInstance).setHwnd(hWnd));

    const auto destroySurface
        = Defer([&] { instance.destroySurfaceKHR(surface); });

    // Pick a GPU
    const auto gpu = [&instance] {
        const auto gpus = instance.enumeratePhysicalDevices();

        if (gpus.size() == 0) {
            throw std::runtime_error("No physical device");
        }

        return gpus.at(0);
    }();

    const auto queueFamilyProperties = gpu.getQueueFamilyProperties();

    // Find appropreate queue family indices
    const auto graphicsQueueFamilyIndex = [&] {

        const auto i = std::distance(queueFamilyProperties.cbegin(),
            std::find_if(queueFamilyProperties.cbegin(),
                queueFamilyProperties.cend(), [](const auto& prop) {
                    return prop.queueFlags & vk::QueueFlagBits::eGraphics;
                }));

        if (i == queueFamilyProperties.size()) {
            throw std::runtime_error("No graphics operation support");
        }

        return static_cast<std::uint32_t>(i);
    }();

    const auto presentQueueFamilyIndex = [&] {
        std::vector<vk::Bool32> supportPresent;

        for (std::uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
            supportPresent.push_back(gpu.getSurfaceSupportKHR(i, surface));
        }

        if (supportPresent.at(graphicsQueueFamilyIndex) == VK_TRUE) {
            return graphicsQueueFamilyIndex;
        }

        const auto i = std::distance(supportPresent.cbegin(),
            std::find(supportPresent.cbegin(), supportPresent.cend(), VK_TRUE));

        if (i == supportPresent.size()) {
            throw std::runtime_error("No presentation support");
        }

        return static_cast<std::uint32_t>(i);
    }();

    const bool separatePresentQueue
        = graphicsQueueFamilyIndex != presentQueueFamilyIndex;

    // Pick a logical device
    const auto device = [&] {
        const float graphicsQueuePriority = 0.0f;
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos{
            vk::DeviceQueueCreateInfo()
                .setPQueuePriorities(&graphicsQueuePriority)
                .setQueueFamilyIndex(graphicsQueueFamilyIndex)
                .setQueueCount(1)
        };

        if (separatePresentQueue) {
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

            const auto eraseBegin = std::remove_if(
                wanted.begin(), wanted.end(), [&props](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.extensionName) == 0;
                        });
                });

            wanted.erase(eraseBegin, wanted.end());

            return wanted;
        }();

        const auto layers = [&] {
            std::vector<const char*> wanted
                = { "VK_LAYER_LUNARG_standard_validation",
                    "VK_LAYER_RENDERDOC_capture" };

            const auto props = gpu.enumerateDeviceLayerProperties();

            const auto eraseBegin = std::remove_if(
                wanted.begin(), wanted.end(), [&](const char* w) {
                    return std::none_of(
                        props.cbegin(), props.cend(), [&w](const auto& p) {
                            return std::strcmp(w, p.layerName) == 0;
                        });
                });

            wanted.erase(eraseBegin, wanted.end());

            return wanted;
        }();

        const auto features = gpu.getFeatures();

        const auto deviceCreateInfo
            = vk::DeviceCreateInfo()
                  .setQueueCreateInfoCount(
                      static_cast<std::uint32_t>(queueCreateInfos.size()))
                  .setPQueueCreateInfos(queueCreateInfos.data())
                  .setEnabledLayerCount(
                      static_cast<std::uint32_t>(layers.size()))
                  .setPpEnabledLayerNames(layers.data())
                  .setEnabledExtensionCount(
                      static_cast<std::uint32_t>(extensions.size()))
                  .setPpEnabledExtensionNames(extensions.data())
                  .setPEnabledFeatures(&features);

        return gpu.createDevice(deviceCreateInfo);
    }();

    const auto destroyDevice = Defer([&] { device.destroy(); });

    // Pick a surface format
    const auto surfaceFormat = [&] {
        const auto formats = gpu.getSurfaceFormatsKHR(surface);

        const auto format = std::find_if(
            formats.cbegin(), formats.cend(), [](const auto format) {
                return format.format == vk::Format::eB8G8R8A8Unorm;
            });

        if (format == formats.cend()) {
            throw std::runtime_error("No appropreate surface format");
        }

        return *format;
    }();

    const auto surfaceCapabilities = gpu.getSurfaceCapabilitiesKHR(surface);

    const auto swapchainExtent = [&] {
        if (surfaceCapabilities.currentExtent.width == -1) {
            const auto windowSize = WindowsHelper::getWindowSize(hWnd);

            return vk::Extent2D{ static_cast<std::uint32_t>(windowSize.cx),
                static_cast<std::uint32_t>(windowSize.cy) };
        }

        return surfaceCapabilities.currentExtent;
    }();

    // Create a swapchain
    const auto swapchain = [&] {
        std::vector<std::uint32_t> queueFamilyIndices
            = { graphicsQueueFamilyIndex };
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

        return device.createSwapchainKHR(
            vk::SwapchainCreateInfoKHR()
                .setSurface(surface)
                .setMinImageCount(imageCount)
                .setImageColorSpace(surfaceFormat.colorSpace)
                .setImageFormat(surfaceFormat.format)
                .setImageExtent(swapchainExtent)
                .setImageArrayLayers(1)
                .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                .setImageSharingMode(sharingMode)
                .setQueueFamilyIndexCount(
                    static_cast<std::uint32_t>(queueFamilyIndices.size()))
                .setPQueueFamilyIndices(queueFamilyIndices.data())
                .setPreTransform(preTransform)
                .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                .setPresentMode(vk::PresentModeKHR::eFifo)
                .setClipped(true));
    }();

    const auto destroySwapchain
        = Defer([&] { device.destroySwapchainKHR(swapchain); });

    const auto swapchainImages = device.getSwapchainImagesKHR(swapchain);

    const auto swapchainImageViews = [&] {
        std::vector<vk::ImageView> views;

        for (const auto& image : swapchainImages) {
            const auto subresourceRange
                = vk::ImageSubresourceRange()
                      .setAspectMask(vk::ImageAspectFlagBits::eColor)
                      .setBaseMipLevel(0)
                      .setLevelCount(1)
                      .setBaseArrayLayer(0)
                      .setLayerCount(1);
            views.emplace_back(device.createImageView(
                vk::ImageViewCreateInfo()
                    .setImage(image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(surfaceFormat.format)
                    .setSubresourceRange(subresourceRange)));
        }

        return views;
    }();

    const auto destroySwapchainImageViews = Defer([&] {
        for (const auto& view : swapchainImageViews) {
            device.destroyImageView(view);
        }
    });

    // Setup Command buffers
    const auto commandPool = device.createCommandPool(
        vk::CommandPoolCreateInfo().setQueueFamilyIndex(
            graphicsQueueFamilyIndex));

    const auto destroyCommandPool
        = Defer([&] { device.destroyCommandPool(commandPool); });

    const auto commandBuffers = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(commandPool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(
                static_cast<std::uint32_t>(swapchainImages.size())));

    const auto destroyCommandBuffers = Defer(
        [&] { device.freeCommandBuffers(commandPool, commandBuffers); });

    // Create depth image
    const auto depthFormat = vk::Format::eD32Sfloat;
    const auto depthImages = [&] {
        std::vector<vk::Image> v(swapchainImages.size());

        std::generate(v.begin(), v.end(), [&] {
            return device.createImage(
                vk::ImageCreateInfo()
                    .setImageType(vk::ImageType::e2D)
                    .setFormat(depthFormat)
                    .setExtent(
                        { swapchainExtent.width, swapchainExtent.height, 1 })
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

    const auto destroyDepthImages = Defer([&] {
        for (const auto& image : depthImages) {
            device.destroyImage(image);
        }
    });

    // TODO: shadow map?

    const auto memoryProps = gpu.getMemoryProperties();

    const auto getMemoryTypeIndex
        = [&](const vk::MemoryRequirements& requirements,
            const vk::MemoryPropertyFlags& propertyFlags) {

              const auto i = std::distance(memoryProps.memoryTypes,
                  std::find_if(memoryProps.memoryTypes,
                      memoryProps.memoryTypes + VK_MAX_MEMORY_TYPES,
                      [&](const auto& memoryType) {
                          return (memoryType.propertyFlags & propertyFlags)
                              == propertyFlags;
                      }));

              if (i == VK_MAX_MEMORY_TYPES) {
                  throw new std::runtime_error("No appropreate memory type");
              }

              return static_cast<std::uint32_t>(i);
          };

    const auto allocateImageMemory = [&](
        const vk::Image& image, const vk::MemoryPropertyFlagBits& flagBit) {
        const auto requirements = device.getImageMemoryRequirements(image);

        const auto memoryTypeIndex = getMemoryTypeIndex(
            requirements, vk::MemoryPropertyFlagBits::eDeviceLocal);

        return device.allocateMemory(vk::MemoryAllocateInfo()
                                         .setAllocationSize(requirements.size)
                                         .setMemoryTypeIndex(memoryTypeIndex));
    };

    const auto freeMemory
        = [&](const vk::DeviceMemory& memory) { device.freeMemory(memory); };

    const auto depthMemories = [&] {
        std::vector<vk::DeviceMemory> v;

        for (const auto& image : depthImages) {
            auto memory = allocateImageMemory(
                image, vk::MemoryPropertyFlagBits::eDeviceLocal);
            device.bindImageMemory(image, memory, 0);
            v.emplace_back(std::move(memory));
        }

        return v;
    }();

    const auto freeDepthMemories = Defer([&] {
        for (const auto& memory : depthMemories) {
            freeMemory(memory);
        }
    });

    const auto depthImageViews = [&] {
        std::vector<vk::ImageView> v;

        for (const auto& image : depthImages) {
            const auto subresourceRange
                = vk::ImageSubresourceRange()
                      .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                      .setBaseMipLevel(0)
                      .setLevelCount(1)
                      .setBaseArrayLayer(0)
                      .setLayerCount(1);
            v.emplace_back(device.createImageView(
                vk::ImageViewCreateInfo()
                    .setImage(image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(depthFormat)
                    .setSubresourceRange(subresourceRange)));
        }

        return v;
    }();

    const auto destroyDepthImageViews = Defer([&] {
        for (const auto& imageView : depthImageViews) {
            device.destroyImageView(imageView);
        }
    });

    UBO ubo{ glm::mat4{ 0.0 }, glm::mat4{ 0.0 }, glm::mat4{ 0.0 } };

    const auto uniformBuffer = device.createBuffer(
        vk::BufferCreateInfo()
            .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
            .setSize(sizeof(ubo))
            .setSharingMode(vk::SharingMode::eExclusive));

    const auto destroyUniformBuffer
        = Defer([&] { device.destroyBuffer(uniformBuffer); });

    const auto uniformMemory = [&] {
        const auto requirements
            = device.getBufferMemoryRequirements(uniformBuffer);
        auto memory = device.allocateMemory(
            vk::MemoryAllocateInfo()
                .setMemoryTypeIndex(getMemoryTypeIndex(requirements,
                    vk::MemoryPropertyFlagBits::eHostVisible
                        | vk::MemoryPropertyFlagBits::eHostCoherent))
                .setAllocationSize(requirements.size));

        auto data = device.mapMemory(memory, 0, requirements.size, {});

        std::memcpy(data, &ubo, sizeof(ubo));

        device.unmapMemory(memory);

        device.bindBufferMemory(uniformBuffer, memory, 0);

        return std::move(memory);
    }();

    const auto freeUniformMemory = Defer(std::bind(freeMemory, uniformMemory));

    const auto descriptorSetLayout = [&] {
        const auto binding
            = vk::DescriptorSetLayoutBinding()
                  .setBinding(0)
                  .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                  .setDescriptorCount(1)
                  .setStageFlags(vk::ShaderStageFlagBits::eVertex);

        return device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo().setBindingCount(1).setPBindings(
                &binding));
    }();

    const auto destroyDescriptorSetLayout = Defer(
        [&] { device.destroyDescriptorSetLayout(descriptorSetLayout); });

    const auto pipelineLayout = device.createPipelineLayout(
        vk::PipelineLayoutCreateInfo()
            .setPushConstantRangeCount(0)
            .setPPushConstantRanges(nullptr)
            .setSetLayoutCount(1)
            .setPSetLayouts(&descriptorSetLayout));
    const auto destroyPipelineLayout
        = Defer([&] { device.destroyPipelineLayout(pipelineLayout); });

    const auto descriptorPool = [&] {
        std::vector<vk::DescriptorPoolSize> size{
            vk::DescriptorPoolSize()
                .setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(1)
        };

        return device.createDescriptorPool(
            { vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1,
                static_cast<std::uint32_t>(size.size()), size.data() });
    }();
    const auto destroyDescriptorPool
        = Defer([&] { device.destroyDescriptorPool(descriptorPool); });

    const auto descriptorSets = device.allocateDescriptorSets(
        { descriptorPool, 1, &descriptorSetLayout });
    const auto destroyDescriptorSets = Defer(
        [&] { device.freeDescriptorSets(descriptorPool, descriptorSets); });

    const vk::DescriptorBufferInfo uniformBufferInfo{ uniformBuffer, 0,
        sizeof(ubo) };

    device.updateDescriptorSets(
        { { descriptorSets.at(0), 0, 0, 1, vk::DescriptorType::eUniformBuffer,
            nullptr, &uniformBufferInfo, nullptr } },
        nullptr);

    const std::array<vk::AttachmentDescription, 2> attachments{
        { { {}, surfaceFormat.format, vk::SampleCountFlagBits::e1,
              vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
              vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
              vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR },
            { {}, depthFormat, vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eDepthStencilAttachmentOptimal } }
    };

    const vk::AttachmentReference colorReference{ 0,
        vk::ImageLayout::eColorAttachmentOptimal };

    const vk::AttachmentReference depthReference{ 1,
        vk::ImageLayout::eDepthStencilAttachmentOptimal };

    const vk::SubpassDescription subpass{ {}, vk::PipelineBindPoint::eGraphics,
        0, nullptr, 1, &colorReference, nullptr, &depthReference, 0, nullptr };

    const auto renderPass = device.createRenderPass(
        { {}, static_cast<std::uint32_t>(attachments.size()),
            attachments.data(), 1, &subpass, 0, nullptr });

    const auto destroyRenderPass
        = Defer([&] { device.destroyRenderPass(renderPass); });

    const auto createShaderModule = [&device](const std::string& fileName) {
        std::ifstream file(fileName, std::ios_base::binary);

        if (file.fail()) {
            throw std::runtime_error("Can't open file");
        }

        const std::vector<char> binary{ std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>() };

        return device.createShaderModule(
            { {}, static_cast<std::size_t>(binary.size() * sizeof(char)),
                reinterpret_cast<const std::uint32_t*>(binary.data()) });
    };

    const auto fragmentShaderModule = createShaderModule("frag.spv");
    const auto vertexShaderModule = createShaderModule("vert.spv");

    const auto destroyShaderModules = Defer([&] {
        device.destroyShaderModule(fragmentShaderModule);
        device.destroyShaderModule(vertexShaderModule);
    });

    const auto framebuffers = [&] {
        std::vector<vk::Framebuffer> framebuffers;

        for (int i = 0; i < swapchainImages.size(); i++) {
            const vk::ImageView attachments[]
                = { swapchainImageViews.at(i), depthImageViews.at(i) };
            framebuffers.emplace_back(
                device.createFramebuffer({ {}, renderPass, 2, attachments,
                    swapchainExtent.width, swapchainExtent.height, 1 }));
        }

        return framebuffers;
    }();

    const auto destroyFrameBuffers = Defer([&] {
        for (const auto& framebuffer : framebuffers) {
            device.destroyFramebuffer(framebuffer);
        }
    });

    const Vertex vertexBufferData[]
        = { { { 1.0, 1.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0, 1.0 } },
            { { 1.0, 1.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0, 1.0 } },
            { { 0.0, -1.0, 0.0, 1.0 }, { 0.0, 0.0, 1.0, 1.0 } } };

    const auto vertexBuffer = device.createBuffer(
        { {}, sizeof(vertexBufferData), vk::BufferUsageFlagBits::eVertexBuffer,
            vk::SharingMode::eExclusive, 0, nullptr });

    const auto destroyVertexBuffer
        = Defer([&] { device.destroyBuffer(vertexBuffer); });

    const auto vertexMemory = [&] {
        const auto requirements
            = device.getBufferMemoryRequirements(vertexBuffer);
        auto memory = device.allocateMemory(
            vk::MemoryAllocateInfo()
                .setMemoryTypeIndex(getMemoryTypeIndex(requirements,
                    vk::MemoryPropertyFlagBits::eHostVisible
                        | vk::MemoryPropertyFlagBits::eHostCoherent))
                .setAllocationSize(requirements.size));

        auto data = device.mapMemory(memory, 0, requirements.size, {});

        std::memcpy(data, &vertexBufferData, sizeof(vertexBufferData));

        device.unmapMemory(memory);

        device.bindBufferMemory(vertexBuffer, memory, 0);

        return std::move(memory);
    }();

    const auto freeVertexMemory = Defer(std::bind(freeMemory, vertexMemory));

    const auto graphicsPipeline = [&] {
        const std::array<vk::PipelineShaderStageCreateInfo, 2> stages
            = { { { {}, vk::ShaderStageFlagBits::eVertex, vertexShaderModule,
                      "main", nullptr },
                { {}, vk::ShaderStageFlagBits::eFragment, fragmentShaderModule,
                    "main", nullptr } } };

        const vk::VertexInputBindingDescription vertexBindingDescription{ 0,
            sizeof(Vertex), vk::VertexInputRate::eVertex };
        const std::array<vk::VertexInputAttributeDescription, 2>
            vertexAttributeDescriptions{
                { { 0, 0, vk::Format::eR32G32B32A32Sfloat, 0 },
                    { 1, 0, vk::Format::eR32G32B32A32Sfloat, 16 } }
            };
        const vk::PipelineVertexInputStateCreateInfo vertexInputState{ {}, 1,
            &vertexBindingDescription,
            static_cast<uint32_t>(vertexAttributeDescriptions.size()),
            vertexAttributeDescriptions.data() };

        const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{ {},
            vk::PrimitiveTopology::eTriangleList, VK_FALSE };

        // TODO: Use dynamic state instead of setting the viewport and the
        // scissor?
        const vk::Viewport viewport{ 0.0f, 0.0f,
            static_cast<float>(swapchainExtent.width),
            static_cast<float>(swapchainExtent.height), 0.0f, 1.0f };
        const vk::Rect2D scissor{ { 0, 0 }, swapchainExtent };
        const vk::PipelineViewportStateCreateInfo viewportState{ {}, 1,
            &viewport, 1, &scissor };

        const vk::PipelineRasterizationStateCreateInfo rasterizationState{ {},
            VK_TRUE, VK_FALSE, vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise, VK_FALSE,
            0.0f, 0.0f, 0.0f, 1.0f };

        const vk::PipelineMultisampleStateCreateInfo multisampleState{ {},
            vk::SampleCountFlagBits::e4, VK_FALSE, 0.0f, nullptr, VK_FALSE,
            VK_FALSE };

        const vk::PipelineDepthStencilStateCreateInfo depthStencilState{ {},
            VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE,
            {}, {}, 0.0f, 0.0f };

        const vk::PipelineColorBlendAttachmentState attachment;
        const vk::PipelineColorBlendStateCreateInfo colorBlendState{ {},
            VK_FALSE, vk::LogicOp::eNoOp, 1, &attachment, { 1.0f } };

        return device.createGraphicsPipeline(nullptr,
            { {}, static_cast<uint32_t>(stages.size()), stages.data(),
                &vertexInputState, &inputAssemblyState, nullptr, &viewportState,
                &rasterizationState, &multisampleState, &depthStencilState,
                &colorBlendState, nullptr, pipelineLayout, renderPass, 0,
                nullptr, 0 });
    }();

    const auto destroyPipeline
        = Defer([&] { device.destroyPipeline(graphicsPipeline); });

    const auto semaphore = device.createSemaphore({});

    const auto destroySemaphore
        = Defer([&] { device.destroySemaphore(semaphore); });

    std::uint32_t currentImageIndex;
    device.acquireNextImageKHR(
        swapchain, UINT64_MAX, semaphore, {}, &currentImageIndex);

    const auto& commandBuffer = commandBuffers.at(currentImageIndex);

    const std::array<vk::ClearValue, 2> clearValues = {
        vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({ 0.0f })),
        vk::ClearValue().setDepthStencil({ 1.0f, 0 })
    };

    vk::CommandBufferBeginInfo beginInfo { {}, nullptr };
    commandBuffer.begin(beginInfo);

    commandBuffer.beginRenderPass(
        { renderPass, framebuffers.at(currentImageIndex),
            { { 0, 0 }, swapchainExtent },
            static_cast<std::uint32_t>(clearValues.size()),
            clearValues.data() },
        vk::SubpassContents::eInline);

    commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, graphicsPipeline);

    commandBuffer.endRenderPass();

    ShowWindow(hWnd, SW_SHOWDEFAULT);

    WindowsHelper::mainLoop();
}
