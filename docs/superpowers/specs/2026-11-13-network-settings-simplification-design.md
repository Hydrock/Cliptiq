# Simplified network settings

Approved scope: macOS and Windows use automatic network selection by default.
Discovery, search, and paired devices remain visible. Interface selection and
manual address/key configuration live in a collapsed Advanced settings section.
The section provides an explicit Automatic option to clear a manual override.
Existing working listener addresses and manual overrides are preserved.

The discovery service currently supports one listener interface. Automatic mode
reuses its configured address; for a fresh loopback-only configuration it chooses
an ordinary LAN adapter ahead of service/tunnel adapters, preferring IPv4 within
each group. It does not expand an existing listener onto additional networks.

Tailscale has no separate protocol or product mode here. Remove the branded
example from the address help; standard reachable VPN IP/DNS endpoints continue
to work through manual configuration. mDNS discovery remains local-network
scoped.

Validation: multi-adapter automatic selection, manual override and reset, native
Mac EN/RU disclosure and draft preservation, Windows native UI test coverage.
Windows execution requires the Windows test environment.
