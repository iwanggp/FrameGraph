// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#include "Public/IPipelineCompiler.h"
#include "VPipelineCache.h"
#include "VDevice.h"
#include "VEnumCast.h"
#include "VRenderPass.h"
#include "VResourceManagerThread.h"
#include "VFrameGraphThread.h"
#include "VShaderDebugger.h"
#include "VStagingBufferManager.h"

namespace FG
{

	//
	// Vulkan Shader Module
	//
	class VShaderModule final : public PipelineDescription::IShaderData< ShaderModuleVk_t >
	{
	// variables
	private:
		VkShaderModule		_module		= VK_NULL_HANDLE;
		StaticString<64>	_entry;


	// methods
	public:
		VShaderModule (VkShaderModule module, StringView entry) :
			_module{ module }, _entry{ entry } {}

		~VShaderModule () {
			CHECK( _module == VK_NULL_HANDLE );
		}

		void Destroy (const VDevice &dev)
		{
			if ( _module )
			{
				dev.vkDestroyShaderModule( dev.GetVkDevice(), _module, null );
				_module = VK_NULL_HANDLE;
			}
		}
		
		ShaderModuleVk_t const&	GetData () const override		{ return BitCast<ShaderModuleVk_t>( _module ); }

		StringView				GetEntry () const override		{ return _entry; }
		
		StringView				GetDebugName () const override	{ return ""; }

		size_t					GetHashOfData () const override	{ ASSERT(false);  return 0; }

		bool					ParseDebugOutput (EShaderDebugMode, ArrayView<uint8_t>, OUT Array<String> &) const override { return false; }
	};
//-----------------------------------------------------------------------------



/*
=================================================
	constructor
=================================================
*/
	VPipelineCache::VPipelineCache () :
		_pipelinesCache{ VK_NULL_HANDLE }
	{
		const uint	max_stages = 32;

		_tempStages.reserve( max_stages );
		_tempShaderGroups.reserve( max_stages );
		_tempSpecialization.reserve( max_stages * FG_MaxSpecConstants );
		_tempSpecEntries.reserve( max_stages * FG_MaxSpecConstants );
		_tempShaderGraphMap.reserve( max_stages );
	}
	
/*
=================================================
	destructor
=================================================
*/
	VPipelineCache::~VPipelineCache ()
	{
		CHECK( _pipelinesCache == VK_NULL_HANDLE );
		CHECK( _shaderCache.empty() );
	}
	
/*
=================================================
	Initialize
=================================================
*/
	bool VPipelineCache::Initialize (const VDevice &dev)
	{
		CHECK_ERR( _CreatePipelineCache( dev ));
		return true;
	}
	
/*
=================================================
	Deinitialize
=================================================
*/
	void VPipelineCache::Deinitialize (const VDevice &dev)
	{
		for (auto& sh : _shaderCache)
		{
			ASSERT( sh.use_count() == 1 );

			Cast<VShaderModule>( sh )->Destroy( dev );
		}
		_shaderCache.clear();

		_compilers.clear();

		if ( _pipelinesCache )
		{
			dev.vkDestroyPipelineCache( dev.GetVkDevice(), _pipelinesCache, null );
			_pipelinesCache = VK_NULL_HANDLE;
		}
	}

/*
=================================================
	MergeCache
=================================================
*/
	bool VPipelineCache::MergeCache (VPipelineCache &)
	{
		// TODO
		return false;
	}

/*
=================================================
	AddCompiler
=================================================
*/
	void VPipelineCache::AddCompiler (const IPipelineCompilerPtr &comp)
	{
		_compilers.insert( comp );
	}
	
/*
=================================================
	_GetBuiltinFormats
=================================================
*/
	FixedArray<EShaderLangFormat,16>  VPipelineCache::_GetBuiltinFormats (const VDevice &dev) const
	{
		const EShaderLangFormat				ver		= dev.GetVkVersion();
		FixedArray<EShaderLangFormat,16>	result;

		// at first request external managed shader modules
		result.push_back( ver | EShaderLangFormat::ShaderModule );

		if ( ver > EShaderLangFormat::Vulkan_110 )
			result.push_back( EShaderLangFormat::VkShader_110 );

		if ( ver > EShaderLangFormat::Vulkan_100 )
			result.push_back( EShaderLangFormat::VkShader_100 );
		

		// at second request shader in binary format
		result.push_back( ver | EShaderLangFormat::SPIRV );

		if ( ver > EShaderLangFormat::Vulkan_110 )
			result.push_back( EShaderLangFormat::SPIRV_110 );

		if ( ver > EShaderLangFormat::Vulkan_100 )
			result.push_back( EShaderLangFormat::SPIRV_100 );

		return result;
	}

/*
=================================================
	CreateFramentOutput
=================================================
*/
	VPipelineCache::FragmentOutputPtr  VPipelineCache::CreateFramentOutput (ArrayView<GraphicsPipelineDesc::FragmentOutput> values)
	{
		VGraphicsPipeline::FragmentOutputInstance	inst{ values };

		return _fragmentOutputCache.insert( std::move(inst) ).first.operator-> ();
	}

/*
=================================================
	_CompileShaders
=================================================
*/
	template <typename DescT>
	bool  VPipelineCache::_CompileShaders (INOUT DescT &desc, const VDevice &dev)
	{
		const EShaderLangFormat		req_format = dev.GetVkVersion() | EShaderLangFormat::ShaderModule;

		// try to use external compilers
		for (auto& comp : _compilers)
		{
			if ( comp->IsSupported( desc, req_format ) )
			{
				return comp->Compile( INOUT desc, req_format );
			}
		}

		// check is shaders supported by default compiler
		const auto	formats		 = _GetBuiltinFormats( dev );
		bool		is_supported = true;

		for (auto& sh : desc._shaders)
		{
			if ( sh.second.data.empty() )
				continue;

			bool	found = false;

			for (auto& fmt : formats)
			{
				auto	iter = sh.second.data.find( fmt );

				if ( iter == sh.second.data.end() )
					continue;

				if ( EnumEq( fmt, EShaderLangFormat::ShaderModule ) )
				{
					auto	shader_data = iter->second;
				
					sh.second.data.clear();
					sh.second.data.insert({ fmt, shader_data });

					found = true;
					break;
				}
			
				if ( EnumEq( fmt, EShaderLangFormat::SPIRV ) )
				{
					VkShaderPtr		mod;
					CHECK_ERR( _CompileSPIRVShader( dev, iter->second, OUT mod ));

					sh.second.data.clear();
					sh.second.data.insert({ fmt, mod });

					found = true;
					break;
				}
			}
			is_supported &= found;
		}

		if ( not is_supported )
			RETURN_ERR( "unsuported shader format!" );

		return true;
	}
	
/*
=================================================
	CompileShaders
=================================================
*/
	bool VPipelineCache::CompileShaders (INOUT GraphicsPipelineDesc &desc, const VDevice &dev)
	{
		return _CompileShaders( INOUT desc, dev );
	}
	
	bool VPipelineCache::CompileShaders (INOUT MeshPipelineDesc &desc, const VDevice &dev)
	{
		return _CompileShaders( INOUT desc, dev );
	}

	bool VPipelineCache::CompileShaders (INOUT RayTracingPipelineDesc &desc, const VDevice &dev)
	{
		return _CompileShaders( INOUT desc, dev );
	}
	
/*
=================================================
	CompileShader
=================================================
*/
	bool VPipelineCache::CompileShader (INOUT ComputePipelineDesc &desc, const VDevice &dev)
	{
		const EShaderLangFormat		req_format = dev.GetVkVersion() | EShaderLangFormat::ShaderModule;
		
		// try to use external compilers
		for (auto& comp : _compilers)
		{
			if ( comp->IsSupported( desc, req_format ) )
			{
				CHECK_ERR( comp->Compile( INOUT desc, req_format ));
				return true;
			}
		}

		// check is shaders supported by default compiler
		const auto	formats = _GetBuiltinFormats( dev );

		for (auto& fmt : formats)
		{
			auto	iter = desc._shader.data.find( fmt );

			if ( iter == desc._shader.data.end() )
				continue;

			if ( EnumEq( fmt, EShaderLangFormat::ShaderModule ) )
			{
				auto	shader_data = iter->second;
				
				desc._shader.data.clear();
				desc._shader.data.insert({ fmt, shader_data });
				return true;
			}
			
			if ( EnumEq( fmt, EShaderLangFormat::SPIRV ) )
			{
				VkShaderPtr		mod;
				CHECK_ERR( _CompileSPIRVShader( dev, iter->second, OUT mod ));

				desc._shader.data.clear();
				desc._shader.data.insert({ fmt, mod });
				return true;
			}
		}

		RETURN_ERR( "unsuported shader format!" );
	}

/*
=================================================
	_CompileShader
=================================================
*/
	bool  VPipelineCache::_CompileSPIRVShader (const VDevice &dev, const PipelineDescription::ShaderDataUnion_t &shaderData, OUT VkShaderPtr &module)
	{
		const auto*	shader_data = UnionGetIf< PipelineDescription::SharedShaderPtr<Array<uint>> >( &shaderData );

		if ( not (shader_data and *shader_data) )
			RETURN_ERR( "invalid shader data format!" );
				
		VkShaderModuleCreateInfo	shader_info = {};
		shader_info.sType		= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		shader_info.codeSize	= size_t(ArraySizeOf( (*shader_data)->GetData() ));
		shader_info.pCode		= (*shader_data)->GetData().data();

		VkShaderModule		shader_id;
		VK_CHECK( dev.vkCreateShaderModule( dev.GetVkDevice(), &shader_info, null, OUT &shader_id ));

		dev.SetObjectName( BitCast<uint64_t>(shader_id), (*shader_data)->GetDebugName(), VK_OBJECT_TYPE_SHADER_MODULE );

		module = MakeShared<VShaderModule>( shader_id, (*shader_data)->GetEntry() );

		_shaderCache.push_back( module );
		return true;
	}
	
/*
=================================================
	_CreatePipelineCache
=================================================
*/
	bool  VPipelineCache::_CreatePipelineCache (const VDevice &dev)
	{
		CHECK_ERR( not _pipelinesCache );

		VkPipelineCacheCreateInfo	info = {};
		info.sType				= VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		info.pNext				= null;
		info.flags				= 0;
		info.initialDataSize	= 0;
		info.pInitialData		= null;

		VK_CHECK( dev.vkCreatePipelineCache( dev.GetVkDevice(), &info, null, OUT &_pipelinesCache ));
		return true;
	}

/*
=================================================
	_ClearTemp
=================================================
*/
	void VPipelineCache::_ClearTemp ()
	{
		_tempStages.clear();
		_tempSpecialization.clear();
		_tempDynamicStates.clear();
		_tempViewports.clear();
		_tempScissors.clear();
		_tempVertexAttribs.clear();
		_tempVertexBinding.clear();
		_tempAttachments.clear();
		_tempSpecEntries.clear();
		_tempSpecData.clear();
		_tempShaderGroups.clear();
		_tempShaderGraphMap.clear();
		_rtShaderSpecs.clear();
	}
	
/*
=================================================
	OverrideColorStates
=================================================
*/
	static void OverrideColorStates (INOUT RenderState::ColorBuffersState &currColorStates, const _fg_hidden_::ColorBuffers_t &newStates)
	{
		for (auto& cb : newStates)
		{
			auto	iter = currColorStates.buffers.find( cb.first );
			ASSERT( iter != currColorStates.buffers.end() );

			if ( iter != currColorStates.buffers.end() )
			{
				iter->second = cb.second;
			}
		}
	}
	
/*
=================================================
	OverrideDepthStencilStates
=================================================
*/
	static void OverrideDepthStencilStates (INOUT RenderState::DepthBufferState &currDepthState, INOUT RenderState::StencilBufferState &currStencilState,
											INOUT RenderState::RasterizationState &currRasterState, INOUT EPipelineDynamicState &dynamicState,
											const _fg_hidden_::DynamicStates &newStates)
	{
		currDepthState.test					= newStates.hasDepthTest ? newStates.depthTest : currDepthState.test;
		currDepthState.write				= newStates.hasDepthWrite ? newStates.depthWrite : currDepthState.write;
		currStencilState.enabled			= newStates.hasStencilTest ? newStates.stencilTest : currStencilState.enabled;
		currRasterState.cullMode			= newStates.hasCullMode ? newStates.cullMode : currRasterState.cullMode;
		currRasterState.rasterizerDiscard	= newStates.hasRasterizedDiscard ? newStates.rasterizerDiscard : currRasterState.rasterizerDiscard;
		currRasterState.frontFaceCCW		= newStates.hasFrontFaceCCW ? newStates.frontFaceCCW : currRasterState.frontFaceCCW;

		if ( currDepthState.test )
		{
			currDepthState.compareOp = newStates.hasDepthCompareOp ? newStates.depthCompareOp : currDepthState.compareOp;
		}

		// override stencil states
		if ( currStencilState.enabled )
		{
			if ( newStates.hasStencilFailOp )
				currStencilState.front.failOp = currStencilState.back.failOp = newStates.stencilFailOp;

			if ( newStates.hasStencilDepthFailOp )
				currStencilState.front.depthFailOp = currStencilState.back.depthFailOp = newStates.stencilDepthFailOp;

			if ( newStates.hasStencilPassOp )
				currStencilState.front.passOp = currStencilState.back.passOp = newStates.stencilPassOp;

			dynamicState |= newStates.hasStencilCompareMask ? EPipelineDynamicState::StencilCompareMask : Default;
			dynamicState |= newStates.hasStencilReference   ? EPipelineDynamicState::StencilReference   : Default;
			dynamicState |= newStates.hasStencilWriteMask   ? EPipelineDynamicState::StencilWriteMask   : Default;
		}
	}
	
/*
=================================================
	SetupExtensions
=================================================
*/
	static void SetupExtensions (const VLogicalRenderPass &logicalRP, INOUT EPipelineDynamicState &dynamicState)
	{
		if ( logicalRP.HasShadingRateImage() )
			dynamicState |= EPipelineDynamicState::ShadingRatePalette;
	}

/*
=================================================
	_SetupShaderDebugging
=================================================
*/
	template <typename Pipeline>
	bool VPipelineCache::_SetupShaderDebugging (VResourceManagerThread &resMngr, VShaderDebugger &shaderDebugger, const Pipeline &ppln, uint debugModeIndex,
												OUT EShaderDebugMode &debugMode, OUT EShaderStages &debuggableShaders, OUT RawPipelineLayoutID &layoutId)
	{
		shaderDebugger.GetDebugModeInfo( debugModeIndex, OUT debugMode, OUT debuggableShaders );
		ASSERT( debugMode != Default );
		
		const VkShaderStageFlags	stages = VEnumCast( debuggableShaders );

		for (auto& sh : ppln._shaders)
		{
			if constexpr( IsSameTypes<VComputePipeline, Pipeline> )
			{
				if ( sh.debugMode != debugMode )
					continue;
			}
			else
			{
				if ( not EnumEq( stages, sh.stage ) or sh.debugMode != debugMode )
					continue;
			}

			shaderDebugger.SetShaderModule( debugModeIndex, sh.module );
		}

		RawDescriptorSetLayoutID	ds_layout;
		uint						binding;
		CHECK( shaderDebugger.GetDescriptorSetLayout( debugMode, debuggableShaders, OUT binding, OUT ds_layout ));

		layoutId = resMngr.ExtendPipelineLayout( layoutId, ds_layout, binding, DescriptorSetID{"dbgStorage"}, true );
		CHECK_ERR( layoutId );

		return true;
	}
	
/*
=================================================
	GetDebugModeHash
=================================================
*/
	ND_ inline uint  GetDebugModeHash (EShaderDebugMode mode, EShaderStages stages)
	{
		return (uint(stages) & 0xFFFFFF) | (uint(mode) << 24);
	}

/*
=================================================
	CreatePipelineInstance
=================================================
*/
	bool  VPipelineCache::CreatePipelineInstance (VResourceManagerThread		&resMngr,
												  Ptr<VShaderDebugger>			 shaderDebugger,
												  const VLogicalRenderPass		&logicalRP,
												  const VBaseDrawVerticesTask	&drawTask,
												  OUT VkPipeline				&outPipeline,
												  OUT VPipelineLayout const*	&outLayout)
	{
		CHECK_ERR( drawTask.pipeline and logicalRP.GetRenderPassID() );


		VDevice const&				dev			= resMngr.GetDevice();
		VGraphicsPipeline const*	gppln		= drawTask.pipeline;
		VRenderPass const*			render_pass	= resMngr.GetResource( logicalRP.GetRenderPassID() );
		EShaderDebugMode			dbg_mode	= Default;
		EShaderStages				dbg_stages	= Default;
		RawPipelineLayoutID			layout_id	= gppln->GetLayoutID();

		if ( drawTask.GetDebugModeIndex() != UMax ) {
			CHECK( _SetupShaderDebugging( resMngr, *shaderDebugger, *gppln, drawTask.GetDebugModeIndex(), OUT dbg_mode, OUT dbg_stages, OUT layout_id ));
		}

		// check topology
		CHECK_ERR(	uint(drawTask.topology) < gppln->_supportedTopology.size() and
					gppln->_supportedTopology[uint(drawTask.topology)] );

		VGraphicsPipeline::PipelineInstance		inst;
		inst.layoutId					= layout_id;
		inst.dynamicState				= EPipelineDynamicState::Viewport | EPipelineDynamicState::Scissor;
		inst.renderPassId				= logicalRP.GetRenderPassID();
		inst.subpassIndex				= uint8_t(logicalRP.GetSubpassIndex());
		inst.vertexInput				= drawTask.vertexInput;
		//inst.flags					= 0;	//pipelineFlags;	// TODO
		inst.viewportCount				= uint8_t(logicalRP.GetViewports().size());
		inst.debugMode					= GetDebugModeHash( dbg_mode, dbg_stages );
		inst.renderState.color			= logicalRP.GetColorState();
		inst.renderState.depth			= logicalRP.GetDepthState();
		inst.renderState.stencil		= logicalRP.GetStencilState();
		inst.renderState.rasterization	= logicalRP.GetRasterizationState();
		inst.renderState.multisample	= logicalRP.GetMultisampleState();

		inst.renderState.inputAssembly.topology			= drawTask.topology;
		inst.renderState.inputAssembly.primitiveRestart = drawTask.primitiveRestart;

		inst.vertexInput.ApplyAttribs( gppln->GetVertexAttribs() );
		OverrideColorStates( INOUT inst.renderState.color, drawTask.colorBuffers );
		OverrideDepthStencilStates( INOUT inst.renderState.depth, INOUT inst.renderState.stencil,
								    INOUT inst.renderState.rasterization, INOUT inst.dynamicState, drawTask.dynamicStates );
		SetupExtensions( logicalRP, INOUT inst.dynamicState );
		_ValidateRenderState( dev, INOUT inst.renderState, INOUT inst.dynamicState );

		inst.UpdateHash();
		
		outLayout = resMngr.GetResource( layout_id );

		// find existing instance
		{
			SHAREDLOCK( gppln->_instanceGuard );

			auto iter = gppln->_instances.find( inst );
			if ( iter != gppln->_instances.end() ) {
				outPipeline = iter->second;
				return true;
			}
		}


		// create new instance
		_ClearTemp();

		VkGraphicsPipelineCreateInfo			pipeline_info		= {};
		VkPipelineInputAssemblyStateCreateInfo	input_assembly_info	= {};
		VkPipelineColorBlendStateCreateInfo		blend_info			= {};
		VkPipelineDepthStencilStateCreateInfo	depth_stencil_info	= {};
		VkPipelineMultisampleStateCreateInfo	multisample_info	= {};
		VkPipelineRasterizationStateCreateInfo	rasterization_info	= {};
		VkPipelineTessellationStateCreateInfo	tessellation_info	= {};
		VkPipelineDynamicStateCreateInfo		dynamic_state_info	= {};
		VkPipelineVertexInputStateCreateInfo	vertex_input_info	= {};
		VkPipelineViewportStateCreateInfo		viewport_info		= {};

		_SetShaderStages( OUT _tempStages, INOUT _tempSpecialization, INOUT _tempSpecEntries, gppln->_shaders, dbg_mode, dbg_stages );
		_SetDynamicState( OUT dynamic_state_info, OUT _tempDynamicStates, inst.dynamicState );
		_SetColorBlendState( OUT blend_info, OUT _tempAttachments, inst.renderState.color, *render_pass, inst.subpassIndex );
		_SetMultisampleState( OUT multisample_info, inst.renderState.multisample );
		_SetTessellationState( OUT tessellation_info, gppln->_patchControlPoints );
		_SetDepthStencilState( OUT depth_stencil_info, inst.renderState.depth, inst.renderState.stencil );
		_SetRasterizationState( OUT rasterization_info, inst.renderState.rasterization );
		_SetupPipelineInputAssemblyState( OUT input_assembly_info, inst.renderState.inputAssembly );
		_SetVertexInputState( OUT vertex_input_info, OUT _tempVertexAttribs, OUT _tempVertexBinding, inst.vertexInput );
		_SetViewportState( OUT viewport_info, OUT _tempViewports, OUT _tempScissors,
						   uint2(1024, 1024), inst.viewportCount, inst.dynamicState );

		pipeline_info.sType					= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.pNext					= null;
		pipeline_info.flags					= 0; //inst.flags;
		pipeline_info.pInputAssemblyState	= &input_assembly_info;
		pipeline_info.pRasterizationState	= &rasterization_info;
		pipeline_info.pColorBlendState		= &blend_info;
		pipeline_info.pDepthStencilState	= &depth_stencil_info;
		pipeline_info.pMultisampleState		= &multisample_info;
		pipeline_info.pTessellationState	= (gppln->_patchControlPoints > 0 ? &tessellation_info : null);
		pipeline_info.pVertexInputState		= &vertex_input_info;
		pipeline_info.pDynamicState			= (_tempDynamicStates.empty() ? null : &dynamic_state_info);
		pipeline_info.basePipelineIndex		= -1;
		pipeline_info.basePipelineHandle	= VK_NULL_HANDLE;
		pipeline_info.layout				= outLayout->Handle();
		pipeline_info.stageCount			= uint(_tempStages.size());
		pipeline_info.pStages				= _tempStages.data();
		pipeline_info.renderPass			= render_pass->Handle();
		pipeline_info.subpass				= inst.subpassIndex;
		
		if ( not rasterization_info.rasterizerDiscardEnable )
		{
			pipeline_info.pViewportState		= &viewport_info;
		}else{
			pipeline_info.pViewportState		= null;
			pipeline_info.pMultisampleState		= null;
			pipeline_info.pDepthStencilState	= null;
			pipeline_info.pColorBlendState		= null;
		}

		outPipeline = {};
		VK_CHECK( dev.vkCreateGraphicsPipelines( dev.GetVkDevice(), _pipelinesCache, 1, &pipeline_info, null, OUT &outPipeline ));

		resMngr.EditStatistic().newGraphicsPipelineCount++;
		
		// try to insert new instance
		{
			EXLOCK( gppln->_instanceGuard );

			auto[iter, inserted] = gppln->_instances.insert({ std::move(inst), outPipeline });
		
			if ( not inserted )
			{
				dev.vkDestroyPipeline( dev.GetVkDevice(), outPipeline, null );

				outPipeline = iter->second;
				return true;
			}
		}
		
		CHECK( resMngr.AcquireResource( layout_id ));
		return true;
	}
/*
=================================================
	CreatePipelineInstance
=================================================
*/
	bool  VPipelineCache::CreatePipelineInstance (VResourceManagerThread		&resMngr,
												  Ptr<VShaderDebugger>			 shaderDebugger,
												  const VLogicalRenderPass		&logicalRP,
												  const VBaseDrawMeshes			&drawTask,
												  OUT VkPipeline				&outPipeline,
												  OUT VPipelineLayout const*	&outLayout)
	{
		CHECK_ERR( resMngr.GetDevice().IsMeshShaderEnabled() );
		CHECK_ERR( drawTask.pipeline and logicalRP.GetRenderPassID() );

		VDevice const&			dev			= resMngr.GetDevice();
		VMeshPipeline const*	mppln		= drawTask.pipeline;
		VRenderPass const*		render_pass	= resMngr.GetResource( logicalRP.GetRenderPassID() );
		EShaderDebugMode		dbg_mode	= Default;
		EShaderStages			dbg_stages	= Default;
		RawPipelineLayoutID		layout_id	= mppln->GetLayoutID();

		if ( drawTask.GetDebugModeIndex() != UMax ) {
			CHECK( _SetupShaderDebugging( resMngr, *shaderDebugger, *mppln, drawTask.GetDebugModeIndex(), OUT dbg_mode, OUT dbg_stages, OUT layout_id ));
		}

		VMeshPipeline::PipelineInstance		inst;
		inst.layoutId					= layout_id;
		inst.dynamicState				= EPipelineDynamicState::Viewport | EPipelineDynamicState::Scissor;
		inst.renderPassId				= logicalRP.GetRenderPassID();
		inst.subpassIndex				= uint8_t(logicalRP.GetSubpassIndex());
		//inst.flags						= 0;	//pipelineFlags;	// TODO
		inst.viewportCount				= uint8_t(logicalRP.GetViewports().size());
		inst.debugMode					= GetDebugModeHash( dbg_mode, dbg_stages );
		inst.renderState.color			= logicalRP.GetColorState();
		inst.renderState.depth			= logicalRP.GetDepthState();
		inst.renderState.stencil		= logicalRP.GetStencilState();
		inst.renderState.rasterization	= logicalRP.GetRasterizationState();
		inst.renderState.multisample	= logicalRP.GetMultisampleState();
		inst.renderState.inputAssembly.topology	= mppln->_topology;

		OverrideColorStates( INOUT inst.renderState.color, drawTask.colorBuffers );
		OverrideDepthStencilStates( INOUT inst.renderState.depth, INOUT inst.renderState.stencil,
								    INOUT inst.renderState.rasterization, INOUT inst.dynamicState, drawTask.dynamicStates );
		SetupExtensions( logicalRP, INOUT inst.dynamicState );
		_ValidateRenderState( dev, INOUT inst.renderState, INOUT inst.dynamicState );

		inst.UpdateHash();
		
		outLayout = resMngr.GetResource( layout_id );

		// find existing instance
		{
			SHAREDLOCK( mppln->_instanceGuard );

			auto iter = mppln->_instances.find( inst );
			if ( iter != mppln->_instances.end() ) {
				outPipeline = iter->second;
				return true;
			}
		}


		// create new instance
		_ClearTemp();

		VkGraphicsPipelineCreateInfo			pipeline_info		= {};
		VkPipelineInputAssemblyStateCreateInfo	input_assembly_info	= {};
		VkPipelineColorBlendStateCreateInfo		blend_info			= {};
		VkPipelineDepthStencilStateCreateInfo	depth_stencil_info	= {};
		VkPipelineMultisampleStateCreateInfo	multisample_info	= {};
		VkPipelineRasterizationStateCreateInfo	rasterization_info	= {};
		VkPipelineDynamicStateCreateInfo		dynamic_state_info	= {};
		VkPipelineVertexInputStateCreateInfo	vertex_input_info	= {};
		VkPipelineViewportStateCreateInfo		viewport_info		= {};

		_SetShaderStages( OUT _tempStages, INOUT _tempSpecialization, INOUT _tempSpecEntries, mppln->_shaders, dbg_mode, dbg_stages );
		_SetDynamicState( OUT dynamic_state_info, OUT _tempDynamicStates, inst.dynamicState );
		_SetColorBlendState( OUT blend_info, OUT _tempAttachments, inst.renderState.color, *render_pass, inst.subpassIndex );
		_SetMultisampleState( OUT multisample_info, inst.renderState.multisample );
		_SetDepthStencilState( OUT depth_stencil_info, inst.renderState.depth, inst.renderState.stencil );
		_SetRasterizationState( OUT rasterization_info, inst.renderState.rasterization );
		_SetupPipelineInputAssemblyState( OUT input_assembly_info, inst.renderState.inputAssembly );
		_SetViewportState( OUT viewport_info, OUT _tempViewports, OUT _tempScissors,
						   uint2(1024, 1024), inst.viewportCount, inst.dynamicState );

		vertex_input_info.sType	= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		pipeline_info.sType					= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.pNext					= null;
		pipeline_info.flags					= 0; //inst.flags;
		pipeline_info.pInputAssemblyState	= &input_assembly_info;
		pipeline_info.pRasterizationState	= &rasterization_info;
		pipeline_info.pColorBlendState		= &blend_info;
		pipeline_info.pDepthStencilState	= &depth_stencil_info;
		pipeline_info.pMultisampleState		= &multisample_info;
		pipeline_info.pVertexInputState		= &vertex_input_info;
		pipeline_info.pDynamicState			= (_tempDynamicStates.empty() ? null : &dynamic_state_info);
		pipeline_info.basePipelineIndex		= -1;
		pipeline_info.basePipelineHandle	= VK_NULL_HANDLE;
		pipeline_info.layout				= outLayout->Handle();
		pipeline_info.stageCount			= uint(_tempStages.size());
		pipeline_info.pStages				= _tempStages.data();
		pipeline_info.renderPass			= render_pass->Handle();
		pipeline_info.subpass				= inst.subpassIndex;
		
		if ( not rasterization_info.rasterizerDiscardEnable )
		{
			pipeline_info.pViewportState		= &viewport_info;
		}else{
			pipeline_info.pViewportState		= null;
			pipeline_info.pMultisampleState		= null;
			pipeline_info.pDepthStencilState	= null;
			pipeline_info.pColorBlendState		= null;
		}

		outPipeline = {};
		VK_CHECK( dev.vkCreateGraphicsPipelines( dev.GetVkDevice(), _pipelinesCache, 1, &pipeline_info, null, OUT &outPipeline ));
		
		resMngr.EditStatistic().newGraphicsPipelineCount++;
		
		// try to insert new instance
		{
			EXLOCK( mppln->_instanceGuard );

			auto[iter, inserted] = mppln->_instances.insert({ std::move(inst), outPipeline });
		
			if ( not inserted )
			{
				dev.vkDestroyPipeline( dev.GetVkDevice(), outPipeline, null );

				outPipeline = iter->second;
				return true;
			}
		}

		CHECK( resMngr.AcquireResource( layout_id ));
		return true;
	}

/*
=================================================
	CreatePipelineInstance
=================================================
*/
	bool  VPipelineCache::CreatePipelineInstance (VResourceManagerThread		&resMngr,
												  Ptr<VShaderDebugger>			 shaderDebugger,
												  const VComputePipeline		&cppln,
												  const Optional<uint3>			&localGroupSize,
												  VkPipelineCreateFlags			 pipelineFlags,
												  uint							 debugModeIndex,
												  OUT VkPipeline				&outPipeline,
												  OUT VPipelineLayout const*	&outLayout)
	{
		VDevice const &		dev			= resMngr.GetDevice();
		EShaderDebugMode	dbg_mode	= Default;
		EShaderStages		dbg_stages	= Default;
		RawPipelineLayoutID	layout_id	= cppln.GetLayoutID();

		if ( debugModeIndex != UMax ) {
			CHECK( _SetupShaderDebugging( resMngr, *shaderDebugger, cppln, debugModeIndex, OUT dbg_mode, OUT dbg_stages, OUT layout_id ));
		}

		VComputePipeline::PipelineInstance		inst;
		inst.layoutId		= layout_id;
		inst.flags			= pipelineFlags;
		inst.localGroupSize	= localGroupSize.value_or( cppln._defaultLocalGroupSize );
		inst.localGroupSize = { cppln._localSizeSpec.x != ComputePipelineDesc::UNDEFINED_SPECIALIZATION ? inst.localGroupSize.x : cppln._defaultLocalGroupSize.x,
								cppln._localSizeSpec.y != ComputePipelineDesc::UNDEFINED_SPECIALIZATION ? inst.localGroupSize.y : cppln._defaultLocalGroupSize.y,
								cppln._localSizeSpec.z != ComputePipelineDesc::UNDEFINED_SPECIALIZATION ? inst.localGroupSize.z : cppln._defaultLocalGroupSize.z };
		inst.debugMode		= GetDebugModeHash( dbg_mode, dbg_stages );
		inst.UpdateHash();
		
		outLayout = resMngr.GetResource( layout_id );

		// find existing instance
		{
			SHAREDLOCK( cppln._instanceGuard );

			auto iter = cppln._instances.find( inst );
			if ( iter != cppln._instances.end() ) {
				outPipeline = iter->second;
				return true;
			}
		}


		// create new instance
		_ClearTemp();

		VkSpecializationInfo			spec = {};
		VkComputePipelineCreateInfo		pipeline_info = {};

		pipeline_info.sType			= VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline_info.layout		= outLayout->Handle();
		pipeline_info.flags			= inst.flags;
		pipeline_info.stage.sType	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipeline_info.stage.flags	= 0;
		pipeline_info.stage.stage	= VK_SHADER_STAGE_COMPUTE_BIT;

		// find module with required debug mode
		for (auto& sh : cppln._shaders)
		{
			if ( sh.debugMode != dbg_mode )
				continue;
			
			pipeline_info.stage.module	= BitCast<VkShaderModule>( sh.module->GetData() );
			pipeline_info.stage.pName	= sh.module->GetEntry().data();
			break;
		}
		CHECK_ERR( pipeline_info.stage.module );

		_AddLocalGroupSizeSpecialization( OUT _tempSpecEntries, OUT _tempSpecData, cppln._localSizeSpec, inst.localGroupSize );

		if ( not _tempSpecEntries.empty() )
		{
			spec.mapEntryCount	= uint(_tempSpecEntries.size());
			spec.pMapEntries	= _tempSpecEntries.data();
			spec.dataSize		= size_t(ArraySizeOf( _tempSpecData ));
			spec.pData			= _tempSpecData.data();

			pipeline_info.stage.pSpecializationInfo	= &spec;
		}

		outPipeline = {};
		VK_CHECK( dev.vkCreateComputePipelines( dev.GetVkDevice(), _pipelinesCache, 1, &pipeline_info, null, OUT &outPipeline ));
		
		resMngr.EditStatistic().newComputePipelineCount++;
		
		// try to insert new instance
		{
			EXLOCK( cppln._instanceGuard );

			auto[iter, inserted] = cppln._instances.insert({ std::move(inst), outPipeline });
		
			if ( not inserted )
			{
				dev.vkDestroyPipeline( dev.GetVkDevice(), outPipeline, null );

				outPipeline = iter->second;
				return true;
			}
		}

		CHECK( resMngr.AcquireResource( layout_id ));
		return true;
	}

/*
=================================================
	InitShaderTable
=================================================
*/
	bool VPipelineCache::InitShaderTable (VFrameGraphThread				&fg,
										  RawRTPipelineID				 pipelineId,
										  VLocalRTScene const*			 rtScene,
										  const RayGenShader			&rayGenShader,
										  ArrayView< RTShaderGroup >	 shaderGroups,
										  uint							 maxRecursionDepth,
										  INOUT VRayTracingShaderTable	&shaderTable,
										  OUT BufferCopyRegions_t		&copyRegions)
	{
		auto&			dev				= fg.GetDevice();
		auto&			res_mngr		= *fg.GetResourceManager();
		const BytesU	handle_size		{ dev.GetDeviceRayTracingProperties().shaderGroupHandleSize };
		const BytesU	alignment		{ dev.GetDeviceRayTracingProperties().shaderGroupBaseAlignment };
		auto*			ppln			= res_mngr.GetResource( pipelineId );
		const uint		geom_stride		= rtScene->HitShadersPerInstance();
		const uint		max_hit_shaders	= rtScene->MaxHitShaderCount();

		CHECK_ERR( ppln );

		FixedArray<EShaderDebugMode, 4>  debug_modes { EShaderDebugMode::None };

		if ( FG_EnableShaderDebugging )
			debug_modes.push_back( EShaderDebugMode::Trace );


		uint	miss_shader_count		= 0;
		uint	hit_shader_count		= 0;
		uint	callable_shader_count	= 0;
		
		_ClearTemp();
		_rtShaderSpecs.emplace_back( SpecializationID{"sbtRecordStride"}, 0u );
		_tempSpecData.push_back( geom_stride );

		for (auto& shader : shaderGroups)
		{
			_tempShaderGraphMap.insert({ &shader, uint(_tempShaderGraphMap.size())+1 });

			switch ( shader.type )
			{
				case EGroupType::MissShader :
					miss_shader_count = Max( shader.offset+1, miss_shader_count );
					break;

				case EGroupType::CallableShader :
					++callable_shader_count;
					break;

				case EGroupType::TriangleHitShader :
				case EGroupType::ProceduralHitShader :
					++hit_shader_count;
					break;
			}
		}
		_tempShaderGroups.resize( 1 + _tempShaderGraphMap.size() );
		_tempSpecialization.reserve( 1 + miss_shader_count + hit_shader_count );
		_tempSpecEntries.reserve( _tempSpecialization.size() * _rtShaderSpecs.size() );
		
		// setup offsets
		BytesU		offset {0};
		shaderTable._rayGenOffset	= offset;
		shaderTable._rayMissOffset	= (offset += handle_size);
		shaderTable._rayHitOffset	= (offset += (handle_size * miss_shader_count));
		shaderTable._callableOffset	= (offset += (handle_size * hit_shader_count));
		shaderTable._blockSize		= (offset += (handle_size * callable_shader_count));
		shaderTable._rayMissStride	= Bytes<uint16_t>{ handle_size };
		shaderTable._rayHitStride	= Bytes<uint16_t>{ handle_size };
		shaderTable._callableStride	= Bytes<uint16_t>{ handle_size };

		const BytesU	req_size	= AlignToLarger( shaderTable._blockSize, alignment ) * debug_modes.size();

		// recreate buffer
		if ( shaderTable._bufferId )
		{
			if ( fg.GetDescription( shaderTable._bufferId ).size < req_size )
				fg.ReleaseResource( INOUT shaderTable._bufferId );
		}
		
		if ( not shaderTable._bufferId )
		{
			shaderTable._bufferId = fg.CreateBuffer( BufferDesc{ req_size, EBufferUsage::TransferDst | EBufferUsage::RayTracing },
													 Default, shaderTable.GetDebugName() );
			CHECK_ERR( shaderTable._bufferId );
		}

		// acquire pipeline
		if ( shaderTable._pipelineId )
			fg.ReleaseResource( INOUT shaderTable._pipelineId );

		CHECK( res_mngr.AcquireResource( pipelineId ));
		shaderTable._pipelineId = RTPipelineID{pipelineId};

		// destroy old pipelines
		for (auto& table : shaderTable._tables)
		{
			if ( table.layoutId )
				res_mngr.GetUnassignIDs().emplace_back( table.layoutId.Release() );

			if ( table.pipeline )
				res_mngr.GetReadyToDeleteQueue().emplace_back( VK_OBJECT_TYPE_PIPELINE, uint64_t(table.pipeline) );
		}
		shaderTable._tables.clear();


		// create shader table for each debug mode
		offset = 0_b;
		for (auto mode : debug_modes)
		{
			// create ray-gen shader
			{
				auto&	stage = _tempStages.emplace_back();
				CHECK_ERR( _InitShaderStage( ppln, rayGenShader.shaderId, mode, OUT stage ));

				auto&	group_ci			= _tempShaderGroups[0];
				group_ci.sType				= VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
				group_ci.pNext				= null;
				group_ci.closestHitShader	= VK_SHADER_UNUSED_NV;
				group_ci.anyHitShader		= VK_SHADER_UNUSED_NV;
				group_ci.intersectionShader	= VK_SHADER_UNUSED_NV;
				group_ci.type				= VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
				group_ci.generalShader		= uint(_tempStages.size()-1);
			}
			
			// create miss & hit shaders
			for (auto& iter : _tempShaderGraphMap)
			{
				CHECK_ERR( _GetShaderGroup( ppln, *iter.first, mode, OUT _tempShaderGroups[iter.second] ));
			}
			
			// acquire pipeline layout
			RawPipelineLayoutID		layout_id = ppln->_baseLayoutId.Get();

			if ( mode != Default )
			{
				RawDescriptorSetLayoutID	ds_layout;
				uint						binding;
				CHECK( fg.CreateShaderDebugger()->GetDescriptorSetLayout( mode, EShaderStages::AllRayTracing, OUT binding, OUT ds_layout ));

				layout_id = res_mngr.ExtendPipelineLayout( layout_id, ds_layout, binding, DescriptorSetID{"dbgStorage"}, true );
				CHECK_ERR( layout_id );
			}

			VRayTracingShaderTable::ShaderTable&	table = shaderTable._tables.emplace_back();
			table.bufferOffset	= offset;
			table.mode			= mode;

			// create pipeline
			VkRayTracingPipelineCreateInfoNV 	pipeline_info = {};
			pipeline_info.sType					= VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
			pipeline_info.flags					= 0;
			pipeline_info.stageCount			= uint(_tempStages.size());
			pipeline_info.pStages				= _tempStages.data();
			pipeline_info.groupCount			= uint(_tempShaderGroups.size());
			pipeline_info.pGroups				= _tempShaderGroups.data();
			pipeline_info.maxRecursionDepth		= maxRecursionDepth;
			pipeline_info.layout				= res_mngr.GetResource( layout_id )->Handle();
			pipeline_info.basePipelineIndex		= -1;
			pipeline_info.basePipelineHandle	= VK_NULL_HANDLE;

			VK_CHECK( dev.vkCreateRayTracingPipelinesNV( dev.GetVkDevice(), _pipelinesCache, 1, &pipeline_info, null, OUT &table.pipeline ));
			res_mngr.EditStatistic().newRayTracingPipelineCount++;
			
			CHECK( res_mngr.AcquireResource( layout_id ));
			table.layoutId = PipelineLayoutID{layout_id};
			

			// 
			RawBufferID		staging_buffer;
			BytesU			buf_offset, buf_size;
			void *			mapped_ptr = null;
		
			CHECK_ERR( fg.GetStagingBufferManager()->GetWritableBuffer( shaderTable._blockSize, shaderTable._blockSize,
																	    OUT staging_buffer, OUT buf_offset, OUT buf_size, OUT mapped_ptr ));
			DEBUG_ONLY( memset( OUT mapped_ptr, 0xAE, size_t(buf_size) ));
	
			// ray-gen shader
			VK_CALL( dev.vkGetRayTracingShaderGroupHandlesNV( dev.GetVkDevice(), table.pipeline, 0, 1, size_t(handle_size), OUT mapped_ptr + shaderTable._rayGenOffset ));

			uint	callable_shader_index = 0;

			for (auto& shader : shaderGroups)
			{
				switch ( shader.type )
				{
					case EGroupType::MissShader :
					{
						BytesU	dst_off	= shaderTable._rayMissOffset + handle_size * shader.offset; 
						auto	group	= _tempShaderGraphMap.find( &shader );
						CHECK_ERR( group != _tempShaderGraphMap.end() );
						
						VK_CALL( dev.vkGetRayTracingShaderGroupHandlesNV( dev.GetVkDevice(), table.pipeline, group->second, 1, size_t(handle_size), OUT mapped_ptr + dst_off ));
						break;
					}

					case EGroupType::CallableShader :
					{
						BytesU	dst_off	= shaderTable._callableOffset + handle_size * callable_shader_index++;
						auto	group	= _tempShaderGraphMap.find( &shader );
						CHECK_ERR( group != _tempShaderGraphMap.end() );
						
						VK_CALL( dev.vkGetRayTracingShaderGroupHandlesNV( dev.GetVkDevice(), table.pipeline, group->second, 1, size_t(handle_size), OUT mapped_ptr + dst_off ));
						break;
					}

					case EGroupType::TriangleHitShader :
					case EGroupType::ProceduralHitShader :
					{
						const auto*	inst	= rtScene->FindInstance( shader.instanceId );
						auto		group	= _tempShaderGraphMap.find( &shader );
						CHECK_ERR( inst and group != _tempShaderGraphMap.end() );

						const auto*	geom	= res_mngr.GetResource( inst->geometry.Get() );
						size_t		index	= geom->GetGeometryIndex( shader.geometryId ) * geom_stride + shader.offset;
						BytesU		dst_off	= shaderTable._rayHitOffset + handle_size * index;

						CHECK( shader.offset < geom_stride );
						CHECK( index < max_hit_shaders );
						
						VK_CALL( dev.vkGetRayTracingShaderGroupHandlesNV( dev.GetVkDevice(), table.pipeline, group->second, 1, size_t(handle_size), OUT mapped_ptr + dst_off ));
						break;
					}
				}
			}
			
			// check for uninitialized shader handles
			DEBUG_ONLY(
			for (BytesU pos = 0_b; pos < buf_size; pos += handle_size)
			{
				uint	matched = 0;
				for (BytesU i = 0_b; i < handle_size; i += 1_b) {
					matched += uint(*Cast<uint8_t>(mapped_ptr + i + pos) == 0xAE);
				}
				ASSERT( matched < handle_size );
			})

			// copy from staging buffer to shader binding table
			auto&	copy	= copyRegions.emplace_back();
			copy.srcBuffer			= res_mngr.ToLocal( staging_buffer );
			copy.dstBuffer			= res_mngr.ToLocal( shaderTable._bufferId.Get() );
			copy.region.srcOffset	= VkDeviceSize(buf_offset);
			copy.region.dstOffset	= VkDeviceSize(table.bufferOffset);
			copy.region.size		= VkDeviceSize(shaderTable._blockSize);

			offset = AlignToLarger( offset + shaderTable._blockSize, alignment );

			// clear temporary arrays
			_tempSpecEntries.clear();
			_tempSpecialization.clear();
			_tempStages.clear();
			// keep '_tempShaderGroups', '_tempShaderGraphMap', '_rtShaderSpecs'
		}

		return true;
	}
	
/*
=================================================
	_InitShaderStage
=================================================
*/
	bool VPipelineCache::_InitShaderStage (const VRayTracingPipeline *ppln, const RTShaderID &id, EShaderDebugMode mode,
											OUT VkPipelineShaderStageCreateInfo &stage)
	{
		ASSERT( id.IsDefined() );

		size_t	best_match	= UMax;

		// find suitable shader module
		for (size_t pos = BinarySearch( ppln->_shaders, id );
				pos < ppln->_shaders.size() and ppln->_shaders[pos].shaderId == id;
				++pos)
		{
			auto&	shader = ppln->_shaders[pos];

			if ( shader.debugMode == mode ) {
				best_match = pos;
				break;
			}
			if ( best_match == UMax and shader.debugMode == Default )
				best_match = pos;
		}
			
		if ( best_match == UMax )
			return false;
			
		auto&			shader		= ppln->_shaders[best_match];
		const size_t	entry_count	= _tempSpecEntries.size();

		stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.pNext  = null;
		stage.flags  = 0;
		stage.module = BitCast<VkShaderModule>( shader.module->GetData() );
		stage.pName  = shader.module->GetEntry().data();
		stage.stage  = shader.stage;
		stage.pSpecializationInfo = null;

		// set specialization constants
		if ( shader.stage == VK_SHADER_STAGE_RAYGEN_BIT_NV		or
			 shader.stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV	or
			 shader.stage == VK_SHADER_STAGE_MISS_BIT_NV )
		{
			for (auto& spec : _rtShaderSpecs)
			{
				auto	iter = shader.specConstants.find( spec.id );
				if ( iter != shader.specConstants.end() )
				{
					VkSpecializationMapEntry&	entry = _tempSpecEntries.emplace_back();
					entry.constantID	= iter->second;
					entry.offset		= spec.offset;
					entry.size			= sizeof(uint);
				}
			}
		}
		
		if ( _tempSpecEntries.size() > entry_count )
		{
			VkSpecializationInfo&	spec_info = _tempSpecialization.emplace_back();
			spec_info.mapEntryCount		= uint(_tempSpecEntries.size() - entry_count);
			spec_info.pMapEntries		= _tempSpecEntries.data() + entry_count;
			spec_info.dataSize			= size_t(ArraySizeOf( _tempSpecData ));
			spec_info.pData				= _tempSpecData.data();
			stage.pSpecializationInfo	= &spec_info;
		}

		return true;
	}
	
/*
=================================================
	_GetShaderGroup
=================================================
*/
	bool VPipelineCache::_GetShaderGroup (const VRayTracingPipeline *ppln, const RTShaderGroup &group, EShaderDebugMode mode,
											OUT VkRayTracingShaderGroupCreateInfoNV &group_ci)
	{
		group_ci.sType				= VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
		group_ci.pNext				= null;
		group_ci.generalShader		= VK_SHADER_UNUSED_NV;
		group_ci.closestHitShader	= VK_SHADER_UNUSED_NV;
		group_ci.anyHitShader		= VK_SHADER_UNUSED_NV;
		group_ci.intersectionShader	= VK_SHADER_UNUSED_NV;

		ENABLE_ENUM_CHECKS();
		switch ( group.type )
		{
			case EGroupType::MissShader :
			{
				auto&	stage = _tempStages.emplace_back();
				CHECK_ERR( _InitShaderStage( ppln, group.mainShader, mode, OUT stage ));

				group_ci.type			= VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
				group_ci.generalShader	= uint(_tempStages.size()-1);
				return true;
			}

			case EGroupType::TriangleHitShader :
			{
				group_ci.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
				if ( group.mainShader.IsDefined() )
				{
					auto&	stage = _tempStages.emplace_back();
					CHECK_ERR( _InitShaderStage( ppln, group.mainShader, mode, OUT stage ));
					group_ci.closestHitShader = uint(_tempStages.size()-1);
				}
				if ( group.anyHitShader.IsDefined() )
				{
					auto&	stage = _tempStages.emplace_back();
					CHECK_ERR( _InitShaderStage( ppln, group.anyHitShader, mode, OUT stage ));
					group_ci.anyHitShader = uint(_tempStages.size()-1);
				}
				return true;
			}

			case EGroupType::ProceduralHitShader :
			{
				group_ci.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV;
				{
					auto&	stage = _tempStages.emplace_back();
					CHECK_ERR( _InitShaderStage( ppln, group.intersectionShader, mode, OUT stage ));
					group_ci.intersectionShader = uint(_tempStages.size()-1);
				}
				if ( group.anyHitShader.IsDefined() )
				{
					auto&	stage = _tempStages.emplace_back();
					CHECK_ERR( _InitShaderStage( ppln, group.anyHitShader, mode, OUT stage ));
					group_ci.anyHitShader = uint(_tempStages.size()-1);
				}
				if ( group.mainShader.IsDefined() )
				{
					auto&	stage = _tempStages.emplace_back();
					CHECK_ERR( _InitShaderStage( ppln, group.mainShader, mode, OUT stage ));
					group_ci.closestHitShader = uint(_tempStages.size()-1);
				}
				return true;
			}

			case EGroupType::CallableShader :
			case EGroupType::Unknown : break;
		}
		DISABLE_ENUM_CHECKS();
		return false;
	}

/*
=================================================
	_SetShaderStages
=================================================
*/
	void VPipelineCache::_SetShaderStages (OUT ShaderStages_t &stages,
										   INOUT Specializations_t &,
										   INOUT SpecializationEntries_t &,
										   ArrayView< ShaderModule_t > shaders,
										   EShaderDebugMode debugMode,
										   EShaderStages debuggableShaders) const
	{
		const VkShaderStageFlags	debuggable_stages	= VEnumCast( debuggableShaders );
		VkShaderStageFlags			exist_stages		= 0;
		VkShaderStageFlags			used_stages			= 0;

		for (auto& sh : shaders)
		{
			ASSERT( sh.module );

			exist_stages |= sh.stage;

			if ( (EnumEq( debuggable_stages, sh.stage ) and sh.debugMode != debugMode) or
				 (not EnumEq( debuggable_stages, sh.stage ) and sh.debugMode != Default) )
				continue;

			used_stages |= sh.stage;

			VkPipelineShaderStageCreateInfo	info = {};
			info.sType	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			info.pNext	= null;
			info.flags	= 0;
			info.module	= BitCast<VkShaderModule>( sh.module->GetData() );
			info.pName	= sh.module->GetEntry().data();
			info.stage	= sh.stage;
			info.pSpecializationInfo = null;	// TODO

			stages.push_back( info );
		}

		// check if all shader stages added
		ASSERT( exist_stages == used_stages );
	}

/*
=================================================
	_SetDynamicState
=================================================
*/
	void VPipelineCache::_SetDynamicState (OUT VkPipelineDynamicStateCreateInfo &outState,
										   OUT DynamicStates_t &states,
										   EPipelineDynamicState inState) const
	{
		outState.sType	= VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		outState.pNext	= null;
		outState.flags	= 0;

		for (EPipelineDynamicState t = EPipelineDynamicState(1 << 0);
			 t < EPipelineDynamicState::_Last;
			 t = EPipelineDynamicState(uint(t) << 1)) 
		{
			if ( not EnumEq( inState, t ) )
				continue;

			states.push_back( VEnumCast( t ));
		}

		outState.dynamicStateCount	= uint(states.size());
		outState.pDynamicStates		= states.data();
	}
	
/*
=================================================
	_SetMultisampleState
=================================================
*/
	void VPipelineCache::_SetMultisampleState (OUT VkPipelineMultisampleStateCreateInfo &outState,
											   const RenderState::MultisampleState &inState) const
	{
		outState.sType					= VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		outState.pNext					= null;
		outState.flags					= 0;
		outState.rasterizationSamples	= VEnumCast( inState.samples );
		outState.sampleShadingEnable	= inState.sampleShading;
		outState.minSampleShading		= inState.minSampleShading;
		outState.pSampleMask			= inState.samples.IsEnabled() ? inState.sampleMask.data() : null;
		outState.alphaToCoverageEnable	= inState.alphaToCoverage;
		outState.alphaToOneEnable		= inState.alphaToOne;
	}
	
/*
=================================================
	_SetTessellationState
=================================================
*/
	void VPipelineCache::_SetTessellationState (OUT VkPipelineTessellationStateCreateInfo &outState,
												uint patchSize) const
	{
		outState.sType				= VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
		outState.pNext				= null;
		outState.flags				= 0;
		outState.patchControlPoints	= patchSize;
	}
	
/*
=================================================
	SetStencilOpState
=================================================
*/
	static void SetStencilOpState (OUT VkStencilOpState &outState, const RenderState::StencilFaceState &inState)
	{
		outState.failOp			= VEnumCast( inState.failOp );
		outState.passOp			= VEnumCast( inState.passOp );
		outState.depthFailOp	= VEnumCast( inState.depthFailOp );
		outState.compareOp		= VEnumCast( inState.compareOp );
		outState.compareMask	= inState.compareMask;
		outState.writeMask		= inState.writeMask;
		outState.reference		= inState.reference;
	}

/*
=================================================
	_SetDepthStencilState
=================================================
*/
	void VPipelineCache::_SetDepthStencilState (OUT VkPipelineDepthStencilStateCreateInfo &outState,
												const RenderState::DepthBufferState &depth,
												const RenderState::StencilBufferState &stencil) const
	{
		outState.sType					= VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		outState.pNext					= null;
		outState.flags					= 0;

		// depth
		outState.depthTestEnable		= depth.test;
		outState.depthWriteEnable		= depth.write;
		outState.depthCompareOp			= VEnumCast( depth.compareOp );
		outState.depthBoundsTestEnable	= depth.boundsEnabled;
		outState.minDepthBounds			= depth.bounds.x;
		outState.maxDepthBounds			= depth.bounds.y;
		
		// stencil
		outState.stencilTestEnable		= stencil.enabled;
		SetStencilOpState( OUT outState.front, stencil.front );
		SetStencilOpState( OUT outState.back,  stencil.back  );
	}
	
/*
=================================================
	_SetRasterizationState
=================================================
*/
	void VPipelineCache::_SetRasterizationState (OUT VkPipelineRasterizationStateCreateInfo &outState,
												 const RenderState::RasterizationState &inState) const
	{
		outState.sType						= VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		outState.pNext						= null;
		outState.flags						= 0;
		outState.polygonMode				= VEnumCast( inState.polygonMode );
		outState.lineWidth					= inState.lineWidth;
		outState.depthBiasConstantFactor	= inState.depthBiasConstFactor;
		outState.depthBiasClamp				= inState.depthBiasClamp;
		outState.depthBiasSlopeFactor		= inState.depthBiasSlopeFactor;
		outState.depthBiasEnable			= inState.depthBias;
		outState.depthClampEnable			= inState.depthClamp;
		outState.rasterizerDiscardEnable	= inState.rasterizerDiscard;
		outState.frontFace					= inState.frontFaceCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
		outState.cullMode					= VEnumCast( inState.cullMode );
	}

/*
=================================================
	_SetupPipelineInputAssemblyState
=================================================
*/
	void VPipelineCache::_SetupPipelineInputAssemblyState (OUT VkPipelineInputAssemblyStateCreateInfo &outState,
														   const RenderState::InputAssemblyState &inState) const
	{
		outState.sType					= VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		outState.pNext					= null;
		outState.flags					= 0;
		outState.topology				= VEnumCast( inState.topology );
		outState.primitiveRestartEnable	= inState.primitiveRestart;
	}

/*
=================================================
	_SetVertexInputState
=================================================
*/
	void VPipelineCache::_SetVertexInputState (OUT VkPipelineVertexInputStateCreateInfo &outState,
											   OUT VertexInputAttributes_t &vertexAttribs,
											   OUT VertexInputBindings_t &vertexBinding,
											   const VertexInputState &inState) const
	{
		outState.sType	= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		outState.pNext	= null;
		outState.flags	= 0;

		for (auto& src : inState.Vertices())
		{
			VkVertexInputAttributeDescription	dst = {};

			ASSERT( src.second.index != UMax );

			dst.binding		= src.second.bufferBinding;
			dst.format		= VEnumCast( src.second.type );
			dst.location	= src.second.index;
			dst.offset		= uint(src.second.offset);

			vertexAttribs.push_back( std::move(dst) );
		}

		for (auto& src : inState.BufferBindings())
		{
			VkVertexInputBindingDescription	dst = {};

			dst.binding		= src.second.index;
			dst.inputRate	= VEnumCast( src.second.rate );
			dst.stride		= uint(src.second.stride);

			vertexBinding.push_back( dst );
		}

		// if pipeline has attributes then buffer binding must be defined too
		CHECK( vertexAttribs.empty() == vertexBinding.empty() );

		outState.pVertexAttributeDescriptions		= vertexAttribs.data();
		outState.vertexAttributeDescriptionCount	= uint(vertexAttribs.size());

		outState.pVertexBindingDescriptions			= vertexBinding.data();
		outState.vertexBindingDescriptionCount		= uint(vertexBinding.size());
	}

/*
=================================================
	_SetVertexInputState
=================================================
*/
	void VPipelineCache::_SetViewportState (OUT VkPipelineViewportStateCreateInfo &outState,
											OUT Viewports_t &tmpViewports,
											OUT Scissors_t &tmpScissors,
											const uint2 &viewportSize,
											const uint viewportCount,
											EPipelineDynamicState dynamicStates) const
	{
		tmpViewports.resize( viewportCount );
		tmpScissors.resize( viewportCount );

		for (uint i = 0; i < viewportCount; ++i)
		{
			tmpViewports[i] = VkViewport{ 0, 0, float(viewportSize.x), float(viewportSize.y), 0.0f, 1.0f };
			tmpScissors[i]	= VkRect2D{ VkOffset2D{ 0, 0 }, VkExtent2D{ viewportSize.x, viewportSize.y } };
		}

		outState.sType			= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		outState.pNext			= null;
		outState.flags			= 0;
		outState.pViewports		= EnumEq( dynamicStates, EPipelineDynamicState::Viewport ) ? null : tmpViewports.data();
		outState.viewportCount	= uint(tmpViewports.size());
		outState.pScissors		= EnumEq( dynamicStates, EPipelineDynamicState::Scissor ) ? null : tmpScissors.data();
		outState.scissorCount	= uint(tmpScissors.size());
	}
	
/*
=================================================
	SetColorBlendAttachmentState
=================================================
*/
	static void SetColorBlendAttachmentState (OUT VkPipelineColorBlendAttachmentState &outState,
											  const RenderState::ColorBuffer &inState,
											  const bool logicOpEnabled)
	{
		outState.blendEnable			= logicOpEnabled ? false : inState.blend;
		outState.srcColorBlendFactor	= VEnumCast( inState.srcBlendFactor.color );
		outState.srcAlphaBlendFactor	= VEnumCast( inState.srcBlendFactor.alpha );
		outState.dstColorBlendFactor	= VEnumCast( inState.dstBlendFactor.color );
		outState.dstAlphaBlendFactor	= VEnumCast( inState.dstBlendFactor.alpha );
		outState.colorBlendOp			= VEnumCast( inState.blendOp.color );
		outState.alphaBlendOp			= VEnumCast( inState.blendOp.alpha );
		outState.colorWriteMask			= (inState.colorMask.x ? VK_COLOR_COMPONENT_R_BIT : 0) |
										  (inState.colorMask.y ? VK_COLOR_COMPONENT_G_BIT : 0) |
										  (inState.colorMask.z ? VK_COLOR_COMPONENT_B_BIT : 0) |
										  (inState.colorMask.w ? VK_COLOR_COMPONENT_A_BIT : 0);
	}

/*
=================================================
	_SetColorBlendState
=================================================
*/
	void VPipelineCache::_SetColorBlendState (OUT VkPipelineColorBlendStateCreateInfo &outState,
											  OUT ColorAttachments_t &attachments,
											  const RenderState::ColorBuffersState &inState,
											  const VRenderPass &renderPass,
											  const uint subpassIndex) const
	{
		ASSERT( subpassIndex < renderPass.GetCreateInfo().subpassCount );

		const bool					logic_op_enabled	= ( inState.logicOp != ELogicOp::None );
		const VkSubpassDescription&	subpass				= renderPass.GetCreateInfo().pSubpasses[ subpassIndex ];

		for (size_t i = 0; i < subpass.colorAttachmentCount; ++i)
		{
			VkPipelineColorBlendAttachmentState		color_state = {};
			color_state.colorWriteMask	= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
										  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			attachments.push_back( color_state );
		}

		for (auto& cb : inState.buffers)
		{
			ASSERT( cb.first.IsDefined() );

			uint	index;
			CHECK( renderPass.GetColorAttachmentIndex( cb.first, OUT index ) );

			SetColorBlendAttachmentState( attachments[index], cb.second, logic_op_enabled );
		}

		outState.sType				= VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		outState.pNext				= null;
		outState.flags				= 0;
		outState.attachmentCount	= uint(attachments.size());
		outState.pAttachments		= attachments.empty() ? null : attachments.data();
		outState.logicOpEnable		= logic_op_enabled;
		outState.logicOp			= logic_op_enabled ? VEnumCast( inState.logicOp ) : VK_LOGIC_OP_CLEAR;

		outState.blendConstants[0] = inState.blendColor.r;
		outState.blendConstants[1] = inState.blendColor.g;
		outState.blendConstants[2] = inState.blendColor.b;
		outState.blendConstants[3] = inState.blendColor.a;
	}
	
/*
=================================================
	_ValidateRenderState
=================================================
*/
	void VPipelineCache::_ValidateRenderState (const VDevice &dev, INOUT RenderState &renderState, INOUT EPipelineDynamicState &dynamicStates) const
	{
		if ( renderState.rasterization.rasterizerDiscard )
		{
			renderState.color	= Default;
			renderState.depth	= Default;
			renderState.stencil	= Default;
			dynamicStates		= dynamicStates & ~(EPipelineDynamicState::RasterizerMask);
		}

		// reset to default state if dynamic state enabled.
		// it is needed for correct hash calculation.
		for (EPipelineDynamicState t = EPipelineDynamicState(1 << 0);
			 t < EPipelineDynamicState::_Last;
			 t = EPipelineDynamicState(uint(t) << 1)) 
		{
			if ( not EnumEq( dynamicStates, t ) )
				continue;

			ENABLE_ENUM_CHECKS();
			switch ( t )
			{
				case EPipelineDynamicState::Viewport :
					break;
				case EPipelineDynamicState::Scissor :
					break;

				//case EPipelineDynamicState::LineWidth :
				//	renderState.rasterization.lineWidth = 1.0f;
				//	break;

				//case EPipelineDynamicState::DepthBias :
				//	ASSERT( renderState.rasterization.depthBias );
				//	renderState.rasterization.depthBiasConstFactor	= 0.0f;
				//	renderState.rasterization.depthBiasClamp		= 0.0f;
				//	renderState.rasterization.depthBiasSlopeFactor	= 0.0f;
				//	break;

				//case EPipelineDynamicState::BlendConstants :
				//	renderState.color.blendColor = RGBA32f{ 1.0f };
				//	break;

				//case EPipelineDynamicState::DepthBounds :
				//	ASSERT( renderState.depth.boundsEnabled );
				//	renderState.depth.bounds = { 0.0f, 1.0f };
				//	break;

				case EPipelineDynamicState::StencilCompareMask :
					ASSERT( renderState.stencil.enabled ); 
					renderState.stencil.front.compareMask = UMax;
					renderState.stencil.back.compareMask  = UMax;
					break;

				case EPipelineDynamicState::StencilWriteMask :
					ASSERT( renderState.stencil.enabled ); 
					renderState.stencil.front.writeMask = UMax;
					renderState.stencil.back.writeMask  = UMax;
					break;

				case EPipelineDynamicState::StencilReference :
					ASSERT( renderState.stencil.enabled ); 
					renderState.stencil.front.reference = 0;
					renderState.stencil.back.reference  = 0;
					break;

				case EPipelineDynamicState::ShadingRatePalette :
					// do nothing
					break;

				case EPipelineDynamicState::Unknown :
				case EPipelineDynamicState::_Last :
				case EPipelineDynamicState::All :
					break;	// to shutup warnings

				default :
					ASSERT(false);	// not supported
					break;
			}
			DISABLE_ENUM_CHECKS();
		}

		// validate color buffer states
		{
			const bool	dual_src_blend	= dev.GetDeviceFeatures().dualSrcBlend;

			const auto	IsDualSrcBlendFactor = [] (EBlendFactor value) {
				switch ( value ) {
					case EBlendFactor::Src1Color :
					case EBlendFactor::OneMinusSrc1Color :
					case EBlendFactor::Src1Alpha :
					case EBlendFactor::OneMinusSrc1Alpha :
						return true;
				}
				return false;
			};

			for (auto& cb : renderState.color.buffers)
			{
				if ( not cb.second.blend )
				{	
					cb.second.srcBlendFactor = { EBlendFactor::One,		EBlendFactor::One };
					cb.second.dstBlendFactor = { EBlendFactor::Zero,	EBlendFactor::Zero };
					cb.second.blendOp		 = { EBlendOp::Add,			EBlendOp::Add };
				}
				else
				{
					if ( not dual_src_blend )
					{
						ASSERT( not IsDualSrcBlendFactor( cb.second.srcBlendFactor.color ) );
						ASSERT( not IsDualSrcBlendFactor( cb.second.srcBlendFactor.alpha ) );
						ASSERT( not IsDualSrcBlendFactor( cb.second.dstBlendFactor.color ) );
						ASSERT( not IsDualSrcBlendFactor( cb.second.dstBlendFactor.alpha ) );
					}
				}
			}
		}

		// validate depth states
		{
			if ( not renderState.depth.test )
				renderState.depth.compareOp = ECompareOp::LEqual;

			//if ( not renderState.depth.write )

			if ( not renderState.depth.boundsEnabled )
				renderState.depth.bounds = { 0.0f, 1.0f };
		}

		// validate stencil states
		{
			if ( not renderState.stencil.enabled )
			{
				renderState.stencil = Default;
			}
		}
	}

/*
=================================================
	_AddLocalGroupSizeSpecialization
=================================================
*/
	void VPipelineCache::_AddLocalGroupSizeSpecialization (INOUT SpecializationEntries_t &outEntries,
														   INOUT SpecializationData_t &outEntryData,
														   const uint3 &localSizeSpec,
														   const uint3 &localGroupSize) const
	{
		const bool3	has_spec = (localSizeSpec != uint3(ComputePipelineDesc::UNDEFINED_SPECIALIZATION));
		//ASSERT(Any( has_spec ));

		if ( has_spec.x )
		{
			VkSpecializationMapEntry	entry;
			entry.constantID	= localSizeSpec.x;
			entry.offset		= uint(ArraySizeOf(outEntryData));
			entry.size			= sizeof(uint);
			outEntries.push_back( entry );
			outEntryData.push_back( BitCast<uint>( localGroupSize.x ));
		}
		
		if ( has_spec.y )
		{
			VkSpecializationMapEntry	entry;
			entry.constantID	= localSizeSpec.y;
			entry.offset		= uint(ArraySizeOf(outEntryData));
			entry.size			= sizeof(uint);
			outEntries.push_back( entry );
			outEntryData.push_back( BitCast<uint>( localGroupSize.y ));
		}
		
		if ( has_spec.z )
		{
			VkSpecializationMapEntry	entry;
			entry.constantID	= localSizeSpec.z;
			entry.offset		= uint(ArraySizeOf(outEntryData));
			entry.size			= sizeof(uint);
			outEntries.push_back( entry );
			outEntryData.push_back( BitCast<uint>( localGroupSize.z ));
		}
	}


}	// FG
