// Linked footprint fixture, not the product executable or an installer.
#include "cliptiq/history.hpp"
int main(){
 auto opened=cliptiq::open_store(":memory:");if(!opened)return 1;
 auto& store=opened.value();cliptiq::ClipboardSnapshot snapshot;
 snapshot.representations.push_back({0,"fixture","text/plain",cliptiq::CanonicalKind::Text,{'h','i'}});
 auto saved=cliptiq::ingest(store,snapshot,{},1);if(!saved||!saved.value())return 2;
 if(!cliptiq::load_snapshot(store,*saved.value()))return 3;
 if(!cliptiq::touch_history(store,*saved.value(),2))return 4;
 if(!cliptiq::list_history(store)||!cliptiq::data_revision(store))return 5;
 if(!cliptiq::tick_cleanup(store,{300000},0))return 6;
 if(!cliptiq::erase_history(store)||!cliptiq::compact_store(store))return 7;
 return 0;
}
