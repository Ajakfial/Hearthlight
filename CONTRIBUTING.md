# Contributing to Hearthlight

Thanks for stopping by the hearth. Keep it calm, welcoming and uncluttered.

## Ground rules

- Real implementations only. Never add mock auth, fake ownership checks,
  fake downloads or hardcoded credentials to make a feature *look* done.
  If something can't be finished in one change, land the real interface
  first with an honest "not available yet" message — never a fake success.
- `core/` must never include widgets. Keep it GUI-thread-free and unit
  tested. UI work goes in `ui/`; bootstrap/settings ownership in `app/`.
- No raw owning pointers. RAII + Qt parent-child ownership everywhere.
- Every user-visible error needs plain language + a "Show details" path.
  Never log or display access tokens (use `Logger::redacted`).
- Offline accounts are first-class: no Microsoft contact, no fake tokens,
  clear MICROSOFT/OFFLINE badges, honest limits on online servers.

## Workflow

1. Build with a preset: `cmake --preset linux && cmake --build --preset linux`.
2. Run tests: `ctest --test-dir build/linux -V`.
3. Format: `clang-format -i` on touched files (config in `.clang-format`).
4. Keep PRs to one focused piece with tests where logic allows.

## Adding an auth provider (example)

1. Subclass `IAccountProvider` in `src/core/` (see `OfflineAccountProvider`).
2. Keep secrets out of `Account`/`accounts.json` (tests enforce this).
3. Wire availability through `NetworkStatus` + Offline Mode.
4. Add UI in `src/ui/` with type badges and honest offline errors.
