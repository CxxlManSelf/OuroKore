#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "ourokore/base/utf8.hpp"
#include "ourokore/component/Handles.hpp"
#include "ourokore/part/OuroPart.hpp"

namespace ork
{

/**
 * @brief 次世代單一持有零件句柄 (PartHandle<T>)
 * 對應於 OwningHandle<T>，專門用於 OuroPart 內部的次世代標準裝配插槽。
 * 限制 T 必須繼承自 OuroPart，並自動向父 OuroPart 物件的第二套名冊 (m_part_handles) 註冊。
 */
template <typename T>
class PartHandle : public OwningHandle<T>
{
public:
  using RawT = std::remove_const_t<T>;
  using OwningHandle<T>::operator=;

  explicit PartHandle(std::string slot_name) :
      OwningHandle<T>(std::move(slot_name), false) // auto_register = false，避免進入第一套私有名冊
  {
    AttachToPartHost();
  }

  template <typename StrT, typename = std::enable_if_t<
      !std::is_base_of_v<OwningContainerHandle, std::decay_t<StrT>> &&
      !std::is_same_v<std::decay_t<StrT>, std::string>
  >>
  explicit PartHandle(const StrT &slot_name) :
      PartHandle(ork::utf8::to_string(slot_name))
  {
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  PartHandle(std::string slot_name, const OuroPtr<U> &ptr) :
      OwningHandle<T>(std::move(slot_name), false)
  {
    AttachToPartHost();
    this->SetTarget(ptr.GetTargetID());
  }

  template <typename StrT, typename U, typename = std::enable_if_t<
      !std::is_same_v<std::decay_t<StrT>, std::string> &&
      std::is_convertible_v<std::remove_const_t<U> *, RawT *>
  >>
  PartHandle(const StrT &slot_name, const OuroPtr<U> &ptr) :
      PartHandle(ork::utf8::to_string(slot_name), ptr)
  {
  }

  virtual ~PartHandle() noexcept
  {
    // 延遲至此進行完整型別約束，完美支援遞迴/不完整型別定義
    static_assert(std::is_base_of_v<OuroPart, RawT>, 
                  "OuroKore Part Invariant: PartHandle<T> can only hold types derived from OuroPart!");
  }

private:
  void AttachToPartHost()
  {
    OuroObject *active_obj = detail::GetActiveObject();
    if (active_obj)
    {
      auto *part = dynamic_cast<OuroPart *>(active_obj);
      if (!part)
      {
        throw std::runtime_error("OuroKore Invariant Error: PartHandle '" + this->GetSlotName() +
                                 "' can only be declared inside a class derived from OuroPart!");
      }
      part->RegisterPartHandle(this);
    }
  }
};

/**
 * @brief 次世代容器持有零件句柄 (PartContainerHandle<T>)
 * 對應於 OwningContainerHandle，專門用於 OuroPart 內部的次世代標準多重零件裝配插槽。
 */
template <typename T>
class PartContainerHandle : public OwningContainerHandle
{
public:
  using RawT = std::remove_const_t<T>;

  explicit PartContainerHandle(std::string slot_name) :
      OwningContainerHandle(std::move(slot_name), false)
  {
    AttachToPartHost();
  }

  template <typename StrT, typename = std::enable_if_t<
      !std::is_base_of_v<OwningContainerHandle, std::decay_t<StrT>> &&
      !std::is_same_v<std::decay_t<StrT>, std::string>
  >>
  explicit PartContainerHandle(const StrT &slot_name) :
      PartContainerHandle(ork::utf8::to_string(slot_name))
  {
  }

  virtual ~PartContainerHandle() noexcept
  {
    static_assert(std::is_base_of_v<OuroPart, RawT>, 
                  "OuroKore Part Invariant: PartContainerHandle<T> can only hold types derived from OuroPart!");
  }

  // 便利加入與查詢方法
  void Add(const OuroPtr<T> &ptr)
  {
    this->AddTarget(ptr.GetTargetID());
  }

private:
  void AttachToPartHost()
  {
    OuroObject *active_obj = detail::GetActiveObject();
    if (active_obj)
    {
      auto *part = dynamic_cast<OuroPart *>(active_obj);
      if (!part)
      {
        throw std::runtime_error("OuroKore Invariant Error: PartContainerHandle '" + this->GetSlotName() +
                                 "' can only be declared inside a class derived from OuroPart!");
      }
      part->RegisterPartHandle(this);
    }
  }
};

} // namespace ork
