"""Exact inverse of the rich-generation E2E checks, preserving all older pins.

Only historical source contracts use this view. Validate complete current bytes,
undo the registered additions, then verify complete predecessor bytes. Actual
native execution runs the unmodified new sources, never this historical view.
"""
from pathlib import Path
import hashlib
import json

SPEC = json.loads(Path(__file__).with_suffix('.json').read_text(encoding='utf-8'))

def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()

def legacy_text(path, text):
    rule = SPEC['files'].get(path)
    if rule is None:
        return text
    if digest(text) != rule['current']:
        raise ValueError('recorded generation current bytes changed: '+path)
    for first, last in rule['cuts']:
        if text.count(first) != 1 or text.count(last) != 1:
            raise ValueError('recorded generation cut changed: '+path)
        i = text.index(first)
        j = text.index(last, i) + len(last)
        text = text[:i] + text[j:]
    for addition in rule['additions']:
        if text.count(addition) != 1:
            raise ValueError('recorded generation addition changed: '+path)
        text = text.replace(addition, '', 1)
    if digest(text) != rule['prior']:
        raise ValueError('recorded generation predecessor changed: '+path)
    return text
