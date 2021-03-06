// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#pragma once

#include "stl/Common.h"
#include "stl/Containers/Ptr.h"

namespace FG
{

/*
=================================================
	Cast
=================================================
*/
	template <typename R, typename T>
	ND_ forceinline SharedPtr<R>  Cast (const SharedPtr<T> &other)
	{
		return std::static_pointer_cast<R>( other );
	}

/*
=================================================
	DynCast
=================================================
*/
	template <typename R, typename T>
	ND_ forceinline SharedPtr<R>  DynCast (const SharedPtr<T> &other)
	{
		return std::dynamic_pointer_cast<R>( other );
	}

/*
=================================================
	Cast
=================================================
*/
	template <typename R, typename T>
	ND_ forceinline constexpr R const volatile*  Cast (T const volatile* value)
	{
		return static_cast< R const volatile *>( static_cast< void const volatile *>(value) );
	}

	template <typename R, typename T>
	ND_ forceinline constexpr R volatile*  Cast (T volatile* value)
	{
		return static_cast< R volatile *>( static_cast< void volatile *>(value) );
	}

	template <typename R, typename T>
	ND_ forceinline constexpr R const*  Cast (T const* value)
	{
		return static_cast< R const *>( static_cast< void const *>(value) );
	}
	
	template <typename R, typename T>
	ND_ forceinline constexpr R*  Cast (T* value)
	{
		return static_cast< R *>( static_cast< void *>(value) );
	}

	template <typename R, typename T>
	ND_ forceinline constexpr Ptr<R const>  Cast (Ptr<T const> value)
	{
		return Cast<R>( value.operator->() );
	}
	
	template <typename R, typename T>
	ND_ forceinline constexpr Ptr<R>  Cast (Ptr<T> value)
	{
		return Cast<R>( value.operator->() );
	}
	
/*
=================================================
	DynCast
=================================================
*/
	template <typename R, typename T>
	ND_ forceinline constexpr R const*  DynCast (T const* value)
	{
		return dynamic_cast< R const *>( value );
	}
	
	template <typename R, typename T>
	ND_ forceinline constexpr R*  DynCast (T* value)
	{
		return dynamic_cast< R *>( value );
	}

	template <typename R, typename T>
	ND_ forceinline constexpr Ptr<R const>  DynCast (Ptr<T const> value)
	{
		return DynCast<R>( value.operator->() );
	}
	
	template <typename R, typename T>
	ND_ forceinline constexpr Ptr<R>  DynCast (Ptr<T> value)
	{
		return DynCast<R>( value.operator->() );
	}

/*
=================================================
	MakeShared
=================================================
*/
	template <typename T, typename ...Types>
	ND_ forceinline SharedPtr<T>  MakeShared (Types&&... args)
	{
		return std::make_shared<T>( std::forward<Types&&>( args )... );
	}
	
/*
=================================================
	BitCast
=================================================
*/
	template <typename To, typename From>
	ND_ inline constexpr To&  BitCast (From& from) noexcept
	{
		//STATIC_ASSERT( std::is_trivial_v<To> and std::is_trivial_v<From>, "must be trivial types!" );
		STATIC_ASSERT( sizeof(To) == sizeof(From), "must be same size!" );
		STATIC_ASSERT( alignof(To) == alignof(From), "must be same align!" );

		return reinterpret_cast< To& >( from );
	}

	template <typename To, typename From>
	ND_ inline constexpr const To&  BitCast (const From& from) noexcept
	{
		//STATIC_ASSERT( std::is_trivial_v<To> and std::is_trivial_v<From>, "must be trivial types!" );
		STATIC_ASSERT( sizeof(To) == sizeof(From), "must be same size!" );
		STATIC_ASSERT( alignof(To) == alignof(From), "must be same align!" );

		return reinterpret_cast< const To& >( from );
	}


}	// FG
