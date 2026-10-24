#pragma once
#include "history.hpp"
#include <functional>
namespace cliptiq {
enum class MenuKind { Main, History, Snippets };
enum class MenuAction {
  None,
  SelectHistory,
  SelectSnippet,
  ClearHistory,
  OpenEditor,
  OpenSettings,
  Quit,
  CheckUpdates,
  ExportDiagnostics,
  DownloadUpdates
};
enum class TrayStyle { Standard, Reversed, Hidden };
struct PreviewRef {
  EntryId entry_id;
  std::optional<std::uint32_t> rgba;
};
struct MenuItem {
  std::string label;
  MenuAction action = MenuAction::None;
  std::string id;
  std::vector<MenuItem> children;
  std::optional<unsigned> number;
  std::string shortcut, tooltip;
  std::optional<PreviewRef> preview;
  bool enabled = true, separator = false;
  CanonicalKind kind = CanonicalKind::Text;
};
struct MenuModel {
  std::vector<MenuItem> items;
  std::uint64_t revision = 0;
  MenuKind kind = MenuKind::Main;
};
struct MenuSettings {
  std::size_t history_limit = 1000;
  std::size_t inline_count = 0, folder_count = 10, title_characters = 20,
              tooltip_characters = 200;
  bool snippets_above = false, numbered = true, zero_start = false,
       numeric_selection = false, icons = true, tooltips = true,
       image_previews = true, hex_swatches = true;
  unsigned preview_width = 100, preview_height = 32;
  SortOrder sort_order = SortOrder::LastUsed;
};
struct InvocationContext {
  std::string target, activation_token;
  bool keyboard = false;
  std::optional<int> x, y;
  unsigned modifiers = 0;
};
struct MenuCommand {
  MenuAction action = MenuAction::None;
  std::string id;
  std::uint64_t revision = 0;
  InvocationContext invocation;
};
Result<void> validate_menu_model(const MenuModel &);
Result<MenuModel> build_menu(Store &, MenuKind, const MenuSettings & = {});
Result<MenuModel> build_folder_menu(Store &, const FolderId &,
                                    const MenuSettings & = {});
Result<void> execute_menu_clear(Store &, const MenuCommand &);
Result<void> validate_menu_command(Store &, const MenuCommand &);
Result<std::string> menu_text(const std::string &, std::size_t,
                              bool first_line = true);
} // namespace cliptiq

namespace cliptiq { Result<std::string> index_text(const Bytes&, TextEncoding); }
