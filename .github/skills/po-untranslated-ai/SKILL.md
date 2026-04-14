---
name: po-untranslated-ai
description: "Use when translating localization/i18n .po files with AI. Only fill untranslated entries (empty msgstr/msgstr_plural), keep existing translations untouched, and add fixed AI comment tag without translating comments. Keywords: PO, i18n, localization, untranslated, zh_CN, AI_TRANSLATED"
---

# PO Untranslated AI Translation

## Outcome
- Fill only untranslated PO entries.
- Keep all existing human translations unchanged.
- Add a fixed comment tag `AI_TRANSLATED` to entries filled by AI.
- Never translate comment text itself.

## Scope
- Target files: `localization/i18n/*/ElegooSlicer_*.po`
- Default test locale: `zh_CN`

## Workflow
1. Count untranslated entries in target locale(s).
2. Translate only entries where `msgstr` is empty (or all plural `msgstr[n]` are empty).
3. Add `#. AI_TRANSLATED` to each newly filled entry.
4. Do not modify non-empty translations.
5. Run a spot-check in the edited locale file to verify quality.

## Execution Command
```bash
python scripts/ai_fill_po_translations.py --locale zh_CN --ai-tag AI_TRANSLATED
```

## Verbose Logging
- Use `--verbose` to print one log line per translated entry.
- Use `--verbose-match <keyword>` to print only records containing the keyword.
- Example: track entries containing `OrcaSlicer` while translating `zh_CN`.

```bash
python scripts/ai_fill_po_translations.py --locale zh_CN --ai-tag AI_TRANSLATED --verbose --verbose-match OrcaSlicer
```

## Quality Checks
- No existing non-empty `msgstr` was overwritten.
- AI tag exists only on newly filled entries.
- Placeholder tokens like `%s`, `%1%`, `\n` remain intact.
