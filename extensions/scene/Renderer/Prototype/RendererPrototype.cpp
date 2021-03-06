// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#include "scene/Renderer/Prototype/RendererPrototype.h"
#include "scene/Renderer/RenderQueue.h"
#include "scene/SceneManager/IViewport.h"
#include "stl/Algorithms/StringUtils.h"

namespace FG
{
namespace {
	static constexpr CommandBatchID		ShadowMapBatch {"ShadowMap"};
	static constexpr CommandBatchID		MainRendererBatch {"Main"};
}

/*
=================================================
	constructor
=================================================
*/
	RendererPrototype::RendererPrototype ()
	{
	}
	
/*
=================================================
	destructor
=================================================
*/
	RendererPrototype::~RendererPrototype ()
	{
	}
	
/*
=================================================
	GetRenderLayerMask
=================================================
*
	ERenderLayer  RendererPrototype::GetRenderLayerMask () const
	{
		return	ERenderLayer::Background	| ERenderLayer::Foreground	|
				ERenderLayer::Shadow		| ERenderLayer::DepthOnly	|
				ERenderLayer::Opaque_1		| ERenderLayer::Opaque_2	|
				ERenderLayer::Opaque_3		| ERenderLayer::Translucent;
	}

/*
=================================================
	Create
=================================================
*/
	bool RendererPrototype::Create (const FGInstancePtr &inst, const FGThreadPtr &worker)
	{
		CHECK_ERR( inst and worker );

		_fgInstance = inst;
		_frameGraph = worker;

		CHECK_ERR( _shaderCache.Load( _frameGraph, FG_SHADER_PATH ));
		_graphicsShaderSource	= _shaderCache.CacheFileSource( "Prototype/forward.glsl" );
		_rayTracingShaderSource	= _shaderCache.CacheFileSource( "Prototype/raytracing.glsl" );

		CHECK_ERR( _CreateUniformBuffer() );
		return true;
	}
	
/*
=================================================
	Destroy
=================================================
*/
	void RendererPrototype::Destroy ()
	{
		if ( _frameGraph )
		{
			_frameGraph->ReleaseResource( _cameraUB );
			_frameGraph->ReleaseResource( _lightsUB );
		}

		_shaderCache.Clear();

		_graphicsShaderSource	= Default;
		_rayTracingShaderSource	= Default;

		_frameGraph	= null;
		_fgInstance	= null;
	}
/*
=================================================
	AddRenderLayer
=================================================
*/
	static void AddRenderLayer (ERenderLayer layer, INOUT ShaderCache::GraphicsPipelineInfo &info)
	{
		ENABLE_ENUM_CHECKS();
		switch ( layer )
		{
			case ERenderLayer::Shadow :			info.constants.emplace_back( "LAYER_SHADOWMAP", 1 );	break;
			case ERenderLayer::DepthOnly :		info.constants.emplace_back( "LAYER_DEPTHPREPASS", 1 );	break;
			case ERenderLayer::Opaque_1 :
			case ERenderLayer::Opaque_2 :
			case ERenderLayer::Opaque_3 :		info.constants.emplace_back( "LAYER_OPAQUE", 1 );		break;
			case ERenderLayer::Translucent :	info.constants.emplace_back( "LAYER_TRANSLUCENT", 1 );	break;
				
			case ERenderLayer::Background :		//info.constants.emplace_back( "LAYER_BACKGROUND", 1 );	break;
			case ERenderLayer::Foreground :		//info.constants.emplace_back( "LAYER_FOREGROUND", 1 );	break;
			case ERenderLayer::Emission :		//info.constants.emplace_back( "LAYER_EMISSION", 1 );	break;
			case ERenderLayer::RayTracing :
			case ERenderLayer::Unknown :
			case ERenderLayer::Layer_32 :
			case ERenderLayer::HUD :
			case ERenderLayer::_Count :
			default :							CHECK(!"unknown render layer");				break;
		}
		DISABLE_ENUM_CHECKS();
	}

/*
=================================================
	GetPipeline
=================================================
*/
	bool RendererPrototype::GetPipeline (ERenderLayer layer, INOUT GraphicsPipelineInfo &info, OUT RawGPipelineID &outPipeline)
	{
		AddRenderLayer( layer, INOUT info );
		info.sourceIDs.push_back( _graphicsShaderSource );

		if ( not _shaderCache.GetPipeline( INOUT info, OUT outPipeline ) )
			return false;

		if ( not _perPassResources.IsInitialized() )
		{
			CHECK( _frameGraph->InitPipelineResources( outPipeline, DescriptorSetID{"PerPass"}, OUT _perPassResources ));
			_perPassResources.BindBuffer( UniformID{"CameraUB"}, _cameraUB, 0_b, SizeOf<CameraUB> );
			//_perPassResources.BindBuffer( UniformID{"LightsUB"}, _lightsUB );
		}
		
		auto&	ppln_res = _shaderOutputResources[ uint(layer) ];
		if ( not ppln_res.IsInitialized() )
		{
			_frameGraph->InitPipelineResources( outPipeline, DescriptorSetID{"RenderTargets"}, OUT ppln_res );
		}
		return true;
	}
	
/*
=================================================
	GetPipeline
=================================================
*/
	bool RendererPrototype::GetPipeline (ERenderLayer, INOUT GraphicsPipelineInfo &, OUT RawMPipelineID &)
	{
		return false;
	}
	
/*
=================================================
	GetPipeline
=================================================
*/
	bool RendererPrototype::GetPipeline (ERenderLayer, INOUT ComputePipelineInfo &, OUT RawCPipelineID &)
	{
		return false;
	}

/*
=================================================
	GetPipeline
=================================================
*/
	bool RendererPrototype::GetPipeline (ERenderLayer layer, INOUT RayTracingPipelineInfo &info, OUT RawRTPipelineID &outPipeline)
	{
		CHECK_ERR( layer == ERenderLayer::RayTracing );

		//AddRenderLayer( layer, INOUT info );
		info.sourceIDs.push_back( _rayTracingShaderSource );
		info.constants.emplace_back( "PRIMARY_RAY_LOC", 0 );
		info.constants.emplace_back( "SHADOW_RAY_LOC",  1 );
		info.constants.emplace_back( "MAX_LIGHT_COUNT", uint(CountOf( &LightsUB::lights )) );

		if ( not _shaderCache.GetPipeline( INOUT info, OUT outPipeline ) )
			return false;

		auto&	ppln_res = _shaderOutputResources[ uint(layer) ];
		if ( not ppln_res.IsInitialized() )
		{
			CHECK( _frameGraph->InitPipelineResources( outPipeline, DescriptorSetID{"RenderTargets"}, OUT ppln_res ));
			ppln_res.BindBuffer( UniformID{"CameraUB"}, _cameraUB, 0_b, SizeOf<CameraUB> );
			ppln_res.BindBuffer( UniformID{"LightsUB"}, _lightsUB );
		}
		return true;
	}

/*
=================================================
	Render
=================================================
*/
	bool RendererPrototype::Render (const ScenePreRender &preRender)
	{
		auto&	cameras		= _GetCameras( preRender );
		uint	sm_counter	= 0;
		uint	mr_counter	= 0;

		_submissionGraph.Clear();
		//_submissionGraph.AddBatch( ShadowMapBatch, uint(cameras.size()) );
		_submissionGraph.AddBatch( MainRendererBatch, 1 );

		CHECK_ERR( _fgInstance->BeginFrame( _submissionGraph ));

		for (auto& cam : cameras)
		{
			if ( cam.scenes.empty() )
				continue;
			
			RenderQueueImpl		queue;
			queue.Create( _frameGraph, cam );

			ImageID		image;

			// detect shadow techniques
			if ( cam.layers[uint(ERenderLayer::Shadow)] )
			{
				CHECK_ERR( _frameGraph->Begin( ShadowMapBatch, sm_counter++, EQueueUsage::Graphics ));
				CHECK_ERR( _SetupShadowPass( cam, INOUT queue, OUT image ));
			}
			else
			// detect color technique
			if ( cam.layers[uint(ERenderLayer::Opaque_1)] )
			{
				CHECK_ERR( _frameGraph->Begin( MainRendererBatch, mr_counter++, EQueueUsage::Graphics ));
				CHECK_ERR( _SetupColorPass( cam, INOUT queue, OUT image ));
			}
			else
			// detect ray trace technique
			if ( cam.layers[uint(ERenderLayer::RayTracing)] )
			{
				CHECK_ERR( _frameGraph->Begin( MainRendererBatch, mr_counter++, EQueueUsage::Graphics ));
				CHECK_ERR( _SetupRayTracingPass( cam, INOUT queue, OUT image ));
			}

			// build render queue
			for (auto& scene : cam.scenes) {
				scene->Draw( INOUT queue );
			}

			if ( cam.viewport )
				cam.viewport->Draw( INOUT queue );

			// submit draw commands
			Task  last_task = queue.Submit();

			// present on viewport
			if ( cam.viewport )
				cam.viewport->AfterRender( _frameGraph, Present{ image }.DependsOn( last_task ));

			CHECK_ERR( _frameGraph->Execute() );
			_frameGraph->ReleaseResource( image );
		}

		CHECK_ERR( _fgInstance->EndFrame() );
		return true;
	}

/*
=================================================
	_SetupShadowPass
=================================================
*/
	bool RendererPrototype::_SetupShadowPass (const CameraData_t &cameraData, INOUT RenderQueueImpl &queue, OUT ImageID &outImage)
	{
		ASSERT( cameraData.layers.count() == 1 );	// other layers will be ignored

		ImageDesc	desc;
		desc.imageType	= EImage::Tex2D;
		desc.dimension	= uint3{float3{ cameraData.viewportSize.x + 0.5f, cameraData.viewportSize.y + 0.5f, 1.0f }};
		desc.format		= EPixelFormat::Depth32F;
		desc.usage		= EImageUsage::DepthStencilAttachment | EImageUsage::Transfer;

		ImageID		shadow_map = _frameGraph->CreateImage( desc );

		RenderPassDesc	rp{ desc.dimension.xy() };
		rp.AddTarget( RenderTargetID("depth"), shadow_map );
		rp.AddViewport( desc.dimension.xy() );
		rp.AddResources( DescriptorSetID{"PerPass"}, &_perPassResources );

		auto&	res = _shaderOutputResources[uint(ERenderLayer::Shadow)];
		if ( res.IsInitialized() ) {
			res.BindImage( UniformID{"un_ShadowMap"}, shadow_map );
		}

		queue.AddLayer( ERenderLayer::Shadow, rp, res, "ShadowMap" );
		
		_UpdateUniformBuffer( ERenderLayer::Shadow, cameraData, queue );

		outImage = std::move(shadow_map);
		return true;
	}
	
/*
=================================================
	_SetupColorPass
=================================================
*/
	bool RendererPrototype::_SetupColorPass (const CameraData_t &cameraData, INOUT RenderQueueImpl &queue, OUT ImageID &outImage)
	{
		ImageDesc	desc;
		desc.imageType	= EImage::Tex2D;
		desc.dimension	= uint3{float3{ cameraData.viewportSize.x + 0.5f, cameraData.viewportSize.y + 0.5f, 1.0f }};
		desc.format		= EPixelFormat::RGBA16F;	// HDR
		desc.usage		= EImageUsage::ColorAttachment | EImageUsage::Transfer;

		ImageID		color_target = _frameGraph->CreateImage( desc );	// TODO: create virtual image

		desc.format		= EPixelFormat::Depth32F;
		desc.usage		= EImageUsage::DepthStencilAttachment | EImageUsage::Transfer;

		ImageID		depth_target = _frameGraph->CreateImage( desc );

		// opaque pass
		{
			RenderPassDesc	rp{ desc.dimension.xy() };
			rp.AddTarget( RenderTargetID("out_Color"), color_target, RGBA32f{0.0f}, EAttachmentStoreOp::Store )
			  .AddTarget( RenderTargetID("depth"), depth_target, DepthStencil{1.0f}, EAttachmentStoreOp::Store );
			rp.AddViewport( desc.dimension.xy() );
			rp.SetDepthTestEnabled( true ).SetDepthWriteEnabled( true );
			rp.SetDepthCompareOp( ECompareOp::LEqual );	// for reverse depth buffer
			rp.AddResources( DescriptorSetID{"PerPass"}, &_perPassResources );
			
			auto&	res = _shaderOutputResources[uint(ERenderLayer::Opaque_1)];
			if ( res.IsInitialized() ) {
				res.BindImage( UniformID{"un_OutColor"}, color_target );
				res.BindImage( UniformID{"un_OutDepth"}, depth_target );
			}

			queue.AddLayer( ERenderLayer::Opaque_1, rp, res, "Opaque" );
		}

		// translucent pass
		/*{
			RenderPassDesc	rp{ desc.dimension.xy() };
			rp.AddTarget( RenderTargetID("out_Color"), color_target, EAttachmentLoadOp::Load, EAttachmentStoreOp::Store )
			  .AddTarget( RenderTargetID("depth"), depth_target, EAttachmentLoadOp::Load, EAttachmentStoreOp::Invalidate );
			rp.AddViewport( desc.dimension.xy() );
			rp.SetDepthTestEnabled( true ).SetDepthWriteEnabled( false );
			rp.SetDepthCompareOp( ECompareOp::GEqual );	// for reverse depth buffer
			rp.AddColorBuffer( RenderTargetID("out_Color"), EBlendFactor::SrcAlpha, EBlendFactor::OneMinusSrcAlpha, EBlendOp::Add );
			rp.AddResources( DescriptorSetID{"PerPass"}, &_perPassResources );
			
			auto&	res = _shaderOutputResources[uint(ERenderLayer::Opaque_1)];
			if ( res.IsInitialized() ) {
				res.BindImage( UniformID{"un_OutColor"}, color_target );
				res.BindImage( UniformID{"un_OutDepth"}, depth_target );
			}

			queue.AddLayer( ERenderLayer::Translucent, rp, res, "Translucent" );
		}*/
		
		_UpdateUniformBuffer( ERenderLayer::Opaque_1, cameraData, queue );

		outImage = std::move(color_target);
		_frameGraph->ReleaseResource( depth_target );
		return true;
	}
	
/*
=================================================
	_SetupRayTracingPass
=================================================
*/
	bool RendererPrototype::_SetupRayTracingPass (const CameraData_t &cameraData, INOUT RenderQueueImpl &queue, OUT ImageID &outImage)
	{
		ASSERT( cameraData.layers.count() == 1 );	// other layers will be ignored

		ImageDesc	desc;
		desc.imageType	= EImage::Tex2D;
		desc.dimension	= uint3{float3{ cameraData.viewportSize.x + 0.5f, cameraData.viewportSize.y + 0.5f, 1.0f }};
		desc.format		= EPixelFormat::RGBA16F;	// HDR
		desc.usage		= EImageUsage::ColorAttachment | EImageUsage::Storage | EImageUsage::Transfer;

		ImageID		color_target = _frameGraph->CreateImage( desc );	// TODO: create virtual image
		
		RenderPassDesc	rp{ desc.dimension.xy() };
		rp.AddTarget( RenderTargetID("out_Color"), color_target, EAttachmentLoadOp::Load, EAttachmentStoreOp::Store );
		rp.AddViewport( desc.dimension.xy() );

		auto&	res = _shaderOutputResources[uint(ERenderLayer::RayTracing)];
		if ( res.IsInitialized() ) {
			res.BindImage( UniformID{"un_OutColor"}, color_target );
		}
		
		queue.AddLayer( ERenderLayer::RayTracing, rp, res, "RayTracing" );

		_UpdateUniformBuffer( ERenderLayer::RayTracing, cameraData, queue );

		outImage = std::move(color_target);
		return true;
	}

/*
=================================================
	_CreateUniformBuffer
=================================================
*/
	bool RendererPrototype::_CreateUniformBuffer ()
	{
		_cameraUB = _frameGraph->CreateBuffer( BufferDesc{ SizeOf<CameraUB>, EBufferUsage::Uniform | EBufferUsage::TransferDst }, Default, "CameraUB" );
		CHECK_ERR( _cameraUB );

		_lightsUB = _frameGraph->CreateBuffer( BufferDesc{ SizeOf<LightsUB>, EBufferUsage::Uniform | EBufferUsage::TransferDst }, Default, "LightsUB" );
		CHECK_ERR( _lightsUB );

		return true;
	}
	
/*
=================================================
	_UpdateUniformBuffer
=================================================
*/
	void RendererPrototype::_UpdateUniformBuffer (ERenderLayer firstLayer, const CameraData_t &cameraData, INOUT RenderQueueImpl &queue)
	{
		// update camera data
		{
			CameraUB	data = {};
			data.viewProj	= cameraData.camera.ToViewProjMatrix();
			data.position	= vec4{ cameraData.camera.transform.position, 0.0f };
			data.clipPlanes	= cameraData.visibilityRange;

			cameraData.frustum.GetRays( OUT data.frustumRayLeftTop,  OUT data.frustumRayLeftBottom,
										OUT data.frustumRayRightTop, OUT data.frustumRayRightBottom );

			queue.AddTask( firstLayer, UpdateBuffer{}.SetBuffer( _cameraUB ).AddData( &data, 1 ));
		}

		// update ligths
		{
			LightsUB	data = {};
			auto&		i = data.lightCount;
			
			data.lights[i].position		= vec3{ 8.0f, 3.0f, 0.0f };
			data.lights[i].radius		= 0.1f;
			data.lights[i].color		= vec4(1.0f); // vec4{ 1.0f, 0.5f, 0.3f, 0.0f };
			data.lights[i].attenuation	= vec4{ 0.0f, 0.3f, 0.0f, 0.0f };
			++i;
			data.lights[i].position		= cameraData.camera.transform.position + vec3{0.3f, 0.0f, 0.0f};
			data.lights[i].radius		= 0.02f;
			data.lights[i].color		= vec4{1.0f}; //vec4{ 0.3f, 1.0f, 0.3f, 0.0f };
			data.lights[i].attenuation	= vec4{ 0.0f, 0.0f, 0.2f, 0.0f };
			++i;
			queue.AddTask( firstLayer, UpdateBuffer{}.SetBuffer( _lightsUB ).AddData( &data, 1 ));
		}
	}


}	// FG
