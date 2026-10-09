"""Exact archive extension view for historical source contracts only.

Validate the real 97/97 inventory and complete added file bytes first. A
registered edited file must match its entire current hash before explicit
splices reconstruct its entire accepted predecessor. Predecessor text is not
accepted as current input; no directory, test family or unmatched edit is
ignored. Product/native execution always uses the unmodified current files.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
SPEC = json.loads(Path(__file__).with_suffix('.json').read_text(encoding='utf-8'))
ADDED = frozenset((
    'cpp/include/mqb/orchestration/ArtifactGenerationArchive.hpp',
    'cpp/src/orchestration/incremental/ArtifactGenerationArchive.cpp',
    'cpp/src/orchestration/incremental/ArtifactGenerationArchiveInternal.hpp',
    'cpp/tests/orchestration/incremental/artifact_generation_archive_tests.cpp',
))
REGISTRATIONS = ('cpp/mqb.json', 'tests/native/run_native_tests.ps1',
                 'tests/native/assert_cpp_layout.ps1')


def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def text_value(value):
    if isinstance(value, bytes):
        return value.decode('utf-8')
    if not isinstance(value, str):
        raise ValueError('archive source view requires UTF-8 text or bytes')
    return value


def changed(path):
    label = {'tests/native/assert_cpp_layout.ps1': 'snapshot layout / reporting layout',
             'tests/native/run_native_tests.ps1': 'native driver policy',
             'cpp/mqb.json': 'registered manifest/policy'}.get(path, 'generation archive')
    return ValueError(label+' current bytes changed: '+path)


def legacy_text(path, text):
    rule = SPEC['modified'].get(path)
    if rule is None:
        return text
    if digest(text) != rule['current']:
        raise changed(path)
    previous_end = 0
    for edit in rule['edits']:
        offset = edit['offset']
        if (type(offset) is not int or offset < previous_end or offset > len(text) or
                text[offset:offset+len(edit['after'])] != edit['after']):
            raise ValueError('generation archive inverse splice changed: '+path)
        previous_end = offset + len(edit['after'])
    for edit in reversed(rule['edits']):
        offset = edit['offset']
        text = text[:offset] + edit['before'] + text[offset+len(edit['after']):]
    if digest(text) != rule['prior']:
        raise ValueError('generation archive predecessor bytes changed: '+path)
    return text


def verify_current_tree(files):
    if set(SPEC['added']) != ADDED:
        raise ValueError('archive added-file registration changed')
    product = {p for p in files if p.startswith('cpp/src/') and p.endswith('.cpp')}
    native = {p for p in files if p.startswith('cpp/tests/') and p.endswith('_tests.cpp')}
    expected_product = set(SPEC['product_tus']) | {p for p in ADDED if p.startswith('cpp/src/') and p.endswith('.cpp')}
    expected_native = set(SPEC['native_tests']) | {p for p in ADDED if p.endswith('_tests.cpp')}
    if len(expected_product) != 97 or product != expected_product:
        raise ValueError('production manifest inventory is not the exact 96+1 union')
    if len(expected_native) != 97 or native != expected_native:
        raise ValueError('native inventory is not the exact 96+1 union')
    cpp_files = {p for p in files if p.startswith('cpp/') and Path(p).suffix in ('.cpp', '.hpp', '.ps1', '.json')}
    if cpp_files != set(SPEC['cpp_files']) | ADDED:
        raise ValueError('archive source inventory contains missing or unregistered files')
    for path, pin in SPEC['added'].items():
        if path not in files or digest(text_value(files[path])) != pin:
            raise ValueError('registered archive addition bytes changed: '+path)
    for path in REGISTRATIONS:
        if path not in files or digest(text_value(files[path])) != SPEC['modified'][path]['current']:
            raise changed(path)


def historical_tree(files):
    """Validate the full current tree before returning a separate old view."""
    verify_current_tree(files)
    result = dict(files)
    for path in ADDED:
        del result[path]
    for path in SPEC['modified'].keys() & result.keys():
        value = result[path]
        old = legacy_text(path, text_value(value))
        result[path] = old.encode('utf-8') if isinstance(value, bytes) else old
    return result


def current_inventory(repo):
    paths = [p for p in (repo/'cpp').rglob('*')
             if p.is_file() and p.suffix in ('.cpp', '.hpp', '.ps1', '.json')]
    files = {p.relative_to(repo).as_posix(): p.read_text(encoding='utf-8') for p in paths}
    for path in ADDED | set(REGISTRATIONS):
        p = repo/path
        if p.is_file():
            files[path] = p.read_text(encoding='utf-8')
    verify_current_tree(files)


def read_text(repo, path):
    return legacy_text(path, (repo/path).read_text(encoding='utf-8'))


def legacy_native_paths(repo):
    current_inventory(repo)
    return [repo/p for p in SPEC['native_tests']]


def legacy_product_paths(repo):
    current_inventory(repo)
    return [repo/p for p in SPEC['product_tus']]


def legacy_source_paths(repo):
    current_inventory(repo)
    return [p for p in (repo/'cpp/src').rglob('*')
            if p.is_file() and p.suffix in ('.cpp', '.hpp') and p.relative_to(repo).as_posix() not in ADDED]
