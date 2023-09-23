#pragma once

#define STRINGIFY_HELPER(a) #a
#define STRINGIFY(a) STRINGIFY_HELPER(a)
#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )

#define check(expression, ...) do { if(!(expression)) { Console::LogFormat(LogType::Warning, __VA_ARGS__); __debugbreak(); } } while(0)
#define noEntry() check(false, "Should not have reached this point!")

#define validateOnce(expression, ...)																			\
	do																											\
	{																											\
		if(!(expression))																						\
		{																										\
			static bool hasExecuted = false;																	\
			if(!hasExecuted)																					\
			{																									\
				Console::LogFormat(LogType::Warning, "Validate failed: '" #expression "'. ", __VA_ARGS__);		\
				hasExecuted = true;																				\
			}																									\
		}																										\
	} while(0)

#define NODISCARD [[nodiscard]]
