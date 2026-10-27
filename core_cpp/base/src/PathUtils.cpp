#include "ourokore/base/PathUtils.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace ork::base::path
{

namespace
{

bool is_valid_tag_segment_char(char c) noexcept
{
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
}

bool is_valid_tag_segment(std::string_view seg) noexcept
{
  if (seg.empty())
  {
    return false;
  }
  if (seg == "." || seg == "..")
  {
    return false;
  }
  return std::all_of(seg.begin(), seg.end(), is_valid_tag_segment_char);
}

std::vector<std::string_view> split_path_segments(std::string_view path)
{
  std::vector<std::string_view> segments;
  size_t start = 0;
  while (start < path.size())
  {
    while (start < path.size() && (path[start] == '/' || path[start] == '\\'))
    {
      ++start;
    }
    if (start >= path.size())
    {
      break;
    }
    size_t end = start;
    while (end < path.size() && path[end] != '/' && path[end] != '\\')
    {
      ++end;
    }
    segments.emplace_back(path.data() + start, end - start);
    start = end;
  }
  return segments;
}

}  // namespace

std::string FactoryTag::get_directory() const
{
  if (subpath.empty())
  {
    return vendor;
  }
  return vendor + "/" + subpath;
}

std::string FactoryTag::to_string() const
{
  if (subpath.empty())
  {
    return vendor + "/" + module_stem;
  }
  return vendor + "/" + subpath + "/" + module_stem;
}

Result<FactoryTag> parse_factory_tag(std::string_view tag_str)
{
  auto segments = split_path_segments(tag_str);
  if (segments.size() < 2)
  {
    return unexpected(Status::InvalidArgument(
        "工廠標籤格式錯誤：至少需包含 Vendor 與 ModuleStem 兩段 (如 Vendor/ModuleStem 或 Vendor/SubPath.../ModuleStem)"));
  }

  for (size_t i = 0; i < segments.size(); ++i)
  {
    if (!is_valid_tag_segment(segments[i]))
    {
      return unexpected(Status::InvalidArgument("標籤路徑第 " + std::to_string(i + 1) + " 段包含非法字元或為空"));
    }
  }

  FactoryTag tag;
  tag.vendor = std::string(segments.front());
  tag.module_stem = std::string(segments.back());

  if (segments.size() > 2)
  {
    std::string mid_path;
    for (size_t i = 1; i + 1 < segments.size(); ++i)
    {
      if (!mid_path.empty())
      {
        mid_path.push_back('/');
      }
      mid_path.append(segments[i]);
    }
    tag.subpath = std::move(mid_path);
  }

  return tag;
}

std::string get_platform_module_filename(std::string_view stem)
{
#if defined(__MINGW32__) || defined(__MINGW64__)
  return "lib" + std::string(stem) + ".dll";
#elif defined(_WIN32)
  return std::string(stem) + ".dll";
#elif defined(__APPLE__)
  return "lib" + std::string(stem) + ".dylib";
#else
  return "lib" + std::string(stem) + ".so";
#endif
}

std::string normalize(std::string_view path)
{
  if (path.empty())
  {
    return "";
  }

  std::string normalized;
  std::string_view remaining = path;

  // 1. 檢查並提取 Windows 磁碟機代號 (如 C:)
  if (remaining.size() >= 2 && std::isalpha(static_cast<unsigned char>(remaining[0])) && remaining[1] == ':')
  {
    normalized.push_back(remaining[0]);
    normalized.push_back(':');
    normalized.push_back('/');
    remaining.remove_prefix(2);
    while (!remaining.empty() && (remaining.front() == '/' || remaining.front() == '\\'))
    {
      remaining.remove_prefix(1);
    }
  }
  else if (remaining.front() == '/' || remaining.front() == '\\')
  {
    normalized.push_back('/');
    while (!remaining.empty() && (remaining.front() == '/' || remaining.front() == '\\'))
    {
      remaining.remove_prefix(1);
    }
  }

  auto segments = split_path_segments(remaining);
  std::vector<std::string_view> result_segments;
  result_segments.reserve(segments.size());

  for (auto seg : segments)
  {
    if (seg == ".")
    {
      continue;
    }
    if (seg == "..")
    {
      if (!result_segments.empty() && result_segments.back() != "..")
      {
        result_segments.pop_back();
      }
      else if (normalized.empty())
      {
        result_segments.push_back(seg);
      }
    }
    else
    {
      result_segments.push_back(seg);
    }
  }

  for (size_t i = 0; i < result_segments.size(); ++i)
  {
    if (i > 0 || (normalized.size() > 1 && normalized.back() != '/'))
    {
      normalized.push_back('/');
    }
    normalized.append(result_segments[i]);
  }

  return normalized.empty() ? "." : normalized;
}

bool is_safe_relative_path(std::string_view relative_path) noexcept
{
  if (relative_path.empty())
  {
    return true;
  }

  // 1. 嚴禁以斜線或磁碟機代號開頭之絕對路徑
  if (relative_path.front() == '/' || relative_path.front() == '\\')
  {
    return false;
  }
  if (relative_path.size() >= 2 && std::isalpha(static_cast<unsigned char>(relative_path[0])) &&
      relative_path[1] == ':')
  {
    return false;
  }

  // 2. 嚴禁包含 NULL 字元注入
  if (relative_path.find('\0') != std::string_view::npos)
  {
    return false;
  }

  // 3. 模擬路徑堆疊深度，嚴禁深度小於 0 (跳出根目錄)
  int depth = 0;
  auto segments = split_path_segments(relative_path);
  for (auto seg : segments)
  {
    if (seg == ".")
    {
      continue;
    }
    if (seg == "..")
    {
      --depth;
      if (depth < 0)
      {
        return false;
      }
    }
    else
    {
      ++depth;
    }
  }

  return depth >= 0;
}

Result<std::string> combine_safe(
    std::string_view base_dir,
    std::string_view relative_path)
{
  if (!is_safe_relative_path(relative_path))
  {
    return unexpected(Status::InvalidArgument(
        "檢測到潛在之路徑穿越攻擊 (Path Traversal): " + std::string(relative_path)));
  }

  std::string combined(base_dir);
  if (!combined.empty() && combined.back() != '/' && combined.back() != '\\')
  {
    combined.push_back('/');
  }
  combined.append(relative_path);

  return normalize(combined);
}

std::string_view get_extension(std::string_view path) noexcept
{
  size_t last_slash = path.find_last_of("/\\");
  size_t start = (last_slash == std::string_view::npos) ? 0 : last_slash + 1;
  size_t dot = path.rfind('.');
  if (dot != std::string_view::npos && dot >= start)
  {
    return path.substr(dot);
  }
  return {};
}

std::string_view get_filename(std::string_view path) noexcept
{
  size_t last_slash = path.find_last_of("/\\");
  if (last_slash == std::string_view::npos)
  {
    return path;
  }
  return path.substr(last_slash + 1);
}

std::string_view get_parent_path(std::string_view path) noexcept
{
  size_t last_slash = path.find_last_of("/\\");
  if (last_slash == std::string_view::npos)
  {
    return {};
  }
  return path.substr(0, last_slash);
}

}  // namespace ork::base::path
