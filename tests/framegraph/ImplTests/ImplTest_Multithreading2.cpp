// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#include "../FGApp.h"
#include "stl/ThreadSafe/Barrier.h"
#include <thread>
#include <shared_mutex>

namespace FG
{
	static constexpr CommandBatchID		batch1		{"main"};
	static constexpr uint				max_count	= 1000;
	static Barrier						sync		{2};
	static std::shared_mutex			image_guard;
	static GPipelineID					pipeline;
	static ImageID						image;


	static bool RenderThread1 (const FGInstancePtr &inst, const FGThreadPtr &fg)
	{
		SubmissionGraph	submission_graph;
		submission_graph.AddBatch( batch1, 2 );
		
		const uint2	view_size	= {800, 600};

		image_guard.lock();

		for (uint i = 0; i < max_count; ++i)
		{
			CHECK_ERR( inst->BeginFrame( submission_graph ));
			
			// wake up all render threads
			sync.wait();

			CHECK_ERR( fg->Begin( batch1, 0, EQueueUsage::Graphics ));
			
			image = fg->CreateImage( ImageDesc{ EImage::Tex2D, uint3{view_size.x, view_size.y, 1}, EPixelFormat::RGBA8_UNorm,
												EImageUsage::ColorAttachment | EImageUsage::TransferSrc }, Default, "RenderTarget1" );

			// notify all threads that image is created
			image_guard.unlock();

			LogicalPassID	render_pass	= fg->CreateRenderPass( RenderPassDesc( view_size )
												.AddTarget( RenderTargetID("out_Color"), image, RGBA32f(0.0f), EAttachmentStoreOp::Store )
												.AddViewport( view_size ) );
		
			fg->AddTask( render_pass, DrawVertices().Draw( 3 ).SetPipeline( pipeline ).SetTopology( EPrimitive::TriangleList ));

			Task	t_draw	= fg->AddTask( SubmitRenderPass{ render_pass });
			FG_UNUSED( t_draw );

			CHECK_ERR( fg->Execute() );
			
			// wait until all threads submit the subbatches
			sync.wait();

			image_guard.lock();
			fg->ReleaseResource( image );

			CHECK_ERR( inst->EndFrame() );
		}

		image_guard.unlock();
		return true;
	}


	static bool RenderThread2 (const FGThreadPtr &fg)
	{
		for (uint i = 0; i < max_count; ++i)
		{
			// wait for BeginFrame()
			sync.wait();

			CHECK_ERR( fg->Begin( batch1, 1, EQueueUsage::Graphics ));
			
			// wait until image was created
			image_guard.lock_shared();

			auto&	image_desc = fg->GetDescription( image );

			LogicalPassID	render_pass	= fg->CreateRenderPass( RenderPassDesc( image_desc.dimension.xy() )
												.AddTarget( RenderTargetID("out_Color"), image, EAttachmentLoadOp::Load, EAttachmentStoreOp::Store )
												.AddViewport( image_desc.dimension.xy() ) );
		
			fg->AddTask( render_pass, DrawVertices().Draw( 3 ).SetPipeline( pipeline ).SetTopology( EPrimitive::TriangleList ));

			Task	t_draw	= fg->AddTask( SubmitRenderPass{ render_pass });
			FG_UNUSED( t_draw );

			CHECK_ERR( fg->Execute() );

			// image is no longer needed
			image_guard.unlock_shared();
			
			// notify that thread already submit the subbatch
			sync.wait();
		}
		return true;
	}


	bool FGApp::ImplTest_Multithreading2 ()
	{
		GraphicsPipelineDesc	ppln;
		ppln.AddShader( EShader::Vertex, EShaderLangFormat::VKSL_100, "main", R"#(
#pragma shader_stage(vertex)
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

out vec3	v_Color;

const vec2	g_Positions[3] = vec2[](
	vec2(0.0, -0.5),
	vec2(0.5, 0.5),
	vec2(-0.5, 0.5)
);

const vec3	g_Colors[3] = vec3[](
	vec3(1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0)
);

void main() {
	gl_Position	= vec4( g_Positions[gl_VertexIndex], 0.0, 1.0 );
	v_Color		= g_Colors[gl_VertexIndex];
}
)#" );
		ppln.AddShader( EShader::Fragment, EShaderLangFormat::VKSL_100, "main", R"#(
#pragma shader_stage(fragment)
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

in  vec3	v_Color;
out vec4	out_Color;

void main() {
	out_Color = vec4(v_Color, 1.0);
}
)#" );
		
		pipeline = _fgThreads[0]->CreatePipeline( ppln );
		
		bool			thread1_result;
		bool			thread2_result;

		std::thread		thread1( [this, &thread1_result]() { thread1_result = RenderThread1( _fgInstance, _fgThreads[0] ); });
		std::thread		thread2( [this, &thread2_result]() { thread2_result = RenderThread2( _fgThreads[1] ); });

		thread1.join();
		thread2.join();

		CHECK_ERR( _fgInstance->WaitIdle() );
		CHECK_ERR( thread1_result and thread2_result );

		DeleteResources( pipeline );

		FG_LOGI( TEST_NAME << " - passed" );
		return true;
	}

}	// FG
