// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'
/*
	This test affects:
		- frame graph building and execution
		- tasks: UpdateBuffer, CopyBuffer, ReadBuffer
		- resources: buffer
		- staging buffers
		- memory managment
*/

#include "../FGApp.h"

namespace FG
{

	bool FGApp::Test_CopyBuffer1 ()
	{
		const BytesU	src_buffer_size = 256_b;
		const BytesU	dst_buffer_size = 512_b;
		
		FGThreadPtr		frame_graph	= _fgThreads[0];
		BufferID		src_buffer	= frame_graph->CreateBuffer( BufferDesc{ src_buffer_size, EBufferUsage::Transfer }, Default, "SrcBuffer" );
		BufferID		dst_buffer	= frame_graph->CreateBuffer( BufferDesc{ dst_buffer_size, EBufferUsage::Transfer }, Default, "DstBuffer" );

		Array<uint8_t>	src_data;	src_data.resize( size_t(src_buffer_size) );

		for (size_t i = 0; i < src_data.size(); ++i) {
			src_data[i] = uint8_t(i);
		}

		bool	cb_was_called	= false;
		bool	data_is_correct	= false;

		const auto	OnLoaded = [&src_data, dst_buffer_size, OUT &cb_was_called, OUT &data_is_correct] (BufferView data)
		{
			cb_was_called	= true;
			data_is_correct	= (data.size() == size_t(dst_buffer_size));

			for (size_t i = 0; i < src_data.size(); ++i)
			{
				bool	is_equal = (src_data[i] == data[i+128]);
				ASSERT( is_equal );

				data_is_correct &= is_equal;
			}
		};
		
		CommandBatchID		batch_id {"main"};
		SubmissionGraph		submission_graph;
		submission_graph.AddBatch( batch_id );
		
		CHECK_ERR( _fgInstance->BeginFrame( submission_graph ));
		CHECK_ERR( frame_graph->Begin( batch_id, 0, EQueueUsage::Graphics ));

		Task	t_update	= frame_graph->AddTask( UpdateBuffer().SetBuffer( src_buffer ).AddData( src_data ) );
		Task	t_copy		= frame_graph->AddTask( CopyBuffer().From( src_buffer ).To( dst_buffer ).AddRegion( 0_b, 128_b, 256_b ).DependsOn( t_update ) );
		Task	t_read		= frame_graph->AddTask( ReadBuffer().SetBuffer( dst_buffer, 0_b, dst_buffer_size ).SetCallback( OnLoaded ).DependsOn( t_copy ) );
		FG_UNUSED( t_read );

		CHECK_ERR( frame_graph->Execute() );
		CHECK_ERR( _fgInstance->EndFrame() );

		CHECK_ERR( CompareDumps( TEST_NAME ));
		CHECK_ERR( Visualize( TEST_NAME ));

		// after execution 'src_data' was copied to 'src_buffer', 'src_buffer' copied to 'dst_buffer', 'dst_buffer' copied to staging buffer...
		CHECK_ERR( not cb_was_called );
		
		// all staging buffers will be synchronized, all 'ReadBuffer' callbacks will be called.
		CHECK_ERR( _fgInstance->WaitIdle() );
		CHECK_ERR( cb_was_called );
		CHECK_ERR( data_is_correct );

		DeleteResources( src_buffer, dst_buffer );

		FG_LOGI( TEST_NAME << " - passed" );
		return true;
	}

}	// FG
