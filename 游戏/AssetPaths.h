#pragma once

#include <string>

// 在多个可能的运行目录中查找资源文件。
// relativePath 使用相对于项目资源根目录的路径；找到时返回可直接交给
// GDI+ 使用的绝对路径，所有候选位置都不存在时返回原始相对路径。
std::wstring FindAssetPath(const std::wstring& relativePath);
