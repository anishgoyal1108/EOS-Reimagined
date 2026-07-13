# EOS Reimagined — Technical Documentation

How the Epic Online Services SDK behaves under the hood, written up interface by interface so this project can be reimplemented cleanly. This is a behavioral spec: control flow, state, async model, and the peer-to-peer wire protocol — not a line-by-line copy of anyone's code.

## Start here
- **[architecture.md](architecture.md)** — the core runtime: handles, singletons, bootstrap, the callback/frame async engine, settings.
- **[protocol.md](protocol.md)** — the peer-to-peer wire protocol (protobuf messages + transport). The interop contract two instances must share.
- **[progress.md](progress.md)** — status manifest for all interfaces.

## Interfaces
- **Identity & social:** [client.md](client.md) (bootstrap), [auth.md](auth.md), [connect.md](connect.md), [presence.md](presence.md), [friends.md](friends.md), [userinfo.md](userinfo.md)
- **Multiplayer:** [p2p.md](p2p.md), [sessions.md](sessions.md), [lobby.md](lobby.md)
- **Content & economy:** [ecom.md](ecom.md), [achievements-stats-leaderboards.md](achievements-stats-leaderboards.md), [storage.md](storage.md), [metrics.md](metrics.md)
- **Other services:** [anticheat.md](anticheat.md), [custominvites.md](custominvites.md), [rtc.md](rtc.md), [integratedplatform-ui-overlay.md](integratedplatform-ui-overlay.md), [external social companion](companion-client.md), [misc-services.md](misc-services.md)

## A note on how this was written
This documentation was produced by static analysis of a compiled EOS SDK emulator plus Epic's public SDK headers, cross-referenced where possible against surviving open-source prior art (see the credit in the main [README](../README.md)). It exists so contributors can build a clean, portable implementation from an understood specification.
