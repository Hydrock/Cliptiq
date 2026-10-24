#include "LifecycleNative.hpp"
#include "db.hpp"
#include "sha256.hpp"
#include <chrono>
#include <filesystem>
#include <thread>
namespace cliptiq {
Result<LaunchAction> parse_launch_action(const std::string &s) {
  for (unsigned n = 1; n <= 5; ++n)
    if (launch_action_name(LaunchAction(n)) == s)
      return LaunchAction(n);
  return Error{
      ErrorCode::InvalidArgument,
      "Only main/history/snippets/settings/editor routes are accepted"};
}
std::string launch_action_name(LaunchAction a) {
  switch (a) {
  case LaunchAction::Main:
    return "main";
  case LaunchAction::History:
    return "history";
  case LaunchAction::Snippets:
    return "snippets";
  case LaunchAction::Settings:
    return "settings";
  case LaunchAction::Editor:
    return "editor";
  }
  return {};
}
struct SessionInstance::Impl {
  sqlite3 *db = nullptr;
  std::shared_ptr<void> guard;
  bool primary = false;
  ~Impl() {
    if (db)
      sqlite3_close_v2(db);
  }
};
SessionInstance::SessionInstance() : impl_(new Impl) {}
SessionInstance::~SessionInstance() = default;
Result<InstanceRole>
SessionInstance::start(InstanceOptions options,
                       std::optional<LaunchAction> action) {
  return detail::guarded<InstanceRole>([&]() -> Result<InstanceRole> {
    if (impl_->db || (action && launch_action_name(*action).empty()) ||
        options.scope.empty() || options.scope.size() > 80 ||
        options.scope.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKL"
                                        "MNOPQRSTUVWXYZ0123456789-_") !=
            std::string::npos)
      return Error{ErrorCode::InvalidArgument,
                   "Invalid lifecycle scope/action or active instance"};
    if (!std::filesystem::u8path(options.directory).is_absolute())
      return Error{ErrorCode::InvalidArgument,
                   "Instance directory must be absolute"};
    auto secure = secure_instance_directory(options.directory);
    if (!secure)
      return secure.error();
    auto prefix = std::filesystem::u8path(options.directory) / options.scope;
    auto locked = lock_instance(prefix.u8string() + ".lock");
    if (!locked)
      return locked.error();
    impl_->guard = std::move(locked.value());
    impl_->primary = bool(impl_->guard);
    if (!impl_->primary && !action)
      return Error{ErrorCode::Unsupported, "Only window-opening actions can be "
                                           "forwarded to an existing instance"};
    auto path = prefix.u8string() + ".sqlite3";
    std::error_code ec;
    if (std::filesystem::is_symlink(std::filesystem::u8path(path), ec))
      return Error{ErrorCode::Io, "IPC symlink rejected"};
    bool existing = std::filesystem::exists(std::filesystem::u8path(path), ec);
    if (existing && (std::filesystem::file_size(std::filesystem::u8path(path),
                                                ec) > 1024 * 1024 ||
                     ec))
      return Error{ErrorCode::TooLarge, "Instance mailbox exceeds 1 MiB"};
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
      sqlite3 *db = nullptr;
      int rc = sqlite3_open_v2(path.c_str(), &db,
                               SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW |
                                   (impl_->primary ? SQLITE_OPEN_CREATE : 0),
                               nullptr);
      if (rc == SQLITE_OK) {
        impl_->db = db;
        break;
      }
      if (db)
        sqlite3_close_v2(db);
      if (impl_->primary)
        return Error{ErrorCode::Io, "Cannot open private instance mailbox"};
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!impl_->db)
      return Error{ErrorCode::Busy, "Primary instance is starting; retry"};
    auto db = impl_->db;
    sqlite3_busy_timeout(db, 250);
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 4096);
    detail::exec(db, "PRAGMA trusted_schema=OFF");
    if (impl_->primary && existing) {
      detail::Statement app(db, "PRAGMA application_id");
      app.row();
      detail::Statement version(db, "PRAGMA user_version");
      version.row();
      bool fresh=false;
      if(app.number(0)==0&&version.number(0)==0){detail::Statement objects(db,"SELECT count(*) FROM sqlite_schema");objects.row();fresh=objects.number(0)==0;}
      if (!fresh&&(app.number(0) != 1129597266 || version.number(0) != 1))
        return Error{ErrorCode::Corrupt,
                     "Unknown instance mailbox format; not overwritten"};
    }
    if (impl_->primary) {
      detail::Transaction tx(db);
      detail::exec(
          db,
          "CREATE TABLE IF NOT EXISTS mailbox(id INTEGER PRIMARY KEY,action "
          "INTEGER NOT NULL CHECK(action BETWEEN 1 AND 5)); DELETE FROM "
          "mailbox; PRAGMA application_id=1129597266; PRAGMA user_version=1");
      tx.commit();
    } else {
      bool ready = false;
      for (unsigned n = 0; n < 20 && !ready; ++n) {
        {
          detail::Statement q(db, "PRAGMA application_id");
          ready = q.row() && q.number(0) == 1129597266;
        }
        if (!ready)
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (!ready)
        return Error{ErrorCode::Busy, "Primary mailbox is not ready"};
      detail::Transaction tx(db);
      detail::Statement count(db, "SELECT count(*) FROM mailbox");
      count.row();
      if (count.number(0) >= 16)
        return Error{ErrorCode::Busy, "Primary action queue is full; retry"};
      detail::Statement insert(db, "INSERT INTO mailbox(action) VALUES(?)");
      insert.bind(1, std::int64_t(*action));
      insert.done();
      tx.commit();
    }
    return impl_->primary ? InstanceRole::Primary : InstanceRole::Forwarded;
  });
}
Result<std::vector<LaunchAction>> SessionInstance::receive() {
  return detail::guarded<std::vector<LaunchAction>>(
      [&]() -> Result<std::vector<LaunchAction>> {
        if (!impl_->db || !impl_->primary)
          return Error{ErrorCode::InvalidArgument,
                       "Only the primary instance receives actions"};
        auto db = impl_->db;
        detail::Transaction tx(db);
        detail::Statement q(db,
                            "SELECT action FROM mailbox ORDER BY id LIMIT 17");
        std::vector<LaunchAction> out;
        while (q.row()) {
          auto a = q.number(0);
          if (a < 1 || a > 5 || out.size() >= 16)
            return Error{ErrorCode::Corrupt, "Invalid instance mailbox"};
          out.push_back(LaunchAction(a));
        }
        detail::exec(db, "DELETE FROM mailbox");
        tx.commit();
        return out;
      });
}
std::string lifecycle_scope() {
  auto hint = native_session_hint();
  detail::Sha256 hash;
  hash.field(hint.data(), hint.size());
  return "session-" + hash.finish().substr(0, 32);
}
Autostart::Autostart(AutostartOptions options) : options_(std::move(options)) {
  status_.reason = "Autostart has not been requested";
}
Result<void> Autostart::apply(bool requested, bool previous,
                              const std::function<Result<void>()> &commit) {
  if (!commit)
    return Error{ErrorCode::InvalidArgument, "Missing settings commit"};
  if (requested != previous) {
    auto changed = set_autostart(requested);
    if (!changed)
      return changed.error();
  }
  auto saved = commit();
  if (!saved && requested != previous) {
    auto restored = set_autostart(previous);
    if (!restored) {
      status_.reason = "Settings save and autostart rollback failed: " +
                       restored.error().message;
      return Error{ErrorCode::Io, status_.reason};
    }
  }
  return saved;
}
Result<AutostartStatus> Autostart::inspect() {
  auto result = native_autostart_status(options_);
  if (result)
    status_ = result.value();
  else
    status_.reason = result.error().message;
  return result;
}
Result<AutostartStatus> Autostart::set_autostart(bool enabled) {
  auto fail = [&](Error e) -> Result<AutostartStatus> {
    status_.reason = e.message;
    return e;
  };
  if (!std::filesystem::u8path(options_.executable).is_absolute() ||
      options_.executable.find_first_of("\r\n\0", 0, 3) != std::string::npos)
    return fail({ErrorCode::InvalidArgument,
                 "Autostart requires an absolute executable path"});
  auto result = native_autostart(enabled, options_);
  if (!result)
    return fail(result.error());
  status_ = result.value();
  return status_;
}
} // namespace cliptiq
