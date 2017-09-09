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

#include "Defer.hpp"
#include "WindowsHelper.hpp"

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

        const vk::ApplicationInfo appInfo{ nullptr, 0, nullptr, 0,
            VK_API_VERSION_1_0 };

        return vk::createInstance({ {}, &appInfo,
            static_cast<std::uint32_t>(layers.size()), layers.data(),
            static_cast<std::uint32_t>(extensions.size()), extensions.data() });
    }();

    const auto destroyInstance = Defer([&] { instance.destroy(); });

    // Create a window
    const auto hWnd = WindowsHelper::createWindow(hInstance);
    ShowWindow(hWnd, SW_SHOWDEFAULT);

    // Create a surface
    const auto surface
        = instance.createWin32SurfaceKHR({ {}, hInstance, hWnd });

    const auto destroySurface
        = Defer([&] { instance.destroySurfaceKHR(surface); });

    // Pick a GPU
    const auto& gpu = [&instance] {
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
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos{ { {},
            graphicsQueueFamilyIndex, 1, &graphicsQueuePriority } };

        if (separatePresentQueue) {
            const float presentQueuePriority = 0.0f;
            queueCreateInfos.emplace_back(vk::DeviceQueueCreateFlags{},
                presentQueueFamilyIndex, 1, &presentQueuePriority);
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

        return gpu.createDevice({ {},
            static_cast<std::uint32_t>(queueCreateInfos.size()),
            queueCreateInfos.data(), static_cast<std::uint32_t>(layers.size()),
            layers.data(), static_cast<std::uint32_t>(extensions.size()),
            extensions.data(), &features });
    }();

    const auto destroyDevice = Defer([&] { device.destroy(); });

    const auto graphicsQueue = device.getQueue(graphicsQueueFamilyIndex, 0);
    const auto presentQueue = device.getQueue(presentQueueFamilyIndex, 0);

    // Pick a surface format
    const auto& surfaceFormat = [&] {
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

        const std::uint32_t minImageCount = std::max(
            static_cast<std::uint32_t>(3), surfaceCapabilities.maxImageCount);

        vk::SharingMode imageSharingMode;

        if (separatePresentQueue) {
            imageSharingMode = vk::SharingMode::eConcurrent;
        } else {
            imageSharingMode = vk::SharingMode::eExclusive;
        }

        vk::SurfaceTransformFlagBitsKHR preTransform;

        if (surfaceCapabilities.supportedTransforms
            & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
            preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        } else {
            preTransform = surfaceCapabilities.currentTransform;
        }

        return device.createSwapchainKHR({ {}, surface, minImageCount,
            surfaceFormat.format, surfaceFormat.colorSpace, swapchainExtent, 1,
            vk::ImageUsageFlagBits::eColorAttachment, imageSharingMode,
            static_cast<std::uint32_t>(queueFamilyIndices.size()),
            queueFamilyIndices.data(), preTransform,
            vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eFifo,
            true });
    }();

    const auto destroySwapchain
        = Defer([&] { device.destroySwapchainKHR(swapchain); });

    const auto swapchainImages = device.getSwapchainImagesKHR(swapchain);

    const auto swapchainImageViews = [&] {
        std::vector<vk::ImageView> views;

        for (const auto& image : swapchainImages) {
            views.push_back(device.createImageView(
                { {}, image, vk::ImageViewType::e2D, surfaceFormat.format, {},
                    { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } }));
        }

        return views;
    }();

    const auto destroySwapchainImageViews = Defer([&] {
        for (const auto& view : swapchainImageViews) {
            device.destroyImageView(view);
        }
    });

    // Setup Command buffers
    const auto commandPool
        = device.createCommandPool({ {}, graphicsQueueFamilyIndex });

    const auto destroyCommandPool
        = Defer([&] { device.destroyCommandPool(commandPool); });

    const auto commandBuffers = device.allocateCommandBuffers(
        { commandPool, vk::CommandBufferLevel::ePrimary,
            static_cast<std::uint32_t>(swapchainImages.size()) });

    const auto destroyCommandBuffers = Defer(
        [&] { device.freeCommandBuffers(commandPool, commandBuffers); });

    // Create depth image
    const auto depthFormat = vk::Format::eD32Sfloat;
    const auto depthImages = [&] {
        std::vector<vk::Image> v(swapchainImages.size());

        std::generate(v.begin(), v.end(), [&] {
            return device.createImage({ {}, vk::ImageType::e2D, depthFormat,
                { swapchainExtent.width, swapchainExtent.height, 1 }, 1, 1,
                vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment
                    | vk::ImageUsageFlagBits::eTransferDst,
                vk::SharingMode::eExclusive, 0, nullptr,
                vk::ImageLayout::eUndefined });
        });

        return v;
    }();

    const auto destroyDepthImages = Defer([&] {
        for (const auto& image : depthImages) {
            device.destroyImage(image);
        }
    });

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

        return device.allocateMemory({ requirements.size, memoryTypeIndex });
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
            v.push_back(device.createImageView(
                { {}, image, vk::ImageViewType::e2D, depthFormat, {},
                    { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 } }));
        }

        return v;
    }();

    const auto destroyDepthImageViews = Defer([&] {
        for (const auto& imageView : depthImageViews) {
            device.destroyImageView(imageView);
        }
    });

    UBO ubo{ {}, {}, {} };

    const auto uniformBuffer = device.createBuffer(
        { {}, sizeof(ubo), vk::BufferUsageFlagBits::eUniformBuffer,
            vk::SharingMode::eExclusive, 0, nullptr });

    const auto destroyUniformBuffer
        = Defer([&] { device.destroyBuffer(uniformBuffer); });

    const auto uniformMemory = [&] {
        const auto requirements
            = device.getBufferMemoryRequirements(uniformBuffer);
        auto memory = device.allocateMemory({ requirements.size,
            getMemoryTypeIndex(requirements,
                vk::MemoryPropertyFlagBits::eHostVisible
                    | vk::MemoryPropertyFlagBits::eHostCoherent) });

        auto data = device.mapMemory(memory, 0, requirements.size, {});

        std::memcpy(data, &ubo, sizeof(ubo));

        device.unmapMemory(memory);

        device.bindBufferMemory(uniformBuffer, memory, 0);

        return std::move(memory);
    }();

    const auto freeUniformMemory = Defer(std::bind(freeMemory, uniformMemory));

    const auto descriptorSetLayout = [&] {
        const vk::DescriptorSetLayoutBinding binding{ 0,
            vk::DescriptorType::eUniformBuffer, 1,
            vk::ShaderStageFlagBits::eVertex, nullptr };

        return device.createDescriptorSetLayout({ {}, 1, &binding });
    }();

    const auto destroyDescriptorSetLayout = Defer(
        [&] { device.destroyDescriptorSetLayout(descriptorSetLayout); });

    const auto pipelineLayout = device.createPipelineLayout(
        { {}, 1, &descriptorSetLayout, 0, nullptr });
    const auto destroyPipelineLayout
        = Defer([&] { device.destroyPipelineLayout(pipelineLayout); });

    const auto descriptorPool = [&] {
        const vk::DescriptorPoolSize poolSize{
            vk::DescriptorType::eUniformBuffer, 1
        };

        return device.createDescriptorPool(
            { vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, 1,
                &poolSize });
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
        = { { { 0.0, -0.5, 0.0, 1.0 }, { 1.0, 0.0, 0.0, 1.0 } },
            { { 0.5, 0.5, 0.0, 1.0 }, { 0.0, 1.0, 0.0, 1.0 } },
            { { -0.5, 0.5, 0.0, 1.0 }, { 0.0, 0.0, 1.0, 1.0 } } };

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
            vk::SampleCountFlagBits::e1, VK_FALSE, 0.0f, nullptr, VK_FALSE,
            VK_FALSE };

        const vk::PipelineDepthStencilStateCreateInfo depthStencilState{ {},
            VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE,
            {}, {}, 0.0f, 0.0f };

        const vk::PipelineColorBlendAttachmentState attachment{ VK_FALSE,
            vk::BlendFactor::eZero, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
            vk::BlendFactor::eZero, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                | vk::ColorComponentFlagBits::eB
                | vk::ColorComponentFlagBits::eA };
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

    const auto imageAcquiredSemaphore = device.createSemaphore({});

    const auto destroyImageAcquiredSemaphore
        = Defer([&] { device.destroySemaphore(imageAcquiredSemaphore); });

    std::uint32_t currentImageIndex;
    device.acquireNextImageKHR(
        swapchain, UINT64_MAX, imageAcquiredSemaphore, {}, &currentImageIndex);

    const auto& commandBuffer = commandBuffers.at(currentImageIndex);

    const std::array<vk::ClearValue, 2> clearValues
        = { vk::ClearColorValue{}, vk::ClearDepthStencilValue{ 1.0f, 0 } };

    vk::CommandBufferBeginInfo beginInfo{ {}, nullptr };
    commandBuffer.begin(beginInfo);

    commandBuffer.beginRenderPass(
        { renderPass, framebuffers.at(currentImageIndex),
            { { 0, 0 }, swapchainExtent },
            static_cast<std::uint32_t>(clearValues.size()),
            clearValues.data() },
        vk::SubpassContents::eInline);

    commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipelineLayout, 0, descriptorSets, nullptr);
    commandBuffer.bindVertexBuffers(0, { vertexBuffer }, { 0 });
    commandBuffer.draw(3, 1, 0, 0);

    commandBuffer.endRenderPass();

    commandBuffer.end();

    const auto drawFence = device.createFence({ vk::FenceCreateFlags{} });
    const auto destroyDrawFence
        = Defer([&] { device.destroyFence(drawFence); });

    const vk::PipelineStageFlags waitDstStageMask
        = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    graphicsQueue.submit({ { 1, &imageAcquiredSemaphore, &waitDstStageMask, 1,
                             &commandBuffer, 0, nullptr } },
        drawFence);

    device.waitForFences({ drawFence }, VK_FALSE, 1'000'000'000);

    presentQueue.presentKHR({ 0, nullptr, 1, &swapchain, &currentImageIndex });

    WindowsHelper::mainLoop();
}
