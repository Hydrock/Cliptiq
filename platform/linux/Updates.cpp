#include "../Updates.hpp"
#include <gtk/gtk.h>
#include <fstream>
#include <chrono>
#include <condition_variable>
#include <thread>
namespace cliptiq {
Result<std::string> native_update_feed(){
  struct Deadline {
    GCancellable* cancel=g_cancellable_new();std::mutex mutex;std::condition_variable changed;bool done=false;
    std::thread timer;
    Deadline():timer([this]{std::unique_lock<std::mutex> lock(mutex);if(!changed.wait_for(lock,std::chrono::seconds(20),[this]{return done;}))g_cancellable_cancel(cancel);}){}
    ~Deadline(){{std::lock_guard<std::mutex> lock(mutex);done=true;}changed.notify_one();timer.join();g_object_unref(cancel);}
  } deadline;
  auto client=g_socket_client_new();g_socket_client_set_timeout(client,5);g_socket_client_set_tls(client,TRUE);
  GError*error=nullptr;
  auto connection=g_socket_client_connect_to_host(client,"github.com",443,deadline.cancel,&error);
  g_object_unref(client);
  if(!connection){if(error)g_error_free(error);return Error{ErrorCode::Io,"Release check offline, TLS unavailable or timed out"};}
  auto started=std::chrono::steady_clock::now();
  const char request[]="GET /Hydrock/Cliptiq/releases.atom HTTP/1.0\r\nHost: github.com\r\nUser-Agent: Cliptiq/0.1.0\r\nAccept: application/atom+xml\r\nConnection: close\r\n\r\n";
  bool good=g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)),request,sizeof(request)-1,nullptr,deadline.cancel,&error);
  std::string response;char block[4096];
  while(good){
    if(std::chrono::steady_clock::now()-started>std::chrono::seconds(15)){good=false;break;}
    auto n=g_input_stream_read(g_io_stream_get_input_stream(G_IO_STREAM(connection)),block,sizeof(block),deadline.cancel,&error);
    if(n<0){good=false;break;}if(!n)break;
    if(response.size()+n>147456){good=false;break;}response.append(block,n);
  }
  g_io_stream_close(G_IO_STREAM(connection),nullptr,nullptr);g_object_unref(connection);if(error)g_error_free(error);
  auto header=response.find("\r\n\r\n");
  if(!good||header==std::string::npos||header>16384||response.compare(0,12,"HTTP/1.1 200")!=0&&response.compare(0,12,"HTTP/1.0 200")!=0)return Error{ErrorCode::Io,"Release check failed or exceeded timeout/size limit"};
  return response.substr(header+4);
}
Result<void> NativeUpdates::initialize(){return {};}
bool NativeUpdates::manages_schedule()const{return false;}
std::int64_t NativeUpdates::last_check()const{return 0;}
bool NativeUpdates::automatic_available()const{return true;}
void NativeUpdates::preferences(bool,std::int64_t){}
std::string NativeUpdates::status()const{return metadata_status()+"; use signed package repository (deb/rpm) or explicit portable download/install. No automatic installation.";}
Result<void> NativeUpdates::check_now(bool automatic){
  (void)automatic;return check_metadata();
}
Result<void> NativeUpdates::open_download(){
  // No shell, sudo, arbitrary package path or executable replacement. Repository
  // packages are installed/updated by the user's package manager, not this app.
  GError*error=nullptr;
  if(!g_app_info_launch_default_for_uri("https://github.com/Hydrock/Cliptiq/releases",nullptr,&error)){
    if(error)g_error_free(error);return Error{ErrorCode::Io,"Cannot open project releases"};
  }
  return {};
}
Result<void> NativeUpdates::export_diagnostics(){
  auto dialog=gtk_file_chooser_dialog_new("Export Local Diagnostics",nullptr,GTK_FILE_CHOOSER_ACTION_SAVE,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,nullptr);
  gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),"Cliptiq-diagnostics.txt");
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);
  bool failed=false;
  if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){
    auto path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    if(!path)failed=true;
    else {std::ofstream f(path,std::ios::binary);f<<local_diagnostics();f.flush();failed=!f;g_free(path);}
  }
  gtk_widget_destroy(dialog);
  if(failed)return Error{ErrorCode::Io,"Cannot export local diagnostics"};return {};
}
}
