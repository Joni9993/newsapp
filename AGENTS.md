# Instructions for AI assistants

Every AI tool working in this repository reads this file (Claude Code reads it through `CLAUDE.md`). The
project itself is described in the [README](README.md).

<!-- owner-rules v9: general (AGENTS.md) - begin -->

## General rules

### People

- Unless "Project knowledge" says otherwise, assume that the people you work with **do not program**: they
  build their projects together with AI assistants and rely on you for the code, its quality and its
  explanation. "Project knowledge" lists who works on the project.
- Talk to them in **German**, in plain words, without jargon. Say what changes for them, not which file
  changed.
- When they ask for ideas or something is unclear, **offer choices and let them pick before building**, with
  a recommendation. Do not build a proposal nobody chose.
- **Report honestly**: a test that failed, a step that was skipped, a fix that turned out to be wrong. Say
  what was **really tested** and what was only built or compiled, and what it means for them.
- **Merge, release or publish only when the person asks for it.** Never merge or overwrite the work of
  someone else without their consent; propose it to them (pull request, comment) instead.

### Language

- **Everything in the repository is English**: files and documentation, code comments, commit messages,
  branch names, pull requests, issues and release notes. Only the conversation with the people is German.
- Text that the product itself shows to its users (an app's interface, a device's display, its store page)
  stays in the language or languages the product is made for. "Project knowledge" names them.
- **A product in several languages stays in all of them.** Every new or changed text goes into every
  language the product offers, in the same change; never leave one language behind, drop one, or fall back
  to another language silently. Write each language naturally (not word for word), with its proper
  characters (ä, ö, ü, ß in German). Check that no text is missing in any language before calling the change
  done.
- Where a project still has older content in another language, do not translate it on your own: offer it as
  a choice.

### Testing

- Where the project has automated tests, **every new behaviour that can be tested automatically gets a test,
  and a sabotage check**: break the behaviour on purpose (it must still compile, and it must break the
  behaviour, not just a selector or a text), run the test, see it fail, put the code back and confirm that
  the file is byte for byte what it was (`cmp`). A test that passes against broken code is worthless.
- **What cannot be tested automatically** (hardware, devices, displays, animations, things that need a real
  account or service) gets no test suite of its own. Check it as far as possible (build, compile, a preview),
  then on the real device or service, and say plainly which part is checked how.
- Where the project has no tests at all, do not set up a test suite on your own: when a change would be
  risky without one, offer it to the owner as a choice.
- Run the project's checks before pushing, as "Project knowledge" or the project's own instructions say.
  Where they say nothing: the checks that cover what changed, and the whole suite when the change could have
  effects elsewhere.

### Safety and data

- **Never put secrets into the repository**: passwords, API keys, tokens, private addresses. Use the
  project's way of keeping them out (environment, settings, `.gitignore`); if there is none, ask.
- **Personal data** (of the people or of the product's users) is not sent anywhere new, logged or published
  without asking.
- **What users have stays usable after an update**: their settings, saved data, accounts. When a change
  would break or reset them, say so before building and offer a way to carry them over.
- **Ask before anything hard to undo**: deleting data or files that are not part of the change, rewriting
  history, changing a live service, publishing to a store.
- **New libraries or services** only with a reason, said in plain words; prefer what the project already
  uses.

### Changes

- **One topic per change** (branch, pull request): easier to check, to release and to undo.
- **One branch per task.** Create it from the latest default branch (usually `main`) with a name that says
  what the change is about: `type/short-topic`, lower case, English, words joined by hyphens - `feature/` for
  something new, `fix/` for a bug, `docs/` for documentation, `chore/` for tidying up (e.g.
  `feature/online-update`, `fix/ota-redirect-query`, `docs/ai-rules`). Not who or when, and nothing vague like
  `test` or `update2`. Push to that branch instead of the random branch name the environment gives the
  session; this is the owner's standing permission to push to such branches. A new task in the same session
  gets a new branch. Merge into the default branch only when the person asks for it; where others work on the
  project, propose the merge as a pull request instead. Where the project has its own naming habits, follow
  them.
- **Commit messages and pull request titles** say in one line what changes, e.g. "Fix online update: keep
  the signed download URL"; more detail, and why, goes below.
- Changes that users notice go into the release notes or changelog in plain words (in English, like
  everything in the repository; texts shown to users inside the product or in a store, such as "what's new",
  in every language the product offers).

### Repository

Aim for what is common practice on GitHub, in proportion to the project: a small experiment needs less than
something others install. **Do not restructure an existing repository on your own**: point out what is
missing, with a recommendation, and let the people choose.

- **README**: what the project is (with a picture where it helps), how to install and use it, how to work on
  it (build, test, release), and its licence. Keep it up to date with every change that affects it.
- **Pull request descriptions**: what changes, why, how it was tested (and what was not), and anything the
  reviewer should know.
- **Automatic checks (CI)**: every push and pull request builds the project and runs its tests
  automatically; merge only when they pass. Where a project has none, offer to set it up.
- **Releases**: version numbers `vMAJOR.MINOR.PATCH` (e.g. `v1.4.2`: major for breaking changes, minor for
  new features, patch for fixes). Files that are published are built by CI, never by hand. Each release says
  in plain words what is new and how to install or update.
- **Basics**: a `.gitignore` for build output and local files, a **licence** file (without one, nobody may
  legally reuse the code, even in a public repository; the choice of licence is the people's), and a
  `CHANGELOG.md` where releases alone do not tell the story.

### Showing changes

- Show visual changes rather than describing them: **screenshots** where the result runs on a computer.
- Where no screenshot is possible (a device, a display, a phone app, print): build a **preview** (a mock-up
  page, a pixel preview), or ask for a **photo** of the real thing.

### Checking facts at the source

- Do not build on memory where the real thing can be checked: read the actual source of a library, look at
  the real answer of a server or service, read the real data. Behaviour that "should" be so is often not.
- When a cause is unclear, **ask for a log or an error message before guessing**, and say what it would show.

### Keeping knowledge in the repository

- Sessions end and are deleted; the repository stays. What a later session needs to know goes into the
  project's files ("Project knowledge", the README, other docs, commit messages), not only into the
  conversation: decisions and why, traps that cost time, how to build, test and release, and the limits of
  the environment (what a cloud session cannot do there).

<!-- owner-rules v9: general (AGENTS.md) - end -->

## Project knowledge

### People

- Two people work on this project: the creator of the repository and a friend who uses the board. **Neither
  of them programs**; both work with AI assistants. They test on the real board and decide what gets merged
  and released.

### Product languages

- The board shows its texts in **German only** (display texts, messages of the web interface). Keep it that
  way; no second language is planned.

### Build and test

- The firmware is an ESP-IDF v5.3 project for the ESP32-C6. Cloud sessions have **no local ESP-IDF**: the
  GitHub build (`.github/workflows/build.yml`, "Firmware bauen") is the compile check. It runs on every
  push, builds the standard and the legacy variant (old 2" panel) and runs the host tests. Wait for the run
  after pushing; merge only when it is green.
- **Host tests** cover logic that also runs on a PC (so far the iCal parser, `tests/test_ical.py`; how to run
  it: README, "Tests"). New logic of that kind (parsers, text wrapping, settings validation) gets a test and
  a sabotage check.
- **Display, animation, Wi-Fi and the update function** cannot be tested automatically: green build, then a
  check on the real board.
- Before every push: `node --check main/app.js`, and a syntax check of `tools/preview.py` when it changed.
- **Previews:** the web UI with `python tools/preview.py` (sample data, README "Testing the web interface
  locally"). For the display, pixel previews (font 3x5 pixels, world 107x80 pixels, each pixel 3x3 on the
  display) or a photo of the board.

### Release

- A release is started **by hand on the Actions page**: workflow "Firmware bauen" -> "Run workflow" on
  `main` -> field `release_tag` (e.g. `v1.9`). The workflow creates tag and release and attaches
  `newsboard.bin`, `newsboard-legacy.bin` and both `-full.bin` files.
- The release description comes from `docs/release-intro.md`, followed by the automatic list of
  changes.
- Boards fetch updates from the latest release themselves (online update). A release reaches every board that
  checks for updates or installs them automatically at night.

### Repository

- **Licence:** MIT (`LICENSE`). The bundled QR code library in `main/vendor/` keeps its own licence.
- **Version scheme:** releases so far use `v1.8` and `v1.8.3`. From now on use `vMAJOR.MINOR.PATCH`
  (e.g. `v1.9.0`); the boards compare version strings for equality only, so the switch is safe.
- **CI** (`.github/workflows/build.yml`): on every push and pull request it checks the web UI script, runs
  the host tests and builds both firmware variants; releases are built by the same workflow.

### Traps

- **Online update** (`main/ota.c`):
  - GitHub redirects `releases/download/...` to a **signed** URL. The signature is in the query;
    `esp_http_client_get_url()` drops the query, so capture the raw `Location` header.
  - Resolve the redirect **with GET**, not HEAD: GitHub signs for the request method, and a URL obtained via
    HEAD returns 401 on download.
  - The download server sends 16 KB TLS records. The board's fragmented heap has no contiguous block for
    that (`alloc(16749 bytes) failed`). So the board downloads in **8 KB Range parts** over one connection
    and does not use `esp_https_ota`.
  - A bug in the update function can only be fixed by a **manual upload**, because the update button is
    what is broken. Review changes to `ota.c` with extra care.
- **Settings** (`main/config.c`, `config.h`):
  - The settings are one blob in NVS. Its size is pinned to 5180 bytes with `_Static_assert` and must not
    change, or old settings become invalid.
  - There is **no free byte** left. New switches live as bits in `theme`: bit 0 figure, bit 1 terminal
    header, bit 2 blinking colon. Read them only through the helpers (`THEME_FIGURE()`,
    `config_tty_header()`, `config_tty_blink()`), and keep the other bits when writing one.
- **Display:**
  - Custom glyphs use control codes below space: 1-3 Ä/Ö/Ü, 4 ß, 5 degree sign (`TEXT_DEG` in
    `main/text.h`).
  - The header bar is narrow (107 pixels, 4 pixels per character): draw new elements only when there is room.

### Limits of the environment

- Cloud sessions **cannot push tags** (HTTP 403) and **cannot delete branches on GitHub**. Releases always go
  through the workflow; a person deletes finished branches on GitHub.
- No local ESP-IDF (see "Build and test").
- The README and the release intro (`docs/release-intro.md`) are English. The board's display texts, the web
  interface and older commit history are German; display texts stay German (the product is made for German
  users), and translating the old history is not something to do on your own.
