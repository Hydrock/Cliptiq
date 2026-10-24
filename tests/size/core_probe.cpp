// Linked footprint fixture, not the product executable or an installer.
#include "cliptiq/history.hpp"
#ifdef CLIPTIQ_WITH_POLICY
#include "cliptiq/policy.hpp"
class ProbeSink : public cliptiq::CaptureSink {
    cliptiq::Store& store_;
public:
    explicit ProbeSink(cliptiq::Store& store):store_(store){}
    cliptiq::Result<std::optional<cliptiq::EntryId>> store(const cliptiq::ClipboardSnapshot& v) override {
        return cliptiq::ingest(store_,v,{},1);
    }
    void thumbnail(const cliptiq::EntryId&,const cliptiq::ClipboardSnapshot&) override {}
    void ocr(const cliptiq::EntryId&,const cliptiq::ClipboardSnapshot&) override {}
};
#endif
int main(){
 auto opened=cliptiq::open_store(":memory:");if(!opened)return 1;
 auto& store=opened.value();cliptiq::ClipboardSnapshot snapshot;
 snapshot.representations.push_back({0,"fixture","text/plain",cliptiq::CanonicalKind::Text,{'h','i'}});
#ifdef CLIPTIQ_WITH_POLICY
 cliptiq::Capabilities caps;caps.platform=cliptiq::CapturePlatform::X11;
 caps.capture={cliptiq::CapabilityStatus::Available,"fixture"};
 caps.native_types.push_back({"fixture","text/plain",cliptiq::CanonicalKind::Text});
 ProbeSink sink(store);
 auto decision=cliptiq::dispatch_capture(snapshot,{},caps,sink);
 if(!decision||decision.value().disposition!=cliptiq::CaptureDisposition::Accept)return 8;
 if(cliptiq::capture_reason_message(decision.value().reason).empty())return 9;
 auto rows=cliptiq::list_history(store);if(!rows||rows.value().empty())return 10;
 cliptiq::Result<std::optional<cliptiq::EntryId>> saved{std::optional<cliptiq::EntryId>{rows.value()[0].id}};
#else
 auto saved=cliptiq::ingest(store,snapshot,{},1);
#endif
 if(!saved||!saved.value())return 2;
 if(!cliptiq::load_snapshot(store,*saved.value()))return 3;
 if(!cliptiq::touch_history(store,*saved.value(),2))return 4;
 if(!cliptiq::list_history(store)||!cliptiq::data_revision(store))return 5;
 if(!cliptiq::tick_cleanup(store,{300000},0))return 6;
 if(!cliptiq::erase_history(store)||!cliptiq::compact_store(store))return 7;
 return 0;
}
