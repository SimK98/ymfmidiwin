#pragma once

#include <windows.h>
#include <vector>
#include <cstdint>
#include <cstdio>

bool ExtractResourceBySizeFromMemoryPE(const void* basePtr, size_t imageSize, DWORD targetSize, std::vector<BYTE>& out);
