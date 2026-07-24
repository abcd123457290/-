#include "AssetPaths.h"

#include <windows.h>

namespace
{
    // GetFileAttributesW 比打开文件更轻量；同时排除同名目录。
    bool FileExists(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    // 资源搜索必须同时考虑 IDE 工作目录和最终 exe 所在目录。
    std::wstring GetExecutableDirectory()
    {
        wchar_t buffer[MAX_PATH]{};
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);

        std::wstring path = buffer;
        const std::wstring::size_type slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            path.erase(slash);
        }
        return path;
    }
}

std::wstring FindAssetPath(const std::wstring& relativePath)
{
    const std::wstring executableDir = GetExecutableDirectory();
    // Visual Studio 的 Debug/Release 输出目录深度不同，按“最可能 -> 最远”搜索。
    // 保留相对路径候选可让开发时从项目根目录直接运行。
    const std::wstring candidates[] =
    {
        relativePath,
        L"..\\" + relativePath,
        L"..\\..\\" + relativePath,
        executableDir + L"\\" + relativePath,
        executableDir + L"\\..\\" + relativePath,
        executableDir + L"\\..\\..\\" + relativePath,
        executableDir + L"\\..\\..\\..\\" + relativePath
    };

    for (const std::wstring& candidate : candidates)
    {
        if (FileExists(candidate))
        {
            return candidate;
        }
    }

    // 返回原值让资源加载方自然失败并启用自己的占位图，而不是在这里抛异常。
    return relativePath;
}
