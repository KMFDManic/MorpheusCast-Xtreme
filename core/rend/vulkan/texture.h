/*
 *  Created on: Oct 3, 2019

	Copyright 2019 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "vulkan_context.h"
#include "buffer.h"
#include "rend/TexCache.h"
#include "hw/pvr/Renderer_if.h"

#include <algorithm>
#include <memory>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

void setImageLayout(vk::CommandBuffer const& commandBuffer, vk::Image image, vk::Format format, u32 mipmapLevels, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout);

class Texture final : public BaseTextureCacheData
{
public:
	Texture(TSP tsp = {}, TCW tcw = {}) : BaseTextureCacheData(tsp, tcw) {
		this->physicalDevice = VulkanContext::Instance()->GetPhysicalDevice();
		this->device = VulkanContext::Instance()->GetDevice();
	}
	Texture(Texture&& other) : BaseTextureCacheData(std::move(other)) {
		std::swap(format, other.format);
		std::swap(extent, other.extent);
		std::swap(mipmapLevels, other.mipmapLevels);
		std::swap(needsStaging, other.needsStaging);
		std::swap(stagingBufferData, other.stagingBufferData);
		std::swap(commandBuffer, other.commandBuffer);
		std::swap(allocation, other.allocation);
		std::swap(image, other.image);
		std::swap(imageView, other.imageView);
		std::swap(readOnlyImageView, other.readOnlyImageView);
		std::swap(physicalDevice, other.physicalDevice);
		std::swap(device, other.device);
	}

	void UploadToGPU(int width, int height, const u8 *data, bool mipmapped, bool mipmapsIncluded = false) override;
	u64 GetIntId() { return (u64)reinterpret_cast<uintptr_t>(this); }
	std::string GetId() override { char s[20]; sprintf(s, "%p", this); return s; }
	vk::ImageView GetImageView() const { return *imageView; }
	vk::Image GetImage() const { return *image; }
	vk::ImageView GetReadOnlyImageView() const { return readOnlyImageView ? readOnlyImageView : *imageView; }
	void SetCommandBuffer(vk::CommandBuffer commandBuffer) { this->commandBuffer = commandBuffer; }
	bool Force32BitTexture(TextureType type) const override { return !VulkanContext::Instance()->IsFormatSupported(type); }
	vk::Extent2D getSize() const { return extent; }

private:
	void Init(u32 width, u32 height, vk::Format format ,u32 dataSize, bool mipmapped, bool mipmapsIncluded);
	void SetImage(u32 size, const void *data, bool isNew, bool genMipmaps);
	void CreateImage(vk::ImageTiling tiling, const vk::ImageUsageFlags& usage, vk::ImageLayout initialLayout,
			const vk::ImageAspectFlags& aspectMask);
	void GenerateMipmaps();

	vk::Format format = vk::Format::eUndefined;
	vk::Extent2D extent;
	u32 mipmapLevels = 1;
	bool needsStaging = false;
	std::unique_ptr<BufferData> stagingBufferData;
	vk::CommandBuffer commandBuffer;

	Allocation allocation;
	vk::UniqueImage image;
	vk::UniqueImageView imageView;
	vk::ImageView readOnlyImageView;

	vk::PhysicalDevice physicalDevice;
	vk::Device device;

	friend class TextureDrawer;
	friend class OITTextureDrawer;
	friend class TextureCache;
};

class SamplerManager
{
public:
	void term() {
		samplers.clear();
	}

	vk::Sampler GetSampler(TSP tsp)
	{
		const u32 samplerHash = tsp.full & TSP_Mask;	// MipMapD, FilterMode, ClampU, ClampV, FlipU, FlipV
		const auto& it = samplers.find(samplerHash);
		if (it != samplers.end())
			return it->second.get();
		vk::Filter filter;
		if (config::TextureFiltering == 0) {
			filter = tsp.FilterMode == 0 ? vk::Filter::eNearest : vk::Filter::eLinear;
		} else if (config::TextureFiltering == 1) {
			filter = vk::Filter::eNearest;
		} else {
			filter = vk::Filter::eLinear;
		}
		const vk::SamplerAddressMode uRepeat = tsp.ClampU ? vk::SamplerAddressMode::eClampToEdge
				: tsp.FlipU ? vk::SamplerAddressMode::eMirroredRepeat : vk::SamplerAddressMode::eRepeat;
		const vk::SamplerAddressMode vRepeat = tsp.ClampV ? vk::SamplerAddressMode::eClampToEdge
				: tsp.FlipV ? vk::SamplerAddressMode::eMirroredRepeat : vk::SamplerAddressMode::eRepeat;

		const bool anisotropicFiltering = config::AnisotropicFiltering > 1 && VulkanContext::Instance()->SupportsSamplerAnisotropy()
				&& filter == vk::Filter::eLinear;
#ifndef __APPLE__
		const float mipLodBias = D_Adjust_LoD_Bias[tsp.MipMapD];
#else
		// not supported by metal
		const float mipLodBias = 0;
#endif
		const vk::SamplerMipmapMode mipmapMode = filter == vk::Filter::eLinear ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
		return samplers.emplace(
					std::make_pair(samplerHash, VulkanContext::Instance()->GetDevice().createSamplerUnique(
						vk::SamplerCreateInfo(vk::SamplerCreateFlags(), filter, filter,
							mipmapMode, uRepeat, vRepeat, vk::SamplerAddressMode::eClampToEdge, mipLodBias,
							anisotropicFiltering, std::min((float)config::AnisotropicFiltering, VulkanContext::Instance()->GetMaxSamplerAnisotropy()),
							false, vk::CompareOp::eNever,
							0.0f, 256.0f, vk::BorderColor::eFloatOpaqueBlack)))).first->second.get();
	}
	static const u32 TSP_Mask = 0x7ef00;

private:
	std::map<u32, vk::UniqueSampler> samplers;
};

class FramebufferAttachment
{
public:
	FramebufferAttachment(vk::PhysicalDevice physicalDevice, vk::Device device)
		: format(vk::Format::eUndefined), physicalDevice(physicalDevice), device(device)
		{}
	void Init(u32 width, u32 height, vk::Format format, const vk::ImageUsageFlags& usage);
	void Reset() { image.reset(); imageView.reset(); }

	vk::ImageView GetImageView() const { return *imageView; }
	vk::Image GetImage() const { return *image; }
	const BufferData* GetBufferData() const { return stagingBufferData.get(); }
	vk::ImageView GetStencilView() const { return *stencilView; }
	vk::Extent2D getExtent() const { return extent; }

private:
	vk::Format format;
	vk::Extent2D extent;

	std::unique_ptr<BufferData> stagingBufferData;
	Allocation allocation;
	vk::UniqueImage image;
	vk::UniqueImageView imageView;
	vk::UniqueImageView stencilView;

	vk::PhysicalDevice physicalDevice;
	vk::Device device;
};

class TextureCache final : public BaseTextureCache<Texture>
{
public:
	TextureCache() {
		Texture::SetDirectXColorOrder(false);
	}
	void SetCurrentIndex(int index) {
		if (currentIndex < inFlightTextures.size())
			std::for_each(inFlightTextures[currentIndex].begin(), inFlightTextures[currentIndex].end(),
				[](Texture *texture) { texture->readOnlyImageView = vk::ImageView(); });
		currentIndex = index;
		EmptyTrash(inFlightTextures);
		EmptyTrash(trashedImageViews);
		EmptyTrash(trashedImages);
		EmptyTrash(trashedMem);
		EmptyTrash(trashedBuffers);
	}

	bool IsInFlight(Texture *texture)
	{
		for (u32 i = 0; i < inFlightTextures.size(); i++)
			if (i != currentIndex && inFlightTextures[i].find(texture) != inFlightTextures[i].end())
				return true;
		return false;
	}

	void SetInFlight(Texture *texture)
	{
		inFlightTextures[currentIndex].insert(texture);
	}

	void DestroyLater(Texture *texture)
	{
		if (!texture->image)
			return;
		trashedImages[currentIndex].push_back(std::move(texture->image));
		trashedImageViews[currentIndex].push_back(std::move(texture->imageView));
		trashedMem[currentIndex].push_back(std::move(texture->allocation));
		trashedBuffers[currentIndex].push_back(std::move(texture->stagingBufferData));
		texture->format = vk::Format::eUndefined;
	}

	void Cleanup();

	void Clear()
	{
		BaseTextureCache::Clear();
		for (auto& set : inFlightTextures)
			set.clear();
		for (auto& v : trashedImageViews)
			v.clear();
		for (auto& v : trashedImages)
			v.clear();
		for (auto& v : trashedMem)
			v.clear();
		for (auto& v : trashedBuffers)
			v.clear();
	}

private:
	bool clearTexture(Texture *tex)
	{
		for (auto& set : inFlightTextures)
			set.erase(tex);

		return tex->Delete();
	}

	template<typename T>
	void EmptyTrash(T& v)
	{
		if (v.size() < currentIndex + 1)
			v.resize(currentIndex + 1);
		v[currentIndex].clear();
	}
	std::vector<std::unordered_set<Texture *>> inFlightTextures;
	std::vector<std::vector<vk::UniqueImageView>> trashedImageViews;
	std::vector<std::vector<vk::UniqueImage>> trashedImages;
	std::vector<std::vector<Allocation>> trashedMem;
	std::vector<std::vector<std::unique_ptr<BufferData>>> trashedBuffers;
	u32 currentIndex = 0;
};
