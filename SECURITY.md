# Security Policy

Black Pearl Engine is a local-only `d3d9.dll` game mod.
No network access, no telemetry, no remote code.

## Scope

- Local game process memory patching only.
- Config files (`bpe*.cfg`) stay next to the game binary.

## Reporting

Report vulnerabilities via GitHub issues on this repository.

## Hygiene

- No secrets, tokens, or credentials in the repo.
- Do not commit `d3d9.dll` binaries or game files.
- Vendored `lib/minhook` and `lib/imgui` are pinned; do not upgrade blindly.
