// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "VTaskGraph.h"
#include "VLocalBuffer.h"
#include "VLocalImage.h"
#include "VLocalRTGeometry.h"
#include "VLocalRTScene.h"
#include "VBarrierManager.h"

namespace FG
{

	//
	// Task Processor
	//

	class VTaskProcessor final
	{
	// types
	private:
		class DrawTaskBarriers;
		class DrawTaskCommands;
		class PipelineResourceBarriers;

		using BufferRange				= VLocalBuffer::BufferRange;
		using ImageRange				= VLocalImage::ImageRange;
		using BufferState				= VLocalBuffer::BufferState;
		using ImageState				= VLocalImage::ImageState;
		using RTGeometryState			= VLocalRTGeometry::GeometryState;
		using RTSceneState				= VLocalRTScene::SceneState;

		using CommitBarrierFn_t			= void (*) (const void *, VBarrierManager &, Ptr<VFrameGraphDebugger>);
		using PendingResourceBarriers_t	= std::unordered_map< void const*, CommitBarrierFn_t, std::hash<void const*>, std::equal_to<void const*>,
															  StdLinearAllocator<Pair<void const* const, CommitBarrierFn_t>> >;	// TODO: use temp allocator
		
		using VkClearValues_t			= FixedArray< VkClearValue, FG_MaxColorBuffers+1 >;
		using BufferCopyRegions_t		= FixedArray< VkBufferCopy, FG_MaxCopyRegions >;
		using ImageCopyRegions_t		= FixedArray< VkImageCopy, FG_MaxCopyRegions >;
		using BufferImageCopyRegions_t	= FixedArray< VkBufferImageCopy, FG_MaxCopyRegions >;
		using BlitRegions_t				= FixedArray< VkImageBlit, FG_MaxBlitRegions >;
		using ResolveRegions_t			= FixedArray< VkImageResolve, FG_MaxResolveRegions >;
		using ImageClearRanges_t		= FixedArray< VkImageSubresourceRange, FG_MaxClearRanges >;
		
		using Statistic_t				= FrameGraphInstance::RenderingStatistics;

		struct PipelineState
		{
			VkPipeline		pipeline	= VK_NULL_HANDLE;
		};


	// variables
	private:
		VFrameGraphThread &			_frameGraph;
		VDevice const &				_dev;

		VkCommandBuffer				_cmdBuffer;
		
		Task						_currTask;
		bool						_enableDebugUtils		: 1;
		bool						_isDefaultScissor		: 1;
		bool						_perPassStatesUpdated	: 1;

		PendingResourceBarriers_t	_pendingResourceBarriers;
		VBarrierManager &			_barrierMngr;

		PipelineState				_graphicsPipeline;
		PipelineState				_computePipeline;
		PipelineState				_rayTracingPipeline;

		// index bufer state
		VkBuffer					_indexBuffer		= VK_NULL_HANDLE;
		VkDeviceSize				_indexBufferOffset	= UMax;
		VkIndexType					_indexType			= VK_INDEX_TYPE_MAX_ENUM;

		VkImageView					_shadingRateImage	= VK_NULL_HANDLE;

		static constexpr float		_dbgColor[4]		= { 1.0f, 1.0f, 1.0f, 1.0f };


	// methods
	public:
		VTaskProcessor (VFrameGraphThread &fg, VBarrierManager &barrierMngr, VkCommandBuffer cmd,
						const CommandBatchID &batchId, uint indexInBatch);
		~VTaskProcessor ();

		void Visit (const VFgTask<SubmitRenderPass> &);
		void Visit (const VFgTask<DispatchCompute> &);
		void Visit (const VFgTask<DispatchComputeIndirect> &);
		void Visit (const VFgTask<CopyBuffer> &);
		void Visit (const VFgTask<CopyImage> &);
		void Visit (const VFgTask<CopyBufferToImage> &);
		void Visit (const VFgTask<CopyImageToBuffer> &);
		void Visit (const VFgTask<BlitImage> &);
		void Visit (const VFgTask<ResolveImage> &);
		void Visit (const VFgTask<GenerateMipmaps> &);
		void Visit (const VFgTask<FillBuffer> &);
		void Visit (const VFgTask<ClearColorImage> &);
		void Visit (const VFgTask<ClearDepthStencilImage> &);
		void Visit (const VFgTask<UpdateBuffer> &);
		void Visit (const VFgTask<Present> &);
		void Visit (const VFgTask<PresentVR> &);
		void Visit (const VFgTask<UpdateRayTracingShaderTable> &);
		void Visit (const VFgTask<BuildRayTracingGeometry> &);
		void Visit (const VFgTask<BuildRayTracingScene> &);
		void Visit (const VFgTask<TraceRays> &);

		static void Visit1_DrawVertices (void *, void *);
		static void Visit2_DrawVertices (void *, void *);
		static void Visit1_DrawIndexed (void *, void *);
		static void Visit2_DrawIndexed (void *, void *);
		static void Visit1_DrawMeshes (void *, void *);
		static void Visit2_DrawMeshes (void *, void *);
		static void Visit1_DrawVerticesIndirect (void *, void *);
		static void Visit2_DrawVerticesIndirect (void *, void *);
		static void Visit1_DrawIndexedIndirect (void *, void *);
		static void Visit2_DrawIndexedIndirect (void *, void *);
		static void Visit1_DrawMeshesIndirect (void *, void *);
		static void Visit2_DrawMeshesIndirect (void *, void *);

		void Run (Task);


	private:
		void _CmdDebugMarker (StringView text) const;
		void _CmdPushDebugGroup (StringView text) const;
		void _CmdPopDebugGroup () const;
		
		template <typename ID>	ND_ auto const*  _ToLocal (ID id) const;
		template <typename ID>	ND_ auto const*  _GetResource (ID id) const;
		
		void _CommitBarriers ();
		
		void _AddRenderTargetBarriers (const VLogicalRenderPass &logicalRP, const DrawTaskBarriers &info);
		void _SetShadingRateImage (const VLogicalRenderPass &logicalRP, OUT VkImageView &view);
		void _ExtractClearValues (const VLogicalRenderPass &logicalRP, const VRenderPass *rp, OUT VkClearValues_t &clearValues) const;
		void _BeginRenderPass (const VFgTask<SubmitRenderPass> &task);
		void _BeginSubpass (const VFgTask<SubmitRenderPass> &task);

		void _ExtractDescriptorSets (const VPipelineLayout &, const VPipelineResourceSet &, OUT VkDescriptorSets_t &);
		void _BindPipelineResources (const VPipelineLayout &layout, const VPipelineResourceSet &resourceSet, VkPipelineBindPoint bindPoint, uint debugModeIndex);
		void _BindPipeline (const VLogicalRenderPass &logicalRP, const VBaseDrawVerticesTask &task, OUT VPipelineLayout const* &pplnLayout);
		void _BindPipeline (const VLogicalRenderPass &logicalRP, const VBaseDrawMeshes &task, OUT VPipelineLayout const* &pplnLayout);
		void _BindPipeline2 (const VLogicalRenderPass &logicalRP, VkPipeline pipelineId);
		void _BindPipeline (const VComputePipeline* pipeline, const Optional<uint3> &localSize, uint debugModeIndex, VkPipelineCreateFlags flags, OUT VPipelineLayout const* &pplnLayout);
		void _PushConstants (const VPipelineLayout &layout, const _fg_hidden_::PushConstants_t &pc) const;
		void _SetScissor (const VLogicalRenderPass *, ArrayView<RectI>);
		void _SetDynamicStates (const _fg_hidden_::DynamicStates &) const;
		void _BindShadingRateImage (VkImageView view);

		void _AddImage (const VLocalImage *img, EResourceState state, VkImageLayout layout, const ImageViewDesc &desc);
		void _AddImage (const VLocalImage *img, EResourceState state, VkImageLayout layout, const VkImageSubresourceLayers &subresLayers);
		void _AddImage (const VLocalImage *img, EResourceState state, VkImageLayout layout, const VkImageSubresourceRange &subres);
		void _AddImageState (const VLocalImage *img, const ImageState &state);

		void _AddBuffer (const VLocalBuffer *buf, EResourceState state, VkDeviceSize offset, VkDeviceSize size);
		void _AddBuffer (const VLocalBuffer *buf, EResourceState state, const VkBufferImageCopy &reg, const VLocalImage *img);
		void _AddBufferState (const VLocalBuffer *buf, const BufferState &state);

		void _AddRTGeometry (const VLocalRTGeometry *geom, EResourceState state);
		void _AddRTScene (const VLocalRTScene *scene, EResourceState state);

		void _BindIndexBuffer (VkBuffer indexBuffer, VkDeviceSize indexOffset, VkIndexType indexType);

		ND_ Statistic_t&  Stat () const;
	};


}	// FG
