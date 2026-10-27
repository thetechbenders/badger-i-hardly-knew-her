# Wiki source, review and publishing

## Why this is a normal PR rather than a direct GitHub Wiki edit

GitHub's built-in **Wiki** is stored in a separate Git repository (`<repository>.wiki.git`), not as files on the application's main branch. Direct wiki edits cannot participate in ordinary review, stacking and CI against feature code. Therefore the versioned source is in **`wiki/`** in this repository.

Review documentation alongside the implementation it describes. If a change depends on a feature branch, merge its prerequisites first and then recheck the documentation diff against the intended release branch. Merging versioned source documentation does **not** itself publish a GitHub Wiki or constitute a firmware release.

## Link policy and maintenance

This version intentionally uses **repository-relative links** among wiki pages and into `../docs/`, `../config/` and source. Those are correct when browsing the `wiki/` directory within GitHub's code viewer. They are **not necessarily correct if copied unchanged** to the separate GitHub Wiki repository.

Before publishing to GitHub Wiki:

1. Agree whether the canonical source will stay in `wiki/` or be maintained only in `*.wiki.git`. Prefer a **single source of truth** to avoid two divergent copies.
2. For an actual Wiki export, rewrite internal page links to that wiki's page slugs and rewrite `../docs/`/source paths to **absolute, version-appropriate repository URLs** (or publish alongside corresponding source files).
3. Confirm the repository actually has a GitHub Wiki enabled and that the maintainer has permission to push the separate Wiki Git repo. This PR **does not** create or configure those permissions.
4. Review rendered Markdown for anchors, tables, code fences and images. Check links after publication.
5. Check every stated feature against the merged code and update hardware evidence links before announcing release.
6. Preserve `Home.md` as the landing page and `_Sidebar.md` as navigation for a GitHub Wiki mirror. `wiki/README.md` is deliberately a repository-source guide, not a sidebar destination.

No automatic sync/push is added here: automatic publication from an unmerged feature branch could publish unreleased claims and would need dedicated credentials and review gates. If automation is added, run it only after an explicit maintainers' approval and check the mirror against the canonical source.

## Documentation upkeep

The existing `docs/` files remain the detailed contracts and dated evidence logs. This wiki organizes them for users, reviewers and maintainers. If instructions conflict, don't silently choose the more convenient one: inspect the current implementation, add the new evidence, and correct both sources in a reviewed commit.

For sensitive examples and testing records, see [privacy](Privacy-and-Backups.md) and [validation](Testing-and-Validation.md).
