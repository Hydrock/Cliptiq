#include "Images.hpp"
#include <gdk-pixbuf/gdk-pixbuf.h>
namespace cliptiq::lan {
Result<Bytes> network_png(const Representation& r) {
 if(r.kind!=CanonicalKind::Image||r.data.empty()||r.data.size()>32*1024*1024)return Error{ErrorCode::TooLarge,"Invalid LAN image"};
 auto loader=gdk_pixbuf_loader_new();if(!loader)return Error{ErrorCode::Io,"Image codec unavailable"};
 struct Guard{GdkPixbufLoader *value;~Guard(){g_object_unref(value);}} guard{loader};
 bool oversized=false;g_signal_connect(loader,"size-prepared",G_CALLBACK(+[](GdkPixbufLoader*l,gint w,gint h,gpointer p){if(w<=0||h<=0||w>8192||h>8192||std::uint64_t(w)*h>8000000){*static_cast<bool*>(p)=true;gdk_pixbuf_loader_set_size(l,1,1);}}),&oversized);
 GError *error=nullptr;bool wrote=gdk_pixbuf_loader_write(loader,r.data.data(),r.data.size(),&error);if(error)g_error_free(error);error=nullptr;bool closed=gdk_pixbuf_loader_close(loader,&error);if(error)g_error_free(error);
 auto image=gdk_pixbuf_loader_get_pixbuf(loader);if(!wrote||!closed||oversized||!image)return Error{ErrorCode::Corrupt,"Invalid or oversized LAN image"};
 if(r.native_type=="image/png"){if(r.data.size()>8*1024*1024)return Error{ErrorCode::TooLarge,"LAN PNG exceeds 8 MiB"};return r.data;}
 gchar *buffer=nullptr;gsize length=0;error=nullptr;if(!gdk_pixbuf_save_to_buffer(image,&buffer,&length,"png",&error,nullptr)){if(error)g_error_free(error);return Error{ErrorCode::Io,"PNG encoding failed"};}
 if(length>8*1024*1024){g_free(buffer);return Error{ErrorCode::TooLarge,"LAN PNG exceeds 8 MiB"};}
 Bytes result(reinterpret_cast<unsigned char*>(buffer),reinterpret_cast<unsigned char*>(buffer)+length);g_free(buffer);return result;
}
}
