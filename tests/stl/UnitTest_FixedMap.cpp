// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#include "stl/Containers/FixedMap.h"
#include "stl/Containers/StaticString.h"
#include "UnitTest_Common.h"


static void FixedMap_Test1 ()
{
	FixedMap<int, int, 16>	map;

	for (uint i = 0; i < 10; ++i) {
		map.insert({ i, i });
	}

	TEST( map.find( 5 ) != map.end() );
	TEST( map.find( 2 ) != map.end() );
	TEST( map.find( 7 ) != map.end() );
	TEST( map.find( 11 ) == map.end() );
}


static void FixedMap_Test2 ()
{
	FixedMap<int, int, 16>	map;

	for (uint i = 0; i < 10; ++i) {
		map.insert({ i, i });
	}

	TEST( map.count( 5 ) == 1 );
	TEST( map.count( 2 ) == 1 );
	TEST( map.count( 7 ) == 1 );
	TEST( map.count( 11 ) == 0 );
}


static void FixedMap_Test3 ()
{
	using T1 = DebugInstanceCounter< int, 1 >;
	using T2 = DebugInstanceCounter< int, 2 >;
	{
		FixedMap< T1, T2, 32 >	map;

		for (uint j = 0; j < 10; ++j)
		{
			for (int i = 0; i < 30; ++i) {
				map.insert({ T1(i), T2(i) });
			}
			map.clear();
		}
	}
	TEST( T1::CheckStatistic() );
	TEST( T2::CheckStatistic() );
}


static void FixedMap_Test4 ()
{
	FixedMap<int, int, 32>	map;

	for (uint i = 0; i < 10; ++i) {
		map.insert({ i, i*2 });
	}

	TEST( map.insert({ 10, 0 }).second );
	TEST( not map.insert({ 1, 0 }).second );
}


static void FixedMap_Test5 ()
{
	FixedMap<int, int, 32>	map1;
	FixedMap<int, int, 32>	map2;

	map1.insert({  1, 1 });
	map1.insert({ 11, 2 });
	map1.insert({  6, 3 });
	map1.insert({  9, 4 });
	map1.insert({  3, 5 });

	map2.insert({ 11, 1 });
	map2.insert({  3, 2 });
	map2.insert({  6, 3 });
	map2.insert({  1, 4 });
	map2.insert({  9, 5 });

	TEST( map1.CalcHash() == map2.CalcHash() );
}


extern void UnitTest_FixedMap ()
{
	FixedMap_Test1();
	FixedMap_Test2();
	FixedMap_Test3();
	FixedMap_Test4();
	FixedMap_Test5();

	FG_LOGI( "UnitTest_FixedMap - passed" );
}
