#pragma once

#define STRINGIFY_HELPER(a) #a
#define STRINGIFY(a) STRINGIFY_HELPER(a)
#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )

#define ENABLE_ASSERTS 0

#if ENABLE_ASSERTS

#define gAssert(expression, ...)									\
	do																\
	{																\
		if(!(expression))											\
		{															\
			Console::LogFormat(LogType::Warning, __VA_ARGS__);		\
			__debugbreak();											\
		}															\
	} while(0)

#define gAssertOnce(expression, ...)								\
	static bool MACRO_CONCAT(assert, __COUNTER__) = false;			\
	do																\
	{																\
		if(!(expression) && !MACRO_CONCAT(assert, __COUNTER__))		\
		{															\
			MACRO_CONCAT(assert, __COUNTER__) = true;				\
			Console::LogFormat(LogType::Warning, __VA_ARGS__);		\
			__debugbreak();											\
		}															\
	} while(0)


#define gVerify(expression, validation, ...)						\
	do																\
	{																\
		if(!((expression) (validation)))							\
		{															\
			Console::LogFormat(LogType::Warning, __VA_ARGS__);		\
			__debugbreak();											\
		}															\
	} while(0)

#else

#define gAssert(expression, ...)
#define gVerify(expression, validation, ...) (expression)
#define gAssertOnce(expression, ...)

#endif

#define gBoundCheck(x, min, max, ...) gAssert(x >= min && x < max, __VA_ARGS__)

#define gUnreachable() gAssert(false, "Should not have reached this point!")

#define NODISCARD [[nodiscard]]
