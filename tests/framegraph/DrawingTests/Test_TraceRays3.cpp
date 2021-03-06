// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#include "../FGApp.h"

namespace FG
{

	bool FGApp::Test_TraceRays3 ()
	{
		if ( not _vulkan.HasDeviceExtension( VK_NV_RAY_TRACING_EXTENSION_NAME ) )
			return true;

		RayTracingPipelineDesc	ppln;

		ppln.AddShader( RTShaderID("Main"), EShader::RayGen, EShaderLangFormat::VKSL_110, "main", R"#(
#version 460 core
#extension GL_NV_ray_tracing : require
layout(binding = 0) uniform accelerationStructureNV  un_RtScene;
layout(binding = 1, rgba8) writeonly uniform image2D  un_Output;
layout(location = 0) rayPayloadNV vec4  payload;

void main ()
{
	const vec2 uv = vec2(gl_LaunchIDNV.xy) / vec2(gl_LaunchSizeNV.xy - 1);

	const vec3 origin = vec3(uv.x, 1.0f - uv.y, -1.0f);
	const vec3 direction = vec3(0.0f, 0.0f, 1.0f);

	traceNV( /*topLevel*/un_RtScene, /*rayFlags*/gl_RayFlagsNoneNV, /*cullMask*/0xFF,
			 /*sbtRecordOffset*/0, /*sbtRecordStride*/1, /*missIndex*/0,
			 /*origin*/origin, /*Tmin*/0.0f, /*direction*/direction, /*Tmax*/10.0f,
			 /*payload*/0 );

	imageStore( un_Output, ivec2(gl_LaunchIDNV), payload );
}
)#");
		
		ppln.AddShader( RTShaderID("PrimaryMiss"), EShader::RayMiss, EShaderLangFormat::VKSL_110, "main", R"#(
#version 460 core
#extension GL_NV_ray_tracing : require
layout(location = 0) rayPayloadInNV vec4  payload;

void main ()
{
	payload = vec4(0.0f);
}
)#");
		
		ppln.AddShader( RTShaderID("PrimaryHit"), EShader::RayClosestHit, EShaderLangFormat::VKSL_110, "main", R"#(
#version 460 core
#extension GL_NV_ray_tracing : require
layout(location = 0) rayPayloadInNV vec4  payload;
					 hitAttributeNV vec2  hitAttribs;

void main ()
{
	const vec3 barycentrics = vec3(1.0f - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);
	payload = vec4(barycentrics, 1.0);
}
)#");

		FGThreadPtr		frame_graph	= _fgThreads[0];
		const uint2		view_size	= {800, 600};
		ImageID			dst_image	= frame_graph->CreateImage( ImageDesc{ EImage::Tex2D, uint3{view_size.x, view_size.y, 1}, EPixelFormat::RGBA8_UNorm,
																			EImageUsage::Storage | EImageUsage::TransferSrc },
															    Default, "OutputImage" );
		
		RTPipelineID	pipeline	= frame_graph->CreatePipeline( ppln );
		CHECK_ERR( pipeline );
		
		const auto		vertices	= ArrayView<float3>{ { 0.25f, 0.25f, 0.0f }, { 0.75f, 0.25f, 0.0f }, { 0.50f, 0.75f, 0.0f } };
		const auto		indices		= ArrayView<uint>{ 0, 1, 2 };

		RayTracingGeometryDesc::Triangles	triangles_info;
		triangles_info.SetID( GeometryID{"Triangle"} ).SetVertices< decltype(vertices[0]) >( vertices.size() )
					.SetIndices< decltype(indices[0]) >( indices.size() ).AddFlags( ERayTracingGeometryFlags::Opaque );

		RTGeometryID		rt_geometry	= frame_graph->CreateRayTracingGeometry( RayTracingGeometryDesc{{ triangles_info }} );
		RTSceneID			rt_scene	= frame_graph->CreateRayTracingScene( RayTracingSceneDesc{ 1 });
		RTShaderTableID		rt_shaders	= frame_graph->CreateRayTracingShaderTable();

		PipelineResources	resources;
		CHECK_ERR( frame_graph->InitPipelineResources( pipeline, DescriptorSetID("0"), OUT resources ));
		
		
		bool	data_is_correct = false;
		
		const auto	OnLoaded =	[OUT &data_is_correct] (const ImageView &imageData)
		{
			const auto	TestPixel = [&imageData] (float x, float y, const RGBA32f &color)
			{
				uint	ix	 = uint( (x + 1.0f) * 0.5f * float(imageData.Dimension().x) + 0.5f );
				uint	iy	 = uint( (y + 1.0f) * 0.5f * float(imageData.Dimension().y) + 0.5f );

				RGBA32f	col;
				imageData.Load( uint3(ix, iy, 0), OUT col );

				bool	is_equal	= Equals( col.r, color.r, 0.1f ) and
									  Equals( col.g, color.g, 0.1f ) and
									  Equals( col.b, color.b, 0.1f ) and
									  Equals( col.a, color.a, 0.1f );
				ASSERT( is_equal );
				return is_equal;
			};

			data_is_correct  = true;
			data_is_correct &= TestPixel( 0.00f, -0.49f, RGBA32f{0.0f, 0.0f, 1.0f, 1.0f} );
			data_is_correct &= TestPixel( 0.49f,  0.49f, RGBA32f{0.0f, 1.0f, 0.0f, 1.0f} );
			data_is_correct &= TestPixel(-0.49f,  0.49f, RGBA32f{1.0f, 0.0f, 0.0f, 1.0f} );
			
			data_is_correct &= TestPixel( 0.00f, -0.51f, RGBA32f{0.0f} );
			data_is_correct &= TestPixel( 0.51f,  0.51f, RGBA32f{0.0f} );
			data_is_correct &= TestPixel(-0.51f,  0.51f, RGBA32f{0.0f} );
			data_is_correct &= TestPixel( 0.00f,  0.51f, RGBA32f{0.0f} );
			data_is_correct &= TestPixel( 0.51f, -0.51f, RGBA32f{0.0f} );
			data_is_correct &= TestPixel(-0.51f, -0.51f, RGBA32f{0.0f} );
		};


		CommandBatchID		batch_id {"main"};
		SubmissionGraph		submission_graph;
		submission_graph.AddBatch( batch_id );

		// frame 1
		{
			CHECK_ERR( _fgInstance->BeginFrame( submission_graph ));
			CHECK_ERR( frame_graph->Begin( batch_id, 0, EQueueUsage::Graphics ));
			
			BuildRayTracingGeometry::Triangles	triangles;
			triangles.SetID( GeometryID{"Triangle"} ).SetVertexArray( vertices ).SetIndexArray( indices );

			BuildRayTracingScene::Instance		instance;
			instance.SetID( InstanceID{"0"} );
			instance.SetGeometry( rt_geometry );

			Task	t_build_geom	= frame_graph->AddTask( BuildRayTracingGeometry{}.SetTarget( rt_geometry ).Add( triangles ));
			Task	t_build_scene	= frame_graph->AddTask( BuildRayTracingScene{}.SetTarget( rt_scene ).Add( instance ).DependsOn( t_build_geom ));
			
			frame_graph->ReleaseResource( rt_geometry );

			Task	t_update_table	= frame_graph->AddTask( UpdateRayTracingShaderTable{}
																.SetTarget( rt_shaders ).SetPipeline( pipeline ).SetScene( rt_scene )
																.SetRayGenShader( RTShaderID{"Main"} )
																.AddMissShader( RTShaderID{"PrimaryMiss"}, 0 )
																.AddHitShader( InstanceID{"0"}, GeometryID{"Triangle"}, 0, RTShaderID{"PrimaryHit"} )
																.DependsOn( t_build_scene ));
			FG_UNUSED( t_update_table );

			CHECK_ERR( frame_graph->Execute() );
			CHECK_ERR( _fgInstance->EndFrame() );
		}

		// frame 2
		{
			CHECK_ERR( _fgInstance->BeginFrame( submission_graph ));
			CHECK_ERR( frame_graph->Begin( batch_id, 0, EQueueUsage::Graphics ));
			
			resources.BindImage( UniformID("un_Output"), dst_image );
			resources.BindRayTracingScene( UniformID("un_RtScene"), rt_scene );

			Task	t_trace	= frame_graph->AddTask( TraceRays{}.AddResources( DescriptorSetID("0"), &resources )
															.SetShaderTable( rt_shaders ).SetGroupCount( view_size.x, view_size.y ));

			Task	t_read	= frame_graph->AddTask( ReadImage{}.SetImage( dst_image, int2(), view_size ).SetCallback( OnLoaded ).DependsOn( t_trace ));
			FG_UNUSED( t_read );

			CHECK_ERR( frame_graph->Execute() );
			CHECK_ERR( _fgInstance->EndFrame() );
		}

		CHECK_ERR( _fgInstance->WaitIdle() );

		CHECK_ERR( data_is_correct );
		
		DeleteResources( pipeline, dst_image, rt_scene, rt_shaders );

		FG_LOGI( TEST_NAME << " - passed" );
		return true;
	}

}	// FG
