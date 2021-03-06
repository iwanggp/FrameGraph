// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "framegraph/Public/BufferDesc.h"
#include "framegraph/Public/MemoryDesc.h"
#include "framegraph/Public/FrameGraphThread.h"
#include "VCommon.h"

namespace FG
{

	//
	// Vulkan Buffer immutable data
	//

	class VBuffer final
	{
		friend class VBufferUnitTest;

	// types
	private:
		using OnRelease_t	= FrameGraphThread::OnExternalBufferReleased_t;


	// variables
	private:
		VkBuffer				_buffer				= VK_NULL_HANDLE;
		MemoryID				_memoryId;
		BufferDesc				_desc;
		EQueueFamilyMask		_queueFamilyMask	= Default;

		DebugName_t				_debugName;
		OnRelease_t				_onRelease;

		RWRaceConditionCheck	_rcCheck;


	// methods
	public:
		VBuffer () {}
		VBuffer (VBuffer &&) = default;
		~VBuffer ();

		bool Create (const VDevice &dev, const BufferDesc &desc, RawMemoryID memId, VMemoryObj &memObj,
					 EQueueFamilyMask queueFamilyMask, StringView dbgName);
		
		bool Create (const VDevice &dev, const VulkanBufferDesc &desc, StringView dbgName, OnRelease_t &&onRelease);

		void Destroy (OUT AppendableVkResources_t, OUT AppendableResourceIDs_t);
		
		//void Merge (BufferViewMap_t &, OUT AppendableVkResources_t) const;

		//ND_ VkBufferView		GetView (const HashedBufferViewDesc &) const;

		ND_ bool				IsReadOnly ()			const;

		ND_ VkBuffer			Handle ()				const	{ SHAREDLOCK( _rcCheck );  return _buffer; }
		ND_ RawMemoryID			GetMemoryID ()			const	{ SHAREDLOCK( _rcCheck );  return _memoryId.Get(); }

		ND_ BufferDesc const&	Description ()			const	{ SHAREDLOCK( _rcCheck );  return _desc; }
		ND_ BytesU				Size ()					const	{ SHAREDLOCK( _rcCheck );  return _desc.size; }
		
		ND_ bool				IsExclusiveSharing ()	const	{ SHAREDLOCK( _rcCheck );  return _queueFamilyMask == Default; }
		ND_ EQueueFamilyMask	GetQueueFamilyMask ()	const	{ SHAREDLOCK( _rcCheck );  return _queueFamilyMask; }
		ND_ StringView			GetDebugName ()			const	{ SHAREDLOCK( _rcCheck );  return _debugName; }
	};
	

}	// FG
