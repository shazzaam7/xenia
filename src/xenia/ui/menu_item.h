/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_MENU_ITEM_H_
#define XENIA_UI_MENU_ITEM_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "xenia/ui/ui_event.h"

namespace xe {
namespace ui {

class Window;

class MenuItem {
 public:
  typedef std::unique_ptr<MenuItem, void (*)(MenuItem*)> MenuItemPtr;

  enum class Type {
    kPopup,  // Popup menu (submenu)
    kSeparator,
    kNormal,  // Root menu
    kString,  // Menu is just a string
    kRadio,   // Checkable leaf item, drawn as a radio dot
  };

  static std::unique_ptr<MenuItem> Create(Type type);
  static std::unique_ptr<MenuItem> Create(Type type, const std::string& text);
  static std::unique_ptr<MenuItem> Create(Type type, const std::string& text,
                                          std::function<void()> callback);
  static std::unique_ptr<MenuItem> Create(Type type, const std::string& text,
                                          const std::string& hotkey,
                                          std::function<void()> callback);

  virtual ~MenuItem();

  MenuItem* parent_item() const { return parent_item_; }
  Type type() { return type_; }
  const std::string& text() { return text_; }
  const std::string& hotkey() { return hotkey_; }
  bool checked() const { return checked_; }

  // Checked state of a kRadio item. The base owns the value so checked() is
  // always truthful on every backend; OnCheckedChanged pushes it to whatever
  // native widget exists. Setting the value it already has does nothing, so
  // re-selecting the active item in a radio group is free.
  //
  // Like SetEnabled, this is not reflected immediately once the menu is
  // attached to a Window - call Window::CompleteMainMenuItemsUpdate after
  // finishing a batch of changes.
  void SetChecked(bool checked) {
    if (checked_ != checked) {
      checked_ = checked;
      OnCheckedChanged();
    }
  }

  // If the menu is currently attached to a Window, changes to it (such as the
  // elements and the enabled / disabled state) may be not reflected
  // immediately - call Window::CompleteMainMenuItemsUpdate when the
  // modifications are done.

  void AddChild(MenuItem* child_item);
  void AddChild(std::unique_ptr<MenuItem> child_item);
  void AddChild(MenuItemPtr child_item);
  void RemoveChild(MenuItem* child_item);
  MenuItem* child(size_t index);

  virtual void SetEnabled(bool enabled) {}

 protected:
  MenuItem(Type type, const std::string& text, const std::string& hotkey,
           std::function<void()> callback);

  virtual void OnChildAdded(MenuItem* child_item) {}
  virtual void OnChildRemoved(MenuItem* child_item) {}
  virtual void OnCheckedChanged() {}

  // Position of this item within its parent's children, or -1 when it has not
  // been attached to a parent yet. Backends that need to address this item
  // within its parent's native widget (Win32 check marks, for one) use this.
  int IndexInParent() const;

  // This MenuItem may be destroyed as a result of the callback, don't do
  // anything with it after the call.
  void OnSelected();

  Type type_;
  MenuItem* parent_item_;
  std::vector<MenuItemPtr> children_;
  std::string text_;
  std::string hotkey_;
  bool checked_ = false;

 private:
  std::function<void()> callback_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MENU_ITEM_H_
