# Kindling packs — curated starter sets

Kindling is the first-run setup: pick a style, get a profile preloaded with a
curated, loader-matched Modrinth set. Besides the built-in styles
(**Just Minecraft**, **Performance boost**, **Adventure & exploration**,
**Building & decoration**), you can drop your own JSON files into
`<dataDir>/kindling/` — they appear in the setup wizard automatically.

## Single-pack file

```json
{
  "id": "cozy",
  "title": "Cozy cottagecore",
  "summary": "Warm blocks and soft lighting for slow evenings.",
  "loader": "fabric",
  "slugs": ["chipped", "macaws-bridges", "appleskin"]
}
```

## Multi-pack file

```json
{
  "packs": [
    { "id": "pvp", "title": "PvP practice", "loader": "fabric", "slugs": ["sodium", "krypton"] }
  ]
}
```

A bare JSON array of pack objects works too.

## Rules

- `id` and `title` are required. `loader` is one of `vanilla`, `fabric`,
  `quilt`, `forge`, `neoforge` (lowercase; anything else skips the file).
- `slugs` are Modrinth project slugs (the short name in the project's page
  URL). Each slug is resolved live; anything that can't be resolved — or has
  no file for the chosen Minecraft version + loader — is listed honestly and
  skipped, never faked.
- Invalid files are skipped with a warning in the log; one bad file never
  blocks the others.
- `summary` is shown under the title; omit it if you like.
