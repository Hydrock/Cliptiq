#include "native.h"
#include "core.h"
#include <gtk/gtk.h>
#include <fstream>
#include <unistd.h>
static GtkWidget* window;
static GMainLoop* loop;
#if GTK_MAJOR_VERSION >= 4
static gboolean close_window(GtkWidget*, gpointer) { if(loop) g_main_loop_quit(loop); window=nullptr; return FALSE; }
#else
static void close_window(GtkWidget*, gpointer) { if(loop) g_main_loop_quit(loop); window=nullptr; }
#endif
static void copy_text(GtkWidget*,gpointer) {
    unsigned char data[4096]; int len=probe_get(0,"text/plain",data,sizeof data-1); if(len<0) return; data[len]=0;
#if GTK_MAJOR_VERSION >= 4
    gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()),(char*)data);
#else
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),(char*)data,len);
#endif
}
bool native_setup(bool visible) {
#if GTK_MAJOR_VERSION >= 4
    if (!gtk_init_check()) return false;
    window=gtk_window_new();
#else
    if (!gtk_init_check(nullptr,nullptr)) return false;
    window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
    gtk_window_set_title(GTK_WINDOW(window),"Cliptiq — footprint prototype");
#if GTK_MAJOR_VERSION >= 4
    g_signal_connect(window,"close-request",G_CALLBACK(close_window),nullptr);
#else
    g_signal_connect(window,"destroy",G_CALLBACK(close_window),nullptr);
#endif
    gtk_window_set_default_size(GTK_WINDOW(window),460,180);
    GtkWidget* box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    GtkWidget* label=gtk_label_new("33 fixture entries / 35 representations\nShared native GTK host; no history monitoring.\nLinux tray/portal integrations not implemented in this spike.");
    GtkWidget* button=gtk_button_new_with_label("Copy text fixture");
    g_signal_connect(button,"clicked",G_CALLBACK(copy_text),nullptr);
#if GTK_MAJOR_VERSION >= 4
    gtk_box_append(GTK_BOX(box),label); gtk_box_append(GTK_BOX(box),button);
    gtk_window_set_child(GTK_WINDOW(window),box);
    if(visible) gtk_window_present(GTK_WINDOW(window));
#else
    gtk_box_pack_start(GTK_BOX(box),label,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(box),button,FALSE,FALSE,0);
    gtk_container_add(GTK_CONTAINER(window),box);
    if(visible) gtk_widget_show_all(window);
#endif
    return true;
}
void native_loop(){ loop=g_main_loop_new(nullptr,FALSE); g_main_loop_run(loop); g_main_loop_unref(loop); loop=nullptr; }
void native_schedule_close(){
    // gtk_window_close() ignores unrealized hidden windows; exercise the real
    // user-close path in Xvfb rather than timing out on a nonexistent surface.
#if GTK_MAJOR_VERSION >= 4
    gtk_window_present(GTK_WINDOW(window));
#else
    gtk_widget_show_all(window);
#endif
    g_timeout_add(25,[](gpointer)->gboolean { if(window) gtk_window_close(GTK_WINDOW(window)); return G_SOURCE_REMOVE; },nullptr);
}
void native_cleanup(){ if(window) {
#if GTK_MAJOR_VERSION >= 4
    gtk_window_destroy(GTK_WINDOW(window));
#else
    gtk_widget_destroy(window);
#endif
} }
size_t native_memory(){ std::ifstream file("/proc/self/statm"); size_t pages=0,resident=0; if(file>>pages>>resident) return resident*size_t(sysconf(_SC_PAGESIZE)); return 0; }
