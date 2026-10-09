"""Exact inverse of the opt-in ordinary-target cache extension.

Validate new whole-file bytes, reverse explicit substitutions, then validate the
complete prior file. Historical tests see unchanged historical behavior; this is
not a wildcard exemption for new or old assertions. New helpers are pinned too.
"""
from pathlib import Path
import hashlib
import json
import recorded_generation_extension as generation
ROOT=Path(__file__).resolve().parents[2]
SPEC=json.loads((Path(__file__).with_name('target_wave_cache_extension.json')).read_text(encoding='utf-8'))

def git_blob(text):
    data=text.encode('utf-8')
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def legacy_text(path,text):
    pin=SPEC['files'].get(path)
    if pin is None:return text
    if git_blob(text)!=pin['current']:raise ValueError('target-wave extension bytes changed: '+path)
    for before,after in reversed(pin['replacements']):
        if text.count(after)!=1:raise ValueError('target-wave inverse anchor changed: '+path)
        text=text.replace(after,before,1)
    if git_blob(text)!=pin['prior']:raise ValueError('preexisting target behavior changed: '+path)
    return text

def verify_helpers():
    for path,pin in SPEC['new_helpers'].items():
        if git_blob(generation.legacy_text(path, (ROOT/path).read_text(encoding='utf-8')))!=pin:
            raise ValueError('target-wave helper assertions changed: '+path)

def legacy_view(values):
    verify_helpers()
    result=dict(values)
    for path,value in values.items():
        if path not in SPEC['files']:continue
        binary=isinstance(value,bytes)
        old=legacy_text(path,value.decode('utf-8') if binary else value)
        result[path]=old.encode('utf-8') if binary else old
    return result
