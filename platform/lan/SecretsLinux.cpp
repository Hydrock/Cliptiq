#include "Secrets.hpp"
#include <gio/gio.h>
#include <memory>
#include <vector>
namespace cliptiq::lan::native {
namespace {
constexpr const char* service="org.freedesktop.secrets";
constexpr const char* root="/org/freedesktop/secrets";
constexpr const char* serviceInterface="org.freedesktop.Secret.Service";
constexpr const char* itemInterface="org.freedesktop.Secret.Item";
using Variant=std::unique_ptr<GVariant,decltype(&g_variant_unref)>;
Variant own(GVariant* v){return Variant(v,g_variant_unref);}
struct Client {
 GDBusConnection* bus=nullptr;
 std::string session;
 Error error{ErrorCode::Io,"Secret Service unavailable; start or unlock your desktop keyring"};
 Client(){GError* e=nullptr;bus=g_bus_get_sync(G_BUS_TYPE_SESSION,nullptr,&e);if(e)g_error_free(e);}
 ~Client(){if(bus){if(!session.empty())call(session.c_str(),"org.freedesktop.Secret.Session","Close",nullptr);g_object_unref(bus);}}
 Variant call(const char* path,const char* interface,const char* method,GVariant* args) {
  if(!bus){if(args)g_variant_unref(g_variant_ref_sink(args));return own(nullptr);}
  GError* e=nullptr;
  auto v=g_dbus_connection_call_sync(bus,service,path,interface,method,args,nullptr,G_DBUS_CALL_FLAGS_NONE,3000,nullptr,&e);
  if(e)g_error_free(e);return own(v);
 }
 bool open() {
  // Secret Service's standard plain session is confined to the user's local D-Bus.
  // Persistence and encryption at rest are owned by the desktop keyring backend.
  auto r=call(root,serviceInterface,"OpenSession",g_variant_new("(sv)","plain",g_variant_new_string("")));
  if(!r)return false;auto path=own(g_variant_get_child_value(r.get(),1));session=g_variant_get_string(path.get(),nullptr);return true;
 }
 bool unlock(const std::string& path) {
  const gchar* paths[]={path.c_str(),nullptr};
  auto r=call(root,serviceInterface,"Unlock",g_variant_new("(^ao)",paths));if(!r)return false;
  auto prompt=own(g_variant_get_child_value(r.get(),1));
  if(std::string(g_variant_get_string(prompt.get(),nullptr))!="/") {
   error={ErrorCode::Io,"Unlock your desktop keyring before accessing LAN credentials"};return false;
  }
  auto unlocked=own(g_variant_get_child_value(r.get(),0));
  // Unlock returns an empty list for already unlocked objects on some backends;
  // the subsequent operation is authoritative.
  return true;
 }
 std::vector<std::string> search(const std::string& ref,bool& success) {
  GVariantBuilder attrs;g_variant_builder_init(&attrs,G_VARIANT_TYPE("a{ss}"));
  g_variant_builder_add(&attrs,"{ss}","application","Cliptiq/LAN");
  g_variant_builder_add(&attrs,"{ss}","reference",ref.c_str());
  auto r=call(root,serviceInterface,"SearchItems",g_variant_new("(a{ss})",&attrs));success=bool(r);std::vector<std::string> paths;
  if(!r)return paths;
  for(gsize i=0;i<2;++i){auto list=own(g_variant_get_child_value(r.get(),i));GVariantIter iter;g_variant_iter_init(&iter,list.get());const gchar* path;
   while(g_variant_iter_next(&iter,"&o",&path)){if(i==1&&!unlock(path)){success=false;return {}; }paths.emplace_back(path);}}
  return paths;
 }
};
}
Result<void> write(const std::string& ref,const std::string& key) {
 Client c;if(!c.open())return c.error;
 auto alias=c.call(root,serviceInterface,"ReadAlias",g_variant_new("(s)","default"));if(!alias)return c.error;
 auto value=own(g_variant_get_child_value(alias.get(),0));std::string collection=g_variant_get_string(value.get(),nullptr);
 if(collection=="/")return Error{ErrorCode::Io,"Create a default desktop keyring before storing LAN credentials"};
 if(!c.unlock(collection))return c.error;
 GVariantBuilder attrs;g_variant_builder_init(&attrs,G_VARIANT_TYPE("a{ss}"));
 g_variant_builder_add(&attrs,"{ss}","application","Cliptiq/LAN");g_variant_builder_add(&attrs,"{ss}","reference",ref.c_str());
 GVariantBuilder props;g_variant_builder_init(&props,G_VARIANT_TYPE("a{sv}"));
 g_variant_builder_add(&props,"{sv}","org.freedesktop.Secret.Item.Label",g_variant_new_string("Cliptiq LAN peer"));
 g_variant_builder_add(&props,"{sv}","org.freedesktop.Secret.Item.Attributes",g_variant_builder_end(&attrs));
 auto empty=g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,nullptr,0,1);
 auto bytes=g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,key.data(),key.size(),1);
 auto secret=g_variant_new("(o@ay@ays)",c.session.c_str(),empty,bytes,"text/plain; charset=utf8");
 auto r=c.call(collection.c_str(),"org.freedesktop.Secret.Collection","CreateItem",g_variant_new("(a{sv}@(oayays)b)",&props,secret,TRUE));
 if(!r)return c.error;auto prompt=own(g_variant_get_child_value(r.get(),1));
 if(std::string(g_variant_get_string(prompt.get(),nullptr))!="/")return Error{ErrorCode::Io,"Unlock your desktop keyring before storing LAN credentials"};return {};
}
Result<std::string> read(const std::string& ref) {
 Client c;if(!c.open())return c.error;bool success=false;auto paths=c.search(ref,success);if(!success)return c.error;
 if(paths.empty())return Error{ErrorCode::NotFound,"Credential not found"};
 auto r=c.call(paths.front().c_str(),itemInterface,"GetSecret",g_variant_new("(o)",c.session.c_str()));if(!r)return c.error;
 auto secret=own(g_variant_get_child_value(r.get(),0));auto bytes=own(g_variant_get_child_value(secret.get(),2));gsize size=0;
 auto data=static_cast<const char*>(g_variant_get_fixed_array(bytes.get(),&size,1));
 if(size!=64)return Error{ErrorCode::Corrupt,"Invalid stored credential"};return std::string(data,size);
}
Result<void> erase(const std::string& ref) {
 Client c;bool success=false;auto paths=c.search(ref,success);if(!success)return c.error;
 for(const auto& path:paths){auto r=c.call(path.c_str(),itemInterface,"Delete",nullptr);if(!r)return c.error;
  auto prompt=own(g_variant_get_child_value(r.get(),0));if(std::string(g_variant_get_string(prompt.get(),nullptr))!="/")return Error{ErrorCode::Io,"Unlock your desktop keyring before deleting LAN credentials"};}
 return {};
}
}
