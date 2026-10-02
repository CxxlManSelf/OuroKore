#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <ourokore/base/Tree.hpp>
#include <ourokore/base/utf8.hpp>

namespace ork::base
{

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
        case ']': out += "\\]"; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
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
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
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

  template <typename T = std::string, typename Func = std::nullptr_t>
  static void Serialize(
      std::ostream &os,
      const std::shared_ptr<TreeNode<T>> &root,
      Func &&data_to_string = nullptr,
      size_t indent_width = 2)
  {
    Serialize<T>(os, std::const_pointer_cast<const TreeNode<T>>(root), std::forward<Func>(data_to_string), indent_width);
  }

  template <typename T = std::string, typename Func = std::nullptr_t>
  static void Serialize(
      std::ostream &os,
      const std::shared_ptr<const TreeNode<T>> &root,
      Func &&data_to_string = nullptr,
      size_t indent_width = 2)
  {
    if (!root)
    {
      return;
    }

    auto convert_data = [&](const T &d) -> std::string {
      if constexpr (!std::is_same_v<std::decay_t<Func>, std::nullptr_t>)
      {
        return data_to_string(d);
      }
      else if constexpr (std::is_same_v<T, std::string>)
      {
        return d;
      }
      else if constexpr (std::is_same_v<T, std::u8string>)
      {
        return ork::utf8::to_string(d);
      }
      else
      {
        return std::string(d);
      }
    };

    struct Frame
    {
      std::shared_ptr<const TreeNode<T>> node;
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
          std::string indent = MakeIndent(f.depth, indent_width);
          if (f.node->IsArray())
          {
            os << indent << ')' << '\n';
          }
          else
          {
            os << indent << '}' << '\n';
          }
        }
        continue;
      }

      if (!f.node)
      {
        continue;
      }

      std::string indent = MakeIndent(f.depth, indent_width);
      std::string name_s = ork::utf8::to_string(f.node->GetName());
      std::string escaped_name = EscapeName(name_s);
      std::string data_s = convert_data(f.node->GetData());
      std::string escaped_data = EscapeContent(data_s);

      // 輸出節點開頭
      if (!f.is_array_element || !escaped_name.empty())
      {
        os << indent << '[' << escaped_name << ']';
        if (!escaped_data.empty())
        {
          os << " = \"" << escaped_data << "\"";
        }
        os << '\n';
      }
      else
      {
        // 純陣列元素且無名稱
        if (!escaped_data.empty())
        {
          os << indent << "\"" << escaped_data << "\"" << '\n';
        }
      }

      // 檢查是否具有陣列元素或具名字節點
      bool is_array = f.node->IsArray();
      size_t elem_count = f.node->ElementCount();
      size_t child_count = f.node->ChildCount();

      if (is_array && elem_count > 0)
      {
        os << indent << '(' << '\n';
        stk.push_back({f.node, 1, f.depth, false});

        // 倒序壓棧確保循序輸出
        for (size_t i = elem_count; i > 0; --i)
        {
          auto elem = std::const_pointer_cast<const TreeNode<T>>(f.node->GetElementAt(i - 1));
          if (elem)
          {
            stk.push_back({elem, 0, f.depth + 1, true});
          }
        }
      }
      else if (!is_array && child_count > 0)
      {
        os << indent << '{' << '\n';
        stk.push_back({f.node, 1, f.depth, false});

        std::vector<std::shared_ptr<const TreeNode<T>>> children;
        children.reserve(child_count);
        f.node->ForEachChild([&children](const auto &c) {
          if (c)
          {
            children.push_back(std::const_pointer_cast<const TreeNode<T>>(c));
          }
        });

        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
          stk.push_back({*it, 0, f.depth + 1, false});
        }
      }
    }
  }

  // =========================================================================
  // 反序列化 (Deserialize) - 寬容型狀態機 (Fault-Tolerant FSM)
  // =========================================================================

  template <typename T = std::string>
  static std::shared_ptr<TreeNode<T>> Deserialize(
      std::istream &is,
      const std::function<T(const std::string &)> &string_to_data = [](const std::string &s) -> T {
        if constexpr (std::is_same_v<T, std::string>)
        {
          return s;
        }
        else if constexpr (std::is_same_v<T, std::u8string>)
        {
          return ork::utf8::to_u8string(s);
        }
        else
        {
          return T(s);
        }
      })
  {
    std::string text((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return DeserializeFromString<T>(text, string_to_data);
  }

  template <typename T = std::string>
  static std::shared_ptr<TreeNode<T>> DeserializeFromString(
      std::string_view text,
      const std::function<T(const std::string &)> &string_to_data = [](const std::string &s) -> T {
        if constexpr (std::is_same_v<T, std::string>)
        {
          return s;
        }
        else if constexpr (std::is_same_v<T, std::u8string>)
        {
          return ork::utf8::to_u8string(s);
        }
        else
        {
          return T(s);
        }
      })
  {
    using NodePtr = std::shared_ptr<TreeNode<T>>;

    size_t pos = 0;
    const size_t len = text.size();

    auto peek = [&]() -> int {
      return (pos < len) ? static_cast<unsigned char>(text[pos]) : -1;
    };

    auto next = [&]() -> int {
      return (pos < len) ? static_cast<unsigned char>(text[pos++]) : -1;
    };

    // 讀取名稱至 ']'（支援 \] 與 \\ 轉義）
    auto read_name = [&]() -> std::string {
      std::string name;
      while (pos < len)
      {
        char c = text[pos++];
        if (c == '\\' && pos < len)
        {
          char esc = text[pos++];
          switch (esc)
          {
            case ']': name.push_back(']'); break;
            case '\\': name.push_back('\\'); break;
            case 'n': name.push_back('\n'); break;
            case 'r': name.push_back('\r'); break;
            case 't': name.push_back('\t'); break;
            default: name.push_back(esc); break;
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
    auto read_string = [&]() -> std::string {
      std::string content;
      while (pos < len)
      {
        char c = text[pos++];
        if (c == '\\' && pos < len)
        {
          char esc = text[pos++];
          switch (esc)
          {
            case '"': content.push_back('"'); break;
            case '\\': content.push_back('\\'); break;
            case 'n': content.push_back('\n'); break;
            case 'r': content.push_back('\r'); break;
            case 't': content.push_back('\t'); break;
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
                  auto hex_val = [](char ch) -> int {
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
            default: content.push_back(esc); break;
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

    parse_container = [&](const NodePtr &current_parent, char terminator) {
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

        // 1. 遇到節點名稱標記 '['
        if (ch == '[')
        {
          next();  // 消耗 '['
          std::string name_s = read_name();
          std::u8string name_u8 = ork::utf8::to_u8string(name_s);

          if (is_array_mode)
          {
            // 陣列中若出現具名節點，作為帶有名稱的陣列元素加入
            active_child = current_parent->PushElement(NodeKind::Object);
            if (active_child)
            {
              active_child->SetName(name_u8);
            }
          }
          else
          {
            active_child = current_parent->AddBackChild(name_u8, NodeKind::Object);
          }
          active_child_has_data = false;
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
              NodePtr elem = current_parent->PushElement(NodeKind::Object);
              if (elem)
              {
                elem->SetData(string_to_data(data_s));
              }
              active_child = nullptr;
              active_child_has_data = false;
            }
            else
            {
              // 賦值給剛剛建構但尚未賦值的 active_child
              active_child->SetData(string_to_data(data_s));
              active_child_has_data = true;
            }
          }
          else
          {
            // 在物件中：若有 active_child 且尚未有資料，填入資料
            if (active_child && !active_child_has_data)
            {
              active_child->SetData(string_to_data(data_s));
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
          target->SetKind(NodeKind::Object);
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
          target->SetKind(NodeKind::Array);
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
    NodePtr root_holder = TreeNode<T>::CreateRoot(u8"__ROOT__");
    parse_container(root_holder, '\0');

    // 若解析出唯一頂層子節點，則傳回該節點作為根；否則傳回 root_holder
    if (root_holder->ChildCount() == 1 && root_holder->ElementCount() == 0)
    {
      auto first = root_holder->GetFirstChild();
      first->DetachFromParent();
      return first;
    }
    if (root_holder->ElementCount() == 1 && root_holder->ChildCount() == 0)
    {
      auto first = root_holder->GetElementAt(0);
      first->DetachFromParent();
      return first;
    }

    return root_holder;
  }
};

}  // namespace ork::base
