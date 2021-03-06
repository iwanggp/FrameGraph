// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "framegraph/Public/FrameGraphDrawTask.h"
#include "VCommon.h"

namespace FG
{

	template <typename Task>
	class VFgDrawTask;


	
	//
	// Draw Task interface
	//
	class IDrawTask
	{
	// types
	public:
		using Name_t			= _fg_hidden_::TaskName_t;
		using ProcessFunc_t		= void (*) (void *visitor, void *taskData);
		

	// variables
	private:
		ProcessFunc_t	_pass1	= null;
		ProcessFunc_t	_pass2	= null;
		Name_t			_taskName;
		RGBA8u			_debugColor;


	// interface
	protected:
		template <typename TaskType>
		IDrawTask (const TaskType &task, ProcessFunc_t pass1, ProcessFunc_t pass2) :
			_pass1{pass1}, _pass2{pass2}, _taskName{task.taskName}, _debugColor{task.debugColor} {}

	public:
		virtual ~IDrawTask () {}
		
		ND_ StringView	GetName ()			const	{ return _taskName; }
		ND_ RGBA8u		GetDebugColor ()	const	{ return _debugColor; }
		
		void Process1 (void *visitor)				{ ASSERT( _pass1 );  _pass1( visitor, this ); }
		void Process2 (void *visitor)				{ ASSERT( _pass2 );  _pass2( visitor, this ); }
	};



	//
	// Base Draw Vertices Task
	//
	class VBaseDrawVerticesTask : public IDrawTask
	{
	// types
	public:
		using VertexBuffers_t	= StaticArray< VLocalBuffer const*, FG_MaxVertexBuffers >;
		using VertexOffsets_t	= StaticArray< VkDeviceSize, FG_MaxVertexBuffers >;
		using VertexStrides_t	= StaticArray< Bytes<uint>, FG_MaxVertexBuffers >;


	// variables
	private:
		VPipelineResourceSet					_resources;

		VertexBuffers_t							_vertexBuffers;
		VertexOffsets_t							_vbOffsets;
		VertexStrides_t							_vbStrides;
		const uint								_vbCount;

		ArrayView< RectI >						_scissors;
		uint									_debugModeIndex	= UMax;

	public:
		VGraphicsPipeline const* const			pipeline;
		const _fg_hidden_::PushConstants_t		pushConstants;

		const VertexInputState					vertexInput;

		const _fg_hidden_::ColorBuffers_t		colorBuffers;
		const _fg_hidden_::DynamicStates		dynamicStates;
		
		const EPrimitive						topology;
		const bool								primitiveRestart;

		mutable VkDescriptorSets_t				descriptorSets;
		

	// methods
	protected:
		template <typename TaskType>
		VBaseDrawVerticesTask (VLogicalRenderPass* rp, VFrameGraphThread *fg, const TaskType &task, ProcessFunc_t pass1, ProcessFunc_t pass2);

	public:
		ND_ VPipelineResourceSet const&		GetResources ()			const	{ return _resources; }
		ND_ ArrayView< RectI >				GetScissors ()			const	{ return _scissors; }
		ND_ uint							GetDebugModeIndex ()	const	{ return _debugModeIndex; }

		ND_ ArrayView< VLocalBuffer const*>	GetVertexBuffers ()		const	{ return ArrayView{ _vertexBuffers.data(), _vbCount }; }
		ND_ ArrayView< VkDeviceSize >		GetVBOffsets ()			const	{ return ArrayView{ _vbOffsets.data(), _vbCount }; }
		ND_ ArrayView< Bytes<uint> >		GetVBStrides ()			const	{ return ArrayView{ _vbStrides.data(), _vbCount }; }
	};



	//
	// Draw Vertices
	//
	template <>
	class VFgDrawTask< DrawVertices > final : public VBaseDrawVerticesTask
	{
	public:
	// variables
		const DrawVertices::DrawCommands_t		commands;

	// methods
		VFgDrawTask (VLogicalRenderPass* rp, VFrameGraphThread *fg, const DrawVertices &task, ProcessFunc_t pass1, ProcessFunc_t pass2);
	};



	//
	// Draw Indexed Vertices
	//
	template <>
	class VFgDrawTask< DrawIndexed > final : public VBaseDrawVerticesTask
	{
	public:
	// variables
		const DrawIndexed::DrawCommands_t	commands;

		VLocalBuffer const* const			indexBuffer;
		const BytesU						indexBufferOffset;
		const EIndex						indexType;

	// methods
		VFgDrawTask (VLogicalRenderPass* rp, VFrameGraphThread *fg, const DrawIndexed &task, ProcessFunc_t pass1, ProcessFunc_t pass2);
	};



	//
	// Draw Vertices Indirect
	//
	template <>
	class VFgDrawTask< DrawVerticesIndirect > final : public VBaseDrawVerticesTask
	{
	public:
	// variables
		const DrawVerticesIndirect::DrawCommands_t		commands;
		VLocalBuffer const* const						indirectBuffer;

	// methods
		VFgDrawTask (VLogicalRenderPass* rp, VFrameGraphThread *fg, const DrawVerticesIndirect &task, ProcessFunc_t pass1, ProcessFunc_t pass2);
	};



	//
	// Draw Indexed Vertices Indirect
	//
	template <>
	class VFgDrawTask< DrawIndexedIndirect > final : public VBaseDrawVerticesTask
	{
	public:
	// variables
		const DrawIndexedIndirect::DrawCommands_t	commands;
		VLocalBuffer const* const					indirectBuffer;

		VLocalBuffer const* const					indexBuffer;
		const BytesU								indexBufferOffset;
		const EIndex								indexType;

	// methods
		VFgDrawTask (VLogicalRenderPass* rp, VFrameGraphThread *fg, const DrawIndexedIndirect &task, ProcessFunc_t pass1, ProcessFunc_t pass2);
	};
	


	//
	// Base Draw Meshes
	//
	class VBaseDrawMeshes : public IDrawTask
	{
	// variables
	private:
		VPipelineResourceSet					_resources;
		ArrayView< RectI >						_scissors;
		uint									_debugModeIndex	= UMax;
	public:
		VMeshPipeline const* const				pipeline;
		const _fg_hidden_::PushConstants_t		pushConstants;

		const _fg_hidden_::ColorBuffers_t		colorBuffers;
		const _fg_hidden_::DynamicStates		dynamicStates;

		mutable VkDescriptorSets_t				descriptorSets;


	// methods
	protected:
		template <typename TaskType>
		VBaseDrawMeshes (VLogicalRenderPass* rp, VFrameGraphThread *fg, const TaskType &task, ProcessFunc_t pass1, ProcessFunc_t pass2);

	public:
		ND_ VPipelineResourceSet const&		GetResources ()			const	{ return _resources; }
		ND_ ArrayView< RectI >				GetScissors ()			const	{ return _scissors; }
		ND_ uint							GetDebugModeIndex ()	const	{ return _debugModeIndex; }
	};



	//
	// Draw Meshes
	//
	template <>
	class VFgDrawTask< DrawMeshes > final : public VBaseDrawMeshes
	{
	public:
	// variables
		const DrawMeshes::DrawCommands_t	commands;

	// methods
		VFgDrawTask (VLogicalRenderPass* rp, VFrameGraphThread *fg, const DrawMeshes &task, ProcessFunc_t pass1, ProcessFunc_t pass2);
	};
	


	//
	// Draw Meshes Indirect
	//
	template <>
	class VFgDrawTask< DrawMeshesIndirect > final : public VBaseDrawMeshes
	{
	public:
	// variables
		const DrawMeshesIndirect::DrawCommands_t	commands;
		VLocalBuffer const* const					indirectBuffer;

	// methods
		VFgDrawTask (VLogicalRenderPass* rp, VFrameGraphThread *fg, const DrawMeshesIndirect &task, ProcessFunc_t pass1, ProcessFunc_t pass2);
	};


}	// FG
