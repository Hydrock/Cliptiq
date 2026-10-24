#ifdef CLIPTIQ_OCR_INDEX
#include "Index.hpp"
#endif
#ifdef CLIPTIQ_WITH_OCR
#include "Ocr.hpp"
#endif
#ifdef CLIPTIQ_WITH_UPDATES
#include "Updates.hpp"
#endif
#include "Application.hpp"
#include "cliptiq/menu.hpp"
#include "cliptiq/locale.hpp"
#ifdef CLIPTIQ_WITH_LIFECYCLE
#include "cliptiq/lifecycle.hpp"
#endif
#ifdef CLIPTIQ_WITH_MEDIA
#include "Media.hpp"
#endif
#ifdef CLIPTIQ_WITH_SETTINGS
#include "cliptiq/settings.hpp"
#ifdef CLIPTIQ_WITH_EDITOR
#include "cliptiq/editor.hpp"
#endif
#include <filesystem>
#endif
#ifdef CLIPTIQ_WITH_PASTE
#include "cliptiq/paste.hpp"
#endif
#if CLIPTIQ_MENU_MODE == 2
#include "cliptiq/native_menu.hpp"
#endif
#ifdef CLIPTIQ_HOST_TEST_ISOLATION
#ifdef _WIN32
#include "../tests/fixtures/windows/IsolatedClipboard.hpp"
#endif
#endif
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#endif
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#ifdef CLIPTIQ_GUI_ENTRY
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif
using namespace cliptiq;
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
void ok(Result<void> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
}
int host(int argc, char **argv) {
  try {
    bool smoke = false, capture = false, demo = false;
#ifdef CLIPTIQ_WITH_UPDATES
    NativeUpdates updates;
#endif
#ifdef CLIPTIQ_WITH_LIFECYCLE
    bool background = false, remove_autostart = false;
    std::string requested_locale;
#endif
    std::string path = ":memory:";
    MenuKind kind = MenuKind::Main;
    TrayStyle tray = TrayStyle::Standard;
#ifdef CLIPTIQ_WITH_SETTINGS
    std::string launch_action;
    bool explicit_database = false, auto_override = false,
         tray_override = false;
    bool modifier_override[3]{};
#endif
#ifdef CLIPTIQ_WITH_PASTE
    bool auto_paste = true;
    Shortcut shortcut = Shortcut::Paste;
    ModifierBindings bindings;
#ifdef __APPLE__
    bindings.plain = Super;
#endif
    auto modifier = [](const std::string &v) -> unsigned {
      if (v == "none")
        return 0;
      if (v == "ctrl")
        return Control;
      if (v == "shift")
        return Shift;
      if (v == "alt")
        return Alt;
      if (v == "super")
        return Super;
      throw std::runtime_error("Modifier must be none/ctrl/shift/alt/super");
    };
#endif
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--smoke")
        smoke = true;
#ifdef CLIPTIQ_WITH_LIFECYCLE
      else if (arg == "--background")
        background = true;
      else if (arg == "--remove-autostart")
        remove_autostart = true;
      else if (arg == "--locale" && i + 1 < argc)
        requested_locale = argv[++i];
      else if (arg == "--about") {
#ifdef CLIPTIQ_GUI_ENTRY
        MessageBoxW(nullptr,
                    L"Cliptiq 0.1.0\nhttps://github.com/Hydrock/Cliptiq",
                    L"Cliptiq", MB_OK);
#else
        std::cout << "Cliptiq 0.1.0\nhttps://github.com/Hydrock/Cliptiq\n";
#endif
        return 0;
      }
#endif
      else if (arg == "--capture")
        capture = true;
      else if (arg == "--demo")
        demo = true;
#ifdef CLIPTIQ_WITH_PASTE
      else if (arg == "--no-auto-paste") {
        auto_paste = false;
#ifdef CLIPTIQ_WITH_SETTINGS
        auto_override = true;
#endif
      } else if (arg == "--terminal-paste")
        shortcut = Shortcut::TerminalPaste;
      else if ((arg == "--plain-modifier" || arg == "--delete-modifier" ||
                arg == "--paste-delete-modifier") &&
               i + 1 < argc) {
        auto bit = modifier(argv[++i]);
#ifdef CLIPTIQ_WITH_SETTINGS
        modifier_override[arg == "--plain-modifier"    ? 0
                          : arg == "--delete-modifier" ? 1
                                                       : 2] = true;
#endif
        if (arg == "--plain-modifier")
          bindings.plain = bit;
        else if (arg == "--delete-modifier")
          bindings.delete_only = bit;
        else
          bindings.paste_delete = bit;
      }
#endif
      else if (arg == "--no-tray") {
        tray = TrayStyle::Hidden;
#ifdef CLIPTIQ_WITH_SETTINGS
        tray_override = true;
#endif
      } else if (arg == "--history")
        kind = MenuKind::History;
      else if (arg == "--snippets")
        kind = MenuKind::Snippets;
#ifdef CLIPTIQ_WITH_SETTINGS
      else if (arg == "--show-main")
        launch_action = "main";
      else if (arg == "--show-history")
        launch_action = "history";
      else if (arg == "--show-snippets")
        launch_action = "snippets";
      else if (arg == "--show-settings")
        launch_action = "settings";
      else if (arg == "--show-editor")
        launch_action = "editor";
      else if (arg == "--clear-history")
        launch_action = "clear";
      else if (arg == "--show-folder" && i + 1 < argc)
        launch_action = "folder:" + std::string(argv[++i]);
#endif
      else if (arg == "--database" && i + 1 < argc) {
        path = argv[++i];
#ifdef CLIPTIQ_WITH_SETTINGS
        explicit_database = true;
#endif
      } else
        throw std::runtime_error(
            "Usage: cliptiq_menu_host [--database PATH] [--demo] [--capture] "
            "[--history|--snippets] [--no-tray] [--smoke]");
    }
    if (smoke && capture)
      throw std::runtime_error("Smoke never captures ambient clipboard");
#ifdef CLIPTIQ_HOST_TEST_ISOLATION
#ifdef _WIN32
    std::unique_ptr<IsolatedClipboard> isolated;
    if (smoke)
      isolated = std::make_unique<IsolatedClipboard>();
#elif !defined(__APPLE__)
    if (smoke && !getenv("CLIPTIQ_TEST_ISOLATED"))
      throw std::runtime_error("Private display launcher required for smoke");
#endif
#endif
#ifdef CLIPTIQ_WITH_PASTE
    take(selection_action(0, bindings));
#endif
#ifdef CLIPTIQ_WITH_SETTINGS
#ifndef CLIPTIQ_WITH_LIFECYCLE
    if (capture && !explicit_database)
      throw std::runtime_error("Capture requires --database PATH");
#endif
    if (path == ":memory:" && !smoke && !demo) {
      std::filesystem::path folder;
#ifdef _WIN32
      auto base = _wgetenv(L"LOCALAPPDATA");
      if (!base)
        throw std::runtime_error(
            "LOCALAPPDATA unavailable; specify --database PATH");
      folder = std::filesystem::path(base) / L"Cliptiq";
#elif defined(__APPLE__)
      auto base = getenv("HOME");
      if (!base)
        throw std::runtime_error("HOME unavailable; specify --database PATH");
      folder =
          std::filesystem::u8path(base) / "Library/Application Support/Cliptiq";
#else
      auto base = getenv("XDG_DATA_HOME");
      if (base && *base)
        folder = std::filesystem::u8path(base) / "cliptiq";
      else {
        auto home = getenv("HOME");
        if (!home)
          throw std::runtime_error("HOME unavailable; specify --database PATH");
        folder = std::filesystem::u8path(home) / ".local/share/cliptiq";
      }
#endif
      std::filesystem::create_directories(folder);
      path = (folder / "history.sqlite3").u8string();
    }
#endif
#ifdef CLIPTIQ_WITH_LIFECYCLE
    Autostart startup({take(executable_path()), {}, smoke});
    if (remove_autostart) {
      take(startup.set_autostart(false));
      std::cout << startup.status().reason << '\n';
      return 0;
    }
    SessionInstance instance;
    if (!smoke && !demo) {
      std::optional<LaunchAction> route = LaunchAction::Main;
      if (!launch_action.empty()) {
        auto parsed = parse_launch_action(launch_action);
        if (parsed)
          route = parsed.value();
        else if (launch_action == "clear" ||
                 launch_action.rfind("folder:", 0) == 0)
          route.reset();
        else
          throw std::runtime_error(parsed.error().message);
      }
      auto role = take(instance.start(
          {take(lifecycle_directory()), lifecycle_scope()}, route));
      if (role == InstanceRole::Forwarded)
        return 0;
      capture = true;
    }
    bool startup_problem = false;
#endif
    auto store = take(open_store(path));
#ifdef CLIPTIQ_WITH_SETTINGS
    auto preferences = take(load_settings(store));
#ifdef CLIPTIQ_WITH_LIFECYCLE
    auto language = requested_locale.empty() ? preferences.values.at("locale")
                                             : requested_locale;
    activate_locale(language == "auto" ? system_locale() : language);
    if (!smoke) {
      auto inspected = startup.inspect();
      startup_problem = preferences.values.at("login") == "1" &&
                        (!inspected || !inspected.value().enabled ||
                         inspected.value().requires_approval);
    }
#endif
#ifdef CLIPTIQ_WITH_UPDATES
    if(!smoke)ok(updates.initialize());
    auto update_polled=std::chrono::steady_clock::now()-std::chrono::minutes(1);
#endif
    auto loaded_bindings = settings_modifiers(preferences);
    if (!auto_override)
      auto_paste = preferences.values.at("auto_paste") == "1";
    if (!modifier_override[0])
      bindings.plain = loaded_bindings.plain;
    if (!modifier_override[1])
      bindings.delete_only = loaded_bindings.delete_only;
    if (!modifier_override[2])
      bindings.paste_delete = loaded_bindings.paste_delete;
    if (!tray_override)
      tray = TrayStyle(std::stoi(preferences.values.at("tray")));
#endif
    if (demo || smoke) {
      for (auto text :
           {std::string("Hello from Cliptiq"), std::string("#f80")}) {
        ClipboardSnapshot s;
        s.representations = {{0, "x11", "UTF8_STRING", CanonicalKind::Text,
                              Bytes(text.begin(), text.end())}};
        take(ingest(store, s, {}, 1));
      }
    }
#ifdef _WIN32
    std::unique_ptr<windows::WindowsCaptureSession> session;
#elif defined(__APPLE__)
    std::unique_ptr<macos::MacCaptureSession> session;
#else
    std::unique_ptr<linux_backend::LinuxCaptureSession> session;
#endif
    if (capture) {
      if (path == ":memory:")
        throw std::runtime_error("Capture requires an explicit database path");
#ifdef _WIN32
      session = std::make_unique<windows::WindowsCaptureSession>(path);
#elif defined(__APPLE__)
      session = std::make_unique<macos::MacCaptureSession>(path);
#else
      session = std::make_unique<linux_backend::LinuxCaptureSession>(
          linux_backend::open_backend(), path);
#endif
      auto start = session->start(
#ifdef CLIPTIQ_WITH_SETTINGS
          settings_capture(preferences), settings_history(preferences),
#else
          {}, {},
#endif
          [](auto event) {
            if (!event)
              std::cerr << event.error().message << '\n';
          });
      if (!start) {
        std::cerr << start.error().message << '\n';
        session.reset();
      }
    }
    auto rows = take(list_history(store));
    if (smoke && rows.size() != 2)
      return 4;
#if CLIPTIQ_MENU_MODE > 0
    MenuSettings settings;
#ifdef CLIPTIQ_WITH_SETTINGS
    settings = settings_menu(preferences);
#else
    settings.inline_count = 5;
#endif
    auto model = take(build_menu(store, kind, settings));
#endif
#if CLIPTIQ_MENU_MODE == 2
    bool quit = false, refresh = false;
#ifdef CLIPTIQ_WITH_PASTE
    NativePasteBackend paste;
    if (!smoke)
      std::cout << "Auto-paste: " << paste.capability().reason << '\n';
    std::unique_ptr<ClipboardBackend> writer;
    ClipboardBackend *clipboard = session ? &session->clipboard() : nullptr;
    // Writer-only backend: never start a capture monitor without --capture.
    if (!clipboard && !smoke) {
#ifdef _WIN32
      writer = std::make_unique<windows::WindowsClipboardBackend>();
#elif defined(__APPLE__)
      writer = std::make_unique<macos::MacClipboardBackend>();
#else
      writer = linux_backend::open_backend();
#endif
      clipboard = writer.get();
    }
    std::optional<MenuCommand> pending_selection;
#endif
#ifdef CLIPTIQ_WITH_SETTINGS
    NativeSettings settings_window;
#ifdef CLIPTIQ_WITH_EDITOR
    NativeSnippetEditor editor_window;
    std::shared_ptr<SnippetEditorSession> editor_session;
    std::function<void()> show_editor;
#endif
    std::optional<std::pair<ActionId, InvocationContext>> pending_action;
    NativeHotkeys hotkeys([&](ActionId action, InvocationContext ctx) {
      auto binding = preferences.hotkeys.find(action);
#ifdef CLIPTIQ_WITH_EDITOR
      if (binding != preferences.hotkeys.end() && binding->second &&
          editor_window.record_chord(*binding->second))
        return;
#endif
      if (binding != preferences.hotkeys.end() && binding->second &&
          settings_window.record_chord(*binding->second))
        return;
      pending_action = std::make_pair(std::move(action), std::move(ctx));
    });
    HotkeyRegistry registry(hotkeys);
    auto startup_keys = take(available_hotkeys(store, preferences.hotkeys));
    if (!smoke)
      for (auto &p : startup_keys) {
        auto bound = registry.bind_hotkey(p.first, p.second);
        if (!bound)
          std::cerr << p.first << ": " << bound.error().message << '\n';
      }
    Capabilities capabilities =
        clipboard ? clipboard->capabilities() : Capabilities{};
    capabilities.auto_paste = paste.capability();
    capabilities.hotkeys = hotkeys.capability();
    std::function<void()> show_settings;
    std::string active_folder;
    auto filter_model = [&](MenuModel &m) {
      for (auto it = m.items.begin(); it != m.items.end();) {
#ifndef CLIPTIQ_WITH_EDITOR
        if (it->action == MenuAction::OpenEditor)
          it->enabled = false;
#endif
        if (it->action == MenuAction::ClearHistory &&
            preferences.values.at("show_clear") == "0")
          it = m.items.erase(it);
        else
          ++it;
      }
    };
    filter_model(model);
#endif
    NativeMenuOptions options;
    options.tray_style = tray;
#ifdef CLIPTIQ_WITH_SETTINGS
    options.preview_width = settings.preview_width;
    options.preview_height = settings.preview_height;
    options.icons = settings.icons;
#endif
#ifdef CLIPTIQ_WITH_PASTE
    if (!smoke)
      options.invocation_provider = [&] {
        return InvocationContext{paste.capture_target()};
      };
#endif
#ifdef CLIPTIQ_WITH_MEDIA
#ifdef _WIN32
    MediaService media(store,CapturePlatform::Windows);
#elif defined(__APPLE__)
    MediaService media(store,CapturePlatform::MacOS);
#else
    MediaService media(store,std::getenv("WAYLAND_DISPLAY")?CapturePlatform::Wayland:CapturePlatform::X11);
#endif
    auto configure_media=[&]{auto result=media.configure(preferences,smoke);if(!result)std::cerr<<result.error().message<<'\n';};
    configure_media();
#ifdef CLIPTIQ_OCR_INDEX
    IndexService search_index_worker(store);
#endif

#ifdef CLIPTIQ_WITH_OCR
    const char* model_path=std::getenv("CLIPTIQ_OCR_MODELS");
    OcrService ocr(store,make_native_ocr(model_path?model_path:(std::filesystem::u8path(take(executable_path())).parent_path()/"tessdata").u8string()),decode_native_image);
    capabilities.ocr={CapabilityStatus::Available,"Optional offline OCR build"};
#endif

    auto decorate_media=[&](MenuModel& menu){auto status=media.observation_status();if(status.status==CapabilityStatus::Unavailable){MenuItem item;item.label="Screenshots: "+tr(status.reason);item.enabled=false;menu.items.push_back(std::move(item));}};
#endif
#ifdef CLIPTIQ_WITH_LIFECYCLE
    auto decorate_lifecycle = [&](MenuModel &menu) {
#ifdef CLIPTIQ_WITH_UPDATES
      MenuItem update_status;update_status.enabled=false;
      auto last=last_update_attempt(store);
      update_status.label=updates.status()+"; last attempt="+(last?std::to_string(last.value()):"unavailable");
      menu.items.push_back(std::move(update_status));
#endif
      if (startup_problem) {
        MenuItem item;
        item.label = tr(startup.status().reason);
        item.enabled = false;
        menu.items.push_back(std::move(item));
      }
    };
#endif
    NativeMenu ui(options, [&](MenuCommand command) {
      auto valid = validate_menu_command(store, command);
      if (!valid) {
        refresh = true;
        return;
      }
      if (command.action == MenuAction::Quit) {
#ifdef CLIPTIQ_WITH_EDITOR
        auto closed = editor_window.request_close();
        if (!closed || !closed.value())
          return;
#endif
        quit = true;
      }
#ifdef CLIPTIQ_WITH_SETTINGS
#ifdef CLIPTIQ_WITH_EDITOR
      else if (command.action == MenuAction::OpenEditor) {
        if (show_editor)
          show_editor();
      }
#endif
#ifdef CLIPTIQ_WITH_UPDATES
      else if(command.action==MenuAction::DownloadUpdates){if(!smoke){auto r=updates.open_download();if(!r)std::cerr<<r.error().message<<'\n';}}
      else if(command.action==MenuAction::CheckUpdates){
        if(!smoke){
          auto stamp=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
          ok(save_update_attempt(store,stamp));
          auto r=updates.check_now();
          if(!r)std::cerr<<r.error().message<<'\n';
          if(show_settings)show_settings();
          settings_window.status(updates.status()+"; last attempt="+std::to_string(stamp));
        }
      }else if(command.action==MenuAction::ExportDiagnostics){
        if(!smoke){auto r=updates.export_diagnostics();if(!r)std::cerr<<r.error().message<<'\n';}
      }
#endif
      else if (command.action == MenuAction::OpenSettings) {
        if (show_settings)
          show_settings();
      }
#endif
      else if (command.action == MenuAction::ClearHistory) {
#ifdef CLIPTIQ_WITH_SETTINGS
        if (preferences.values.at("confirm_clear") == "1" &&
            !settings_window.confirm_clear())
          return;
#endif
        auto clear = execute_menu_clear(store, command);
        if (!clear)
          std::cerr << clear.error().message << '\n';
        refresh = true;
      } else {
#ifdef CLIPTIQ_WITH_PASTE
        if (!smoke && (command.action == MenuAction::SelectHistory ||
                       command.action == MenuAction::SelectSnippet))
          pending_selection = std::move(command);
#else
        std::cout << "Selected " << command.id << '\n';
#endif
      }
    }
#ifdef CLIPTIQ_WITH_MEDIA
    ,[&](const PreviewRef&r){return media.preview(r);}
#endif
    );
    ok(ui.initialize());
    for (auto &i : model.items)
      if (
#ifndef CLIPTIQ_WITH_EDITOR
          i.action == MenuAction::OpenEditor ||
#endif
#ifndef CLIPTIQ_WITH_SETTINGS
          i.action == MenuAction::OpenSettings ||
#endif
#ifndef CLIPTIQ_WITH_UPDATES
          i.action==MenuAction::CheckUpdates || i.action==MenuAction::ExportDiagnostics || i.action==MenuAction::DownloadUpdates ||
#endif
          false)
        i.enabled = false;
#ifdef CLIPTIQ_WITH_MEDIA
    decorate_media(model);
#endif
#ifdef CLIPTIQ_WITH_LIFECYCLE
    decorate_lifecycle(model);
#endif
    ok(ui.update(model));
#ifdef CLIPTIQ_WITH_SETTINGS
    auto rebuild = [&]() -> Result<void> {
      auto next = active_folder.empty()
                      ? build_menu(store, kind, settings)
                      : build_folder_menu(store, active_folder, settings);
      if (!next && !active_folder.empty() &&
          next.error().code == ErrorCode::NotFound) {
        active_folder.clear();
        kind = MenuKind::Snippets;
        next = build_menu(store, kind, settings);
      }
      if (!next)
        return next.error();
      model = std::move(next.value());
      filter_model(model);
#ifdef CLIPTIQ_WITH_MEDIA
      decorate_media(model);
#endif
#ifdef CLIPTIQ_WITH_LIFECYCLE
      decorate_lifecycle(model);
#endif
      return ui.update(model);
    };
    auto restart_capture = [&] {
      if (!session)
        return;
      auto result =
          session->start(settings_capture(preferences),
                         settings_history(preferences), [](auto event) {
                           if (!event)
                             std::cerr << event.error().message << '\n';
                         });
      if (!result)
        std::cerr << result.error().message << '\n';
    };
    show_settings = [&] {
      auto latest = load_settings(store);
      if (!latest) {
        std::cerr << latest.error().message << '\n';
        return;
      }
      auto opened = settings_window.show(
          std::move(latest.value()), capabilities,
          [&](Settings draft) -> Result<void> {
#ifdef CLIPTIQ_WITH_LIFECYCLE
            bool login_changed =
                draft.values.at("login") != preferences.values.at("login");
            if (smoke && login_changed)
              return Error{ErrorCode::Unsupported,
                           "Smoke never changes autostart"};
#endif
            if (session)
              session->stop();
#ifdef CLIPTIQ_WITH_MEDIA
            media.stop();
            auto media_applied=media.configure(draft,smoke);
            if(!media_applied){
              configure_media();
              restart_capture();
              return media_applied;
            }
#endif
#ifdef CLIPTIQ_WITH_LIFECYCLE
            auto applied =
                startup.apply(draft.values.at("login") == "1",
                              preferences.values.at("login") == "1", [&] {
                                return apply_settings(store, registry, draft);
                              });
            if (login_changed)
              startup_problem = !applied || startup.status().requires_approval;
#else
            auto applied = apply_settings(store, registry, draft);
#endif
            if (!applied) {
              restart_capture();
#ifdef CLIPTIQ_WITH_MEDIA
              configure_media();
#endif
              return applied;
            }
            preferences = take(load_settings(store));
#ifdef CLIPTIQ_WITH_UPDATES
            if(!smoke)updates.preferences(preferences.values.at("updates_auto")=="1",std::stoll(preferences.values.at("updates_interval")));
#endif
#ifdef CLIPTIQ_WITH_MEDIA
            configure_media();
#endif
            settings = settings_menu(preferences);
            auto_paste = preferences.values.at("auto_paste") == "1";
            bindings = settings_modifiers(preferences);
            options.tray_style =
                TrayStyle(std::stoi(preferences.values.at("tray")));
            options.preview_width = settings.preview_width;
            options.preview_height = settings.preview_height;
            options.icons = settings.icons;
            auto result = ui.set_options(options);
            if (!result)
              std::cerr << result.error().message << '\n';
            restart_capture();
            ok(tick_cleanup(
                store,
                {preferences.values.at("periodic") == "1"
                     ? std::int64_t(std::stoll(preferences.values.at(
                                        "cleanup_seconds")) *
                                    1000)
                     : 0},
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()));
            refresh = true;
            return {};
          },
          [&](const HotkeyMap &keys) {
            auto available = available_hotkeys(store, keys);
            if (!available)
              return Result<void>{available.error()};
            return hotkeys.configure_portal(available.value());
          });
      if (!opened)
        std::cerr << opened.error().message << '\n';
#ifdef CLIPTIQ_WITH_MEDIA
      else settings_window.status(tr(media.observation_status().reason));
#endif
#ifdef CLIPTIQ_WITH_LIFECYCLE
      if (opened && startup_problem)
        settings_window.status(tr(startup.status().reason));
#endif
    };
#ifdef CLIPTIQ_WITH_EDITOR
    show_editor = [&] {
      if (!editor_session) {
        SnippetEditorCallbacks callbacks;
        // Text autosave completion only invalidates the menu. Folder routing
        // is reconciled by the existing periodic host pass, not per keystroke.
        callbacks.changed = [&] { refresh = true; };
        callbacks.bind_folder =
            [&](const FolderId &id,
                std::optional<KeyChord> chord) -> Result<void> {
          auto latest = load_settings(store);
          if (!latest)
            return latest.error();
          latest.value().hotkeys["folder:" + id] = std::move(chord);
          auto applied = apply_settings(store, registry, latest.value());
          if (!applied)
            return applied;
          preferences = take(load_settings(store));
          refresh = true;
          return {};
        };
        callbacks.folder_chord =
            [&](const FolderId &id) -> std::optional<KeyChord> {
          auto latest = load_settings(store);
          if (!latest)
            return {};
          auto found = latest.value().hotkeys.find("folder:" + id);
          return found == latest.value().hotkeys.end()
                     ? std::optional<KeyChord>{}
                     : found->second;
        };
        callbacks.binding_status = [&](const FolderId &id) {
          auto catalog = settings_folders(store);
          if (!catalog || !catalog.value().count(id) || !catalog.value().at(id))
            return std::string("Folder disabled: shortcut preference retained "
                               "without registration.");
          auto action = "folder:" + id;
          auto portal = hotkeys.portal_statuses();
          if (portal.count(action))
            return portal.at(action).registered ? portal.at(action).trigger
                                                : portal.at(action).reason;
          if (registry.registered(action))
            return std::string("Folder shortcut registered.");
          return std::string("Shortcut not registered: ") +
                 hotkeys.capability().reason +
                 "; use Settings to inspect/authorize.";
        };
        editor_session =
            std::make_shared<SnippetEditorSession>(store, std::move(callbacks));
        auto loaded = editor_session->reload();
        if (!loaded) {
          std::cerr << loaded.error().message << '\n';
          editor_session.reset();
          return;
        }
      }
      auto opened = editor_window.show(editor_session);
      if (!opened)
        std::cerr << opened.error().message << '\n';
    };
#endif
    if (smoke) {
      show_settings();
#ifdef CLIPTIQ_WITH_EDITOR
      show_editor();
#endif
    } else if (!launch_action.empty())
      pending_action = std::make_pair(launch_action, InvocationContext{});
    auto cleanup_checked = std::chrono::steady_clock::now();
    std::string portal_status;
#endif
    auto last = take(data_revision(store));
    auto checked = std::chrono::steady_clock::now();
    unsigned iterations = 0;
#ifdef CLIPTIQ_WITH_LIFECYCLE
    auto ipc_checked = std::chrono::steady_clock::now();
#endif
    while (!quit) {
#ifdef CLIPTIQ_WITH_LIFECYCLE
      if (!smoke && !demo &&
          std::chrono::steady_clock::now() - ipc_checked >=
              std::chrono::milliseconds(100)) {
        auto events = instance.receive();
        if (!events)
          std::cerr << events.error().message << '\n';
        else
          for (auto action : events.value()) {
            if (action == LaunchAction::Settings)
              show_settings();
#ifdef CLIPTIQ_WITH_EDITOR
            else if (action == LaunchAction::Editor)
              show_editor();
#endif
            else {
              active_folder.clear();
              kind = action == LaunchAction::History    ? MenuKind::History
                     : action == LaunchAction::Snippets ? MenuKind::Snippets
                                                        : MenuKind::Main;
              ok(rebuild());
              ok(ui.show());
            }
          }
        ipc_checked = std::chrono::steady_clock::now();
      }
#endif
#ifdef CLIPTIQ_WITH_SETTINGS
#ifdef CLIPTIQ_WITH_EDITOR
      ok(editor_window.process_events());
#endif
      ok(settings_window.process_events());
#endif
      ok(ui.process_events(smoke ? 1 : 5));
#ifdef CLIPTIQ_WITH_SETTINGS
      auto dispatched = hotkeys.dispatch();
      if (!dispatched)
        std::cerr << dispatched.error().message << '\n';
      std::string next_status;
      for (auto &p : hotkeys.portal_statuses())
        next_status +=
            p.first + ": " +
            (p.second.registered ? p.second.trigger : p.second.reason) + "; ";
      if (next_status != portal_status) {
        portal_status = next_status;
        settings_window.status(portal_status);
      }
      if (pending_action) {
        auto action = std::move(*pending_action);
        pending_action.reset();
#ifdef CLIPTIQ_WITH_EDITOR
        if (action.first == "editor") {
          show_editor();
        } else
#endif
            if (action.first == "settings" || action.first == "editor") {
          show_settings();
          if (action.first == "editor")
            settings_window.status("Snippet editor unavailable until T10");
        } else if (action.first == "clear") {
          if (preferences.values.at("confirm_clear") != "1" ||
              settings_window.confirm_clear()) {
            auto cleared =
                execute_menu_clear(store, {MenuAction::ClearHistory, "",
                                           take(data_revision(store))});
            if (!cleared)
              std::cerr << cleared.error().message << '\n';
            refresh = true;
          }
        } else {
          if (action.first.rfind("folder:", 0) == 0) {
            auto catalog = take(settings_folders(store));
            auto id = action.first.substr(7);
            if (catalog.count(id) && catalog.at(id)) {
              active_folder = id;
              kind = MenuKind::Snippets;
            } else {
              registry.reconcile_folders(catalog);
              continue;
            }
          } else {
            active_folder.clear();
            kind = action.first == "history"    ? MenuKind::History
                   : action.first == "snippets" ? MenuKind::Snippets
                                                : MenuKind::Main;
          }
          if (action.second.target.empty())
            action.second.target = paste.capture_target();
          ok(rebuild());
          ok(ui.show(action.second));
        }
      }
      auto cleanup_now = std::chrono::steady_clock::now();
      if (cleanup_now - cleanup_checked > std::chrono::seconds(1)) {
        auto synced = synchronize_folder_hotkeys(store, registry);
        if (!synced)
          std::cerr << synced.error().message << '\n';
        if (preferences.values.at("periodic") == "1")
          ok(tick_cleanup(
              store,
              {std::int64_t(
                  std::stoll(preferences.values.at("cleanup_seconds")) * 1000)},
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count()));
        cleanup_checked = cleanup_now;
      }
#endif
#ifdef CLIPTIQ_WITH_PASTE
      if (pending_selection && clipboard) {
        SelectionIntent intent;
        intent.command = std::move(*pending_selection);
        pending_selection.reset();
        auto action =
            selection_action(intent.command.invocation.modifiers, bindings);
        if (!action)
          std::cerr << action.error().message << '\n';
        else {
          intent.action = action.value();
          intent.auto_paste = auto_paste;
          intent.shortcut = shortcut;
          intent.now_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          auto result = execute_selection(store, intent, *clipboard, paste);
          if (!result)
            std::cerr << result.error().message << '\n';
          else {
            std::cout << "Selection status="
                      << static_cast<unsigned>(result.value().status) << " "
                      << result.value().reason << '\n';
            if (result.value().history_error)
              std::cerr << result.value().history_error->message << '\n';
          }
        }
        refresh = true;
      }
#ifndef _WIN32
#ifndef __APPLE__
      if (writer)
        ok(static_cast<linux_backend::LinuxClipboardBackend *>(writer.get())
               ->dispatch(0));
#endif
#endif
#endif
#ifndef _WIN32
#ifndef __APPLE__
      if (session)
        ok(session->dispatch(0));
#endif
#endif
      auto now = std::chrono::steady_clock::now();
#ifdef CLIPTIQ_WITH_UPDATES
      if(!smoke&&updates.poll()){settings_window.status(updates.status());refresh=true;}
      if(!smoke&&now-update_polled>=std::chrono::minutes(1)){
        update_polled=now;
        auto stamp=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto last_check=last_update_attempt(store);
        auto provider_check=updates.last_check();
        if(last_check&&provider_check>last_check.value()){ok(save_update_attempt(store,provider_check));last_check=provider_check;refresh=true;}
        if(last_check&&!updates.manages_schedule()&&updates.automatic_available()&&update_due(preferences.values.at("updates_auto")=="1",std::stoll(preferences.values.at("updates_interval")),last_check.value(),stamp)){
          ok(save_update_attempt(store,stamp));auto result=updates.check_now(true);if(!result)std::cerr<<result.error().message<<'\n';
        }
      }
#endif
#ifdef CLIPTIQ_WITH_MEDIA
#ifdef CLIPTIQ_WITH_OCR
      auto ocr_policy=settings_capture(preferences);ocr_policy.request_ocr=!smoke;
      auto ocr_result=ocr.poll(ocr_policy,capabilities);
      if(!ocr_result)std::cerr<<ocr_result.error().message<<'\n';
#endif
#ifdef CLIPTIQ_OCR_INDEX
      if(!smoke){auto indexed=search_index_worker.poll();if(!indexed)std::cerr<<indexed.error().message<<'\n';}
#endif
      auto media_result=media.poll();
      if(!media_result){std::cerr<<media_result.error().message<<'\n';settings_window.status(tr(media.observation_status().reason));refresh=true;}
      else if(media_result.value())refresh=true;
#endif
      if (refresh || now - checked > std::chrono::milliseconds(250)) {
        auto revision = take(data_revision(store));
        if (refresh || last != revision) {
#ifdef CLIPTIQ_WITH_SETTINGS
          ok(rebuild());
#else
          model = take(build_menu(store, kind, settings));
          for (auto &i : model.items)
            if (i.action == MenuAction::OpenEditor ||
                i.action == MenuAction::OpenSettings)
              i.enabled = false;
          ok(ui.update(model));
#endif
          last = revision;
          refresh = false;
        }
        checked = now;
      }
      if (smoke && ++iterations >= 3)
        break;
    }
#else
    (void)kind;
    (void)tray;
#endif
    if (session)
      session->stop();
#ifdef CLIPTIQ_WITH_MEDIA
    media.stop();
#endif
#ifdef CLIPTIQ_WITH_SETTINGS
    if (!smoke && preferences.values.at("clear_quit") == "1")
      ok(erase_history(store));
#endif
    if (smoke)
      std::cout << "PASS menu_host_smoke\n";
    return 0;
  } catch (const std::exception &e) {
#ifdef CLIPTIQ_GUI_ENTRY
    MessageBoxA(nullptr, e.what(), "Cliptiq", MB_OK | MB_ICONERROR);
#endif
    std::cerr << e.what() << '\n';
    return 1;
  }
}
#ifdef CLIPTIQ_GUI_ENTRY
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto raw = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!raw)
    return 1;
  std::vector<std::string> args;
  for (int n = 0; n < count; ++n) {
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw[n], -1,
                                   nullptr, 0, nullptr, nullptr);
    if (!size) {
      LocalFree(raw);
      return 1;
    }
    std::string value(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw[n], -1, value.data(),
                        size, nullptr, nullptr);
    value.pop_back();
    args.push_back(std::move(value));
  }
  LocalFree(raw);
  std::vector<char *> pointers;
  for (auto &arg : args)
    pointers.push_back(arg.data());
  return host(count, pointers.data());
}
#else
int main(int argc, char **argv) {
#ifdef __APPLE__
  @autoreleasepool {
    return host(argc, argv);
  }
#else
  return host(argc, argv);
#endif
}
#endif
