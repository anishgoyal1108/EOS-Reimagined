<div align="center">

# EOS Reimagined

### *The FOSS alternative to EOS.*

<img src="assets/logo.svg" alt="EOS Reimagined logo" width="230">

<br>
<br>

[![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue?style=for-the-badge)](LICENSE)
![C++11](https://img.shields.io/badge/std-C%2B%2B11-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux-2DD4BF?style=for-the-badge)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen?style=for-the-badge)](#contributing)

[**Explore the docs »**](docs/)

[Report a bug](https://github.com/anishgoyal1108/EOS-Reimagined/issues/new) · [Request a feature](https://github.com/anishgoyal1108/EOS-Reimagined/issues/new)

</div>

<details>
  <summary>Table of contents</summary>
  <ol>
    <li><a href="#background">Background</a></li>
    <li><a href="#how-it-works">How it works</a></li>
    <li><a href="#a-statement-on-feature-parity">A statement on feature parity</a></li>
    <li><a href="#a-statement-on-piracy-and-cheating">A statement on piracy and cheating</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>

## Background

[Epic Online Services](https://onlineservices.epicgames.com/sdk) is currently one of the most popular solutions for facilitating peer-to-peer connectivity in video games. It's free and cross-platform, and all you have to do as a developer is ship your game against the EOS SDK. Epic's backend handles the rest: accounts, matchmaking, lobbies, sessions, and the peer-to-peer connections between players. The catch is that every one of those features lives on Epic's servers. When those servers go away because the service is sunset, the game is delisted, or the publisher moves on, the multiplayer goes with them, even for players who legitimately own the game and still have working copies. For this reason, the [Stop Killing Games](https://www.stopkillinggames.com/) movement has gained significant traction within the open source community for modifying disserviced games' netcode for online play again.  

As firm believers in the Stop Killing Games movement, we believe there needs to be an alternative for those who don't want to rely on Epic for connectivity: no account; no login; no cloud; just the game facilitating a connection between clients. That's where EOS Reimagined comes in. All games that use the EOS SDK must be linked against `EOSSDK-Win64-Shipping.dll`, which provides the API through Epic's backend. EOS Reimagined is a standalone drop-in for that shared library. It answers every call the game makes and wires players together directly over the local network instead of through Epic. As far as the game can tell, everything works normally.

<p align="center">
  <img src="assets/how-it-works.svg" alt="Route and protocol: two unmodified copies of a game, each loading EOS Reimagined, connected directly over the local network by UDP peer discovery, a TCP control mesh, and UDP game data" width="850">
</p>

## How it works

Under the hood, EOS Reimagined exports the same flat C API as the real SDK, so the game genuinely cannot tell the difference: it loads the library, calls `EOS_Initialize` and `EOS_Platform_Create`, grabs interface handles, and ticks the platform once a frame, exactly as it would against Epic's DLL. Behind those exports, every interface is implemented locally. Your identity comes from a small config file next to the game rather than an Epic account, ownership checks are answered on the spot, and achievements, stats, and save data live on your own disk. Async calls follow the SDK's own contract too: a request is queued, resolved either immediately or when a peer replies, and the game's callback fires on a later `EOS_Platform_Tick` — the same lifecycle the game already expects.

Multiplayer is a mesh over your local network. Every running copy of the library periodically broadcasts a small UDP advertisement announcing the TCP port it listens on; when two copies spot each other, they connect, exchange a short handshake (app id, username, and peer id, so different games on the same LAN ignore one another), and keep that link open as part of a full TCP mesh between all peers. Control traffic — presence, sessions, lobbies, invites — travels over that mesh as small length-prefixed messages addressed by player id, while `EOS_P2P` game packets go over UDP with acknowledgments, since that's the traffic that actually cares about latency. There is no central anything: a lobby or session lives on the peer that created it, and a search is just a question sent to the peers you know, answered by whoever hosts a match. The wire format and per-interface behavior are documented in [`docs/protocol.md`](docs/protocol.md) and the rest of [`docs/`](docs/).

## A statement on feature parity
EOS Reimagined is a reverse-engineered stand-in, not a certified replacement, and we cannot guarantee it behaves one-to-one with Epic's real SDK. True parity would mean rebuilding Epic's entire backend---encryption, authentication, matchmaking servers, voice relays, anti-cheat---and that is an enormous amount of work we have not done and may never fully do. So far, the emulator does the part that keeps games playable: it answers the SDK's calls locally and connects players directly to each other over the network. Identity, sessions, lobbies, presence, friends, and peer-to-peer messaging are wired up, so the multiplayer core works (for most games, anyway). Much of the rest is deliberately faked or stubbed so games don't refuse to launch. Ownership and DLC checks always come back "yes," achievements, stats, leaderboards, and metrics are local-only, RTC voice rooms are created but their audio and data payloads are dropped, and most content, economy, and telemetry services accept-and-discard. You can see exactly what is real, faked, or stubbed for each interface in [`docs/`](docs/) (start with [`docs/progress.md`](docs/progress.md)). There's still a lot of work to be done to ensure the utmost compatibility, but here are two core limitations that we won't be able to address:

1) **It is not a secure connection.** The peer-to-peer transport is essentially plaintext. Epic's real encryption path is not implemented (the SDK even carries an encryption key we never use), and what the SDK calls "anti-cheat protection" is, in this build, just a CRC32-C checksum appended to each message rather than any real cryptography — anti-cheat is a transparent passthrough that reports "authenticated" to every call so protected games will run. Therefore, in theory, this SDK is more susceptible to cheating, unless the game has implemented some logic of their own.

2) **It does not talk to Epic.** The emulator speaks its own reconstructed peer-to-peer protocol between copies of *itself*, discovered over the local network; it never connects to Epic's servers. Because of that, we cannot guarantee — and generally do not expect — that someone running EOS Reimagined can matchmake or play with someone running the genuine EOS SDK against Epic's live backend. This is a tool for connecting emulator to emulator, not emulator to Epic. It follows, then, that we do **not** intend to support letting pirated copies slip onto official servers or play alongside legitimate owners (more on this below).  

## A statement on piracy and cheating

**This tool is for keeping games playable. We will *never* support players who wish to use EOS Reimagined to play on cracked copies of games or develop cheats.**

This project shall *only* be used to facilitate splitscreen, couch co-op, or running multiple sessions for games that do not natively support those features and are dying. Think LAN parties for a game whose servers went dark, or getting two local copies to see each other for co-op that the developer never shipped. Therefore, it is expected that you have your own legitimately-owned copies of the game. There are already some great communities for accomplishing this, and we encourage you to look into those as well (*i.e.,* [SplitScreen.me](https://splitscreen.me), [Nucleus Co-op](https://nucleus-coop.github.io), and [PartyDeck](https://github.com/partydeck/partydeck) ). 


## Contributing

This project is, and always will be, free for everyone. This is still a work in progress, but the eventual hope is to get split screen and local co-op working on all EOS games, regardless of the operating system.

Pull requests, issues, and contributions are more than encouraged! Whether it's adding support for another game, fixing a busted call, improving the documentation, or porting to another platform, we'd love to have your help. Some technical documentation of how the EOS SDK behaves lives in [`docs/`](docs/).

## License

EOS Reimagined is free software, released under the GNU General Public License v3.0. See [`LICENSE`](LICENSE) for the full text.

## Acknowledgments

This project would not exist without Nemirtingas, whose original work on the EOS emulator years ago helped make this project a reality. We would also like to thank the following communities for their pivotal work in making co-op gaming a reality and reducing planned obsolescence in gaming as a whole: Stop Killing Games, SplitScreen.me, Nucleus Co-op, and PartyDeck.
