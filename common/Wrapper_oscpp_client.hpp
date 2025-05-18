#pragma once

// NOTE: This wrapper is kept for backward compatibility with existing code.
// New code should include oscpp headers directly after ensuring NOMINMAX is defined.
// See WindowsConfig.hpp and OSCManager.hpp for the proper approach.

// Define NOMINMAX before any Windows headers might be included
#define NOMINMAX

// Temporarily redefine min and max macros to prevent expansion
#ifdef min
#define OSCPP_SAVED_MIN min
#undef min
#endif

#ifdef max
#define OSCPP_SAVED_MAX max
#undef max
#endif

// Include the actual oscpp header
#include <oscpp/client.hpp>

// Restore original macros if they were defined
#ifdef OSCPP_SAVED_MIN
#define min OSCPP_SAVED_MIN
#undef OSCPP_SAVED_MIN
#endif

#ifdef OSCPP_SAVED_MAX
#define max OSCPP_SAVED_MAX
#undef OSCPP_SAVED_MAX
#endif 