# GitHub cleanup notes

This repository presentation was cleaned up for the **PC GUI v1.5 + XEX v7.04** stable source.

## Repository-only changes

- Replaced the old `README.txt` with a GitHub-formatted `README.md`.
- Corrected public version naming to PC GUI v1.5 / XEX v7.04.
- Consolidated old `.txt` notes into Markdown under `docs/`.
- Removed the stale source-tree `SHA256SUMS.txt` that referenced v1.4 GUI files.
- Removed the outdated GUI preview image that showed an older version.
- Added `.gitignore` and `.gitattributes`.
- Added a bug-report issue template and pull-request template.
- Added clear safe-unload, LAN security, build and release guidance.

## Intentionally not changed

- No PC GUI source behavior changes.
- No XEX source behavior changes.
- No XDK/RGLoader cache-flush portability changes.
- No insecure-socket privilege 6 addition.
- No changes to `xex.xml`.
- No protocol, port, hook, voice, network or unload logic changes.

The `PC-GUI-source/` and `XEX-source/` files in the cleaned repository were verified byte-for-byte against the supplied v1.5/v7.04 source package.
