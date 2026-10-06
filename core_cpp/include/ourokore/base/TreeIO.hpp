#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
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
  Pretty = 0,       ///< 格式化排版模式（Allman 風格：獨立換行與縮排，具名賦值使用 " = "）
  Compact = 1,      ///< 緊湊模式（無縮排與換行，具名賦值必然保留關鍵字 "="）
  None = 0,         ///< 向下相容別名：格式化排版
  WithEqual = 1,    ///< 向下相容別名：緊湊模式
  WithoutEqual = 1  ///< 向下相容別名：緊湊模式（消除歧義，全面保留關鍵字等號）
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
 * 1. 節點名稱：[名稱]，跳脫字元支援 \] 與 \\。
 * 2. 賦值關鍵字：=，具名節點賦值時必然使用。
 * 3. 資料內容："資料"，原始位元組直接傳遞（二進位安全），跳脫字元支援 \\ 與 \"。
 * 4. 容器區塊：{ ... }，Allman 風格換行排版，涵蓋具名與匿名所有子節點。
 * 5. 寬容型狀態機：不在關鍵標記內的字元、說明文字或雜訊全數安全無視；支援 //, /* *\/, # 註解。
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
      char c = s[i];
      switch (c)
      {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        default:
          out.push_back(c);
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
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Func>, bool> && !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static void Serialize(
      std::ostream &os, const std::shared_ptr<Node> &root, Func &&data_to_string = nullptr, size_t indent_width = 2,
      CompactMode mode = CompactMode::None
  )
  {
    Serialize<Node>(
        os, std::const_pointer_cast<const Node>(root), std::forward<Func>(data_to_string), indent_width, mode
    );
  }

  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Func>, bool> && !std::is_same_v<std::decay_t<Func>, CompactMode>>>
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
        else if constexpr (requires {
                             node_ptr->GetData();
                           } && std::is_invocable_v<Func, decltype(node_ptr->GetData())>)
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
      int state;  // 0: 輸出開始與內容, 1: 關閉區塊
      int depth;
    };

    std::vector<Frame> stk;
    stk.push_back({root, 0, 1});

    while (!stk.empty())
    {
      Frame f = stk.back();
      stk.pop_back();

      if (f.state == 1)
      {
        if (f.node)
        {
          std::string indent = is_compact ? "" : MakeIndent(f.depth, indent_width);
          os << indent << '}';
          if (!is_compact)
          {
            os << '\n';
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
      if (!escaped_name.empty())
      {
        os << indent << '[' << escaped_name << ']';
        if (!escaped_data.empty())
        {
          if (is_compact)
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
        // 匿名節點：若為純資料葉節點（無子節點）或帶有資料，輸出字串引號 ""
        // 若為匿名容器節點（ChildCount > 0 且無資料），則直接由後續容器括號括起，無須輸出空引號
        if (!escaped_data.empty() || f.node->ChildCount() == 0)
        {
          os << indent << "\"" << escaped_data << "\"";
          if (!is_compact)
          {
            os << '\n';
          }
        }
      }

      // 檢查是否具有子節點
      size_t count = f.node->ChildCount();
      if (count > 0)
      {
        os << indent << '{';
        if (!is_compact)
        {
          os << '\n';
        }
        stk.push_back({f.node, 1, f.depth});

        // 倒序壓棧確保循序輸出 (底層統一為 m_elements)
        for (size_t i = count; i > 0; --i)
        {
          auto elem = f.node->GetElementAt(i - 1);
          if (elem)
          {
            stk.push_back({elem, 0, f.depth + 1});
          }
        }
      }
    }
  }

  // --- 相容 bool compact 的多載 ---
  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Func>, bool> && !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static void Serialize(
      std::ostream &os, const std::shared_ptr<const Node> &root, Func &&data_to_string, size_t indent_width,
      bool compact
  )
  {
    Serialize<Node>(
        os, root, std::forward<Func>(data_to_string), indent_width, compact ? CompactMode::WithEqual : CompactMode::None
    );
  }

  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Func>, bool> && !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static void Serialize(
      std::ostream &os, const std::shared_ptr<Node> &root, Func &&data_to_string, size_t indent_width, bool compact
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
      std::ostream &os, const std::shared_ptr<Node> &root, CompactMode mode = CompactMode::WithEqual,
      Func &&data_to_string = nullptr
  )
  {
    Serialize<Node>(os, root, std::forward<Func>(data_to_string), 0, mode);
  }

  template <typename Node = TreeNode<std::string>, typename Func = std::nullptr_t>
  static void SerializeCompact(
      std::ostream &os, const std::shared_ptr<const Node> &root, CompactMode mode = CompactMode::WithEqual,
      Func &&data_to_string = nullptr
  )
  {
    Serialize<Node>(os, root, std::forward<Func>(data_to_string), 0, mode);
  }

  // --- 字串序列化便捷函式 (SerializeToString) ---
  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Func>, bool> && !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static std::string SerializeToString(
      const std::shared_ptr<const Node> &root, Func &&data_to_string = nullptr, size_t indent_width = 2,
      CompactMode mode = CompactMode::None
  )
  {
    std::ostringstream oss;
    Serialize<Node>(oss, root, std::forward<Func>(data_to_string), indent_width, mode);
    return oss.str();
  }

  template <
      typename Node = TreeNode<std::string>, typename Func = std::nullptr_t,
      typename = std::enable_if_t<
          !std::is_same_v<std::decay_t<Func>, bool> && !std::is_same_v<std::decay_t<Func>, CompactMode>>>
  static std::string SerializeToString(
      const std::shared_ptr<Node> &root, Func &&data_to_string = nullptr, size_t indent_width = 2,
      CompactMode mode = CompactMode::None
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
      std::istream &is, Func &&data_handler = nullptr
  )
  {
    std::string text((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return DeserializeFromString<NodeOrData>(text, std::forward<Func>(data_handler));
  }

  template <typename NodeOrData = TreeNode<std::string>, typename Func = std::nullptr_t>
  static std::shared_ptr<detail::resolve_node_type_t<NodeOrData>> DeserializeFromString(
      std::string_view text, Func &&data_handler = nullptr
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

    // 讀取字串內容至 '"'（原始字節直接讀取，支援 \"、\\、\0、\n、\r、\t 轉義）
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
            case '0':
              content.push_back('\0');
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

    // 建立頂層容器節點進行全體解析 (預設名稱為空字串，防止內部標記字串外洩)
    NodePtr root_holder = NodeType::CreateRoot(u8"");
    bool root_adopted_as_container = false;

    // 非遞迴顯式走訪堆疊幀 (免疫巨深階層 Call Stack Overflow)
    struct ParseFrame
    {
      NodePtr current_parent;
      char terminator{'\0'};
      NodePtr active_child{nullptr};
      bool has_equal{false};
      bool active_child_has_data{false};
    };

    std::vector<ParseFrame> parse_stack;
    parse_stack.reserve(64);
    parse_stack.push_back({root_holder, '\0', nullptr, false, false});

    while (pos < len && !parse_stack.empty())
    {
      auto &frame = parse_stack.back();

      int ch = peek();
      if (ch == -1)
      {
        break;
      }

      // 遇到容器終止符（'}' 或 ')'）
      if (frame.terminator != '\0' && (ch == frame.terminator || (frame.terminator == '}' && ch == ')')))
      {
        next();                  // 消耗終止符
        parse_stack.pop_back();  // 顯式彈棧：結束當前層級，零 Call Stack 消耗
        continue;
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

      // 1. 遇到節點名稱標記 '['：建立具名新節點
      if (ch == '[')
      {
        next();  // 消耗 '['
        std::string name_s = read_name();
        std::u8string name_u8 = ork::utf8::to_u8string(name_s);

        frame.active_child = frame.current_parent->AddChild(name_u8);
        frame.has_equal = false;
        frame.active_child_has_data = false;
        continue;
      }

      // 2. 遇到賦值運算子 '='
      if (ch == '=')
      {
        next();  // 消耗 '='
        if (frame.active_child && !frame.active_child_has_data)
        {
          frame.has_equal = true;
        }
        continue;
      }

      // 3. 遇到引號 '"'
      if (ch == '"')
      {
        next();  // 消耗 '"'
        std::string data_s = read_string();

        if (frame.active_child && frame.has_equal && !frame.active_child_has_data)
        {
          // 具名節點的資料賦值！
          apply_data(frame.active_child, data_s);
          frame.active_child_has_data = true;
          frame.has_equal = false;
        }
        else
        {
          // 獨立匿名節點！
          NodePtr anon = frame.current_parent->AddChild(u8"");
          if (anon)
          {
            apply_data(anon, data_s);
          }
          frame.active_child = anon;
          frame.active_child_has_data = true;
          frame.has_equal = false;
        }
        continue;
      }

      // 4. 遇到容器開啟符 '{' 或 '('：顯式壓棧進入深層，完全非遞迴
      if (ch == '{' || ch == '(')
      {
        char term = (ch == '{') ? '}' : ')';
        next();  // 消耗 '{' 或 '('
        NodePtr target = frame.active_child;
        if (!target)
        {
          if (parse_stack.size() == 1 && frame.current_parent == root_holder && !root_adopted_as_container &&
              root_holder->ChildCount() == 0)
          {
            target = root_holder;
            root_adopted_as_container = true;
          }
          else
          {
            target = frame.current_parent->AddChild(u8"");
          }
        }
        frame.active_child = nullptr;
        frame.has_equal = false;
        frame.active_child_has_data = false;

        parse_stack.push_back({std::move(target), term, nullptr, false, false});
        continue;
      }

      // 5. 任何其他符號或空白或非預期字元：由寬容狀態機安全無視！
      next();
    }

    // 拆箱判定：
    // 只有當 root_holder 未被頂層顯式括號直接作為匿名容器使用，
    // 且頂層恰好僅解析出唯一一個獨立子節點時，方可進行安全拆箱（將其從 root_holder 解除綁定傳回）。
    // 若頂層為匿名容器或多節點，則完整保留容器拓撲，絕不破壞結構一致性。
    if (!root_adopted_as_container && root_holder->ChildCount() == 1)
    {
      auto first = root_holder->GetFirstChild();
      first->DetachFromParent();
      return first;
    }

    return root_holder;
  }
};

}  // namespace ork::base
