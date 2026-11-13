#pragma once
#include "cliptiq/native_menu.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
namespace cliptiq::windows {
// Public control IDs also let the process-level test use real UI commands.
constexpr int WorkspaceQuit = 7108;
class Workspace {
  HWND owner_, history_, snippets_, settings_, filter_, list_, heading_,
       summary_, preview_heading_, preview_, image_, copy_, erase_, clear_, quit_, status_;
  HWND snippet_panel_ = nullptr;
  bool editor_mounted_ = false;
  HFONT font_ = nullptr, title_font_ = nullptr, bold_font_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HIMAGELIST row_images_ = nullptr;
  std::vector<HWND> children_;
  std::vector<WorkspaceRow> rows_;
  std::vector<size_t> visible_;
  MenuModel model_;
  MenuKind page_ = MenuKind::History;
  std::function<void(MenuCommand)> action_;
  NativeMenuOptions options_;
  MenuPreviewProvider provider_;
  unsigned dpi_;
  bool changing_ = false;
  HWND control(const wchar_t*,const wchar_t*,DWORD,int,DWORD=0);
  int px(int) const;
  void filter();
  void selected();
  void execute(bool);
  const WorkspaceRow* selection() const;
public:
  Workspace(HWND,const NativeMenuOptions&,std::function<void(MenuCommand)>,MenuPreviewProvider);
  ~Workspace();
  void layout(unsigned dpi);
  void update(MenuModel);
  Result<void> select_page(MenuKind);
  void status(const std::string&);
  bool message(UINT,WPARAM,LPARAM,LRESULT&);
  bool key(MSG&);
};
}
