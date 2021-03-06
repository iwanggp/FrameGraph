// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "VCommon.h"

namespace FG
{

	//
	// Vulkan Swapchain interface
	//

	class VSwapchain
	{
	// interface
	public:
		virtual ~VSwapchain () {}

		virtual bool Acquire (ESwapchainImage, OUT RawImageID &) = 0;
		virtual bool Present (RawImageID) = 0;

		virtual void Deinitialize () = 0;
	};


}	// FG
