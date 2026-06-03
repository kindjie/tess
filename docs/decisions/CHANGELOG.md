# Design Changelog

Records meaningful design changes from the original TDDs.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```

## 2026-06-01 - Documentation Model

- Changed: TDDs are treated as historical design intent, while maintained
  architecture docs track current implementation behavior.
- Reason: TDDs are likely to drift as implementation validates and revises the
  original design.
- Affected docs: `docs/README.md`, `docs/architecture/README.md`,
  `docs/tdd/README.md`
- Affected code: none
