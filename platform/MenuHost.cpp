#include "Application.hpp"
#include "cliptiq/menu.hpp"
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
      else if (arg == "--no-auto-paste")
        auto_paste = false;
      else if (arg == "--terminal-paste")
        shortcut = Shortcut::TerminalPaste;
      else if ((arg == "--plain-modifier" || arg == "--delete-modifier" ||
                arg == "--paste-delete-modifier") &&
               i + 1 < argc) {
        auto bit = modifier(argv[++i]);
        if (arg == "--plain-modifier")
          bindings.plain = bit;
        else if (arg == "--delete-modifier")
          bindings.delete_only = bit;
        else
          bindings.paste_delete = bit;
      }
#endif
      else if (arg == "--no-tray")
        tray = TrayStyle::Hidden;
      else if (arg == "--history")
        kind = MenuKind::History;
      else if (arg == "--snippets")
        kind = MenuKind::Snippets;
      else if (arg == "--database" && i + 1 < argc)
        path = argv[++i];
      else
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
    auto store = take(open_store(path));
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
      auto start = session->start({}, {}, [](auto event) {
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
    settings.inline_count = 5;
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
    NativeMenuOptions options;
    options.tray_style = tray;
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
      if (command.action == MenuAction::Quit)
        quit = true;
      else if (command.action == MenuAction::ClearHistory) {
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
      if (i.action == MenuAction::OpenEditor ||
          i.action == MenuAction::OpenSettings)
        i.enabled = false;
    ok(ui.update(model));
    auto last = take(data_revision(store));
    auto checked = std::chrono::steady_clock::now();
    unsigned iterations = 0;
    while (!quit) {
      ok(ui.process_events(smoke ? 1 : 5));
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
          model = take(build_menu(store, kind, settings));
          for (auto &i : model.items)
            if (i.action == MenuAction::OpenEditor ||
                i.action == MenuAction::OpenSettings)
              i.enabled = false;
          ok(ui.update(model));
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
