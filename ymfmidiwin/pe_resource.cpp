#include <windows.h>
#include <vector>
#include <cstdint>
#include <cstdio>

#include "pe_resource.h"

static bool SafeRvaToPtr(
    const BYTE* base, size_t imageSize,
    DWORD rva, size_t size,
    const IMAGE_NT_HEADERS* nt,
    const BYTE*& outPtr)
{
    const IMAGE_SECTION_HEADER* sec =
        IMAGE_FIRST_SECTION(nt);

    for (UINT i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        DWORD va = sec->VirtualAddress;
        DWORD vsize = sec->Misc.VirtualSize;
        DWORD raw = sec->PointerToRawData;
        DWORD rsize = sec->SizeOfRawData;

        if (rva >= va && rva < va + max(vsize, rsize))
        {
            DWORD offset = rva - va + raw;

            if (offset > imageSize) return false;
            if (offset + size < offset) return false;
            if (offset + size > imageSize) return false;

            outPtr = base + offset;
            return true;
        }
    }

    return false; // 範囲外エラー
}

struct SafeContext {
    const BYTE* base;
    size_t imageSize;
    DWORD rsrcRva;
    DWORD rsrcSize;
};

static bool EnumResourceRecursive(
    SafeContext& ctx,
    DWORD dirRva,
    int depth,
    DWORD targetSize,
    const IMAGE_NT_HEADERS* nt,
    std::vector<BYTE>& out)
{
    if (depth > 5) return false; // 深すぎるのは変

    const BYTE* dirPtr;
    if (!SafeRvaToPtr(ctx.base, ctx.imageSize, dirRva,
        sizeof(IMAGE_RESOURCE_DIRECTORY), nt, dirPtr))
        return false;

    auto dir = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY*>(dirPtr);

    WORD total = dir->NumberOfIdEntries + dir->NumberOfNamedEntries;

    const BYTE* entryBase = dirPtr + sizeof(IMAGE_RESOURCE_DIRECTORY);

    if (!SafeRvaToPtr(ctx.base, ctx.imageSize,
        dirRva + sizeof(IMAGE_RESOURCE_DIRECTORY),
        total * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY),
        nt,
        dirPtr))
        return false;

    auto entry = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY_ENTRY*>(entryBase);

    for (WORD i = 0; i < total; i++, entry++)
    {
        DWORD offset = entry->OffsetToData & 0x7FFFFFFF;
        DWORD childRva = ctx.rsrcRva + offset;

        if (entry->OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY)
        {
            if (EnumResourceRecursive(ctx, childRva, depth + 1, targetSize, nt, out))
                return true;
        }
        else
        {
            const BYTE* dataEntryPtr;
            if (!SafeRvaToPtr(ctx.base, ctx.imageSize,
                childRva, sizeof(IMAGE_RESOURCE_DATA_ENTRY),
                nt,
                dataEntryPtr))
                continue;

            auto data = reinterpret_cast<const IMAGE_RESOURCE_DATA_ENTRY*>(dataEntryPtr);

            if (data->Size != targetSize)
                continue;

            const BYTE* dataPtr;
            if (!SafeRvaToPtr(ctx.base, ctx.imageSize,
                data->OffsetToData,
                data->Size,
                nt,
                dataPtr))
                continue;

            out.assign(dataPtr, dataPtr + data->Size);
            return true;
        }
    }

    return false;
}

// dllを解析して指定サイズのリソースがあれば抽出
bool ExtractResourceBySizeFromMemoryPE(const void* basePtr, size_t imageSize, DWORD targetSize, std::vector<BYTE>& out)
{
    out.clear();

    if (!basePtr || imageSize < sizeof(IMAGE_DOS_HEADER))
        return false;

    const BYTE* base = reinterpret_cast<const BYTE*>(basePtr);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > imageSize)
        return false;

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    DWORD magic = nt->OptionalHeader.Magic;

    DWORD rsrcRva;
    DWORD rsrcSize;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        auto nt32 = reinterpret_cast<const IMAGE_NT_HEADERS32*>(nt);
        rsrcRva = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
        rsrcSize = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size;
    }
    else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        auto nt64 = reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt);
        rsrcRva = nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
        rsrcSize = nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size;
    }
    else
    {
        return false; // 不明 or 破損
    }

    if (!rsrcRva || !rsrcSize)
        return false;

    const BYTE* rsrcBase;
    if (!SafeRvaToPtr(base, imageSize, rsrcRva, rsrcSize, nt, rsrcBase))
        return false;

    SafeContext ctx{ base, imageSize, rsrcRva, rsrcSize };

    return EnumResourceRecursive(ctx, rsrcRva, 0, targetSize, nt, out);
}
