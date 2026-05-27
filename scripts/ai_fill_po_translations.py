#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import time
from pathlib import Path
from typing import Dict, List

import polib
from deep_translator import GoogleTranslator

I18N_ROOT = Path("localization/i18n")
AI_TAG = "AI_TRANSLATED"
DEFAULT_NO_TRANSLATE_TERMS = ["Orca", "OrcaSlicer"]

# Folder name -> Google target language code.
LANG_MAP = {
    "ca": "ca",
    "cs": "cs",
    "de": "de",
    "en": "en",
    "es": "es",
    "fr": "fr",
    "hu": "hu",
    "it": "it",
    "ja": "ja",
    "ko": "ko",
    "lt": "lt",
    "nl": "nl",
    "pl": "pl",
    "pt_BR": "pt",
    "ru": "ru",
    "sv": "sv",
    "tr": "tr",
    "uk": "uk",
    "zh_CN": "zh-CN",
    "zh_TW": "zh-TW",
}

PLACEHOLDER_ONLY_RE = re.compile(r"^[\s\d\W_]+$")
FORMAT_TOKEN_RE = re.compile(
    r"(%%|%\d+%|%(?:\d+\$)?[#0\- +'']*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|L|z|j|t)?[diuoxXfFeEgGaAcCsSpn])"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fill untranslated PO entries with AI-generated translations."
    )
    parser.add_argument(
        "--locale",
        action="append",
        default=[],
        help="Locale folder to process (can be passed multiple times), e.g. zh_CN",
    )
    parser.add_argument(
        "--ai-tag",
        default=AI_TAG,
        help="Comment tag added to AI-filled entries. This tag is not translated.",
    )
    parser.add_argument(
        "--no-translate-term",
        action="append",
        default=list(DEFAULT_NO_TRANSLATE_TERMS),
        help=(
            "Term that must stay unchanged in translated output. "
            "Can be passed multiple times, e.g. --no-translate-term OrcaSlicer"
        ),
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print detailed records for each entry translated by AI.",
    )
    parser.add_argument(
        "--verbose-match",
        action="append",
        default=[],
        help=(
            "Only print verbose records containing this substring in msgid/msgstr "
            "(can be passed multiple times)."
        ),
    )
    return parser.parse_args()


def is_untranslated(entry: polib.POEntry) -> bool:
    if entry.obsolete:
        return False
    if entry.msgid == "":
        return False
    if entry.msgstr_plural:
        return all((v or "").strip() == "" for v in entry.msgstr_plural.values())
    return (entry.msgstr or "").strip() == ""


def add_ai_tag(entry: polib.POEntry, ai_tag: str) -> None:
    existing = (entry.comment or "").splitlines()
    if ai_tag not in existing:
        existing.append(ai_tag)
    entry.comment = "\n".join(line for line in existing if line.strip())


def build_term_pattern(protected_terms: List[str]) -> re.Pattern | None:
    terms = [t for t in protected_terms if t and t.strip()]
    if not terms:
        return None
    # Match longest terms first so "OrcaSlicer" is protected before "Orca".
    terms = sorted(set(terms), key=len, reverse=True)
    escaped = "|".join(re.escape(term) for term in terms)
    return re.compile(escaped)


def protect_terms(text: str, protected_terms: List[str]) -> tuple[str, Dict[str, str]]:
    pattern = build_term_pattern(protected_terms)
    if pattern is None:
        return text, {}

    token_map: Dict[str, str] = {}
    counter = 0

    def _replace(match: re.Match[str]) -> str:
        nonlocal counter
        token = f"__NT_{counter}__"
        token_map[token] = match.group(0)
        counter += 1
        return token

    return pattern.sub(_replace, text), token_map


def restore_terms(text: str, token_map: Dict[str, str]) -> str:
    for token, original in token_map.items():
        text = text.replace(token, original)
    return text


def protect_format_tokens(text: str) -> tuple[str, Dict[str, str]]:
    token_map: Dict[str, str] = {}
    counter = 0

    def _replace(match: re.Match[str]) -> str:
        nonlocal counter
        token = f"__FMT_{counter}__"
        token_map[token] = match.group(0)
        counter += 1
        return token

    return FORMAT_TOKEN_RE.sub(_replace, text), token_map


def align_boundary_newlines(source: str, translated: str) -> str:
    """Keep msgstr leading/trailing newline counts consistent with msgid."""
    src = source or ""
    dst = translated or ""
    src_leading = len(src) - len(src.lstrip("\n"))
    src_trailing = len(src) - len(src.rstrip("\n"))
    core = dst.lstrip("\n").rstrip("\n")
    return ("\n" * src_leading) + core + ("\n" * src_trailing)


# ---------------------------------------------------------------------------
# Raw-text surgery – write translations without calling polib.save() so that
# unchanged lines are never touched (no git noise, no format changes).
# ---------------------------------------------------------------------------

def _po_escape(text: str) -> str:
    """Escape special characters inside a PO quoted string value."""
    out: List[str] = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        else:
            out.append(ch)
    return "".join(out)


def _po_value_lines(keyword: str, text: str, eol: str) -> List[str]:
    """
    Return PO-formatted lines for  ``keyword "text"``.
    Uses the multi-line ``""`` / continuation format when *text* contains newlines.
    *eol* is the file's line terminator ('\\n' or '\\r\\n').
    """
    if "\n" not in text:
        return [f'{keyword} "{_po_escape(text)}"{eol}']
    parts = text.split("\n")
    out = [f'{keyword} ""{eol}']
    for i, part in enumerate(parts):
        is_last = i == len(parts) - 1
        if is_last and not part:
            break  # trailing \n already represented as \\n in the previous line
        nl_suffix = "" if is_last else "\\n"
        out.append(f'"{_po_escape(part)}{nl_suffix}"{eol}')
    return out


def write_translations_raw(
    po_path: Path,
    entries_to_write: List[tuple],
    ai_tag: str,
) -> int:
    """
    Surgically insert translations into the raw PO file.

    *entries_to_write* items are either:
      - ``(entry, msgstr)``               for regular entries
      - ``(entry, msgstr_0, msgstr_1)``   for plural entries

    Only the empty msgstr lines and the AI-tag comment line are written;
    every other byte in the file is preserved exactly.
    Returns the number of entries written.
    """
    if not entries_to_write:
        return 0

    raw = po_path.read_bytes()
    eol = "\r\n" if b"\r\n" in raw else "\n"
    # Use newline='' to prevent Python's universal-newlines from converting
    # \r\n → \n on read (and \n → \r\n on write), which would double CRLF endings.
    with po_path.open("r", encoding="utf-8", newline="") as fh:
        lines: List[str] = fh.read().splitlines(keepends=True)

    # Process highest line numbers first so earlier edits don't shift later indices.
    sorted_items = sorted(entries_to_write, key=lambda x: x[0].linenum, reverse=True)

    written = 0
    for item in sorted_items:
        entry: polib.POEntry = item[0]
        is_plural = len(item) == 3

        start = entry.linenum - 1  # convert to 0-based
        msgid_idx: int | None = None
        msgstr_idx: int | None = None
        limit = min(start + 80, len(lines))
        for i in range(start, limit):
            bare = lines[i].rstrip("\r\n")
            if bare.startswith("msgid ") and msgid_idx is None:
                msgid_idx = i
            if bare.startswith("msgstr"):
                msgstr_idx = i
                break

        if msgstr_idx is None:
            continue

        # Determine the end of the msgstr block.
        end = msgstr_idx + 1
        while end < len(lines) and lines[end].lstrip().startswith('"'):
            end += 1
        if is_plural:
            # Consume additional msgstr[n] lines.
            while end < len(lines) and re.match(r"msgstr\[\d+\]", lines[end].rstrip("\r\n")):
                end += 1
                while end < len(lines) and lines[end].lstrip().startswith('"'):
                    end += 1

        # Insert the AI-tag comment immediately before the msgid line.
        if msgid_idx is not None:
            already_tagged = any(
                ai_tag in lines[k]
                for k in range(max(0, msgid_idx - 5), msgid_idx)
            )
            if not already_tagged:
                lines.insert(msgid_idx, f"#. {ai_tag}{eol}")
                msgstr_idx += 1
                end += 1

        # Build replacement msgstr lines.
        if is_plural:
            _, tr0, tr1 = item
            tr0 = align_boundary_newlines(entry.msgid, tr0)
            tr1 = align_boundary_newlines(entry.msgid_plural or entry.msgid, tr1)
            new = _po_value_lines("msgstr[0]", tr0, eol) + _po_value_lines("msgstr[1]", tr1, eol)
        else:
            _, tr = item
            tr = align_boundary_newlines(entry.msgid, tr)
            new = _po_value_lines("msgstr", tr, eol)

        lines[msgstr_idx:end] = new
        written += 1

    if written:
        with po_path.open("w", encoding="utf-8", newline="") as fh:
            fh.write("".join(lines))

    return written


def log_text(text: str) -> str:
    # Keep full content visible in logs while preserving line breaks as escape sequences.
    return (text or "").replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")


def should_log_entry(msgid: str, translated: str, verbose_match: List[str]) -> bool:
    if not verbose_match:
        return True
    haystack = f"{msgid}\n{translated}".lower()
    return any(term.lower() in haystack for term in verbose_match if term.strip())


def translate_text(
    translator: GoogleTranslator,
    text: str,
    cache: Dict[str, str],
    protected_terms: List[str],
    retries: int = 4,
) -> str:
    if text in cache:
        return cache[text]

    if not text.strip() or PLACEHOLDER_ONLY_RE.match(text):
        cache[text] = text
        return text

    if translator.target == "en":
        cache[text] = text
        return text

    protected_text, token_map = protect_terms(text, protected_terms)
    protected_text, format_map = protect_format_tokens(protected_text)

    wait = 0.8
    for attempt in range(retries):
        try:
            translated = translator.translate(protected_text)
            if translated is None:
                translated = protected_text
            translated = restore_terms(translated, format_map)
            translated = restore_terms(translated, token_map)
            translated = align_boundary_newlines(text, translated)
            cache[text] = translated
            return translated
        except Exception:
            if attempt == retries - 1:
                cache[text] = text
                return text
            time.sleep(wait)
            wait *= 1.8

    cache[text] = text
    return text


def fill_cache_with_batches(
    translator: GoogleTranslator,
    texts: List[str],
    cache: Dict[str, str],
    protected_terms: List[str],
    batch_size: int = 50,
) -> None:
    pending = [t for t in texts if t not in cache and t.strip() and not PLACEHOLDER_ONLY_RE.match(t)]
    if translator.target == "en":
        for text in pending:
            cache[text] = text
        return

    for i in range(0, len(pending), batch_size):
        chunk = pending[i : i + batch_size]
        protected_chunk = []
        token_maps: List[Dict[str, str]] = []
        for text in chunk:
            protected_text, token_map = protect_terms(text, protected_terms)
            protected_text, format_map = protect_format_tokens(protected_text)
            protected_chunk.append(protected_text)
            token_maps.append(token_map)
            token_maps.append(format_map)

        wait = 0.8
        translated_chunk = None
        for attempt in range(4):
            try:
                translated_chunk = translator.translate_batch(protected_chunk)
                break
            except Exception:
                if attempt == 3:
                    translated_chunk = protected_chunk
                    break
                time.sleep(wait)
                wait *= 1.8

        for idx, (src, dst) in enumerate(zip(chunk, translated_chunk)):
            translated = dst if dst else src
            translated = restore_terms(translated, token_maps[idx * 2 + 1])
            translated = restore_terms(translated, token_maps[idx * 2])
            cache[src] = align_boundary_newlines(src, translated)

        print(f"    translated batch {i + len(chunk)}/{len(pending)}", flush=True)


def process_file(
    po_path: Path,
    ai_tag: str,
    protected_terms: List[str],
    verbose: bool,
    verbose_match: List[str],
) -> tuple[int, int]:
    locale = po_path.parent.name
    target = LANG_MAP.get(locale)
    if not target:
        return 0, 0

    # Use polib only for parsing and translation; writing is done via raw surgery.
    po = polib.pofile(str(po_path), encoding="utf-8")
    translator = GoogleTranslator(source="en", target=target)
    cache: Dict[str, str] = {}

    texts_needed: List[str] = []
    for entry in po:
        if not is_untranslated(entry):
            continue
        if entry.msgstr_plural:
            texts_needed.append(entry.msgid)
            texts_needed.append(entry.msgid_plural or entry.msgid)
        else:
            texts_needed.append(entry.msgid)

    unique_texts = list(dict.fromkeys(texts_needed))
    fill_cache_with_batches(translator, unique_texts, cache, protected_terms)

    untranslated_before = 0
    entries_to_write: List[tuple] = []

    for entry in po:
        if not is_untranslated(entry):
            continue

        untranslated_before += 1

        if entry.msgstr_plural:
            tr0 = translate_text(translator, entry.msgid, cache, protected_terms)
            tr1 = translate_text(translator, entry.msgid_plural or entry.msgid, cache, protected_terms)
            entries_to_write.append((entry, tr0, tr1))
            if verbose and should_log_entry(entry.msgid, tr0, verbose_match):
                print(
                    f"ENTRY {po_path.as_posix()}\t"
                    f"msgid='{log_text(entry.msgid)}'\t"
                    f"msgstr[0]='{log_text(tr0)}'\t"
                    f"msgstr[1]='{log_text(tr1)}'",
                    flush=True,
                )
        else:
            translated = translate_text(translator, entry.msgid, cache, protected_terms)
            entries_to_write.append((entry, translated))
            if verbose and should_log_entry(entry.msgid, translated, verbose_match):
                print(
                    f"ENTRY {po_path.as_posix()}\t"
                    f"msgid='{log_text(entry.msgid)}'\t"
                    f"msgstr='{log_text(translated)}'",
                    flush=True,
                )

    written = write_translations_raw(po_path, entries_to_write, ai_tag)
    return untranslated_before, written


def main() -> None:
    args = parse_args()
    selected_locales = set(args.locale or [])
    protected_terms = [term.strip() for term in args.no_translate_term if (term or "").strip()]
    verbose_match = [term.strip() for term in args.verbose_match if (term or "").strip()]

    total_before = 0
    total_done = 0

    files = sorted(I18N_ROOT.glob("*/ElegooSlicer_*.po"))
    for po_path in files:
        locale = po_path.parent.name
        if selected_locales and locale not in selected_locales:
            continue

        print(f"PROCESS {po_path.as_posix()}", flush=True)
        before, done = process_file(
            po_path,
            args.ai_tag,
            protected_terms,
            args.verbose,
            verbose_match,
        )
        total_before += before
        total_done += done
        print(f"DONE {po_path.as_posix()}\tbefore={before}\ttranslated={done}", flush=True)

    print(f"TOTAL\tbefore={total_before}\ttranslated={total_done}")


if __name__ == "__main__":
    main()
