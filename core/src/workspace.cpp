#include "cliptiq/menu.hpp"
#include "db.hpp"
namespace cliptiq {
std::vector<WorkspaceRow> workspace_rows(const MenuModel& model, MenuKind kind) {
  std::vector<WorkspaceRow> rows;
  if(!validate_menu_model(model))return rows;
  std::function<void(const std::vector<MenuItem>&,std::string)> visit = [&](const auto& items,std::string group){
    for(const auto& item:items){
      if(!item.enabled||item.separator) continue;
      const bool history=item.action==MenuAction::SelectHistory;
      const bool snippet=item.action==MenuAction::SelectSnippet;
      if((history&&kind!=MenuKind::Snippets)||(snippet&&kind!=MenuKind::History)) rows.push_back({item,group});
      if(!item.children.empty())visit(item.children, snippet||history?group:item.label);
    }
  };
  visit(model.items,{});return rows;
}
Result<std::string> workspace_text(Store& store,const MenuCommand& command){
 return detail::guarded<std::string>([&]()->Result<std::string>{
  if(command.action!=MenuAction::SelectHistory&&command.action!=MenuAction::SelectSnippet)
    return Error{ErrorCode::InvalidArgument,"Not a workspace item"};
  detail::exec(store.connection(),"BEGIN");
  struct Read {sqlite3* db;~Read(){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);}} read{store.connection()};
  auto valid=validate_menu_command(store,command);if(!valid)return valid.error();
  if(command.action==MenuAction::SelectSnippet){
    detail::Statement q(store.connection(),"SELECT substr(CAST(content AS BLOB),1,65536) FROM snippets WHERE id=?");
    q.bind(1,command.id);if(!q.row())return Error{ErrorCode::NotFound,"Snippet disappeared"};
    return index_text(q.bytes(0),TextEncoding::Utf8);
  }
  detail::Statement q(store.connection(),"SELECT substr(data,1,65536),text_encoding FROM representations WHERE entry_id=? AND kind IN (0,5,6) ORDER BY CASE kind WHEN 0 THEN 0 ELSE 1 END,ordinal LIMIT 1");
  q.bind(1,command.id);if(!q.row())return std::string{};
  return index_text(q.bytes(0),static_cast<TextEncoding>(q.number(1)));
 });
}
}
