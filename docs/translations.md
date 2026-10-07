# Translating Hearthlight

Hearthlight is written in English. Every user-visible string goes through
Qt's `tr()`, so the whole UI can be translated without touching code.

## Using a translation

1. Put the compiled file in the translations folder, named with a Qt locale
   code:
   - Windows: `%APPDATA%\Hearthlight\translations\hearthlight_de.qm`
     (or `HearthlightData\translations\` next to the exe in portable mode)
   - macOS: `~/Library/Application Support/Hearthlight/translations/hearthlight_de.qm`
   - Linux: `~/.local/share/Hearthlight/translations/hearthlight_de.qm`
2. Open **Settings → General → Language** and pick it (new files appear in
   the list automatically), or leave **System default** — Hearthlight follows
   your OS locale on its own (e.g. a German system loads `hearthlight_de`).
3. Restart. Missing strings silently stay English; nothing ever shows a key.

Lookup order is `hearthlight_<full-locale>` (e.g. `pt_BR`) then the base
language (`pt`), first in the bundled resources, then in your translations
folder — so your files always win.

## Making a translation

You need Qt's Linguist tools (they ship with every Qt install: `lupdate`,
`linguist`, `lrelease`).

```sh
# 1. Extract every translatable string into a fresh .ts file:
lupdate src -ts translations/hearthlight_de.ts

# 2. Translate (Qt Linguist GUI, or any text editor — .ts is XML):
linguist translations/hearthlight_de.ts

# 3. Compile to the binary format Hearthlight loads:
lrelease translations/hearthlight_de.ts -qm hearthlight_de.qm

# 4. Drop the .qm into the translations folder above and restart.
```

Tips:

- `tr()` contexts are class names (`MainWindow`, `HearthPage`, …) — the same
  English word in two places can need two different translations.
- Placeholders like `%1` and HTML (`<b>…</b>`) must survive translation:
  keep them, move them, never delete them.
- Strings with `QLatin1Char('\n')` joins or `·` separators are just text.
- Accelerator-free plain language is a feature: translate the meaning, keep
  it calm and short.
- Re-run `lupdate` after pulling new code; it merges new strings and keeps
  your finished work. Unfinished entries fall back to English per string.
- To contribute a translation upstream, send the `.ts` source (not just the
  `.qm`) so it can be updated with the code.
