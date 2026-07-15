# EOS Reimagined alpha quick start

This is a LAN-only compatibility alpha. It does not connect to Epic services, traverse the public
Internet, provide anti-cheat, or attest Epic accounts or game ownership.

Use `EOSSDK-Win64-Shipping.dll` for Windows games, including games running through Proton or Wine.
Use `libEOSSDK-Linux-Shipping.so` only for native Linux games.

For the first run, use the included runner from the extracted release package:

```sh
python3 tools/alpha_runner.py run "/path/to/Game" \
  --artifact "/path/to/the/release/artifact" --manual
```

It inventories the game's EOS calls, verifies and replaces the original SDK, prints launcher settings,
restores the original after the game exits, and creates a sanitized diagnostic bundle. If the runner or
machine is interrupted while staged, recover with:

```sh
python3 tools/alpha_runner.py restore --sdk "/path/to/Game/.../EOSSDK-Win64-Shipping.dll"
```

Every player must use the same game version and EOS Reimagined build on the same LAN. Start with the
game's own host and server/lobby browser. Overlay-only invitations remain unavailable until the planned
external companion is implemented.
