#pragma once

// Prevent Windows min/max macros from interfering with std::min/max
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Prevent Windows from defining its own byte type
#ifndef BYTE
#define BYTE unsigned char
#endif 