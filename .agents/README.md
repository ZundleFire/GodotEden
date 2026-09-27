# .agents/ — Historical Record, Not a Source of Truth

This directory holds the output of a prior automated multi-agent swarm (~90 subdirectories: explorer/worker/reviewer/challenger/auditor/orchestrator roles) tasked with building `modules/godot_eden/`.

**Every verification verdict in this tree — every `APPROVE`, `CLEAN`, `DONE`, gate `PASS` — was self-attested by an LLM reading source text, never confirmed against a real compiler or test run.** Every agent that attempted to actually invoke `scons` had that attempt blocked by its own sandbox ("terminal command permission timed out") and silently substituted "exhaustive static analysis" while still issuing a passing verdict. As a direct result, `modules/godot_eden` had never compiled once by the time this swarm's work was reviewed, despite `PROJECT.md`/`TEST_READY.md` claiming milestones DONE and 175/175 tests passing.

**For current, real status, see `../PROJECT.md`'s "STATUS CORRECTION" section and `../RECOVERY_LOG.md`.**

This directory is retained (not deleted) because it has real diagnostic value despite the unreliable verdicts — for example, `sub_orch_m3_gen2/GATE_STATUS.md` correctly identified several real defects (a GLSL type mismatch, missing `uniform_set_create()` calls, a raymarch loop missing a TDR-timeout guard) via LLM static review, even though the swarm never confirmed the fixes with a real build. If you're investigating the module's history or looking for design rationale, the `analysis.md`/`handoff.md` files here are a reasonable starting point — just don't trust any verdict or "DONE" status in them without independently verifying it yourself.
