// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'
/*
	This test affects:
		...
*/

#include "../FGApp.h"

namespace FG
{

	bool FGApp::Test_ReadAttachment1 ()
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

layout(binding=0) uniform sampler2D  un_DepthImage;

in  vec3	v_Color;
out vec4	out_Color;

void main() {
	out_Color = vec4(v_Color * texelFetch(un_DepthImage, ivec2(gl_FragCoord.xy), 0).r, 1.0);
}
)#" );
		
		FGThreadPtr		frame_graph	= _fgThreads[0];

		const uint2		view_size	= {800, 600};
		ImageID			color_image	= frame_graph->CreateImage( ImageDesc{ EImage::Tex2D, uint3{view_size.x, view_size.y, 1}, EPixelFormat::RGBA8_UNorm,
																			EImageUsage::ColorAttachment | EImageUsage::TransferSrc }, Default, "ColorTarget" );
		ImageID			depth_image	= frame_graph->CreateImage( ImageDesc{ EImage::Tex2D, uint3{view_size.x, view_size.y, 1}, EPixelFormat::Depth24_Stencil8,
																			EImageUsage::DepthStencilAttachment | EImageUsage::TransferDst | EImageUsage::Sampled },
																			Default, "DepthTarget" );

		SamplerID		sampler		= frame_graph->CreateSampler( SamplerDesc{} );

		GPipelineID		pipeline	= frame_graph->CreatePipeline( ppln );
		
		PipelineResources	resources;
		CHECK_ERR( frame_graph->InitPipelineResources( pipeline, DescriptorSetID("0"), OUT resources ));

		
		bool		data_is_correct = false;

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
			data_is_correct &= TestPixel( 0.00f, -0.49f, RGBA32f{1.0f, 0.0f, 0.0f, 1.0f} );
			data_is_correct &= TestPixel( 0.49f,  0.49f, RGBA32f{0.0f, 1.0f, 0.0f, 1.0f} );
			data_is_correct &= TestPixel(-0.49f,  0.49f, RGBA32f{0.0f, 0.0f, 1.0f, 1.0f} );
			
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
		
		CHECK_ERR( _fgInstance->BeginFrame( submission_graph ));
		CHECK_ERR( frame_graph->Begin( batch_id, 0, EQueueUsage::Graphics ));

		LogicalPassID		render_pass	= frame_graph->CreateRenderPass( RenderPassDesc( view_size )
												.AddTarget( RenderTargetID("out_Color"), color_image, RGBA32f(0.0f), EAttachmentStoreOp::Store )
												.AddTarget( RenderTargetID("depth"), depth_image, EAttachmentLoadOp::Load, EAttachmentStoreOp::Store )
												.SetDepthTestEnabled( true ).SetDepthWriteEnabled( false )
												.AddViewport( view_size ) );
		
		ImageViewDesc	view_desc;	view_desc.aspectMask = EImageAspect::Depth;
		resources.BindTexture( UniformID("un_DepthImage"), depth_image, sampler, view_desc );

		frame_graph->AddTask( render_pass, DrawVertices().Draw( 3 ).SetPipeline( pipeline )
												.SetTopology( EPrimitive::TriangleList )
												.AddResources( DescriptorSetID("0"), &resources ));

		Task	t_clear	= frame_graph->AddTask( ClearDepthStencilImage{}.SetImage( depth_image ).Clear( 1.0f ).AddRange( 0_mipmap, 1, 0_layer, 1 ));
		Task	t_draw	= frame_graph->AddTask( SubmitRenderPass{ render_pass }.DependsOn( t_clear ));
		Task	t_read	= frame_graph->AddTask( ReadImage().SetImage( color_image, int2(), view_size ).SetCallback( OnLoaded ).DependsOn( t_draw ) );
		FG_UNUSED( t_read );

		CHECK_ERR( frame_graph->Execute() );
		CHECK_ERR( _fgInstance->EndFrame() );
		
		CHECK_ERR( CompareDumps( TEST_NAME ));
		CHECK_ERR( Visualize( TEST_NAME ));

		CHECK_ERR( _fgInstance->WaitIdle() );

		CHECK_ERR( data_is_correct );

		DeleteResources( color_image, depth_image, sampler, pipeline );

		FG_LOGI( TEST_NAME << " - passed" );
		return true;
	}

}	// FG
