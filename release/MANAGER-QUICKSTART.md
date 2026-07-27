# EOS Reimagined Manager quick start

`EOSReimaginedManager.exe` on Windows and `eos-reimagined-manager` on Linux are self-contained GUI
applications. They do not require Python or an installed FLTK runtime. Keep the executable,
`MANIFEST.json`, and the `artifacts` directory together: the manager verifies the exact packaged SDK
artifact before enabling Install.

## Add, install, and launch a Steam game

1. Close the game. Start the manager and choose **Discover Steam**. Native and Flatpak Steam
   libraries are supported. If a game is missed, choose **Add game folder**.
2. Select the game and review the exact EOS targets found by the bounded scan. The manager marks a
   unique safe target **Recommended** when the evidence is decisive; for Unity games it prefers the
   architecture-specific `Plugins/x86_64` target over a legacy root duplicate. It selects nothing
   automatically when evidence remains ambiguous. A Proton game should show a **Windows x64 DLL**;
   a native Linux game should show a **Linux x86_64 shared object**.
3. Choose an **EOS Reimagined network display name** and local instance label. The network name is
   what EOS UserInfo and authenticated peers see; the game's own Steam/local nickname is separate.
   The manager creates an isolated private profile/configuration directory and enables `lifecycle`
   tracing for the alpha. `profile.key`, not either name, is the persistent player identity.
4. Read the final review, including the original-backup path, then choose **Install**. If the target
   is running, changed, symlinked, or cannot be backed up and verified, the operation stops safely.
   If health says **Original unknown**, EOS Reimagined was installed outside this manager and there
   is no manager-owned verified original backup. Use Steam's **Verify integrity of game files**,
   recheck health, and install only after the genuine original is restored.
5. Choose **Launch**. This uses normal Steam launch handling and waits up to 45 seconds for a new
   `runtime.json`; seeing it proves the selected SDK and instance loaded.

Use **Configure** to edit every supported directive. Unsupported compatibility toggles remain
visible but disabled. Inherited `EOSR_*` environment variables take precedence over the saved file.
The **Instances → Edit label** action changes only the manager's cosmetic label; it does not change
`eosr.json`, the game nickname, or the profile identity. After launch, the Runs view compares the
saved network name with the authoritative effective value in `runtime.json` and labels every
effective directive as coming from the environment, the file, or a default.

## Diagnostics and restoration

The **Runs** view shows SDK/build, OS/Wine, lifecycle, Sessions/Lobby, P2P, and stubbed-function
evidence. **Build support bundle** creates a new deny-by-default sanitized sibling directory and
reports what it included or excluded. Review `summary.json` before sharing; profile keys, raw stdout,
display names, and unknown files are excluded.

To remove EOS Reimagined from a game, close it and choose **Restore**. The manager restores only the
hash-verified original backup and removes only sidecars it still owns. If Steam or another tool
changed the live DLL/SO, nothing is overwritten; use **Open game folder** and Steam's **Verify
integrity of game files** workflow instead.

Deleting an instance requires an explicit choice to retain its directory, export and verify its
private identity first, or confirm permanent identity loss.

The manager never edits Steam's private `localconfig.vdf`, uses no undocumented Steam IPC, and never
runs automated tests against real game installations.
