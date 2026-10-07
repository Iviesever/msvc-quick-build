"""Exact inverse for the LINK-role regression additions, not product execution.

Validate all new bytes, undo only registered edits, validate all prior bytes.
The existing recorded-storage spec and its earlier fingerprints remain unchanged.
"""
from pathlib import Path
import hashlib
import json

SPEC = json.loads(Path(__file__).with_name('link_observation_role_extension.json').read_text(encoding='utf-8'))

def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()

def legacy_text(path, text):
    rule = SPEC['files'].get(path)
    if rule is None:
        return text
    # A revision must first reconstruct the complete first-candidate bytes;
    # the existing current/prior pins and main900 inverse remain authoritative.
    revision = rule.get('revision')
    if revision is not None:
        if digest(text) != revision['current']:
            raise ValueError('LINK-role revised bytes changed: '+path)
        for pair in revision['replacements']:
            if text.count(pair['after']) != 1:
                raise ValueError('LINK-role revision anchor changed: '+path)
            text = text.replace(pair['after'], pair['before'], 1)
    if digest(text) != rule['current']:
        raise ValueError('LINK-role current bytes changed: '+path)
    for first, last in rule.get('cuts', []):
        if text.count(first) != 1 or text.count(last) != 1:
            raise ValueError('LINK-role inverse boundary changed: '+path)
        i = text.index(first)
        j = text.index(last, i) + len(last)
        text = text[:i] + text[j:]
    for pair in rule['replacements']:
        if text.count(pair['after']) != 1:
            raise ValueError('LINK-role inverse anchor changed: '+path)
        text = text.replace(pair['after'], pair['before'], 1)
    if digest(text) != rule['prior']:
        raise ValueError('LINK-role original bytes changed: '+path)
    return text
