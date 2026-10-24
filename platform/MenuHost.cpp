#include "Application.hpp"
#include "cliptiq/menu.hpp"
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
    if (capture && !explicit_database)
      throw std::runtime_error("Capture requires --database PATH");
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
    auto store = take(open_store(path));
#ifdef CLIPTIQ_WITH_SETTINGS
    auto preferences = take(load_settings(store));
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
    });
    ok(ui.initialize());
    for (auto &i : model.items)
      if (
#ifndef CLIPTIQ_WITH_EDITOR
          i.action == MenuAction::OpenEditor ||
#endif
#ifndef CLIPTIQ_WITH_SETTINGS
          i.action == MenuAction::OpenSettings ||
#endif
          false)
        i.enabled = false;
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
            if (session)
              session->stop();
            auto applied = apply_settings(store, registry, draft);
            if (!applied) {
              restart_capture();
              return applied;
            }
            preferences = take(load_settings(store));
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
    while (!quit) {
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
#ifdef CLIPTIQ_WITH_SETTINGS
    if (!smoke && preferences.values.at("clear_quit") == "1")
      ok(erase_history(store));
#endif
    if (smoke)
      std::cout << "PASS menu_host_smoke\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
int main(int argc, char **argv) {
#ifdef __APPLE__
  @autoreleasepool {
    return host(argc, argv);
  }
#else
  return host(argc, argv);
#endif
}
