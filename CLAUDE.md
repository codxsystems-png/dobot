<!-- code-review-graph MCP tools -->
## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph. ALWAYS use the
code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore
the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file
scanning cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes` or `query_graph` instead of Grep
- **Understanding impact**: `get_impact_radius` instead of manually tracing imports
- **Code review**: `detect_changes` + `get_review_context` instead of reading entire files
- **Finding relationships**: `query_graph` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview` + `list_communities`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool | Use when |
| ------ | ---------- |
| `detect_changes` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context` | Need source snippets for review — token-efficient |
| `get_impact_radius` | Understanding blast radius of a change |
| `get_affected_flows` | Finding which execution paths are impacted |
| `query_graph` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes` | Finding functions/classes by name or keyword |
| `get_architecture_overview` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes` for code review.
3. Use `get_affected_flows` to understand impact.
4. Use `query_graph` pattern="tests_for" to check coverage.

---

<!-- superpowers extension -->
## Superpowers Skills (Active in this workspace)

The `superpowers` extension is installed and **all skills are enabled** for this project.
Use the `Skill` tool to invoke any skill by name before acting.

### Skill Activation Rules

| Skill | Trigger condition |
|---|---|
| `using-superpowers` | **Every** conversation start — loads skill index |
| `brainstorming` | Before ANY creative/feature/component work |
| `writing-plans` | Before multi-step tasks or when given a spec |
| `executing-plans` | When running a written implementation plan |
| `test-driven-development` | Before writing any implementation code |
| `systematic-debugging` | On any bug, test failure, or unexpected behavior |
| `verification-before-completion` | Before claiming work is done or creating PRs |
| `requesting-code-review` | After completing features or major changes |
| `receiving-code-review` | When processing review feedback |
| `subagent-driven-development` | For parallel independent tasks in current session |
| `dispatching-parallel-agents` | When 2+ independent tasks can run concurrently |
| `using-git-worktrees` | Before feature work needing workspace isolation |
| `finishing-a-development-branch` | After implementation complete, all tests pass |
| `writing-skills` | When creating or editing skill definitions |

### Instruction Priority

1. **User instructions** (`CLAUDE.md`, direct requests) — highest priority
2. **Superpowers skills** — override default behavior where they conflict
3. **Default system prompt** — lowest priority

### Key Rule

> Invoke relevant skills **BEFORE any response or action**. Even a 1% chance a skill applies means invoke it first.
