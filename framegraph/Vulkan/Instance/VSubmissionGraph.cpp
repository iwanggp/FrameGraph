// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#include "VSubmissionGraph.h"
#include "VDevice.h"
#include "VFrameGraphInstance.h"
#include "stl/Algorithms/StringUtils.h"

namespace FG
{
namespace {
	static constexpr uint	READY_TO_SUBMIT		= 0x80000000;
	static constexpr uint	BEFORE_SUBMITTING	= 0x81111111;
	static constexpr uint	SUBMITTED			= 0xFFFFFFFF;
}

/*
=================================================
	constructor
=================================================
*/
	VSubmissionGraph::VSubmissionGraph (const VDevice &dev) :
		_device{ dev }
	{
	}
	
/*
=================================================
	destructor
=================================================
*/
	VSubmissionGraph::~VSubmissionGraph ()
	{
		CHECK( _frames.empty() );
		CHECK( _freeFences.empty() );
		CHECK( _freeSemaphores.empty() );
	}
	
/*
=================================================
	Initialize
=================================================
*/
	bool VSubmissionGraph::Initialize (uint ringBufferSize)
	{
		_frames.resize( ringBufferSize );
		return true;
	}
	
/*
=================================================
	Deinitialize
=================================================
*/
	void VSubmissionGraph::Deinitialize ()
	{
		for (auto fence : _freeFences) {
			_device.vkDestroyFence( _device.GetVkDevice(), fence, null );
		}
		for (auto sem : _freeSemaphores) {
			_device.vkDestroySemaphore( _device.GetVkDevice(), sem, null );
		}

		_freeSemaphores.clear();
		_freeFences.clear();
		_frames.clear();
	}

/*
=================================================
	Recreate
=================================================
*/
	bool VSubmissionGraph::Recreate (uint frameId, const SubmissionGraph &graph, const VFrameGraphInstance &inst)
	{
		auto&	frame = _frames[frameId];
		CHECK_ERR( frame.waitFences.empty() );

		_batches.clear();

		// build graph
		for (auto& src : graph.Batches())
		{
			auto[iter, inserted] = _batches.insert({ src.first, Batch{} });
			CHECK_ERR( inserted );
			ASSERT( src.second.threadCount <= Batch::MaxSubBatches );

			Batch&	dst = iter->second;
			dst.threadCount = src.second.threadCount;
			dst.queue		= inst.FindQueue( src.second.usage );
			CHECK_ERR( dst.queue );

			if ( src.second.dependsOn.empty() )
				iter->second.exeOrderIndex = ExeOrderIndex::First;

			for (auto& dep : src.second.dependsOn)
			{
				auto	dep_iter = _batches.find( dep );
				CHECK_ERR( dep_iter != _batches.end() );

				dst.input.push_back( &dep_iter->second );
				dep_iter->second.output.push_back( &dst );
			}
		}

		// set batch execution order
		for (bool complete = false; not complete;)
		{
			complete = true;

			for (auto& batch : _batches)
			{
				if ( batch.second.exeOrderIndex != ExeOrderIndex::Unknown )
					continue;
			
				// wait for inputs
				ExeOrderIndex	max_index = ExeOrderIndex::First;

				for (auto in_batch : batch.second.input) {
					max_index = std::max( in_batch->exeOrderIndex, max_index );
				}
				
				if ( max_index == ExeOrderIndex::Unknown ) {
					complete = false;
					continue;
				}

				batch.second.exeOrderIndex = ++max_index;
			}
		}

		// add synchronizations
		for (auto& batch : _batches)
		{
			batch.second.exeOrderIndex = ExeOrderIndex(uint(batch.second.exeOrderIndex) * Batch::MaxSubBatches);

			if ( batch.second.output.empty() )
			{
				auto	fence = _CreateFence();

				batch.second.waitFence = fence;
				frame.waitFences.push_back( fence );
				continue;
			}

			for (auto* out : batch.second.output)
			{
				auto	sem = _CreateSemaphore();
				frame.semaphores.push_back( sem );

				CHECK( _SignalSemaphore( batch.second, sem ));
				CHECK( _WaitSemaphore( *out, sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT ));	// TODO: optimize stages
			}
		}
		return true;
	}
	
/*
=================================================
	IsAllBatchesSubmitted
=================================================
*/
	bool VSubmissionGraph::IsAllBatchesSubmitted () const
	{
		for (auto& batch : _batches)
		{
			const uint	mask = (1u << batch.second.threadCount) - 1;
			const uint	bits = batch.second.atomics.existsSubBatchBits.load( memory_order_acquire );

			ASSERT( bits <  mask				or
					bits == READY_TO_SUBMIT		or
					bits == BEFORE_SUBMITTING	or
					bits == SUBMITTED );

			if ( bits != SUBMITTED )
			{
				FG_LOGI( "Waiting for batch: "s << batch.first.GetName() );
				return false;
			}
		}
		return true;
	}
	
/*
=================================================
	WaitFences
=================================================
*/
	bool VSubmissionGraph::WaitFences (uint frameId)
	{
		auto&	frame = _frames[frameId];
		
		if ( not frame.waitFences.empty() )
		{
			VK_CALL( _device.vkWaitForFences( _device.GetVkDevice(), uint(frame.waitFences.size()), frame.waitFences.data(), VK_TRUE, UMax ));		// TODO: set timeout ?
			VK_CALL( _device.vkResetFences( _device.GetVkDevice(), uint(frame.waitFences.size()), frame.waitFences.data() ));

			// recycle fences
			for (auto fence : frame.waitFences) {
				_freeFences.push_back( fence );
			}
			frame.waitFences.clear();

			// recycle semaphores
			for (auto sem : frame.semaphores) {
				_freeSemaphores.push_back( sem );
			}
			frame.semaphores.clear();
		}
		return true;
	}
	
/*
=================================================
	_CreateFence
=================================================
*/
	VkFence  VSubmissionGraph::_CreateFence ()
	{
		if ( not _freeFences.empty() )
		{
			auto	fence = _freeFences.back();
			_freeFences.pop_back();
			return fence;
		}

		VkFence				fence	= VK_NULL_HANDLE;
		VkFenceCreateInfo	info	= {};
		info.sType	= VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		info.flags	= 0;

		VK_CHECK( _device.vkCreateFence( _device.GetVkDevice(), &info, null, OUT &fence ));
		return fence;
	}
	
/*
=================================================
	_CreateSemaphore
=================================================
*/
	VkSemaphore  VSubmissionGraph::_CreateSemaphore ()
	{
		if ( not _freeSemaphores.empty() )
		{
			auto	sem = _freeSemaphores.back();
			_freeSemaphores.pop_back();
			return sem;
		}

		VkSemaphore				sem		= VK_NULL_HANDLE;
		VkSemaphoreCreateInfo	info	= {};
		info.sType	= VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VK_CHECK( _device.vkCreateSemaphore( _device.GetVkDevice(), &info, null, OUT &sem ));
		return sem;
	}

/*
=================================================
	SignalSemaphore
----
	warning: all semaphores must be added BEFORE
	calling 'Submit' with same 'batchId'
=================================================
*/
	bool VSubmissionGraph::SignalSemaphore (const CommandBatchID &batchId, VkSemaphore sem) const
	{
		auto	batch_iter = _batches.find( batchId );
		CHECK_ERR( batch_iter != _batches.end() );

		return _SignalSemaphore( batch_iter->second, sem );
	}
	
	bool VSubmissionGraph::_SignalSemaphore (const Batch &batch, VkSemaphore sem) const
	{
		const uint	idx = batch.atomics.signalSemaphoreCount.fetch_add( 1, memory_order_release );
		
		if ( idx < MaxSemaphores )
		{
			batch.signalSemaphores[idx] = sem;

			// batch already submited and this function has no effect
			ASSERT( batch.atomics.existsSubBatchBits.load( memory_order_acquire ) < SUBMITTED );
			return true;
		}
		RETURN_ERR( "overflow!" );
	}

/*
=================================================
	WaitSemaphore
----
	warning: all semaphores must be added BEFORE
	calling 'Submit' with same 'batchId'
=================================================
*/
	bool VSubmissionGraph::WaitSemaphore (const CommandBatchID &batchId, VkSemaphore sem, VkPipelineStageFlags dstStageMask) const
	{
		auto	batch_iter = _batches.find( batchId );
		CHECK_ERR( batch_iter != _batches.end() );
		
		return _WaitSemaphore( batch_iter->second, sem, dstStageMask );
	}

	bool VSubmissionGraph::_WaitSemaphore (const Batch &batch, VkSemaphore sem, VkPipelineStageFlags dstStageMask) const
	{
		const uint	idx = batch.atomics.waitSemaphoreCount.fetch_add( 1, memory_order_release );
		
		if ( idx < MaxSemaphores )
		{
			batch.waitSemaphores[idx] = sem;
			batch.waitDstStages[idx]  = dstStageMask;
			
			// batch already submited and this function has no effect
			ASSERT( batch.atomics.existsSubBatchBits.load( memory_order_acquire ) < SUBMITTED );
			return true;
		}
		RETURN_ERR( "overflow!" );
	}

/*
=================================================
	SkipSubBatch
=================================================
*/
	bool VSubmissionGraph::SkipSubBatch (const CommandBatchID &batchId, uint indexInBatch) const
	{
		auto	batch_iter = _batches.find( batchId );
		CHECK_ERR( batch_iter != _batches.end() );
		
		ASSERT( batch_iter->second.subBatches[indexInBatch].empty() );

		return _OnSubBatchSubmittion( batch_iter->second, indexInBatch );
	}
	
/*
=================================================
	Submit
=================================================
*/
	bool VSubmissionGraph::Submit (const VDeviceQueueInfoPtr queuePtr, const CommandBatchID &batchId, uint indexInBatch, ArrayView<VkCommandBuffer> commands) const
	{
		auto	batch_iter = _batches.find( batchId );
		CHECK_ERR( batch_iter != _batches.end() );

		auto&	batch = batch_iter->second;
		CHECK_ERR( batch.queue == queuePtr );

		//ASSERT( not commands.empty() );
		ASSERT( commands.size() < Batch::MaxCommands );
		ASSERT( batch.subBatches[indexInBatch].empty() );	// TODO: add RC check

		batch.subBatches[indexInBatch].assign( commands.begin(), commands.end() );

		return _OnSubBatchSubmittion( batch, indexInBatch );
	}
	
/*
=================================================
	_OnSubBatchSubmittion
=================================================
*/
	bool VSubmissionGraph::_OnSubBatchSubmittion (const Batch &batch, uint indexInBatch) const
	{
		const uint	all_bits	= (1u << batch.threadCount) - 1;
		const uint	new_bit		= (1u << indexInBatch);
		const uint	prev_bits	= batch.atomics.existsSubBatchBits.fetch_or( new_bit, memory_order_release );
		const uint	curr_bits	= prev_bits | new_bit;

		ASSERT( curr_bits <= all_bits );
		
		if ( prev_bits & new_bit )
			RETURN_ERR( "subbatch already submitted" );

		if ( curr_bits < all_bits )
			return true;	// not all subbatches had been submitted yet
		
		uint	expected = (1u << batch.threadCount) - 1;
		if ( not batch.atomics.existsSubBatchBits.compare_exchange_strong( INOUT expected, READY_TO_SUBMIT, memory_order_release, memory_order_relaxed ))
			return true;	// other thread is trying to submit this batch

		return _SubmitBatch( batch );
	}

/*
=================================================
	_SubmitBatch
=================================================
*/
	bool VSubmissionGraph::_SubmitBatch (const Batch &batch) const
	{
		// check dependencies
		for (auto* in : batch.input)
		{
			if ( in->atomics.existsSubBatchBits.load( memory_order_acquire ) != SUBMITTED )
				return true;	// dependencies is not complete
		}
		
		uint	expected = READY_TO_SUBMIT;
		if ( not batch.atomics.existsSubBatchBits.compare_exchange_strong( INOUT expected, BEFORE_SUBMITTING, memory_order_release, memory_order_relaxed ))
			return true;	// other thread is trying to submit this batch

		// submit
		{
			// make visible all batch data
			std::atomic_thread_fence( memory_order_acq_rel );	// TODO: is it needed?

			FixedArray<VkCommandBuffer, Batch::MaxSubBatches * Batch::MaxCommands>	all_commands;

			for (uint i = 0; i < batch.threadCount; ++i)
			{
				for (auto& cmd : batch.subBatches[i]) {
					all_commands.push_back( cmd );
				}
			}

			// submit commands
			VkSubmitInfo	info = {};
			info.sType					= VK_STRUCTURE_TYPE_SUBMIT_INFO;
			info.pNext					= null;
			info.pCommandBuffers		= all_commands.data();
			info.commandBufferCount		= uint(all_commands.size());
			info.pSignalSemaphores		= batch.signalSemaphores.data();
			info.signalSemaphoreCount	= uint(batch.atomics.signalSemaphoreCount.load( memory_order_relaxed ));
			info.pWaitSemaphores		= batch.waitSemaphores.data();
			info.pWaitDstStageMask		= batch.waitDstStages.data();
			info.waitSemaphoreCount		= uint(batch.atomics.waitSemaphoreCount.load( memory_order_relaxed ));

			EXLOCK( batch.queue->lock );
			VK_CALL( _device.vkQueueSubmit( batch.queue->handle, 1, &info, OUT batch.waitFence ));
		}
		
		// only one thread can set 'SUBMITTED' flag!
		expected = BEFORE_SUBMITTING;
		CHECK_ERR( batch.atomics.existsSubBatchBits.compare_exchange_strong( INOUT expected, SUBMITTED, memory_order_release, memory_order_relaxed ));

		// submit dependencies
		for (auto* out : batch.output)
		{
			if ( out->atomics.existsSubBatchBits.load( memory_order_acquire ) == READY_TO_SUBMIT )
			{
				CHECK_ERR( _SubmitBatch( *out ));
			}
		}
		return true;
	}
	
/*
=================================================
	GetExecutionOrder
=================================================
*/
	ExeOrderIndex  VSubmissionGraph::GetExecutionOrder (const CommandBatchID &batchId, uint indexInBatch) const
	{
		auto	batch_iter = _batches.find( batchId );
		CHECK_ERR( batch_iter != _batches.end() );

		return ExeOrderIndex( uint(batch_iter->second.exeOrderIndex) + Min( indexInBatch, batch_iter->second.threadCount ));
	}


}	// FG
