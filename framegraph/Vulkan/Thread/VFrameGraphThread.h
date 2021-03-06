// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "framegraph/Public/FrameGraphThread.h"
#include "VFrameGraphInstance.h"
#include "VResourceManagerThread.h"
#include "VSwapchain.h"
#include "VTaskGraph.h"
#include "VTaskProcessor.h"

namespace FG
{

	//
	// Frame Graph Thread
	//

	class VFrameGraphThread final : public FrameGraphThread
	{
	// types
	private:
		enum class EState : uint
		{
			Initial,
			Initializing,
			Idle,
			BeforeStart,
			Ready,
			BeforeRecording,
			Recording,		// in this state used delayed resource creation
			Compiling,
			Pending,		// if compilation succeded
			Execute,
			WaitIdle,
			BeforeDestroy,
			Failed,			// if compilation failed
			Destroyed,
		};
		
		static constexpr uint	MaxQueues			= 4;
		static constexpr uint	MaxFrames			= 8;
		static constexpr uint	MaxCommandsPerFrame	= 8;

		using CommandBuffers_t	= FixedArray< VkCommandBuffer, MaxCommandsPerFrame >;
		using TimePoint_t		= std::chrono::high_resolution_clock::time_point;

		struct PerFrame
		{
			CommandBuffers_t	pending;
			CommandBuffers_t	executed;
			uint				queryIndex		= UMax;
			Nanoseconds			executionTime	{0};
		};
		using PerFrameArray_t	= FixedArray< PerFrame, MaxFrames >;


		struct PerQueue
		{
			PerFrameArray_t		frames;
			VkCommandPool		cmdPool		= VK_NULL_HANDLE;
			VDeviceQueueInfoPtr	ptr;
		};
		using PerQueueArray_t	= FixedArray< PerQueue, MaxQueues >;


		struct QueryHelper
		{
			VkQueryPool			pool		= VK_NULL_HANDLE;
		};

		
		using TempTaskArray_t	= std::vector< Task, StdLinearAllocator<Task> >;
		using TaskGraph_t		= VTaskGraph< VTaskProcessor >;
		using Allocator_t		= LinearAllocator<>;
		using Statistic_t		= FrameGraphInstance::Statistics;


	// variables
	private:
		Allocator_t					_mainAllocator;

		std::atomic<EState>			_state;
		ECompilationFlags			_compilationFlags	= Default;

		PerQueueArray_t				_queues;
		Ptr<PerQueue>				_currQueue;
		EQueueUsage					_currUsage			= Default;
		uint						_frameId			: 3;		// 0..7
		bool						_isFirstUsage		= true;		// 'true' if it is first call of 'Begin' in current frame
		
		struct {
			VkQueryPool					pool			= VK_NULL_HANDLE;
			uint						capacity		= 0;
			uint						size			= 0;
		}							_query;

		TaskGraph_t					_taskGraph;
		uint						_visitorID			= 0;
		
		VSubmissionGraph const*		_submissionGraph	= null;
		CommandBatchID				_cmdBatchId;
		uint						_indexInBatch		= UMax;

		Statistic_t					_statistic;

		const DebugName_t			_debugName;

		VFrameGraphInstance &				_instance;
		VResourceManagerThread				_resourceMngr;
		VBarrierManager						_barrierMngr;
		SharedPtr< VMemoryManager >			_memoryMngr;		// memory manager must live until all memory objects have been destroyed
		UniquePtr< VStagingBufferManager >	_stagingMngr;
		UniquePtr< VSwapchain >				_swapchain;
		UniquePtr< VFrameGraphDebugger >	_debugger;
		UniquePtr< VShaderDebugger >		_shaderDebugger;
		ShaderDebugCallback_t				_shaderDebugCallback;

		RaceConditionCheck					_rcCheck;


	// methods
	public:
		VFrameGraphThread (VFrameGraphInstance &, EThreadUsage, StringView);
		~VFrameGraphThread ();
			
		// resource manager
		MPipelineID		CreatePipeline (INOUT MeshPipelineDesc &desc, StringView dbgName) override;
		GPipelineID		CreatePipeline (INOUT GraphicsPipelineDesc &desc, StringView dbgName) override;
		CPipelineID		CreatePipeline (INOUT ComputePipelineDesc &desc, StringView dbgName) override;
		RTPipelineID	CreatePipeline (INOUT RayTracingPipelineDesc &desc) override;
		ImageID			CreateImage (const ImageDesc &desc, const MemoryDesc &mem, StringView dbgName) override;
		BufferID		CreateBuffer (const BufferDesc &desc, const MemoryDesc &mem, StringView dbgName) override;
		ImageID			CreateImage (const ExternalImageDesc &desc, OnExternalImageReleased_t &&, StringView dbgName) override;
		BufferID		CreateBuffer (const ExternalBufferDesc &desc, OnExternalBufferReleased_t &&, StringView dbgName) override;
		SamplerID		CreateSampler (const SamplerDesc &desc, StringView dbgName) override;
		RTShaderTableID	CreateRayTracingShaderTable (StringView dbgName) override;
		RTGeometryID	CreateRayTracingGeometry (const RayTracingGeometryDesc &desc, const MemoryDesc &mem, StringView dbgName) override;
		RTSceneID		CreateRayTracingScene (const RayTracingSceneDesc &desc, const MemoryDesc &mem, StringView dbgName) override;
		bool			InitPipelineResources (RawGPipelineID pplnId, const DescriptorSetID &id, OUT PipelineResources &resources) const override;
		bool			InitPipelineResources (RawCPipelineID pplnId, const DescriptorSetID &id, OUT PipelineResources &resources) const override;
		bool			InitPipelineResources (RawMPipelineID pplnId, const DescriptorSetID &id, OUT PipelineResources &resources) const override;
		bool			InitPipelineResources (RawRTPipelineID pplnId, const DescriptorSetID &id, OUT PipelineResources &resources) const override;
		bool			CachePipelineResources (INOUT PipelineResources &resources) override;
		
		void			ReleaseResource (INOUT GPipelineID &id) override;
		void			ReleaseResource (INOUT CPipelineID &id) override;
		void			ReleaseResource (INOUT MPipelineID &id) override;
		void			ReleaseResource (INOUT RTPipelineID &id) override;
		void			ReleaseResource (INOUT ImageID &id) override;
		void			ReleaseResource (INOUT BufferID &id) override;
		void			ReleaseResource (INOUT SamplerID &id) override;
		void			ReleaseResource (INOUT RTGeometryID &id) override;
		void			ReleaseResource (INOUT RTSceneID &id) override;
		void			ReleaseResource (INOUT RTShaderTableID &id) override;
		void			ReleaseResource (INOUT PipelineResources &resources) override;

		BufferDesc const&	GetDescription (RawBufferID id) const override;
		ImageDesc  const&	GetDescription (RawImageID id) const override;
		//SamplerDesc const&	GetDescription (RawSamplerID id) const override;

		// initialization
		bool		Initialize (const SwapchainCreateInfo *swapchainCI) override;
		void		Deinitialize () override;
		bool		SetCompilationFlags (ECompilationFlags flags, ECompilationDebugFlags debugFlags) override;
		bool		RecreateSwapchain (const SwapchainCreateInfo &) override;
		bool		SetShaderDebugCallback (ShaderDebugCallback_t &&) override;

		// frame execution
		bool		SyncOnBegin (const VSubmissionGraph *);
		bool		Begin (const CommandBatchID &id, uint index, EQueueUsage usage) override;
		bool		Execute () override;
		bool		SyncOnExecute (INOUT Statistic_t &);
		bool		OnWaitIdle ();
		
		// resource acquiring
		//bool		Acquire (const ImageID &id, bool immutable) override;
		//bool		Acquire (const ImageID &id, MipmapLevel baseLevel, uint levelCount, ImageLayer baseLayer, uint layerCount, bool immutable) override;
		//bool		Acquire (const BufferID &id, bool immutable) override;
		//bool		Acquire (const BufferID &id, BytesU offset, BytesU size, bool immutable) override;
		
		//ImageID	GetSwapchainImage (ESwapchainImage type) override;
		
		bool		UpdateHostBuffer (RawBufferID id, BytesU offset, BytesU size, const void *data) override;
		bool		MapBufferRange (RawBufferID id, BytesU offset, INOUT BytesU &size, OUT void* &data) override;

		// tasks
		Task		AddTask (const SubmitRenderPass &) override;
		Task		AddTask (const DispatchCompute &) override;
		Task		AddTask (const DispatchComputeIndirect &) override;
		Task		AddTask (const CopyBuffer &) override;
		Task		AddTask (const CopyImage &) override;
		Task		AddTask (const CopyBufferToImage &) override;
		Task		AddTask (const CopyImageToBuffer &) override;
		Task		AddTask (const BlitImage &) override;
		Task		AddTask (const ResolveImage &) override;
		Task		AddTask (const GenerateMipmaps &) override;
		Task		AddTask (const FillBuffer &) override;
		Task		AddTask (const ClearColorImage &) override;
		Task		AddTask (const ClearDepthStencilImage &) override;
		Task		AddTask (const UpdateBuffer &) override;
		Task		AddTask (const UpdateImage &) override;
		Task		AddTask (const ReadBuffer &) override;
		Task		AddTask (const ReadImage &) override;
		Task		AddTask (const FG::Present &) override;
		//Task		AddTask (const PresentVR &) override;
		Task		AddTask (const UpdateRayTracingShaderTable &) override;
		Task		AddTask (const BuildRayTracingGeometry &) override;
		Task		AddTask (const BuildRayTracingScene &) override;
		Task		AddTask (const TraceRays &) override;

		// draw tasks
		LogicalPassID  CreateRenderPass (const RenderPassDesc &desc) override;
		void		AddTask (LogicalPassID, const DrawVertices &) override;
		void		AddTask (LogicalPassID, const DrawIndexed &) override;
		void		AddTask (LogicalPassID, const DrawVerticesIndirect &) override;
		void		AddTask (LogicalPassID, const DrawIndexedIndirect &) override;
		void		AddTask (LogicalPassID, const DrawMeshes &) override;
		void		AddTask (LogicalPassID, const DrawMeshesIndirect &) override;
		
		void SignalSemaphore (VkSemaphore sem);
		void WaitSemaphore (VkSemaphore sem, VkPipelineStageFlags stage);
		
		bool AddPipelineCompiler (const IPipelineCompilerPtr &comp);
		
		void TransitImageLayoutToDefault (RawImageID imageId, VkImageLayout initialLayout, uint queueFamily,
										  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

		
		ND_ bool							IsInSeparateThread ()		const;
		ND_ bool							IsDestroyed ()				const	{ EXLOCK( _rcCheck );  return _GetState() == EState::Destroyed; }
		ND_ Allocator_t &					GetAllocator ()						{ EXLOCK( _rcCheck );  return _mainAllocator; }
		ND_ VDevice const&					GetDevice ()				const	{ EXLOCK( _rcCheck );  return _instance.GetDevice(); }
		ND_ uint							GetRingBufferSize ()		const	{ EXLOCK( _rcCheck );  return _instance.GetRingBufferSize(); }
		ND_ Statistic_t &					EditStatistic ()					{ EXLOCK( _rcCheck );  return _statistic; }

		ND_ VFrameGraphInstance const*		GetInstance ()				const	{ EXLOCK( _rcCheck );  return &_instance; }
		ND_ VResourceManagerThread*			GetResourceManager ()				{ EXLOCK( _rcCheck );  return &_resourceMngr; }
		ND_ VResourceManagerThread const*	GetResourceManager ()		const	{ EXLOCK( _rcCheck );  return &_resourceMngr; }
		ND_ Ptr<VMemoryManager>				GetMemoryManager ()					{ EXLOCK( _rcCheck );  return _memoryMngr.operator->(); }
		ND_ Ptr<VPipelineCache>				GetPipelineCache ()					{ EXLOCK( _rcCheck );  return _resourceMngr.GetPipelineCache(); }
		ND_ Ptr<VFrameGraphDebugger>		GetDebugger ()						{ EXLOCK( _rcCheck );  return _debugger.get(); }
		ND_ Ptr<VSwapchain>					GetSwapchain ()						{ EXLOCK( _rcCheck );  return _swapchain.get(); }	// temp
		ND_ Ptr<VStagingBufferManager>		GetStagingBufferManager ()			{ EXLOCK( _rcCheck );  return _stagingMngr.get(); }
		ND_ Ptr<VShaderDebugger>			GetShaderDebugger ()				{ EXLOCK( _rcCheck );  return _shaderDebugger.get(); }
		ND_ Ptr<VShaderDebugger>			CreateShaderDebugger ();


	private:
		template <typename Desc, typename ID>
		ND_ Desc const&  _GetDescription (const ID &id) const;
		
		template <typename PplnID>
		bool _InitPipelineResources (const PplnID &pplnId, const DescriptorSetID &id, OUT PipelineResources &resources) const;

		template <typename ID>
		void _ReleaseResource (INOUT ID &id);

		template <typename T>
		bool _AllocStorage (size_t count, OUT const VLocalBuffer* &buf, OUT VkDeviceSize &offset, OUT T* &ptr);
		bool _StoreData (const void *dataPtr, BytesU dataSize, BytesU offsetAlign, OUT const VLocalBuffer* &buf, OUT VkDeviceSize &offset);

		ND_ EState	_GetState () const				{ return _state.load( memory_order_acquire ); }
			void	_SetState (EState newState);
		ND_ bool	_SetState (EState expected, EState newState);

		ND_ bool	_IsRecording ()			const	{ return _GetState() == EState::Recording; }
		ND_ bool	_IsInitialized ()		const;
		ND_ bool	_IsInitialOrIdleState ()const;
		
		ND_ VkCommandBuffer		_CreateCommandBuffer () const;


		bool _CreateCommandBuffers (INOUT PerQueue &) const;
		bool _InitGpuQueries (INOUT PerQueueArray_t &, INOUT VkQueryPool &) const;

		bool _BuildCommandBuffers ();
		bool _ProcessTasks (VkCommandBuffer cmd);
		
		bool _RecreateSwapchain (const VulkanSwapchainCreateInfo &);

		ND_ Task  _AddUpdateBufferTask (const UpdateBuffer &task);
		ND_ Task  _AddUpdateImageTask (const UpdateImage &task);
		ND_ Task  _AddReadBufferTask (const ReadBuffer &task);
		ND_ Task  _AddReadImageTask (const ReadImage &task);
		ND_ Task  _AddPresentTask (const FG::Present &task);
	};


}	// FG
