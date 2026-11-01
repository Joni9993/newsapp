@AGENTS.md

<!-- owner-rules v10: claude (CLAUDE.md) - begin -->

## Claude: models and usage

The owner pays for a plan with a usage limit, and the stronger a model, the faster it uses the limit up. The
cost of a task is not the size of the change but **the number of steps times the length of the conversation**:
every step processes the whole conversation again.

### Choosing the model

Use **the cheapest model that does the task reliably**. As of late 2026 there are three tiers; the names change
over time, the principle does not. Check the models currently offered (`/model`) rather than trusting this list,
and when the owner says the limits or the offer have changed, follow what they say.

| Tier                                       | For                                                                                   |
| ------------------------------------------ | ------------------------------------------------------------------------------------- |
| Small and fast (now: **Haiku**)            | Subagents only: finding things in the code, reading logs and test output, mechanical edits across many files with an exact brief. Not for decisions, not for talking to the owner. |
| Middle (now: **Sonnet**) - **the default** | Small fixes, texts and styling, documentation, questions about how something works or what a message means, data analyses, and the steps of a task Opus has planned. |
| Strongest (now: **Opus**; **Fable** where the owner has access) | A bug whose cause is unclear, a larger new feature or anything touching security or updates, a decision that is hard to take back, a problem the middle tier has failed at twice. |

- The session runs on the model the owner picked; only they can switch it (`/model`). **Say so** when a task
  would be better done on another tier - up for something hard, down once the hard part is done.
- Subagents get their model set explicitly (`model: "haiku"`, `model: "sonnet"`), never left to inherit the
  session's.

### Delegating

- On the strongest tier, **delegate the steps**. Anything with several steps - editing, running tests, waiting
  for a build, sabotage checks, screenshots - goes to a subagent on the cheapest tier that can do it, with a
  short brief, told to report tersely: what changed, the test and build results, nothing else. The strong model
  plans, reads the diff and the report, decides, and commits.
- This holds for small changes too: a fresh subagent's short context is cheaper than each step dragging a long
  conversation along. Do not argue a task is too small to hand over.
- Never trust a report blindly: read the diff before committing, and check its claims about libraries or
  services against the real source where it matters (see "Checking facts at the source").
- Subagents must not leave background jobs running when they finish.

### Keeping the cost down

- **Screenshots** only when something visible changed. Images are expensive.
- **A long session costs on every step.** Once a topic is done and committed, suggest starting a new session for
  the next one; everything worth keeping is in the repository.
- Keep tool output short: filter logs and test runs to what matters rather than reading them whole.
- Waiting for something (a build, a release): schedule a check-in instead of polling step by step.

<!-- owner-rules v10: claude (CLAUDE.md) - end -->
