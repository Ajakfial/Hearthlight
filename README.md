# Hearthlight — Your home for every world.

An all-in-one Minecraft: Java Edition launcher. Calm, welcoming, uncluttered:
a total beginner is playing in 60 seconds, modpack power users still get depth
under an **Advanced** toggle.

> **Disclaimer:** Hearthlight is not an official Minecraft product, and is not
> approved by or associated with Mojang or Microsoft.

What it does: Microsoft and offline accounts, isolated profiles with
Fabric/Quilt/Forge/NeoForge loaders, Modrinth discovery with one-click
installs, safe-update snapshots with undo, guided first-run setup, crash
explanations with fixes, world backups, and a friendly live log — all usable
offline once installed.

## Download a ready-to-run build (no compiling)

Every push to `main` builds all three OSes in CI. To grab one:

1. Open the repo's **Actions** tab
   ([Hearthlight actions](https://github.com/Ajakfial/Hearthlight/actions))
   and click the most recent successful run (green check).
2. Scroll down to **Artifacts** at the bottom of the run summary.
3. Download your OS and unpack it:
   - `Hearthlight-windows` — zip of the app folder. Extract and run
     `Hearthlight.exe` (ships with `portable.txt`, so it keeps its data
     next to the exe).
   - `Hearthlight-macos` — `Hearthlight.app` (universal arm64 + x86_64).
     On first launch, right-click → Open if macOS complains it is unsigned.
   - `Hearthlight-linux` — `.AppImage`. Run
     `chmod +x Hearthlight-*.AppImage && ./Hearthlight-*.AppImage`.

Notes: downloading artifacts needs a (free) GitHub account, and artifacts
are per-commit dev builds that expire — for a permanent copy, build from
source below.

## Build instructions

Requirements: CMake 3.21+, a C/C++20 compiler, Qt 6.5+ (`Core Gui Widgets
Network Svg SvgWidgets Concurrent Test`). First configure downloads miniz
(zip extraction) once into the build dir — after that, builds and the
launcher itself work offline.

### Windows (MSVC)

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc --config RelWithDebInfo
ctest --test-dir build/windows-msvc -C RelWithDebInfo -V
.\build\windows-msvc\src\app\RelWithDebInfo\Hearthlight.exe --portable
```

### Windows (MinGW)

```powershell
cmake --preset windows-mingw
cmake --build --preset windows-mingw
```

### macOS (universal arm64 + x86_64)

```sh
cmake --preset macos
cmake --build --preset macos
./build/macos/src/app/Hearthlight.app/Contents/MacOS/Hearthlight
```

### Linux (GCC/Clang)

```sh
cmake --preset linux
cmake --build --preset linux
ctest --test-dir build/linux -V
./build/linux/src/app/Hearthlight
```

Qt install: use your distro package (`qt6-base` etc.), the
[Qt online installer](https://www.qt.io/download), or
[aqtinstall](https://github.com/miurahr/aqtinstall). CI installs Qt 6.10.3 with
aqt (see `.github/workflows/ci.yml`).

Portable mode: pass `--portable`, create `portable.txt` next to the binary,
or `--data-dir <path>`. Paths with spaces/Unicode and Windows long paths are
supported (app manifest declares long-path awareness).

## Architecture overview

```
src/core/   no widget includes. Testable without a GUI.
  Constants, AppSettings, Logger, NetworkStatus
  Task (+LambdaTask, +SequentialTaskGroup), DownloadManager (incl. parallel batch)
  Account, AccountProvider (Offline real + Microsoft real), AccountStore, SecureTokenStore
  MicrosoftAuth (device-code/browser, Xbox/XSTS/Minecraft chain, skins/capes)
  GamePaths, VersionModel (rules/args/classpath), MojangApi, ZipUtil (miniz)
  VersionInstaller, JavaManager (Temurin), Launcher (vanilla + loader-merged, Quick Play)
  Instance, InstanceManager (.hearthpack/.mrpack export+import, CurseForge full import with user API key), ModLoader (Fabric/Quilt/Forge/NeoForge)
  ModrinthApi (v2 search/project/versions, dep resolution), CurseForgeApi (user-key file resolution),
  ModManager (mods + resource/shader/datapacks + updates), ContentPack helpers in ModManager
  Embers (safe-update snapshots + undo), Hearthstones (world/config backups)
  CrashDoctor (plain-English crash diagnosis), Kindling (curated starter sets), Markdown (renderer)
src/ui/     Theme, IconProvider, MainWindow shell (version + instance pipelines,
            safe-mode, Crash Doctor fixes, Kindling first-run),
            pages (Hearth dashboard, Versions, Profiles, Discover/Modrinth,
            Accounts w/ skins-capes, Settings),
            dialogs (offline account, Microsoft login, instance wizard, instance settings,
            content (mods + resource/shader/data packs), worlds/backups, screenshots,
            Kindling setup, task progress),
            Campfire GameLogDialog, widgets, dialogs
src/app/    main(), Application bootstrap (owns settings/store/tokens/services)
tests/      Qt Test: offline UUID, account store, task system, version model,
            java manager / launch-token rules, Microsoft parsers/errors,
            secure store, instances, loader metadata, Modrinth, CurseForge, Markdown,
            installed mods, content packs, .mrpack export, Embers, Crash Doctor, Hearthstones, Kindling
assets/     hand-written 24x24 SVGs (currentColor, 1.75px rounded strokes)
packaging/  Windows NSIS installer + portable zip, macOS .dmg script,
            Linux AppImage script + Flatpak manifest (see Packaging below)
```

Key rules: signal/slot everywhere, never block the GUI thread (all
file/network work runs on the global QThreadPool via `Task`), RAII, Qt
parent-child ownership, no raw owning pointers.

## How Microsoft authentication works

Full chain: Microsoft OAuth2 (**device-code flow by default**, browser flow
optional) → Xbox Live → XSTS → Minecraft services login → ownership check →
profile fetch (UUID, username, skin).

How to use it: **Accounts → Sign in with Microsoft**. A code appears, your
browser opens, you approve, and the window continues by itself. If the code
flow fails (kiosks, policies), the dialog offers the browser auth-code flow
instead: sign in, paste the code back, done.

Refresh tokens live in the OS credential store (Windows Credential Manager,
macOS Keychain, Linux Secret Service via `secret-tool` when present, else a
`0600` restricted file that says what it is) — never in plain text, never in
`accounts.json`, never in logs. Silent refresh runs inside the prepare task
before every Microsoft launch. Minecraft access tokens live in memory only.

Clear errors are implemented for: no Xbox profile (points at xbox.com),
child account needing family approval, no Minecraft purchase, region
restrictions, expired auth, and temporary service outages.

Skins/capes: the Accounts page shows your official skins and capes, changes
skins by public PNG URL (classic/slim), and equips/hides capes — all through
the official Minecraft services API. No ownership is ever spoofed, and
offline accounts never touch these endpoints.

The Azure client ID lives in one place:
`src/core/Constants.h` (`kAzureClientId`).

### Register your own Azure app

1. https://portal.azure.com → Microsoft Entra ID → App registrations → New.
2. Supported account types: personal Microsoft accounts + organizational.
3. Mark as public client (Device-code flow needs no redirect URI, but the
   portal requires "Allow public client flows" → Yes).
4. API permissions (delegated): `Xboxlive.signin`, `Xboxlive.offline_access`.
5. Copy the Application (client) ID into `kAzureClientId`.

Clear errors are specified for: no Xbox profile, child account needing family
approval, no Minecraft purchase, region restrictions, expired auth, and
temporary service outages.

## How offline accounts work

First-class local profiles, not a hidden dev mode. Use them for single-player,
LAN where the game permits, testing/mod development, or no-internet machines.

- Creation: **Accounts → Create Offline Account** (username, optional avatar
  color, optional custom UUID for advanced users).
- Default UUID is deterministic and Java-ecosystem compatible:
  `UUID.nameUUIDFromBytes(("OfflinePlayer:" + name).getBytes(UTF_8))`
  = MD5 of `OfflinePlayer:<name>`, version nibble forced to 3, variant to
  RFC 4122. See `offlineUuidForUsername()` in `src/core/Account.cpp`.
- No network contact. Stored in `<dataDir>/accounts/accounts.json` (atomic
  `QSaveFile` writes, schema validation, corrupt files quarantined to
  `*.corrupt-<stamp>.bak`). Never any tokens in that file — `Account::fromJson`
  rejects token-bearing objects.
- Launch identity: username + UUID, `user_type=legacy`, placeholder token
  `"0"` (obviously not a Microsoft JWT, never sent anywhere). No
  Microsoft/Xbox/XSTS calls, no ownership check.
- Duplicates rejected: same UUID, or same offline username
  case-insensitively. Type can never silently change Microsoft↔Offline.

### Limitations of offline accounts

- **No ownership, no authenticated online servers.** Joining one explains:
  *"This server requires a Microsoft-authenticated Minecraft account."*
- No official skin/cape upload (*"Official skin and cape management requires
  a Microsoft account."*), no ownership verification.
- Never bypasses server auth or impersonates players. Local skins stay local
  and labeled as such.

## Offline mode

Settings → General → Offline Mode: **Automatic / Always Offline /
Never Offline**. `Always Offline` blocks network-dependent auth and optional
requests. First launch with no internet still opens; installed versions,
offline accounts, detected/managed Java and cached metadata stay usable.

## Playing vanilla

**Versions page**: type filters (Releases default; Snapshots, Old Beta, Old
Alpha), search, release dates, installed badges, Refresh (background refresh
also runs at startup). Install downloads the client jar, rule-filtered
libraries, natives (extracted with exclude rules), the full asset objects
tree (shared `assets/objects` hash layout; old `legacy`/`pre-*` versions
also materialize `resources/` / `assets/virtual/`), and logging XML — all
SHA-1/SHA-256 verified with resume + backoff, honoring Settings →
Downloads parallelism (default 8).

**Play** (bottom bar or Versions page): installs anything missing, provisions
the `javaVersion.majorVersion` JVM (detected installs first, else verified
Eclipse Temurin into `java/<major>/`), builds the classpath and both legacy
(`minecraftArguments`) and modern (`arguments` game+jvm, feature rules)
argument formats with every placeholder substituted, then spawns the game
with a live log window. Playtime is recorded; non-zero exits show the exit
code, any fresh `crash-reports/*.txt`, and the redacted command under
"Show details". Tokens never appear in logs.

**Java**: Settings → Java lists every detected runtime (custom path,
managed, `JAVA_HOME`, `PATH`, well-known system locations) with a real
`java -version` test. Memory defaults to the suggested quarter-of-RAM
(shown as "Recommended" in Versions → Advanced); extras live under the
same Advanced toggle as window size, demo flag and launch behavior.

**Demo**: tick "Demo mode" on Versions to launch with the official `--demo`
flag and a clearly-labeled local `Player` profile — no account needed.

**Microsoft accounts** refresh silently before launch (worker thread, never
blocking the UI). Offline entries carry over untouched; nothing is ever faked.

## Profiles / instances

Each profile is an isolated instance (`instances/<id>/instance.json` plus
`game/{mods,config,saves,resourcepacks,shaderpacks,screenshots}`).

- **Profiles → New profile** wizard: name → Minecraft version → loader
  (Vanilla/Fabric/Quilt/Forge/NeoForge, "Latest stable" recommended) →
  account (current / specific / ask every launch).
- Right-click a profile: Play, Settings, Switch loader/version (backs up
  `mods+config+saves` first, warns about incompatible mods), Clone, Rename,
  Export `.hearthpack`, Delete (to Trash).
- Drag to reorder; filter by collection; search.
- Per-instance settings: Java path (+Test), memory (Automatic = Recommended),
  JVM args, window size/fullscreen, pre-launch/post-exit commands,
  environment variables (`NAME=value` per line), game-folder override,
  Quick Play world/server/realm.
- Import: Hearthlight `.hearthpack` (full round-trip), Modrinth `.mrpack`
  (overrides + URL downloads with SHA verification), CurseForge-style zip
  (full import when your API key is saved in **Settings → Mod sources** —
  every `projectID`/`fileID` resolves via the CurseForge API and downloads
  verified; without a key it installs local files and lists remote ones as
  skipped, honestly).
- Export: `.hearthpack` (everything) or `.mrpack` (Modrinth-known mods as
  URL downloads, configs/packs/hand-added jars in `overrides/`).
- Quick Play appends the official `--quickPlaySingleplayer/Multiplayer/Realms`
  flags on supported versions. Launching an authenticated online server with
  an offline profile asks for confirmation and explains the limitation.
- Playtime, last played, and last account are recorded per profile.

## Mod loaders

- **Fabric/Quilt**: official meta APIs list loader versions per game version;
  the launcher profile JSON is fetched, saved under `meta/loaders/`, merged
  over vanilla with `ParsedVersion::merge`, and missing libraries download
  with the same verified pipeline. Stays launchable offline once cached.
- **Forge/NeoForge**: promotions list (Forge) / Maven metadata (NeoForge)
  picks the version ("Latest stable" default); the official installer jar
  downloads, `install_profile.json` + version info extract, libraries
  download verified, and headless processors run with Java (legacy
  installers without processors just merge). Both legacy (pre-1.13) and
  modern installer layouts are handled; processor failures report the exact
  step and its output instead of pretending success.
- The resolved loader version is recorded in `instance.json`. Switching
  loader/version on an existing profile snapshots first (see above).

## Finding mods (Modrinth)

**Discover** searches Modrinth with filters (type, loader, game version,
category), sorting, and pagination. Project pages render the description,
gallery, version list with compatibility marks, changelog, links, and
license. **Install** picks the newest file matching the profile's Minecraft
version and loader, resolves required dependencies recursively, asks for
confirmation (listing mods and notes), snapshots first via Embers, then
downloads everything with SHA-512 verification into the right folder
(`mods/`, `resourcepacks/`, `shaderpacks/`, `datapacks/`). Modpacks download
as `.mrpack` and import as brand-new profiles. Each profile's **Content…** dialog has four tabs: Mods (on/off toggles,
removal, update checks, manual-detection for hand-added jars, Embers
rollback) plus Resource packs, Shader packs and Data packs (`.zip` on/off
toggles, add-by-hand, removal, open-folder). Drop a `.jar` onto a profile
row to add it by hand. Offline, cached results are labeled as such;
installs honestly need a connection.

## Curated mod lists (Kindling)

Drop a JSON file into `<dataDir>/kindling/` (single pack, `{"packs": [...]}`,
or a bare array); the first-run wizard offers it as a starting style
alongside the built-ins. Schema and rules in `docs/kindling.md`.

## Signature features

- **The Hearth**: big Play for the last-played profile, recent worlds,
  playtime summary, and one gentle suggestion (mod updates get a Review
  button; offline shows a calm notice instead).
- **Embers**: silent snapshots (mods + config, saves on loader switches)
  before every change; “Undo last change” on profiles, in Content…, and on the
  Hearth. Today's state is preserved before every restore, and old snapshots
  prune to the last 5.
- **Kindling**: first-run wizard (account → style → progress) building a
  profile preloaded with loader-matched Modrinth mods. Works offline (mods
  wait honestly); custom packs via `<dataDir>/kindling/*.json`.
- **Crash Doctor**: non-zero exits get a plain-English cause plus one-click
  fixes (safe-mode relaunch, more memory, open mods/crash report, Java and
  account shortcuts) next to the Campfire log.
- **Hearthstones**: per-profile world + settings backups, automatic before
  launch when worlds changed (last 3 autos kept, manuals never pruned),
  manual named backups, restore with confirmation.
- **Campfire log**: severity filter, search, Next-error jump, error/warning
  counts, auto-scroll, copy, **Save to file**, **System info** copy for bug
  reports, and **Share** (redacted upload to mclogs with a link) — tokens
  always redacted.
- **Screenshots**: per-profile browser (Profiles → Screenshots…) with
  previews, open-folder, delete-to-trash and copy-path.
- **Mod safe-mode**: “Play without mods once” hides `mods/` for a single
  launch and restores it on every exit path.
- **Offline Hearth**: no network, no problem — installed versions, offline
  accounts, cached metadata and search results, local worlds, Java, loaders
  and mods all stay usable; network features say so instead of breaking.

## Packaging

- **Windows**: `packaging/windows/Hearthlight.nsi` (NSIS) builds
  `Hearthlight-Setup-<version>.exe` — license page, Start Menu + desktop
  shortcuts, Add/Remove Programs entry, clean uninstall. Stage with
  `windeployqt` first; instructions are at the top of the script. Zipping the
  staged folder plus an empty `portable.txt` makes the portable build.
- **macOS**: `packaging/macos/make-dmg.sh` deploys Qt into `Hearthlight.app`
  (`macdeployqt`, universal arm64 + x86_64 via the `macos` preset) and writes
  a `.dmg` (`hdiutil` fallback included).
- **Linux**: `packaging/linux/make-appimage.sh` bundles an AppImage with
  linuxdeploy + the Qt plugin; `packaging/linux/flatpak.json` is a Flatpak
  manifest (KDE 6.7 runtime, network/GPU/notifications/secrets permissions).
- `cmake --install` works from any preset (binary, license, README, and the
  Linux desktop file land in the standard locations).

## License

MIT — see LICENSE (reasoning inside: maximum freedom for a community
launcher; no copyleft burden on pack makers).
