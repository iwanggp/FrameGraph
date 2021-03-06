// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "framegraph/Public/IDs.h"

namespace FG
{

	using RawMemoryID				= _fg_hidden_::ResourceID<14>;
	using RawPipelineLayoutID		= _fg_hidden_::ResourceID<15>;
	using RawRenderPassID			= _fg_hidden_::ResourceID<16>;
	using RawFramebufferID			= _fg_hidden_::ResourceID<17>;
	//using DescriptorPoolID		= _fg_hidden_::ResourceID<18>;
	
	using MemoryID					= _fg_hidden_::ResourceIDWrap< RawMemoryID >;
	using PipelineLayoutID			= _fg_hidden_::ResourceIDWrap< RawPipelineLayoutID >;
	using RenderPassID				= _fg_hidden_::ResourceIDWrap< RawRenderPassID >;
	using FramebufferID				= _fg_hidden_::ResourceIDWrap< RawFramebufferID >;
	using PipelineResourcesID		= _fg_hidden_::ResourceIDWrap< RawPipelineResourcesID >;
	using DescriptorSetLayoutID		= _fg_hidden_::ResourceIDWrap< RawDescriptorSetLayoutID >;

}	// FG
