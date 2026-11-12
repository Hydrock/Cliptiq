// Isolated UI experiment, not a replacement for the production settings window.
#include "cliptiq/settings.hpp"
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef SETTINGS_RAVEN
#include <Raven.h>
#endif

using namespace cliptiq;
using Clock = std::chrono::steady_clock;
namespace {
constexpr UINT Reopen = WM_APP + 41, Quit = WM_APP + 42;
bool reopen_requested = false, quit_requested = false;
LRESULT CALLBACK control_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == Reopen) { reopen_requested = true; return 0; }
  if (msg == Quit) { quit_requested = true; return 0; }
  return DefWindowProcW(w, msg, wp, lp);
}
double ms(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void require(Result<void> result) {
  if (!result) throw std::runtime_error(result.error().message);
}

#ifdef SETTINGS_RAVEN
class SettingsUi {
  struct Control { std::string key; Rid widget; bool boolean, multi; };
  Rid window_ = R_INVALID_ID, status_ = R_INVALID_ID;
  std::vector<Control> controls_;
  Settings settings_;
  std::function<Result<void>(Settings)> apply_;
  void save() {
    std::map<std::string, std::string> values;
    for (const auto& c : controls_) {
      if (c.boolean) values[c.key] = Rcheckbox_is_checked(c.widget) ? "1" : "0";
      else values[c.key] = c.multi ? Rtext_editor_get_text(c.widget) : Rtext_input_get_text(c.widget);
    }
    auto edited = edit_settings(settings_, values);
    if (!edited) { Rlabel_set_text(status_, edited.error().message.c_str()); return; }
    auto result = apply_(edited.value());
    if (!result) { Rlabel_set_text(status_, result.error().message.c_str()); return; }
    settings_ = std::move(edited.value());
    ++settings_.revision;
    Rlabel_set_text(status_, "Saved to isolated experiment profile. No system settings were changed.");
  }
  static int record(Rid widget, const REvent* event, void*) {
    if (event->type != R_EVENT_KEY_DOWN) return R_FALSE;
    if (!event->modifiers && event->key == R_KEY_BACKSPACE) {
      Rtext_input_set_text(widget, ""); return R_TRUE;
    }
    // Raven's modifier bit layout is translated explicitly below.
    unsigned mods = 0;
    if (event->modifiers & R_MOD_CTRL) mods |= 1;
    if (event->modifiers & R_MOD_SHIFT) mods |= 2;
    if (event->modifiers & R_MOD_ALT) mods |= 4;
    if (event->modifiers & R_MOD_SUPER) mods |= 8;
    std::string key;
    if ((event->key >= 'A' && event->key <= 'Z') || (event->key >= '0' && event->key <= '9')) key = char(event->key);
    else if (event->key >= R_KEY_F1 && event->key <= R_KEY_F11) key = "F" + std::to_string(event->key - R_KEY_F1 + 1);
    if (mods && !key.empty()) {
      auto value = format_chord({mods, key});
      if (parse_chord(value)) { Rtext_input_set_text(widget, value.c_str()); return R_TRUE; }
    }
    return R_FALSE;
  }
public:
  ~SettingsUi() { if (Ris_initialized()) Rshutdown(); }
  void show(Settings settings, const Capabilities& caps, std::function<Result<void>(Settings)> apply) {
    if (!Ris_initialized()) {
      // Keep Raven's default worker/render-thread configuration; disable visual
      // animations to compare a static form with the nonanimated Win32 form.
      RInitDesc init{}; init.backend = R_BACKEND_VULKAN; init.app_name = "Cliptiq settings experiment";
      if (!Rinit_ex(&init)) throw std::runtime_error("Raven Vulkan initialization failed");
      Rset_animations_enabled(R_FALSE);
      Rlook_set(R_LOOK_WINDOWS_LIGHT);
    }
    if (window_ != R_INVALID_ID) Rwindow_destroy(window_);
    controls_.clear(); settings_ = std::move(settings); apply_ = std::move(apply);
    window_ = Rwindow_create("Cliptiq Settings - Raven experiment", 904, 761, R_WINDOW_HIDDEN);
    if (window_ == R_INVALID_ID) throw std::runtime_error("Raven window creation failed");
    Rwindow_set_close_callback(window_, [](Rid, void*) { quit_requested = true; return R_FALSE; }, nullptr);
    auto root = Rwidget_create(R_WIDGET_VBOX);
    Rslot_set_padding(root, 12, 12, 12, 12);
    Rpanel_set_spacing(root, 10);
    Rwindow_set_content(window_, root);
    auto tabs = Rwidget_add_child(root, Rwidget_create(R_WIDGET_TAB_VIEW));
    Rslot_set_fill(tabs, 1);
    const char* names[] = {"General", "Menu", "Types", "Excluded", "Shortcuts", "Updates", "Beta", "About"};
    Rid pages[8];
    for (int i = 0; i < 8; ++i) {
      auto scroll = Rwidget_create(R_WIDGET_SCROLL_BOX);
      pages[i] = Rwidget_add_child(scroll, Rwidget_create(R_WIDGET_VBOX));
      Rslot_set_padding(pages[i], 12, 12, 12, 12);
      Rpanel_set_spacing(pages[i], 8);
      Rtab_view_add_tab(tabs, names[i], scroll);
    }
    for (const auto& f : settings_fields(settings_, caps)) {
      auto row = Rwidget_add_child(pages[unsigned(f.group)], Rwidget_create(R_WIDGET_HBOX));
      Rpanel_set_spacing(row, 16);
      auto label = Rwidget_add_child(row, Rlabel_create(f.label.c_str()));
      Rlabel_set_wrap(label, R_TRUE); Rslot_set_fill(label, 1);
      const bool multi = f.key == "excluded";
      auto input = Rwidget_add_child(row, f.boolean ? Rcheckbox_create("") : Rwidget_create(multi ? R_WIDGET_TEXT_EDITOR : R_WIDGET_TEXT_INPUT));
      Rwidget_set_size(input, 390, multi ? 76.0f : 25.0f);
      Rwidget_set_enabled(input, f.enabled);
      if (f.boolean) Rcheckbox_set_checked(input, f.value == "1");
      else if (multi) { Rtext_editor_set_text(input, f.value.c_str()); Rtext_editor_set_line_numbers(input, R_FALSE); }
      else { Rtext_input_set_text(input, f.value.c_str()); Rtext_input_set_max_length(input, 65536); }
      if (f.shortcut) Rwidget_set_event_callback(input, record, nullptr);
      controls_.push_back({f.key, input, f.boolean, multi});
      if (!f.reason.empty()) {
        auto reason = Rwidget_add_child(pages[unsigned(f.group)], Rlabel_create(f.reason.c_str()));
        Rlabel_set_wrap(reason, R_TRUE);
      }
    }
    auto footer = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
    Rpanel_set_spacing(footer, 12);
    status_ = Rwidget_add_child(footer, Rlabel_create("Isolated UI experiment. Apply only saves this test profile."));
    Rslot_set_fill(status_, 1); Rlabel_set_wrap(status_, R_TRUE);
    auto button = Rwidget_add_child(footer, Rbutton_create("Apply"));
    Rbutton_set_clicked_callback(button, [](Rid, void* self) { static_cast<SettingsUi*>(self)->save(); }, this);
    Rwindow_show(window_);
  }
  HWND handle() const { RNativeWindow native{}; Rwindow_get_native_handle(window_, &native); return reinterpret_cast<HWND>(native.window); }
  void pump(bool wait) { if (wait) Rwait_frame(-1); else Rdraw_frame(); }
};
constexpr const char* variant = "raven";
#else
class SettingsUi {
  NativeSettings settings_;
public:
  void show(Settings settings, const Capabilities& caps, std::function<Result<void>(Settings)> apply) {
    require(settings_.show(std::move(settings), caps, std::move(apply)));
    SetWindowTextW(handle(), L"Cliptiq Settings - Native experiment");
    settings_.status("Isolated UI experiment. Apply only saves this test profile.");
  }
  HWND handle() const { return reinterpret_cast<HWND>(settings_.native_handle()); }
  void pump(bool wait) {
    if (wait) MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    require(settings_.process_events());
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    UpdateWindow(handle());
    if (!IsWindowVisible(handle())) quit_requested = true;
  }
};
constexpr const char* variant = "native";
#endif
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  std::filesystem::path report;
  try {
    const auto entry = Clock::now();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool memory = false;
    for (int i = 1; i < argc; ++i) {
      if (std::wstring(argv[i]) == L"--memory-profile") memory = true;
      else if (std::wstring(argv[i]) == L"--report" && i + 1 < argc) report = argv[++i];
      else throw std::runtime_error("Usage: settings_[native|raven] [--memory-profile] [--report FILE]");
    }
    LocalFree(argv);
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
    auto profile = std::filesystem::path(exe).parent_path() / (std::string("profile-") + variant);
    if (!memory) std::filesystem::create_directories(profile);
    auto opened = open_store(memory ? ":memory:" : (profile / "settings.sqlite3").u8string());
    if (!opened) throw std::runtime_error(opened.error().message);
    auto store = std::move(opened.value());
    auto loaded = load_settings(store, CapturePlatform::Windows);
    if (!loaded) throw std::runtime_error(loaded.error().message);
    auto settings = std::move(loaded.value());
    Capabilities caps; caps.platform = CapturePlatform::Windows;
    const Capability available{CapabilityStatus::Available, "Experiment: UI only; no system actions"};
    caps.capture = caps.write = caps.hotkeys = caps.auto_paste = caps.source_app = caps.tray = caps.pointer_placement = available;
    WNDCLASSW wc{}; wc.lpfnWndProc = control_proc; wc.hInstance = instance; wc.lpszClassName = L"Cliptiq.Settings.Experiment.Control";
    RegisterClassW(&wc);
    auto control = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!control) throw std::runtime_error("Cannot create experiment control window");
    SettingsUi ui;
    std::vector<double> opens;
    auto apply = [&](Settings draft) -> Result<void> {
      auto result = save_settings(store, draft);
      if (result) { settings = std::move(draft); ++settings.revision; }
      return result;
    };
    auto show = [&] {
      auto start = Clock::now(); ui.show(settings, caps, apply); ui.pump(false);
      opens.push_back(ms(start));
    };
    show();
    const double entry_to_ready = ms(entry);
    auto write_report = [&] {
      if (report.empty()) return;
      // Each generation has an immutable filename. Replacing a JSON file while
      // Python/AV has it open can fail on Windows (sharing/delete permissions).
      auto destination = report;
      if (opens.size() > 1) destination += L"." + std::to_wstring(opens.size());
      auto tmp = destination; tmp += L".tmp";
      { std::ofstream out(tmp); out << "{\"variant\":\"" << variant << "\",\"pid\":" << GetCurrentProcessId()
          << ",\"control_hwnd\":" << reinterpret_cast<std::uintptr_t>(control)
          << ",\"window_hwnd\":" << reinterpret_cast<std::uintptr_t>(ui.handle())
          << ",\"field_count\":" << settings_fields(settings, caps).size()
          << ",\"entry_to_ready_ms\":" << entry_to_ready << ",\"open_dispatch_ms\":[";
        for (size_t i = 0; i < opens.size(); ++i) { if (i) out << ','; out << opens[i]; }
        out << "]}";
      }
      if (!MoveFileExW(tmp.c_str(), destination.c_str(), 0)) throw std::runtime_error("Cannot publish benchmark report, Win32=" + std::to_string(GetLastError()));
    };
    write_report();
    while (!quit_requested) {
      ui.pump(true);
      if (reopen_requested && !quit_requested) { reopen_requested = false; show(); write_report(); }
    }
    DestroyWindow(control);
    return 0;
  } catch (const std::exception& e) {
    if (!report.empty()) { auto error = report; error += L".error.txt"; std::ofstream(error) << e.what(); }
    else MessageBoxA(nullptr, e.what(), "Cliptiq settings experiment failed", MB_OK | MB_ICONERROR);
    return 1;
  }
}
