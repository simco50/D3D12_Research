#pragma once

#define _STRINGIFY(a) #a
#define STRINGIFY(a) _STRINGIFY(a)
#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )

#define checkf(expression, msg, ...) if((expression)){} else { Console::LogFormat(LogType::Warning, msg, __VA_ARGS__); __debugbreak(); }
#define check(expression) checkf(expression, "")
#define noEntry() checkf(false, "Should not have reached this point!")
#define validateOncef(expression, msg, ...) if(!(expression)) { \
	static bool hasExecuted = false; \
	if(!hasExecuted) \
	{ \
		Console::LogFormat(LogType::Warning, "Validate failed: '" #expression "'. " msg, ##__VA_ARGS__); \
		hasExecuted = true; \
	} \
} \

#define validateOnce(expression) validateOncef(expression, "")

#define NODISCARD [[nodiscard]]
