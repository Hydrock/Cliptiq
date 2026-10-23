#include "ocr.h"
#include <glib.h>
#include <cairo.h>
#include <cstring>
#include <unistd.h>
const char* native_ocr_test() {
    gchar* engine=g_find_program_in_path("tesseract");
    if(!engine) return "engine_not_installed";
    gchar* filename=nullptr; GError* error=nullptr;
    int fd=g_file_open_tmp("cliptiq-ocr-XXXXXX",&filename,&error);
    if(fd<0) { g_free(engine); if(error) g_error_free(error); return "tempfile_error"; }
    close(fd);
    cairo_surface_t* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,400,80); cairo_t* context=cairo_create(surface);
    cairo_set_source_rgb(context,1,1,1); cairo_paint(context); cairo_set_source_rgb(context,0,0,0);
    cairo_select_font_face(context,"sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(context,40);
    cairo_move_to(context,10,55); cairo_show_text(context,"Cliptiq");
    cairo_status_t image_status=cairo_surface_write_to_png(surface,filename); cairo_destroy(context); cairo_surface_destroy(surface);
    if(image_status!=CAIRO_STATUS_SUCCESS) { unlink(filename); g_free(filename); g_free(engine); return "image_error"; }
    gchar* argv[]={engine,filename,(gchar*)"stdout",(gchar*)"-l",(gchar*)"eng",(gchar*)"--psm",(gchar*)"7",nullptr};
    gchar *output=nullptr,*stderr_text=nullptr; gint exit_status=0;
    gboolean ran=g_spawn_sync(nullptr,argv,nullptr,G_SPAWN_DEFAULT,nullptr,nullptr,&output,&stderr_text,&exit_status,&error);
    bool found=ran&&exit_status==0&&output&&std::strstr(output,"Cliptiq");
    const char* status=!ran ? "engine_launch_failed" : (exit_status!=0 ? "engine_failed" : (found ? "recognized" : "completed_text_mismatch"));
    unlink(filename); g_free(filename); g_free(engine); g_free(output); g_free(stderr_text); if(error) g_error_free(error);
    return status;
}
