#pragma once
#include "settings.hpp"
namespace cliptiq {
enum class UpdateChannel { Stable };
enum class UpdateRoute { Sparkle, SignedInstaller, PackageManager, Download };
enum class UpdateOutcome { Delegated, Cancelled };
struct UpdateInfo {
  std::string version, platform, arch, package_url, signing_metadata;
  std::uint64_t bytes=0;
  UpdateRoute route=UpdateRoute::Download;
};
struct UpdateTarget {
  std::string platform, arch;
  UpdateRoute route;
  std::uint64_t max_bytes=5ull*1024*1024;
};
// Only trusted application code supplies a provider. Metadata is never proof of
// authenticity. authenticate must verify actual bytes with a pinned publisher;
// install must maintain that binding through native delegation.
class UpdateProvider {
public:
  virtual ~UpdateProvider()=default;
  virtual Result<UpdateInfo> fetch(UpdateChannel)=0;
  virtual Result<void> authenticate(const UpdateInfo&)=0;
  virtual Result<UpdateOutcome> install(const UpdateInfo&)=0;
};
Result<UpdateInfo> check_update(UpdateProvider&,UpdateChannel,const std::string& current,const UpdateTarget&);
Result<UpdateOutcome> apply_update(UpdateProvider&,const UpdateInfo&,const std::string& current,const UpdateTarget&);
bool update_due(bool automatic,std::int64_t interval,std::int64_t last,std::int64_t now);
Result<std::int64_t> last_update_attempt(Store&);
Result<void> save_update_attempt(Store&,std::int64_t now);
// Whitelist-only report: no Store, file paths, identifiers, errors or user input.
std::string local_diagnostics();
// Repository Atom data is an availability hint, never release authentication.
Result<std::string> release_hint(const std::string& bounded_atom,const std::string& current);
}
