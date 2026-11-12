#include "cliptiq/menu.hpp"
#include "cliptiq/snippets.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void check(bool b){if(!b)throw std::runtime_error("workspace assertion failed");}
int main(){try{
 MenuModel m;MenuItem group;group.label="Folder";
 MenuItem history;history.id="history";history.action=MenuAction::SelectHistory;history.label="Text";
 MenuItem snippet;snippet.id="snippet";snippet.action=MenuAction::SelectSnippet;snippet.label="Snippet";
 group.children={history,snippet};m.items={group};
 check(workspace_rows(m,MenuKind::History).size()==1);
 check(workspace_rows(m,MenuKind::Snippets).at(0).item.id=="snippet");
 m.items[0].children[1].enabled=false;check(workspace_rows(m,MenuKind::Snippets).empty());
 auto db=take(open_store(":memory:"));ClipboardSnapshot capture;
 capture.representations={{0,"windows","CF_TEXT",CanonicalKind::Text,Bytes(100000,'x')}};
 auto id=take(ingest(db,capture,{},1));check(bool(id));
 MenuCommand command{MenuAction::SelectHistory,*id,take(data_revision(db))};
 auto text=take(workspace_text(db,command));check(!text.empty()&&text.size()<=65536);
 auto invalid=workspace_text(db,{MenuAction::Quit,"",command.revision});check(!invalid);
 auto erased=erase_history(db,*id);check(bool(erased));check(!workspace_text(db,command));
 std::cout<<"PASS workspace filtering, bounded preview, action and stale-ID checks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
