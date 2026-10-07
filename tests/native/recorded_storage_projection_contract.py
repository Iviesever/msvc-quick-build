"""Exact additive inverse for the explicit recorded-storage consumer.

Validate the complete new file, undo only registered additions, then validate
all old bytes. No wildcard exclusion, no changed historical hashes. This view
is only for older source contracts, never product execution or CI substitution.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
SPEC = json.loads(Path(__file__).with_name('recorded_storage_projection_extension.json').read_text(encoding='utf-8'))


def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def legacy_text(path, text):
    rule = SPEC['files'].get(path)
    if rule is None:
        return text
    if digest(text) != rule['current']:
        raise ValueError('recorded storage projection bytes changed: '+path)
    for first, last in rule['cuts']:
        if text.count(first) != 1 or text.count(last) != 1:
            raise ValueError('recorded storage projection boundary changed: '+path)
        i = text.index(first)
        j = text.index(last, i) + len(last)
        text = text[:i] + text[j:]
    for before, after in rule['replacements']:
        if text.count(after) != 1:
            raise ValueError('recorded storage projection inverse anchor changed: '+path)
        text = text.replace(after, before, 1)
    if digest(text) != rule['prior']:
        raise ValueError('original storage projection behavior changed: '+path)
    return text


def legacy_view(values):
    result = dict(values)
    for path, value in values.items():
        if path not in SPEC['files']:
            continue
        binary = isinstance(value, bytes)
        old = legacy_text(path, value.decode('utf-8') if binary else value)
        result[path] = old.encode('utf-8') if binary else old
    return result
