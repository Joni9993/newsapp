# Instructions for AI assistants

Every AI tool working in this repository reads this file (Claude Code reads it through `CLAUDE.md`). The
project itself is described in the [README](README.md).

<!-- owner-rules v14: general (AGENTS.md) - begin -->

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
- **Never overwrite `main` with a force-push and never delete it.** Not even when asked in passing; the
  owner does that themselves if it is ever needed.
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
  gets a new branch. Propose every change as a pull request into the default branch. It is merged when the person merges it on GitHub or asks for it ("übernehmen"); then merge it yourself (never squash away someone else's commits). Where the project has its own naming habits, follow
  them.
- **After a merge or release, the new version is not everywhere at once.** GitHub keeps copies for a while:
  GitHub Pages is usually live a minute or two after its run has finished, but browsers may show the old page
  for up to 10 minutes (raw files and download links up to about 5) unless reloaded without the cache;
  whatever a workflow builds or publishes only exists once its run has finished. Tell the person **how long to wait** (from the cache times
  and how long the project's last run took) and **how to check that the new version has arrived**: the run
  finished green under "Actions", the new version number in the app, on the release page or in the file, the
  new text on the page after a reload that bypasses the browser's cache. Where you can check it yourself, do
  so before calling it done, and say what you saw.
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

- **Protecting `main`**: when the rules are installed, check whether the repository is public or private.
  - **Public**: recommend to protect `main` against force-pushes and deletion, and give the steps below. The
    owner of the repository decides whether it makes sense for this project. You cannot set it up yourself,
    because you have no access to the repository settings.
  - **Private**: recommend nothing where the repository belongs to the rules' owner: they have no GitHub Pro,
    and in a free account branch protection and rulesets exist only for public repositories (source:
    `github/docs`, file `data/reusables/gated-features/protected-branches.md`). They can ignore GitHub's
    notes about missing protection there. Where the repository belongs to someone else, their plan decides
    and you cannot see it: mention once that protection on a private repository needs a paid plan (Pro or
    Team), and leave the decision to its owner.
  - **If you cannot tell** whether the repository is public or private (no way to look it up), ask instead of
    guessing.
  - **Whose repository it is matters**: only the repository's owner (or an admin) can open its settings. Where
    it belongs to someone else, say so and tell the person to pass the steps on to them.

  Steps for the owner (give them in German):

  1. Auf GitHub das Repository öffnen: **Settings → Rules → Rulesets → New ruleset → New branch ruleset**.
  2. Name: `main schützen`. **Enforcement status**: `Active`.
  3. **Target branches → Add target → Include default branch**.
  4. **Restrict deletions** und **Block force pushes** angehakt lassen.
  5. **Require status checks to pass** nur anhaken, wenn das Projekt CI hat; dann die CI-Prüfung auswählen.
     Achtung: Dann kann niemand mehr direkt auf `main` pushen, jede Änderung muss erst auf einem anderen
     Branch die Prüfung bestehen. Das passt nur, wenn ohnehin nichts direkt auf `main` geschrieben wird
     (auch kein Workflow). Bleibt ein Merge hängen, zuerst dort nachsehen: ob die Prüfung umbenannt wurde
     oder bei dieser Änderung gar nicht lief.
  6. **Nicht** "Require a pull request before merging" mit Approvals anhaken: Wer allein arbeitet, kann eigene
     Pull Requests nicht freigeben und würde sich selbst aussperren. Auch zu zweit ist es nicht nötig und
     bremst nur jeden Merge; der Schutz vor Force-Push und Löschen reicht.
  7. Auf **Create** klicken.

  Geklappt hat es, wenn das Ruleset in der Liste unter **Rulesets** als "Active" steht.
  Für weitere Projekte: das Ruleset dort über **Export ruleset** speichern und im nächsten Projekt über
  **Import a ruleset** übernehmen.

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

<!-- owner-rules v14: general (AGENTS.md) - end -->

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
