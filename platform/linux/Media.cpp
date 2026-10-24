#include "../Media.hpp"
#include <cstring>
#include <gtk/gtk.h>
namespace cliptiq {
Result<MenuImage> decode_native_image(const Representation &r) {
  if (r.kind != CanonicalKind::Image || r.data.empty() ||
      r.data.size() > 32 * 1024 * 1024)
    return Error{ErrorCode::TooLarge, "Invalid encoded image"};
  GError *creation_error = nullptr;
  auto loader = gdk_pixbuf_loader_new_with_mime_type(r.native_type.c_str(),
                                                     &creation_error);
  if (!loader) {
    if (creation_error)
      g_error_free(creation_error);
    return Error{ErrorCode::Unsupported,
                 "System codec unavailable for offered image type"};
  }
  bool oversized = false;
  g_signal_connect(
      loader, "size-prepared",
      G_CALLBACK(+[](GdkPixbufLoader *l, gint w, gint h, gpointer p) {
        if (w <= 0 || h <= 0 || std::uint64_t(w) * h > media_pixel_limit) {
          *static_cast<bool *>(p) = true;
          gdk_pixbuf_loader_set_size(l, 1, 1);
        }
      }),
      &oversized);
  GError *error = nullptr;
  bool written =
      gdk_pixbuf_loader_write(loader, r.data.data(), r.data.size(), &error);
  if (error)
    g_error_free(error);
  error = nullptr;
  bool closed = gdk_pixbuf_loader_close(loader, &error);
  if (error)
    g_error_free(error);
  auto image = gdk_pixbuf_loader_get_pixbuf(loader);
  if (!written || !closed || oversized || !image) {
    g_object_unref(loader);
    return Error{ErrorCode::Corrupt,
                 "System codec rejected image or 40 MP limit"};
  }
  unsigned w = gdk_pixbuf_get_width(image), h = gdk_pixbuf_get_height(image);
  auto stride = gdk_pixbuf_get_rowstride(image);
  auto channels = gdk_pixbuf_get_n_channels(image);
  auto pixels = gdk_pixbuf_get_pixels(image);
  MenuImage out{w, h, Bytes(size_t(w) * h * 4)};
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) {
      auto src = pixels + size_t(y) * stride + x * channels;
      auto dst = out.rgba.data() + (size_t(y) * w + x) * 4;
      std::memcpy(dst, src, 3);
      dst[3] = channels == 4 ? src[3] : 255;
    }
  g_object_unref(loader);
  return out;
}
struct ScreenshotWatcher::Impl {
  GFileMonitor *monitor = nullptr;
  std::function<void(std::string)> callback;
};
ScreenshotWatcher::ScreenshotWatcher() : impl_(new Impl) {}
ScreenshotWatcher::~ScreenshotWatcher() { stop(); }
void ScreenshotWatcher::stop() {
  if (impl_->monitor) {
    g_signal_handlers_disconnect_by_data(impl_->monitor, impl_.get());
    g_file_monitor_cancel(impl_->monitor);
    g_object_unref(impl_->monitor);
    impl_->monitor = nullptr;
  }
  impl_->callback = {};
}
Result<void> ScreenshotWatcher::start(const std::string &root,
                                      std::function<void(std::string)> cb) {
  stop();
  auto file = g_file_new_for_path(root.c_str());
  GError *error = nullptr;
  impl_->monitor = g_file_monitor_directory(file, G_FILE_MONITOR_WATCH_MOVES,
                                            nullptr, &error);
  g_object_unref(file);
  if (!impl_->monitor) {
    std::string reason =
        error ? error->message : "Directory monitor unavailable";
    if (error)
      g_error_free(error);
    return Error{ErrorCode::Io, reason};
  }
  impl_->callback = std::move(cb);
  g_signal_connect(impl_->monitor, "changed",
                   G_CALLBACK(+[](GFileMonitor *, GFile *file, GFile *other,
                                  GFileMonitorEvent event, gpointer data) {
                     if (event != G_FILE_MONITOR_EVENT_CREATED &&
                         event != G_FILE_MONITOR_EVENT_CHANGED &&
                         event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
                         event != G_FILE_MONITOR_EVENT_MOVED_IN &&
                         event != G_FILE_MONITOR_EVENT_RENAMED)
                       return;
                     auto self = static_cast<Impl *>(data);
                     auto name = g_file_get_basename(
                         event == G_FILE_MONITOR_EVENT_RENAMED && other ? other
                                                                        : file);
                     if (name) {
                       if (self->callback)
                         self->callback(name);
                       g_free(name);
                     }
                   }),
                   impl_.get());
  return {};
}
} // namespace cliptiq
