// The graphics pipeline: three generated modules checked before they reach the
// driver, then every pipeline the frame loop binds, built from them. It is the
// longest single step of device setup, and the only one that has to know what a
// batch looks like, so it reads on its own.

#include "vk_internal.h"

#include "shaders/spirv_shaders.h"

#include <cstdio>
#include <cstring>

namespace gtc::native {
namespace detail {
namespace {
// A generated module gets one last look before it reaches the driver: the magic
// number, the word count of every instruction, and the declared bound. If the
// generator ever regresses, this refuses the module at startup instead of
// handing a driver something it will reject with a validation error nobody in
// this process can see.
bool moduleSane(const std::uint32_t* words, std::size_t count) {
    if (count < 5 || words[0] != 0x07230203u || words[3] == 0) return false;
    std::size_t offset = 5;
    while (offset < count) {
        const std::uint32_t length = words[offset] >> 16;
        if (length == 0 || offset + length > count) return false;
        offset += length;
    }
    return offset == count;
}


} // namespace

bool Renderer::createPipeline() {
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule vertexNoUv = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (!moduleSane(vk::spirv::skeleton_vert,
                    sizeof(vk::spirv::skeleton_vert) / sizeof(vk::spirv::skeleton_vert[0])) ||
        !moduleSane(vk::spirv::skeleton_no_uv_vert,
                    sizeof(vk::spirv::skeleton_no_uv_vert) / sizeof(vk::spirv::skeleton_no_uv_vert[0])) ||
        !moduleSane(vk::spirv::skeleton_frag,
                    sizeof(vk::spirv::skeleton_frag) / sizeof(vk::spirv::skeleton_frag[0]))) {
        std::fprintf(stderr, "[vk] generated shader module is malformed\n");
        return false;
    }
    VkShaderModuleCreateInfo vertexInfo{};
    vertexInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertexInfo.codeSize = sizeof(vk::spirv::skeleton_vert);
    vertexInfo.pCode = vk::spirv::skeleton_vert;
    VkShaderModuleCreateInfo fragmentInfo{};
    fragmentInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragmentInfo.codeSize = sizeof(vk::spirv::skeleton_frag);
    fragmentInfo.pCode = vk::spirv::skeleton_frag;
    if (createShaderModule(device_, &vertexInfo, nullptr, &vertexModule) != VK_SUCCESS) return false;
    vertexInfo.codeSize = sizeof(vk::spirv::skeleton_no_uv_vert);
    vertexInfo.pCode = vk::spirv::skeleton_no_uv_vert;
    if (createShaderModule(device_, &vertexInfo, nullptr, &vertexNoUv) != VK_SUCCESS) {
        destroyShaderModule(device_, vertexModule, nullptr);
        return false;
    }
    if (createShaderModule(device_, &fragmentInfo, nullptr, &fragmentModule) != VK_SUCCESS) {
        destroyShaderModule(device_, vertexNoUv, nullptr);
        destroyShaderModule(device_, vertexModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";
    VkPipelineShaderStageCreateInfo stagesNoUv[2] = {stages[0], stages[1]};
    stagesNoUv[0].module = vertexNoUv;

    // One interleaved vertex: position, normal, texture coordinate -- the same
    // GpuVertex the OpenGL path uploads.
    // Two shapes of vertex, and two pipelines to match. Position and normal are
    // in one array for every vertex; the texture coordinate is in a second array
    // that only textured batches are bound to, because fetching eight bytes of
    // coordinate for a skeleton bone or a wall tile is eight bytes per vertex of
    // the frame spent to fill a lane the shader never reads.
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(PackedVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(UvVertex);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(PackedVertex, x);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(PackedVertex, nx);
    attributes[2].location = 2;
    attributes[2].binding = 1;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(UvVertex, u);

    VkPipelineVertexInputStateCreateInfo vertexInput[2]{};
    for (int variant = 0; variant < 2; ++variant) {
        vertexInput[variant].sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput[variant].vertexBindingDescriptionCount = variant == 0 ? 1 : 2;
        vertexInput[variant].pVertexBindingDescriptions = bindings;
        vertexInput[variant].vertexAttributeDescriptionCount = variant == 0 ? 2 : 3;
        vertexInput[variant].pVertexAttributeDescriptions = attributes;
    }

    VkPipelineInputAssemblyStateCreateInfo assembly{};

    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport{};

    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};

    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    // Neither side is culled: the CPU path draws whatever order the triangles
    // arrive in, and the shading is two-sided, so a back face is still a face.
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};

    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};

    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_FALSE;
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;

    // The interface is drawn with the same shaders and the same vertex layouts,
    // and differs in exactly two things: it blends, and it does not test depth.
    // Blending because a panel is a tint and a glyph is coverage, and no depth
    // because the interface is in front of the world by definition -- giving it
    // a depth of its own would be inventing a third dimension for a picture that
    // has none.
    VkPipelineColorBlendAttachmentState blended{};
    blended.blendEnable = VK_TRUE;
    blended.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blended.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blended.colorBlendOp = VK_BLEND_OP_ADD;
    blended.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blended.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blended.alphaBlendOp = VK_BLEND_OP_ADD;
    blended.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blendingUi{};
    blendingUi.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendingUi.attachmentCount = 1;
    blendingUi.pAttachments = &blended;
    // Translucent skin is depth-tested against the opaque scene, but does not
    // write depth; draw it after the bones. Cull its back side to avoid applying
    // the 50% opacity a second time through the back of the foot.
    VkPipelineDepthStencilStateCreateInfo depthSkin=depth;
    depthSkin.depthWriteEnable=VK_FALSE;
    VkPipelineRasterizationStateCreateInfo rasterSkin=raster;
    rasterSkin.cullMode=VK_CULL_MODE_BACK_BIT;
    VkPipelineDepthStencilStateCreateInfo depthUi{};
    depthUi.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthUi.depthTestEnable = VK_FALSE;
    depthUi.depthWriteEnable = VK_FALSE;
    depthUi.stencilTestEnable = VK_FALSE;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(PushConstants);

    const VkDescriptorSetLayout layouts[2] = {frameLayout_, textureLayout_};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = layouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (createPipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        destroyShaderModule(device_, vertexModule, nullptr);
        destroyShaderModule(device_, vertexNoUv, nullptr);
        destroyShaderModule(device_, fragmentModule, nullptr);
        return false;
    }

    // Eight world/UI variants plus translucent UV and non-UV geometry.
    VkGraphicsPipelineCreateInfo info[12]{};
    for (int index = 0; index < 12; ++index) {
        VkGraphicsPipelineCreateInfo& entry = info[index];
        entry.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        entry.stageCount = 2;
        const bool skinPass = index >= 8;
        const int uvVariant = skinPass ? (index < 10 ? 1 : 0) : ((index / 2) % 2);
        entry.pStages = uvVariant ? stages : stagesNoUv;
        entry.pVertexInputState = &vertexInput[uvVariant];
        entry.pInputAssemblyState = &assembly;
        entry.pViewportState = &viewport;
        entry.pRasterizationState = skinPass ? &rasterSkin : &raster;
        entry.pMultisampleState = &multisample;
        const bool interfacePass = index >= 4 && !skinPass;
        entry.pDepthStencilState = skinPass ? &depthSkin : interfacePass ? &depthUi : &depth;
        entry.pColorBlendState = (skinPass || interfacePass) ? &blendingUi : &blending;
        entry.pDynamicState = &dynamic;
        entry.layout = pipelineLayout_;
        entry.subpass = 0;
        entry.renderPass = index % 2 == 0 ? renderPass_ : renderPassCovered_;
    }
    VkPipeline built[12] = {};
    const VkResult result = createGraphicsPipelines(device_, VK_NULL_HANDLE, 12, info, nullptr, built);
    destroyShaderModule(device_, vertexModule, nullptr);
    destroyShaderModule(device_, vertexNoUv, nullptr);
    destroyShaderModule(device_, fragmentModule, nullptr);
    if (result != VK_SUCCESS) {
        for (VkPipeline pipeline : built) if (pipeline) destroyPipeline(device_, pipeline, nullptr);
        std::fprintf(stderr, "[vk] pipeline creation failed (%d)\n", static_cast<int>(result));
        return false;
    }
    pipeline_ = built[0];
    pipelineCovered_ = built[1];
    pipelineUv_ = built[2];
    pipelineUvCovered_ = built[3];
    pipelineInterface_ = built[4];
    pipelineInterfaceCovered_ = built[5];
    pipelineInterfaceUv_ = built[6];
    pipelineInterfaceUvCovered_ = built[7];
    pipelineSkin_ = built[8];
    pipelineSkinCovered_ = built[9];
    pipelineTranslucent_ = built[10];
    pipelineTranslucentCovered_ = built[11];
    return true;
}

} // namespace detail
} // namespace gtc::native
