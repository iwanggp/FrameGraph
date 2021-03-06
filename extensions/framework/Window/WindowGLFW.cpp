// Copyright (c) 2018-2019,  Zhirnov Andrey. For more information see 'LICENSE'

#include "WindowGLFW.h"
#include "stl/Containers/Singleton.h"
#include "stl/Algorithms/StringUtils.h"

#ifdef FG_ENABLE_GLFW

namespace FG
{
namespace {
	struct GLFWInstance
	{
		uint	refCounter	= 0;
		bool	initialized	= false;
	};
}

/*
=================================================
	constructor
=================================================
*/
	WindowGLFW::WindowGLFW () :
		_window{ null }
	{
	}
	
/*
=================================================
	destructor
=================================================
*/
	WindowGLFW::~WindowGLFW ()
	{
		Destroy();
	}
	
/*
=================================================
	Create
=================================================
*/
	bool WindowGLFW::Create (uint2 size, StringView title)
	{
		CHECK_ERR( not _window );

		auto&	inst = *Singleton<GLFWInstance>();

		if ( not inst.initialized )
		{
			glfwSetErrorCallback( &_GLFW_ErrorCallback );

			CHECK_ERR( glfwInit() == GLFW_TRUE );
			CHECK_ERR( glfwVulkanSupported() == GLFW_TRUE );

			inst.initialized = true;
		}

		++inst.refCounter;


		glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );

		_window = glfwCreateWindow( int(size.x),
									int(size.y),
									title.data(),
									null,
									null );
		CHECK_ERR( _window );

		glfwSetWindowUserPointer( _window, this );
		glfwSetWindowRefreshCallback( _window, &_GLFW_RefreshCallback );
		glfwSetFramebufferSizeCallback( _window, &_GLFW_ResizeCallback );
		glfwSetKeyCallback( _window, &_GLFW_KeyCallback );
		glfwSetMouseButtonCallback( _window, &_GLFW_MouseButtonCallback );
		glfwSetCursorPosCallback( _window, &_GLFW_CursorPos );

		return true;
	}
	
/*
=================================================
	AddListener
=================================================
*/
	void WindowGLFW::AddListener (IWindowEventListener *listener)
	{
		ASSERT( listener );
		_listeners.insert( listener );
	}
	
/*
=================================================
	RemoveListener
=================================================
*/
	void WindowGLFW::RemoveListener (IWindowEventListener *listener)
	{
		ASSERT( listener );
		_listeners.erase( listener );
	}

/*
=================================================
	_GLFW_ErrorCallback
=================================================
*/
	void WindowGLFW::_GLFW_ErrorCallback (int code, const char* msg)
	{
		FG_LOGE( "GLFW error: " + ToString( code ) + ", msg: \"" + msg + "\"" );
	}
	
/*
=================================================
	_GLFW_RefreshCallback
=================================================
*/
	void WindowGLFW::_GLFW_RefreshCallback (GLFWwindow* wnd)
	{
		auto*	self = static_cast<WindowGLFW *>(glfwGetWindowUserPointer( wnd ));
		
		for (auto& listener : self->_listeners) {
			listener->OnRefresh();
		}
	}
	
/*
=================================================
	_GLFW_ResizeCallback
=================================================
*/
	void WindowGLFW::_GLFW_ResizeCallback (GLFWwindow* wnd, int w, int h)
	{
		auto*	self = static_cast<WindowGLFW *>(glfwGetWindowUserPointer( wnd ));
		
		for (auto& listener : self->_listeners) {
			listener->OnResize( uint2{int2{ w, h }} );
		}
	}

/*
=================================================
	_GLFW_KeyCallback
=================================================
*/
	void WindowGLFW::_GLFW_KeyCallback (GLFWwindow* wnd, int key, int, int action, int)
	{
		auto*	self = static_cast<WindowGLFW *>(glfwGetWindowUserPointer( wnd ));

		StringView	key_name	= _MapKey( key );
		EKeyAction	key_action	= (action == GLFW_PRESS    ? EKeyAction::Down :
									action == GLFW_RELEASE ? EKeyAction::Up   :
									EKeyAction::Pressed);

		if ( key_name.empty() )
			return;

		for (auto& listener : self->_listeners) {
			listener->OnKey( key_name, key_action );
		}
	}
	
/*
=================================================
	_GLFW_MouseButtonCallback
=================================================
*/
	void WindowGLFW::_GLFW_MouseButtonCallback (GLFWwindow* wnd, int button, int action, int)
	{
		auto*	self = static_cast<WindowGLFW *>(glfwGetWindowUserPointer( wnd ));
		
		StringView	key_name	= _MapMouseButton( button );
		EKeyAction	key_action	= (action == GLFW_PRESS    ? EKeyAction::Down :
									action == GLFW_RELEASE ? EKeyAction::Up   :
									EKeyAction::Pressed);

		for (auto& listener : self->_listeners) {
			listener->OnKey( key_name, key_action );
		}
	}
	
/*
=================================================
	_GLFW_CursorPos
=================================================
*/
	void WindowGLFW::_GLFW_CursorPos (GLFWwindow* wnd, double xpos, double ypos)
	{
		auto*	self = static_cast<WindowGLFW *>(glfwGetWindowUserPointer( wnd ));
		float2	pos  = { float(xpos), float(ypos) };

		for (auto& listener : self->_listeners) {
			listener->OnMouseMove( pos );
		}
	}

/*
=================================================
	Update
=================================================
*/
	bool WindowGLFW::Update ()
	{
		if ( not _window )
			return false;

		if ( glfwWindowShouldClose( _window ) ) {
			Destroy();
			return false;
		}

		glfwPollEvents();

		for (auto& listener : _listeners) {
			listener->OnUpdate();
		}

		return true;
	}
	
/*
=================================================
	Quit
=================================================
*/
	void WindowGLFW::Quit ()
	{
		if ( _window )
		{
			glfwSetWindowShouldClose( _window, GLFW_TRUE );
		}
	}
	
/*
=================================================
	Destroy
=================================================
*/
	void WindowGLFW::Destroy ()
	{
		Listeners_t		listeners;
		std::swap( listeners, _listeners );

		for (auto& listener : listeners) {
			listener->OnDestroy();
		}

		if ( _window )
		{
			glfwDestroyWindow( _window );
			_window = null;
		

			// delete GLFW instance
			auto&	inst = *Singleton<GLFWInstance>();

			if ( (--inst.refCounter) == 0 and inst.initialized )
			{
				glfwTerminate();

				inst.initialized = false;
			}
		}
	}
	
/*
=================================================
	SetTitle
=================================================
*/
	void WindowGLFW::SetTitle (StringView value)
	{
		CHECK_ERR( _window, void() );

		glfwSetWindowTitle( _window, value.data() );
	}
	
/*
=================================================
	SetSize
=================================================
*/
	void WindowGLFW::SetSize (const uint2 &value)
	{
		CHECK_ERR( _window, void() );

		glfwSetWindowSize( _window, int(value.x), int(value.y) );
	}
	
/*
=================================================
	SetPosition
=================================================
*/
	void WindowGLFW::SetPosition (const int2 &value)
	{
		CHECK_ERR( _window, void() );

		glfwSetWindowPos( _window, value.x, value.y );
	}

/*
=================================================
	GetSize
=================================================
*/
	uint2  WindowGLFW::GetSize () const
	{
		CHECK_ERR( _window );

		int2	size;
		glfwGetWindowSize( _window, OUT &size.x, OUT &size.y );

		return uint2(size);
	}

/*
=================================================
	_MapKey
=================================================
*/
	StringView  WindowGLFW::_MapKey (int key)
	{
		switch ( key )
		{
			case GLFW_KEY_SPACE :		return "space";
			case GLFW_KEY_APOSTROPHE :	return "'";
			case GLFW_KEY_COMMA :		return ",";
			case GLFW_KEY_MINUS :		return "-";
			case GLFW_KEY_PERIOD :		return ".";
			case GLFW_KEY_SLASH :		return "/";
			case GLFW_KEY_0 :			return "0";
			case GLFW_KEY_1 :			return "1";
			case GLFW_KEY_2 :			return "2";
			case GLFW_KEY_3 :			return "3";
			case GLFW_KEY_4 :			return "4";
			case GLFW_KEY_5 :			return "5";
			case GLFW_KEY_6 :			return "6";
			case GLFW_KEY_7 :			return "7";
			case GLFW_KEY_8 :			return "8";
			case GLFW_KEY_9 :			return "9";
			case GLFW_KEY_SEMICOLON :	return ";";
			case GLFW_KEY_EQUAL :		return "=";
			case GLFW_KEY_A :			return "A";
			case GLFW_KEY_B :			return "B";
			case GLFW_KEY_C :			return "C";
			case GLFW_KEY_D :			return "D";
			case GLFW_KEY_E :			return "E";
			case GLFW_KEY_F :			return "F";
			case GLFW_KEY_G :			return "G";
			case GLFW_KEY_H :			return "H";
			case GLFW_KEY_I :			return "I";
			case GLFW_KEY_J :			return "J";
			case GLFW_KEY_K :			return "K";
			case GLFW_KEY_L :			return "L";
			case GLFW_KEY_M :			return "M";
			case GLFW_KEY_N :			return "N";
			case GLFW_KEY_O :			return "O";
			case GLFW_KEY_P :			return "P";
			case GLFW_KEY_Q :			return "Q";
			case GLFW_KEY_R :			return "R";
			case GLFW_KEY_S :			return "S";
			case GLFW_KEY_T :			return "T";
			case GLFW_KEY_U :			return "U";
			case GLFW_KEY_V :			return "V";
			case GLFW_KEY_W :			return "W";
			case GLFW_KEY_X :			return "X";
			case GLFW_KEY_Y :			return "Y";
			case GLFW_KEY_Z :			return "Z";
			case GLFW_KEY_LEFT_BRACKET:	return "[";
			case GLFW_KEY_BACKSLASH :	return "\\";
			case GLFW_KEY_RIGHT_BRACKET:return "]";
			case GLFW_KEY_GRAVE_ACCENT:	return "~";
			case GLFW_KEY_ESCAPE :		return "escape";
			case GLFW_KEY_ENTER :		return "enter";
			case GLFW_KEY_TAB :			return "tab";
			case GLFW_KEY_BACKSPACE :	return "backspace";
			case GLFW_KEY_INSERT :		return "insert";
			case GLFW_KEY_DELETE :		return "delete";
			case GLFW_KEY_RIGHT :		return "arrow right";
			case GLFW_KEY_LEFT :		return "arrow left";
			case GLFW_KEY_DOWN :		return "arrow down";
			case GLFW_KEY_UP :			return "arrow up";
			case GLFW_KEY_PAGE_UP :		return "page up";
			case GLFW_KEY_PAGE_DOWN :	return "page down";
			case GLFW_KEY_HOME :		return "home";
			case GLFW_KEY_END :			return "end";
			case GLFW_KEY_CAPS_LOCK :	return "caps lock";
			case GLFW_KEY_SCROLL_LOCK :	return "scroll lock";
			case GLFW_KEY_NUM_LOCK :	return "num lock";
			case GLFW_KEY_PRINT_SCREEN:	return "print screen";
			case GLFW_KEY_PAUSE :		return "pause";
			case GLFW_KEY_F1 :			return "F1";
			case GLFW_KEY_F2 :			return "F2";
			case GLFW_KEY_F3 :			return "F3";
			case GLFW_KEY_F4 :			return "F4";
			case GLFW_KEY_F5 :			return "F5";
			case GLFW_KEY_F6 :			return "F6";
			case GLFW_KEY_F7 :			return "F7";
			case GLFW_KEY_F8 :			return "F8";
			case GLFW_KEY_F9 :			return "F9";
			case GLFW_KEY_F10 :			return "F10";
			case GLFW_KEY_F11 :			return "F11";
			case GLFW_KEY_F12 :			return "F12";
		}
		return "";
	}
	
/*
=================================================
	_MapMouseButton
=================================================
*/
	StringView  WindowGLFW::_MapMouseButton (int button)
	{
		switch ( button )
		{
			case GLFW_MOUSE_BUTTON_LEFT :		return "left mb";
			case GLFW_MOUSE_BUTTON_RIGHT :		return "right mb";
			case GLFW_MOUSE_BUTTON_MIDDLE :		return "middle mb";
			case GLFW_MOUSE_BUTTON_4 :			return "mouse btn 4";
			case GLFW_MOUSE_BUTTON_5 :			return "mouse btn 5";
			case GLFW_MOUSE_BUTTON_6 :			return "mouse btn 6";
			case GLFW_MOUSE_BUTTON_7 :			return "mouse btn 7";
			case GLFW_MOUSE_BUTTON_8 :			return "mouse btn 8";
		}
		return "";
	}

/*
=================================================
	GetVulkanSurface
=================================================
*/
	UniquePtr<IVulkanSurface>  WindowGLFW::GetVulkanSurface () const
	{
		return UniquePtr<IVulkanSurface>{new VulkanSurface( _window )};
	}
//-----------------------------------------------------------------------------


	
/*
=================================================
	GetRequiredExtensions
=================================================
*/
	Array<const char*>  WindowGLFW::VulkanSurface::GetRequiredExtensions () const
	{
		uint32_t			required_extension_count = 0;
		const char **		required_extensions		 = glfwGetRequiredInstanceExtensions( OUT &required_extension_count );
		Array<const char*>	extensions;
		
		extensions.assign( required_extensions, required_extensions + required_extension_count );
		return extensions;
	}
	
/*
=================================================
	Create
=================================================
*/
	VkSurfaceKHR  WindowGLFW::VulkanSurface::Create (VkInstance inst) const
	{
		VkSurfaceKHR	surf = VK_NULL_HANDLE;
		glfwCreateWindowSurface( inst, _window, null, OUT &surf );
		return surf;
	}


}	// FG

#endif	// FG_ENABLE_GLFW
