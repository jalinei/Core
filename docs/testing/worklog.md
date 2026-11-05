# Test work log

A dated log of every change made to **production code** (anything under `src/` or `owntech/`) while building the test procedure. Test-only changes are not logged here; they are described by their commits.

The procedure tests existing features. It must not change behaviour. Each entry therefore states the change, why the tests needed it, and the evidence that behaviour is preserved.

## Entry format

```markdown
## YYYY-MM-DD — <short title>

| Field | Value |
|---|---|
| Commit | `<sha7>` |
| Files | `path/a`, `path/b` |
| Reason | Why the test procedure needed this change |
| Behaviour change | None / description |
| Evidence | Tests, campaign record, or hardware check that shows behaviour is preserved |
| Hardware check | Not done / done on <date> with <board/shield version>: <result> |

<free-form notes: anything a reviewer must know, e.g. changed order of operations inside a tick>
```

## Entries

_No production-code changes yet. The first planned entry is the `controlStep` seam extraction (commit 5 of the plan in [test-procedure.md](test-procedure.md#9-commit-plan))._
