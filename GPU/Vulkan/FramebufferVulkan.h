// Copyright (c) 2015- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "Common/Vulkan/VulkanLoader.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Vulkan/VulkanUtil.h"

// TODO: Remove?
enum VulkanFBOColorDepth {
	VK_FBO_8888,
	VK_FBO_565,
	VK_FBO_4444,
	VK_FBO_5551,
};

class TextureCacheVulkan;
class DrawEngineVulkan;
class VulkanContext;
class ShaderManagerVulkan;
class VulkanTexture;
class VulkanPushBuffer;

static const char *ub_post_shader =
R"(	vec2 texelDelta;
	vec2 pixelDelta;
	vec4 time;
)";

class FramebufferManagerVulkan : public FramebufferManagerCommon {
public:
	FramebufferManagerVulkan(Draw::DrawContext *draw, VulkanContext *vulkan);
	~FramebufferManagerVulkan();

	void SetTextureCache(TextureCacheVulkan *tc);
	void SetShaderManager(ShaderManagerVulkan *sm);
	void SetDrawEngine(DrawEngineVulkan *td);

	// x,y,w,h are relative to destW, destH which fill out the target completely.
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) override;

	void DestroyAllFBOs();

	virtual void Init() override;

	void BeginFrameVulkan();  // there's a BeginFrame in the base class, which this calls
	void EndFrame();

	void Resized() override;
	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);
	int GetLineWidth();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) override;

	bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override;

	virtual void RebindFramebuffer() override;
	VkImageView BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags);

	// If within a render pass, this will just issue a regular clear. If beginning a new render pass,
	// do that.
	void NotifyClear(bool clearColor, bool clearAlpha, bool clearDepth, uint32_t color, float depth);

protected:
	void Bind2DShader() override;
	void BindPostShader(const PostShaderUniforms &uniforms) override;
	void SetViewport2D(int x, int y, int w, int h) override;
	void DisableState() override {}

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;
	bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	// The returned texture does not need to be free'd, might be returned from a pool (currently single entry)
	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) override;

	void UpdatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight);

	void InitDeviceObjects();
	void DestroyDeviceObjects();

	VulkanContext *vulkan_;

	// Used to keep track of command buffers here but have moved all that into Thin3D.

	// Used by DrawPixels
	VulkanTexture *drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;

	u8 *convBuf_;
	u32 convBufSize_;

	TextureCacheVulkan *textureCacheVulkan_;
	ShaderManagerVulkan *shaderManagerVulkan_;
	DrawEngineVulkan *drawEngineVulkan_;

	enum {
		MAX_COMMAND_BUFFERS = 32,
	};

	// Commandbuffers are handled internally in thin3d, one for each framebuffer pass.
	struct FrameData {
		VulkanPushBuffer *push_;
	};

	FrameData frameData_[VulkanContext::MAX_INFLIGHT_FRAMES];
	int curFrame_;

	// This gets copied to the current frame's push buffer as needed.
	PostShaderUniforms postUniforms_;

	VkPipelineCache pipelineCache2D_;

	// Basic shaders
	VkShaderModule fsBasicTex_;
	VkShaderModule vsBasicTex_;

	VkPipeline cur2DPipeline_ = VK_NULL_HANDLE;

	// Postprocessing
	VkPipeline pipelinePostShader_;

	VkSampler linearSampler_;
	VkSampler nearestSampler_;

	// hack!
	VkImageView overrideImageView_ = VK_NULL_HANDLE;

	// Simple 2D drawing engine.
	Vulkan2D vulkan2D_;
};
