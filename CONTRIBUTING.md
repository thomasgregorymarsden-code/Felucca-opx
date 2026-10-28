# Contributing to OPX

OPX is a fork of [Felucca](https://github.com/hugelton/Felucca) that adds the OPX engine. Please report
problems with this firmware here, not to upstream Felucca: its maintainers did not write the OPX engine and cannot
support it.

| What | Where |
|---|---|
| A bug in the OPX engine, its pages or its storage, or anything that only happens on OPX | [Issues](https://github.com/thomasgregorymarsden-code/Felucca-opx/issues/new/choose): one issue per bug |
| A feature idea or request for OPX | [Issues](https://github.com/thomasgregorymarsden-code/Felucca-opx/issues/new/choose), as an idea |
| A bug that also happens on upstream Felucca 1.5.1 (installed from its own installer) | [Felucca's Issues](https://github.com/hugelton/Felucca/issues) |

## Reporting a bug

Please include:

- the version (MENU > SYSTEM > ABOUT, e.g. `v0.0.1`);
- whether the track plays OPX, and which SLOT (F1–F7 or OWN) and ALG;
- your computer's OS and browser, if the installer, the editor or USB is involved;
- the steps to reproduce it, what you expected and what happened. A photo or a short video helps.

## Pull requests

Pull requests are welcome, including AI-assisted ones, with the same bar for all:

- keep it small and focused on one thing;
- make sure you understand the change and can explain it;
- test it on a real FM-1 when it changes sound, timing, storage or USB;
- run `tests/run_tests.sh` (see [BUILDING.md](BUILDING.md)); CI runs it on every push;
- new files carry the GPL-3.0-only SPDX header.

Changes to the OPX engine stay inside its own files and the `FELUCCA_OPX` blocks where possible, so the fork can
follow upstream Felucca and upstream could take the engine later. Storage changes follow
[docs/OPX_STORAGE.md](docs/OPX_STORAGE.md).

By contributing you agree that your contribution is licensed under GPL-3.0-only.
