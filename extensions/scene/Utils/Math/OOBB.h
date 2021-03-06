// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "scene/Utils/Math/GLM.h"

namespace FG
{

	//
	// Object Oriented Bounding Box
	//

	template <typename T>
	struct ObjectOrientedBoundingBox
	{
	// types
		using Self		= ObjectOrientedBoundingBox<T>;
		using Value_t	= T;


	// variables

	// methods
	};


	using OOBB = ObjectOrientedBoundingBox<float>;


}	// FG
