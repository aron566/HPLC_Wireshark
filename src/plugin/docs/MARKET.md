# Plugin Marketplace

VS Code-style extension marketplace for BPLC STA Monitor.
Source: `src/plugin/market/` (`plugin_market.*` backend, `market_dialog.*` UI).

## Layout

```
+---------------------------------------------------------------+
| [search............]  [filter: All/Installed/Not installed]    |
+------------------------------+------------------------------+
| INSTALLED                    |  (icon) Display Name         |
|  card: name, author,          |  Author / version / category |
|  version, badge              |  [Install|Update] [Uninstall]|
| MARKETPLACE                  |  [x] Enabled                 |
|  card ...                    |  full description            |
|                              |  available versions          |
+------------------------------+------------------------------+
| status line                                               |
+---------------------------------------------------------------+
| [Install from file...] [Refresh]                  [Close] |
+---------------------------------------------------------------+
```

Entry point: **Plugins → Plugin marketplace** menu in the main window.

## Market feed

`market.json` (see `BPLC_Plugin_Market` repo):

```json
{
  "version": 1,
  "plugins": [{
    "name": "js-topo",
    "display_name": "...", "display_name_en": "...",
    "description": "...",  "description_en": "...",
    "category": "diagnosis | report | graphics | protocol",
    "author": "bplc",
    "versions": [{
      "version": "1.1.0",
      "url": "https://.../js-topo-1.1.0.zip",
      "sha256": "<hex>",
      "size": 2400,
      "min_app_version": "1.3.0"
    }]
  }]
}
```

Default feed URL: `https://raw.githubusercontent.com/aron566/BPLC_Plugin_Market/main/market.json`.
Override for dev/test: env var `BPLC_MARKET_FEED_URL` (supports `file://`).

## Package format

A plugin package is a zip containing at minimum:

```
plugin.json   # manifest, see plugin_api/plugin_manifest.h
<entry>       # entry script named by manifest "entry"
```

## Install flow

1. Market install: download zip → verify **sha256** against the feed value
   (mismatch = rejected) → unzip to temp dir → validate `plugin.json`
   (manifest + entry file must exist) → check `min_app_version` against the
   running app version → copy to the install dir.
2. Offline install (`Install from file...`): same chain minus sha256
   (no feed to compare against) — manifest validity and entry-file
   existence are still enforced.
3. Zip-slip guard: entries with absolute paths or `..` are rejected.

Install dir: `QStandardPaths::AppDataLocation + "/plugins/<name>/"`.
Each installed plugin gets `meta.json`: `{ "enabled": bool, "installed_at" }`.
Re-installing the same name replaces it (= update). `uninstall(name)`
deletes the dir; name is restricted to a single path segment
(`..`, `/`, `\` rejected).

## Enable / disable

`meta.json` `enabled=false` plugins are silently skipped by
`PluginWorker::start_load` (no error panel). Toggling in the dialog takes
effect after plugins are reloaded (dialog close triggers a reload of all
plugin dirs).

## Verification chain (current)

- sha256 of the downloaded package vs the feed (market installs).
- Manifest + entry-file validation for every install.
- `min_app_version` compatibility check before market installs.

**Not yet implemented:** Ed25519 official-signature verification of
packages (per the plugin-system interface draft). The feed and the
transport are not trusted; the local checks above are the trust anchor
for now. Signature verification is the next step before shipping
non-test plugins.

## API notes

- `PluginMarket` lives on the GUI thread; downloads are async via
  `QNetworkAccessManager` (`feed_ready`/`feed_error`,
  `install_progress`/`install_finished`).
- Static helpers (`parse_feed`, `verify_sha256`, `unzip_to_dir`,
  `compare_version`, `app_version_ok`) are unit-testable without a GUI.
- The main window loads plugin dirs in order: CLI `--plugins-dir`
  (or `<app>/plugins`) first, then the market install dir. Same plugin
  name in both: the later load wins (worker map keyed by manifest name).
