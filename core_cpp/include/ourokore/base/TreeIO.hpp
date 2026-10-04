#pragma once

#include <cctype>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <ourokore/base/Tree.hpp>
#include <ourokore/base/utf8.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ork::base
{

/**
 * @brief TreeIO 緊湊序列化模式 (Compact Mode)
 */
enum class CompactMode : uint8_t
{
  None = 0,          ///< 沒有緊湊（保留縮排與換行，鍵值賦值使用 " = "）
  WithEqual = 1,     ///< 保留等號之緊湊模式（無縮排與換行，鍵值賦值使用 "=\""）
  WithoutEqual = 2   ///< 不保留等號之極致緊湊模式（無縮排與換行，鍵值賦值使用 "\""）
};

namespace detail
{
template <typename T, typename = void>
struct is_tree_node_derived : std::false_type
{
};

template <typename T>
struct is_tree_node_derived<T, std::void_t<>> : std::is_base_of<TreeNodeBase<T>, T>
{
};

template <typename T>
inline constexpr bool is_tree_node_derived_v = is_tree_node_derived<T>::value;

template <typename T>
struct resolve_node_type
{
  using type = std::conditional_t<is_tree_node_derived_v<T>, T, TreeNode<T>>;
};

template <typename T>
using resolve_node_type_t = typename resolve_node_type<T>::type;
}  // namespace detail

/**
 * @brief 樹狀容器文字 DSL 狀態機序列化與反序列化器
 *
 * 語法規範：
 * 1. 節點名稱：[名稱]，脫字元支援 \] 與 \\。
 * 2. 資料內容："資料"，脫字元支援 \"、\\、\n、\r、\t 與 \xHH，0~255 二進位位元組安全。
 * 3. 物件區塊：{ ... }，內含具名字節點。
 * 4. 陣列區塊：( ... )，內含陣列循序元素（元素可為字串、子物件或子陣列）。
 * 5. 寬容型狀態機：不在當前狀態內的字元或多餘符號（如等號、多餘引號、說明文字）全數安全無視。
 * 6. 非遞迴走訪：使用顯式堆疊走訪，免疫巨深階層呼叫堆疊溢位 (Stack Overflow)。
 */
class TreeIO
{
public:
  // --- 跳脫字元處理輔助函式 ---

  static std::string EscapeName(std::string_view s)
  {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
      switch (c)
      {
        case ']':
          out += "\\]";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out.push_back(c);
          break;
      }
    }
    return out;
  }

  static std::string EscapeContent(std::string_view s)
  {
    std::string out;
    out.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i)
    {
      unsigned char c = static_cast<unsigned char>(s[i]);
      switch (c)
      {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          if (c < 32 || c == 127)
          {
            // 不可見控制字元使用 \xHH 轉義確保 0~255 安全
            char buf[8];
            snprintf(buf, sizeof(buf), "\\x%02X", c);
            out += buf;
          }
          else
          {
            out.push_back(static_cast<char>(c));
          }
          break;
      }
    }
    return out;
  }

  static std::string MakeIndent(int depth, size_t indent_width)
  {
    return (depth > 0 && indent_width > 0) ? std::string(indent_width * (depth - 1), ' ') : "";
  }

  // =========================================================================
  // 序列化 (Serialize) - 顯式堆疊非遞迴
  // =========================================================================

  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, bool> &&
                                  !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static void Serialize(
      std::ostream &os, const std::shared_ptr<Node> &root, Func &&data_to_string = nullptr,
      size_t indent_width = 2, CompactMode mode = CompactMode::None
  )
  {
    Serialize<Node>(
        os, std::const_pointer_cast<const Node>(root), std::forward<Func>(data_to_string), indent_width, mode
    );
  }

  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, bool> &&
                                  !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static void Serialize(
      std::ostream &os, const std::shared_ptr<const Node> &root, Func &&data_to_string = nullptr,
      size_t indent_width = 2, CompactMode mode = CompactMode::None
  )
  {
    if (!root)
    {
      return;
    }

    bool is_compact = (mode != CompactMode::None);

    auto convert_data = [&](const std::shared_ptr<const Node> &node_ptr) -> std::string
    {
      if (!node_ptr)
      {
        return "";
      }
      if constexpr (!std::is_same_v<std::decay_t<Func>, std::nullptr_t>)
      {
        // 1. 優先：直接將 node 本身交給自訂函式全權處理 (支援 const Node &)
        if constexpr (std::is_invocable_v<Func, const Node &>)
        {
          return data_to_string(*node_ptr);
        }
        // 2. 支援接收節點指針 (const std::shared_ptr<const Node> &)
        else if constexpr (std::is_invocable_v<Func, const std::shared_ptr<const Node> &>)
        {
          return data_to_string(node_ptr);
        }
        // 3. 支援萬能引用/泛型泛用呼叫 (auto &node 或 auto node_ptr)
        else if constexpr (requires { data_to_string(*node_ptr); })
        {
          return data_to_string(*node_ptr);
        }
        else if constexpr (requires { data_to_string(node_ptr); })
        {
          return data_to_string(node_ptr);
        }
        // 4. 向下相容：若自訂函式只接受 Payload 資料，且該節點具備 GetData()
        else if constexpr (requires { node_ptr->GetData(); } &&
                           std::is_invocable_v<Func, decltype(node_ptr->GetData())>)
        {
          return data_to_string(node_ptr->GetData());
        }
        else
        {
          return "";
        }
      }
      else
      {
        // 未傳入自訂函式時的預設提取邏輯（若具備 GetData()）
        if constexpr (requires { node_ptr->GetData(); })
        {
          using DataT = std::decay_t<decltype(node_ptr->GetData())>;
          if constexpr (std::is_same_v<DataT, std::string>)
          {
            return node_ptr->GetData();
          }
          else if constexpr (std::is_same_v<DataT, std::u8string>)
          {
            return ork::utf8::to_string(node_ptr->GetData());
          }
          else if constexpr (std::is_convertible_v<DataT, std::string>)
          {
            return std::string(node_ptr->GetData());
          }
          else
          {
            return "";
          }
        }
        else
        {
          return "";
        }
      }
    };

    struct Frame
    {
      std::shared_ptr<const Node> node;
      int state;  // 0: 輸出開始與內容, 1: 關閉物件/陣列區塊
      int depth;
      bool is_array_element;
    };

    std::vector<Frame> stk;
    stk.push_back({root, 0, 1, false});

    while (!stk.empty())
    {
      Frame f = stk.back();
      stk.pop_back();

      if (f.state == 1)
      {
        if (f.node)
        {
          std::string indent = is_compact ? "" : MakeIndent(f.depth, indent_width);
          if (f.node->IsArray())
          {
            os << indent << ')';
            if (!is_compact)
            {
              os << '\n';
            }
          }
          else
          {
            os << indent << '}';
            if (!is_compact)
            {
              os << '\n';
            }
          }
        }
        continue;
      }

      if (!f.node)
      {
        continue;
      }

      std::string indent = is_compact ? "" : MakeIndent(f.depth, indent_width);
      std::string name_s = ork::utf8::to_string(f.node->GetName());
      std::string escaped_name = EscapeName(name_s);
      std::string data_s = convert_data(f.node);
      std::string escaped_data = EscapeContent(data_s);

      // 輸出節點開頭
      if (!f.is_array_element || !escaped_name.empty())
      {
        os << indent << '[' << escaped_name << ']';
        if (!escaped_data.empty())
        {
          if (mode == CompactMode::WithoutEqual)
          {
            os << "\"" << escaped_data << "\"";
          }
          else if (mode == CompactMode::WithEqual)
          {
            os << "=\"" << escaped_data << "\"";
          }
          else
          {
            os << " = \"" << escaped_data << "\"";
          }
        }
        if (!is_compact)
        {
          os << '\n';
        }
      }
      else
      {
        // 純陣列元素且無名稱
        if (!escaped_data.empty())
        {
          os << indent << "\"" << escaped_data << "\"";
          if (!is_compact)
          {
            os << '\n';
          }
        }
      }

      // 檢查是否具有子節點或陣列元素 (底層統一為單一容器)
      bool is_array = f.node->IsArray();
      size_t count = f.node->ChildCount();

      if (count > 0)
      {
        if (is_array)
        {
          os << indent << '(';
        }
        else
        {
          os << indent << '{';
        }
        if (!is_compact)
        {
          os << '\n';
        }
        stk.push_back({f.node, 1, f.depth, false});

        // 倒序壓棧確保循序輸出 (底層統一為 m_elements)
        for (size_t i = count; i > 0; --i)
        {
          auto elem = f.node->GetElementAt(i - 1);
          if (elem)
          {
            stk.push_back({elem, 0, f.depth + 1, is_array});
          }
        }
      }
    }
  }

  // --- 相容 bool compact 的多載 ---
  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, bool> &&
                                  !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static void Serialize(
      std::ostream &os, const std::shared_ptr<const Node> &root, Func &&data_to_string,
      size_t indent_width, bool compact
  )
  {
    Serialize<Node>(
        os, root, std::forward<Func>(data_to_string), indent_width,
        compact ? CompactMode::WithEqual : CompactMode::None
    );
  }

  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, bool> &&
                                  !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static void Serialize(
      std::ostream &os, const std::shared_ptr<Node> &root, Func &&data_to_string,
      size_t indent_width, bool compact
  )
  {
    Serialize<Node>(
        os, std::const_pointer_cast<const Node>(root), std::forward<Func>(data_to_string), indent_width,
        compact ? CompactMode::WithEqual : CompactMode::None
    );
  }

  // --- 便捷重載 ---
  template <typename Node = TreeNode<std::string>>
  static void Serialize(std::ostream &os, const std::shared_ptr<const Node> &root, CompactMode mode)
  {
    Serialize<Node>(os, root, nullptr, (mode == CompactMode::None) ? 2 : 0, mode);
  }

  template <typename Node = TreeNode<std::string>>
  static void Serialize(std::ostream &os, const std::shared_ptr<Node> &root, CompactMode mode)
  {
    Serialize<Node>(os, std::const_pointer_cast<const Node>(root), mode);
  }

  template <typename Node = TreeNode<std::string>>
  static void Serialize(std::ostream &os, const std::shared_ptr<const Node> &root, bool compact)
  {
    Serialize<Node>(os, root, compact ? CompactMode::WithEqual : CompactMode::None);
  }

  template <typename Node = TreeNode<std::string>>
  static void Serialize(std::ostream &os, const std::shared_ptr<Node> &root, bool compact)
  {
    Serialize<Node>(os, std::const_pointer_cast<const Node>(root), compact);
  }

  template <typename Node = TreeNode<std::string>, typename Func = std::nullptr_t>
  static void SerializeCompact(
      std::ostream &os, const std::shared_ptr<Node> &root,
      CompactMode mode = CompactMode::WithEqual, Func &&data_to_string = nullptr
  )
  {
    Serialize<Node>(os, root, std::forward<Func>(data_to_string), 0, mode);
  }

  template <typename Node = TreeNode<std::string>, typename Func = std::nullptr_t>
  static void SerializeCompact(
      std::ostream &os, const std::shared_ptr<const Node> &root,
      CompactMode mode = CompactMode::WithEqual, Func &&data_to_string = nullptr
  )
  {
    Serialize<Node>(os, root, std::forward<Func>(data_to_string), 0, mode);
  }

  // --- 字串序列化便捷函式 (SerializeToString) ---
  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, bool> &&
                                  !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static std::string SerializeToString(
      const std::shared_ptr<const Node> &root, Func &&data_to_string = nullptr,
      size_t indent_width = 2, CompactMode mode = CompactMode::None
  )
  {
    std::ostringstream oss;
    Serialize<Node>(oss, root, std::forward<Func>(data_to_string), indent_width, mode);
    return oss.str();
  }

  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, bool> &&
                                  !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static std::string SerializeToString(
      const std::shared_ptr<Node> &root, Func &&data_to_string = nullptr,
      size_t indent_width = 2, CompactMode mode = CompactMode::None
  )
  {
    return SerializeToString<Node>(
        std::const_pointer_cast<const Node>(root), std::forward<Func>(data_to_string), indent_width, mode
    );
  }

  template <typename Node = TreeNode<std::string>>
  static std::string SerializeToString(const std::shared_ptr<const Node> &root, CompactMode mode)
  {
    return SerializeToString<Node>(root, nullptr, (mode == CompactMode::None) ? 2 : 0, mode);
  }

  template <typename Node = TreeNode<std::string>>
  static std::string SerializeToString(const std::shared_ptr<Node> &root, CompactMode mode)
  {
    return SerializeToString<Node>(std::const_pointer_cast<const Node>(root), mode);
  }

  template <typename Node = TreeNode<std::string>>
  static std::string SerializeToString(const std::shared_ptr<const Node> &root, bool compact)
  {
    return SerializeToString<Node>(root, compact ? CompactMode::WithEqual : CompactMode::None);
  }

  template <typename Node = TreeNode<std::string>>
  static std::string SerializeToString(const std::shared_ptr<Node> &root, bool compact)
  {
    return SerializeToString<Node>(std::const_pointer_cast<const Node>(root), compact);
  }

  // =========================================================================
  // 反序列化 (Deserialize) - 寬容型狀態機 (Fault-Tolerant FSM)
  // =========================================================================

  template <typename NodeOrData = TreeNode<std::string>, typename Func = std::nullptr_t>
  static std::shared_ptr<detail::resolve_node_type_t<NodeOrData>> Deserialize(
      std::istream &is,
      Func &&data_handler = nullptr
  )
  {
    std::string text((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return DeserializeFromString<NodeOrData>(text, std::forward<Func>(data_handler));
  }

  template <typename NodeOrData = TreeNode<std::string>, typename Func = std::nullptr_t>
  static std::shared_ptr<detail::resolve_node_type_t<NodeOrData>> DeserializeFromString(
      std::string_view text,
      Func &&data_handler = nullptr
  )
  {
    using NodeType = detail::resolve_node_type_t<NodeOrData>;
    using NodePtr = std::shared_ptr<NodeType>;

    auto apply_data = [&](const NodePtr &node, const std::string &raw_str)
    {
      if (!node)
      {
        return;
      }

      if constexpr (!std::is_same_v<std::decay_t<Func>, std::nullptr_t>)
      {
        if constexpr (std::is_invocable_v<Func, const NodePtr &, const std::string &>)
        {
          data_handler(node, raw_str);
        }
        else if constexpr (std::is_invocable_v<Func, const std::string &>)
        {
          auto val = data_handler(raw_str);
          if constexpr (requires { node->SetData(val); })
          {
            node->SetData(val);
          }
        }
      }
      else
      {
        if constexpr (requires { node->SetData(raw_str); })
        {
          node->SetData(raw_str);
        }
        else if constexpr (requires { node->SetData(ork::utf8::to_u8string(raw_str)); })
        {
          node->SetData(ork::utf8::to_u8string(raw_str));
        }
        else if constexpr (requires { typename NodeType::DataType; })
        {
          using DT = typename NodeType::DataType;
          if constexpr (std::is_constructible_v<DT, const std::string &>)
          {
            node->SetData(DT(raw_str));
          }
        }
      }
    };

    size_t pos = 0;
    const size_t len = text.size();

    auto peek = [&]() -> int { return (pos < len) ? static_cast<unsigned char>(text[pos]) : -1; };

    auto next = [&]() -> int { return (pos < len) ? static_cast<unsigned char>(text[pos++]) : -1; };

    // 讀取名稱至 ']'（支援 \] 與 \\ 轉義）
    auto read_name = [&]() -> std::string
    {
      std::string name;
      while (pos < len)
      {
        char c = text[pos++];
        if (c == '\\' && pos < len)
        {
          char esc = text[pos++];
          switch (esc)
          {
            case ']':
              name.push_back(']');
              break;
            case '\\':
              name.push_back('\\');
              break;
            case 'n':
              name.push_back('\n');
              break;
            case 'r':
              name.push_back('\r');
              break;
            case 't':
              name.push_back('\t');
              break;
            default:
              name.push_back(esc);
              break;
          }
        }
        else if (c == ']')
        {
          break;
        }
        else
        {
          name.push_back(c);
        }
      }
      return name;
    };

    // 讀取字串內容至 '"'（支援 \"、\\、\n、\xHH 等 0~255 二進位位元組）
    auto read_string = [&]() -> std::string
    {
      std::string content;
      while (pos < len)
      {
        char c = text[pos++];
        if (c == '\\' && pos < len)
        {
          char esc = text[pos++];
          switch (esc)
          {
            case '"':
              content.push_back('"');
              break;
            case '\\':
              content.push_back('\\');
              break;
            case 'n':
              content.push_back('\n');
              break;
            case 'r':
              content.push_back('\r');
              break;
            case 't':
              content.push_back('\t');
              break;
            case 'x':
            case 'X':
              // 支援 \xHH 十六進位二進位轉義
              if (pos + 1 < len)
              {
                char h1 = text[pos];
                char h2 = text[pos + 1];
                if (std::isxdigit(static_cast<unsigned char>(h1)) && std::isxdigit(static_cast<unsigned char>(h2)))
                {
                  pos += 2;
                  auto hex_val = [](char ch) -> int
                  {
                    if (ch >= '0' && ch <= '9') return ch - '0';
                    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                    return 0;
                  };
                  int val = (hex_val(h1) << 4) | hex_val(h2);
                  content.push_back(static_cast<char>(val));
                  break;
                }
              }
              content.push_back('x');
              break;
            default:
              content.push_back(esc);
              break;
          }
        }
        else if (c == '"')
        {
          break;
        }
        else
        {
          content.push_back(c);
        }
      }
      return content;
    };

    // 遞迴解析節點或元素群
    std::function<void(const NodePtr &, char)> parse_container;

    parse_container = [&](const NodePtr &current_parent, char terminator)
    {
      bool is_array_mode = (terminator == ')');
      NodePtr active_child = nullptr;
      bool active_child_has_data = false;

      while (pos < len)
      {
        int ch = peek();
        if (ch == -1)
        {
          break;
        }

        // 遇到容器終止符（'}' 或 ')'）
        if (terminator != '\0' && ch == terminator)
        {
          next();  // 消耗終止符
          break;
        }

        // 0. 註解處理：支援 // 單行註解、/* ... */ 區塊註解、# 腳本風格單行註解
        if (ch == '/')
        {
          if (pos + 1 < len && text[pos + 1] == '/')
          {
            // // 單行註解：消耗至行尾或 EOF
            pos += 2;
            while (pos < len && text[pos] != '\n' && text[pos] != '\r')
            {
              pos++;
            }
            continue;
          }
          if (pos + 1 < len && text[pos + 1] == '*')
          {
            // /* ... */ 區塊註解：消耗至 */ 或 EOF
            pos += 2;
            bool closed = false;
            while (pos + 1 < len)
            {
              if (text[pos] == '*' && text[pos + 1] == '/')
              {
                pos += 2;
                closed = true;
                break;
              }
              pos++;
            }
            if (!closed)
            {
              pos = len;
            }
            continue;
          }
        }
        else if (ch == '#')
        {
          // # 單行註解：消耗至行尾或 EOF
          next();
          while (pos < len && text[pos] != '\n' && text[pos] != '\r')
          {
            pos++;
          }
          continue;
        }

        // 1. 遇到節點名稱標記 '['
        if (ch == '[')
        {
          next();  // 消耗 '['
          std::string name_s = read_name();
          std::u8string name_u8 = ork::utf8::to_u8string(name_s);

          if (is_array_mode)
          {
            // 陣列中若出現具名節點，同時享有循序元素存取與具名索引尋址
            active_child = current_parent->AddChild(name_u8);
            active_child_has_data = false;
          }
          else
          {
            if (!active_child || active_child_has_data)
            {
              active_child = current_parent->AddChild(name_u8);
              active_child_has_data = false;
            }
            // 若 active_child 存在且尚未接收資料或子容器，連續出現的 [名稱] 標籤視為雜訊安全忽略
          }
          continue;
        }

        // 2. 遇到引號 '"'
        if (ch == '"')
        {
          next();  // 消耗 '"'
          std::string data_s = read_string();

          if (is_array_mode)
          {
            // 在陣列中遇到純引號
            if (!active_child || active_child_has_data)
            {
              // 作為純字串陣列元素
              NodePtr elem = current_parent->PushElement();
              if (elem)
              {
                apply_data(elem, data_s);
              }
              active_child = nullptr;
              active_child_has_data = false;
            }
            else
            {
              // 賦值給剛剛建構但尚未賦值的 active_child
              apply_data(active_child, data_s);
              active_child_has_data = true;
            }
          }
          else
          {
            // 在物件中：若有 active_child 且尚未有資料，填入資料
            if (active_child && !active_child_has_data)
            {
              apply_data(active_child, data_s);
              active_child_has_data = true;
            }
            else
            {
              // 若前面沒有節點名稱或已經有資料，視為「多餘引號」，由寬容狀態機無視！
            }
          }
          continue;
        }

        // 3. 遇到物件子區塊 '{'
        if (ch == '{')
        {
          next();  // 消耗 '{'
          NodePtr target = active_child ? active_child : current_parent;
          parse_container(target, '}');
          active_child = nullptr;
          active_child_has_data = false;
          continue;
        }

        // 4. 遇到陣列子區塊 '('
        if (ch == '(')
        {
          next();  // 消耗 '('
          NodePtr target = active_child ? active_child : current_parent;
          parse_container(target, ')');
          active_child = nullptr;
          active_child_has_data = false;
          continue;
        }

        // 5. 任何其他符號或空白或非預期字元：由寬容狀態機安全無視！
        next();
      }
    };

    // 建立虛擬根節點進行全體解析
    NodePtr root_holder = NodeType::CreateRoot(u8"__ROOT__");
    parse_container(root_holder, '\0');

    // 若解析出唯一頂層子節點，則傳回該節點作為根；否則傳回 root_holder
    if (root_holder->ChildCount() == 1)
    {
      auto first = root_holder->GetFirstChild();
      first->DetachFromParent();
      return first;
    }

    return root_holder;
  }
};

}  // namespace ork::base
