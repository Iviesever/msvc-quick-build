"""016-only phase permissions; frozen 014 imports supply JSON utilities only."""
import posixpath
import re
from bcrypt_disassembly_model_014 import Refusal, require, encode_json, decode_json

BASELINE_COMMIT = 'ff30a122e92b1b8fef645111ddf277aae065c275'
EVIDENCE_ROOT = '/workspace/scratch/a8b3faf101c5'
SELECTION_OUTPUT_DIR = EVIDENCE_ROOT + '/evidence-016/selection-016'
SELECTION_REVIEW_PATH = EVIDENCE_ROOT + '/evidence-016/selection-independent-016/review.json'
ACTUAL_OUTPUT_DIR = EVIDENCE_ROOT + '/evidence-016/offline-016'
ENV = {'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC', 'DEBUGINFOD_URLS': ''}
ACTUAL_TARGET = {'runtime_function_index': 252, 'section_index': 0,
                 'file_start': 0xDB3C, 'file_end': 0xDB8F, 'rva_start': 0xDB3C,
                 'rva_end': 0xDB8F, 'preferred_vma_start': 0x18000DB3C, 'sha256': None}
U64 = (1 << 64) - 1


def _pin(path, size, digest):
    return {'path': path, 'bytes': size, 'sha256': digest}


FIXED_IDENTITIES = {
    'image': _pin(EVIDENCE_ROOT + '/evidence/offline-011/bcrypt-image.bytes', 170984, '663ac06ba31380fb8a9d096fa8d40df529e0461ccf6d469ab5d858c724dbafbb'),
    'index': _pin(EVIDENCE_ROOT + '/evidence-013/offline-013/report.json', 271920, 'ba4a8611a7e62050451c15b03bc12c8b4b741b5c9bdfabd030a0a486f9c6d65e'),
    'admission': _pin(EVIDENCE_ROOT + '/evidence-013/runtime-review-admission-013.json', 20232, 'c34b264673b6f43144810ea262df7408b6204eea002c6501d9c8f8aac6fdca5b'),
    'caller': _pin(EVIDENCE_ROOT + '/evidence-015/offline-015/report.json', 6740, '5c55e875110e732068f3695d35504f40e7ae7f4e7bf32de4a7bf10b18f3a1e9b'),
    'python': _pin('/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3.12', 30894944, 'fa67443527ed9647f760d807e2a38f26340757123e643c4639cf273ed15d5ea7'),
    'tool': _pin('/usr/bin/x86_64-linux-gnu-objdump', 390848, '7fd25a9f02382a952fe983e40bd41aa12127d1bbe3f4dc5c93ea13d03279a2f5'),
    'loader_cache': _pin('/etc/ld.so.cache', 34015, 'ff9c19ffbceb26e0ba9b3c6ce7cccf292ff8c240bff2a8f425aac61e3955a89d'),
    'dep01': _pin('/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2', 236616, '6222a16be7f2d458d6870efe6e715fc0c8d45766fb79cf7dcc3125538d703e28'),
    'dep02': _pin('/usr/lib/x86_64-linux-gnu/libopcodes-2.42-system.so', 911656, 'dd17972fb5fcca8a796a213b109da1c7b3bc9d573e9e0044af0136d1bb4d0883'),
    'dep03': _pin('/usr/lib/x86_64-linux-gnu/libctf.so.0.0.0', 220384, '1203c2ed3f4bd8cc18a7fd7fcc3dabb0c1bb40b400bb8500034a4373e99ed026'),
    'dep04': _pin('/usr/lib/x86_64-linux-gnu/libbfd-2.42-system.so', 1479888, '43b796c2603b848614cfa55adc198338bce103cddd2aedcc5c863243e90b9d06'),
    'dep05': _pin('/usr/lib/x86_64-linux-gnu/libsframe.so.1.0.0', 35168, '441735b953017c8154989f1d47d7ca7c94683555164950fd2adf5ba5bd37cc64'),
    'dep06': _pin('/usr/lib/x86_64-linux-gnu/libc.so.6', 2125328, '511f825ee075610ac9c0f7f91e2c13de2000d0f7b859f6461137e809a0a009d0'),
    'dep07': _pin('/usr/lib/x86_64-linux-gnu/libz.so.1.3', 113000, '9b64150b28505a33d6bc3ecf709c279f6de97a1c184dbda65d06ee4537f6d286'),
    'dep08': _pin('/usr/lib/x86_64-linux-gnu/libzstd.so.1.5.5', 755864, '0a2128bc10841fb29e76d08d945864dfb0b6a66da5df6df5d8299197439e54bb'),
}
_DOC = '/workspace/scratch/3d56575ba9f2/mqb-work/producer-clean-handoff/docs/20261012-092535-bcrypt-helper-015/'
PROVENANCE = {
    'manifest015': _pin(EVIDENCE_ROOT + '/evidence-015/invocation-manifest-015.json', 8513, '6d8ceb25ab8e3fd550fb01bd6136565b103de51c8554be30db1c60f8c9640ddb'),
    'runtime015': _pin(EVIDENCE_ROOT + '/evidence-015/runtime-review-admission-015.json', 24444, '6cff1daa185450717d2c08aa23a89f6bd50608471eb647b37a64ca9ce106f437'),
    'closure015': _pin(EVIDENCE_ROOT + '/evidence-015/final-closure-admission-015.json', 14819, 'c9b9ec9dc0397b0768f7193f1ae16aa94f0ea14b2c4e249370ffb9bed050e7b5'),
    'result015': _pin(_DOC + 'native_startup_result_015.md', 15931, 'a75e96a1da8da2f973e83e87326a33733ba528dffdfd3509985456035c4a2891'),
    'handoff016': _pin(_DOC + 'native_startup_handoff_016.md', 13792, 'd7bf0bd39a05fd6d8a78a9c8d41731da43443db62cc7943000c22cfb611222d4'),
}
FROZEN_SOURCES = {
    'bcrypt_disassembly_model_014.py': {'bytes': 16716, 'sha256': '3b355306adfdc1ac13c2b193663bc36fb702784779b34891bf4b1a585dc55f12'},
    'bcrypt_disassembly_listing_014.py': {'bytes': 5164, 'sha256': 'f21cd65291e7a079885b6c7dc76b65f309ca311d06f3904cd1b145968b8e8e6a'},
    'bcrypt_disassembly_io_014.py': {'bytes': 10976, 'sha256': '18596e673d8f352a7b49c1955e608e931d69c7dd01d7b9ded47ee402fe90e72e'},
}
_COMMON = {'child_cpu_seconds': 2, 'child_address_space_bytes': 268435456,
           'child_file_bytes': 131072, 'child_core_bytes': 0, 'child_open_files': 64,
           'manifest_bytes': 32768, 'image_bytes': 262144, 'index_bytes': 327680,
           'admission_bytes': 32768, 'caller_bytes': 32768, 'selection_bytes': 8192,
           'selection_review_bytes': 16384, 'identity_bytes': 67108864,
           'identity_total_bytes': 67108864, 'identity_chunk_bytes': 65536,
           'source_bytes': 32768, 'provenance_count': 5, 'provenance_bytes': 32768,
           'provenance_total_bytes': 163840, 'path_chars': 256,
           'owner_stderr_bytes': 65, 'owner_stdout_bytes': 0, 'target_bytes': 83,
           'instructions': 83, 'instruction_bytes': 15, 'raw_line_bytes': 512,
           'assembly_chars': 384, 'marker_bytes': 1024, 'report_wrapper_bytes': 16384}


def limits_for(phase):
    require(type(phase) is str and phase in ('selection', 'actual'), 'MANIFEST_PHASE')
    preparation = phase == 'selection'
    return dict(_COMMON, owner_seconds=8 if preparation else 12,
                outer_seconds=12 if preparation else 20, report_bytes=8192 if preparation else 262144,
                source_count=6 if preparation else 7, identity_roles=5 if preparation else 17,
                alias_count=0 if preparation else 12, absent_path_count=0 if preparation else 8,
                tool_argv_count=0 if preparation else 18)


LIMITS, SELECTION_LIMITS = limits_for('actual'), limits_for('selection')


def source_names(phase):
    limits_for(phase)
    start = ('bcrypt_disassembly_model_014.py',)
    if phase == 'actual':
        start += ('bcrypt_disassembly_listing_014.py',)
    return start + ('bcrypt_disassembly_io_014.py', 'bcrypt_disassembly_model_016.py',
                    'bcrypt_selection_016.py', 'prepare_bcrypt_selection_016.py' if phase == 'selection'
                    else 'bcrypt_disassembly_owner_016.py', 'run_bcrypt_disassembly_016.py')


def tool_argv(path, target):
    _path(path)
    _target(target, 'actual')
    start = target['preferred_vma_start']
    return [path, '-D', '-z', '-b', 'binary', '-m', 'i386:x86-64', '-M', 'intel',
            '--insn-width=16', '--wide', '--show-raw-insn', '--visualize-jumps=off',
            '--disassembler-color=off', f'--adjust-vma={start:#x}', f'--start-address={start:#x}',
            f"--stop-address={start + target['rva_end'] - target['rva_start']:#x}", 'target.bin']


def _keys(value, names):
    require(type(value) is dict and set(value) == set(names), 'FIELDS')


def _integer(value, low=0, high=U64):
    require(type(value) is int and low <= value <= high, 'INTEGER')


def _hex(value, size=64):
    require(type(value) is str and len(value) == size and
            all(c in '0123456789abcdef' for c in value), 'HEX')


def _path(value):
    require(type(value) is str and 1 < len(value) <= 256 and value.startswith('/')
            and not value.startswith('//') and not value.endswith('/')
            and all(32 <= ord(c) < 127 for c in value)
            and posixpath.normpath(value) == value, 'PATH')


def _identity(record, cap):
    _keys(record, ('path', 'bytes', 'sha256'))
    _path(record['path'])
    _integer(record['bytes'], 1, cap)
    _hex(record['sha256'])


def _target(target, phase):
    _keys(target, ACTUAL_TARGET)
    for key in ACTUAL_TARGET:
        if key != 'sha256':
            _integer(target[key])
    length = target['rva_end'] - target['rva_start']
    require(1 <= length <= 83 and target['file_end'] - target['file_start'] == length
            and target['preferred_vma_start'] + length <= U64, 'TARGET_RANGE')
    if phase == 'selection':
        require(target['sha256'] is None, 'SELECTION_DIGEST')
    else:
        _hex(target['sha256'])


def _call_id(value, mode, actual):
    require(type(value) is str and 1 <= len(value) <= 64, 'CALL_ID')
    require(value == actual if mode == 'actual' else
            re.fullmatch(r'016-synthetic-[A-Za-z0-9][A-Za-z0-9_-]*', value), 'CALL_ID')


def validate_manifest(manifest):
    """Exact 016 authority; no call into the frozen 014 manifest/target policy."""
    encode_json(manifest, 32768)
    _keys(manifest, ('schema', 'phase', 'mode', 'call_id', 'baseline_commit', 'source_commit',
                     'output_dir', 'identities', 'provenance', 'sources', 'aliases',
                     'absent_paths', 'target', 'tool_argv', 'environment', 'limits'))
    m, phase = manifest, manifest['phase']
    limits = limits_for(phase)
    require(m['schema'] == 'bcrypt-disassembly-manifest-016-v1'
            and type(m['mode']) is str and m['mode'] in ('actual', 'synthetic'), 'MANIFEST_MODE')
    require(m['baseline_commit'] == BASELINE_COMMIT, 'BASELINE')
    _call_id(m['call_id'], m['mode'], '016-selection-primary-1' if phase == 'selection'
             else '016-chain-static-1')
    if phase == 'selection':
        require(m['source_commit'] is None, 'SOURCE_COMMIT')
    else:
        _hex(m['source_commit'], 40)
    _path(m['output_dir'])
    _keys(m['limits'], limits)
    require(all(type(v) is int for v in m['limits'].values()) and m['limits'] == limits, 'LIMITS')
    _keys(m['environment'], ENV)
    require(m['environment'] == ENV, 'ENVIRONMENT')
    roles = ('image', 'index', 'admission', 'caller', 'python')
    if phase == 'actual':
        roles += ('tool', 'loader_cache', *(f'dep{i:02d}' for i in range(1, 9)),
                  'selection', 'selection_review')
    _keys(m['identities'], roles)
    _keys(m['sources'], source_names(phase))
    _keys(m['provenance'], PROVENANCE)
    records, parents = [], set()
    for role, record in m['identities'].items():
        _identity(record, limits.get(role + '_bytes', limits['identity_bytes']))
        records.append(record)
    for name, record in m['sources'].items():
        _identity(record, limits['source_bytes'])
        require(posixpath.basename(record['path']) == name, 'SOURCE_NAME')
        parents.add(posixpath.dirname(record['path']))
        if name in FROZEN_SOURCES:
            require(all(record[k] == v for k, v in FROZEN_SOURCES[name].items()), 'FROZEN_SOURCE')
        records.append(record)
    for record in m['provenance'].values():
        _identity(record, limits['provenance_bytes'])
        records.append(record)
    require(sum(r['bytes'] for r in m['provenance'].values()) <= limits['provenance_total_bytes'],
            'PROVENANCE_BUDGET')
    paths = {r['path'] for r in records}
    require(len(parents) == 1 and len(paths) == len(records), 'IDENTITY_PATHS')
    require(sum(r['bytes'] for r in records) <= limits['identity_total_bytes']
            and m['output_dir'] not in paths
            and not any(p.startswith(m['output_dir'] + '/') for p in paths), 'IDENTITY_BUDGET')
    require(type(m['aliases']) is list and len(m['aliases']) <= limits['alias_count'], 'ALIASES')
    occupied = paths | {m['output_dir']}
    for alias in m['aliases']:
        _keys(alias, ('path', 'target'))
        _path(alias['path'])
        _path(alias['target'])
        require(alias['target'] in paths and alias['path'] not in occupied, 'ALIAS_TARGET')
        occupied.add(alias['path'])
    require(type(m['absent_paths']) is list and len(m['absent_paths']) <= limits['absent_path_count'],
            'ABSENT_PATHS')
    for path in m['absent_paths']:
        _path(path)
        require(path not in occupied, 'ABSENT_PATH')
        occupied.add(path)
    _target(m['target'], phase)
    expected_argv = [] if phase == 'selection' else tool_argv(m['identities']['tool']['path'], m['target'])
    require(type(m['tool_argv']) is list and m['tool_argv'] == expected_argv, 'TOOL_ARGV')
    if m['mode'] == 'actual':
        for role, expected in FIXED_IDENTITIES.items():
            if role in roles:
                require(m['identities'][role] == expected, 'ACTUAL_IDENTITY')
        require(m['provenance'] == PROVENANCE, 'ACTUAL_PROVENANCE')
        require(m['target'] == dict(ACTUAL_TARGET, sha256=m['target']['sha256']), 'ACTUAL_TARGET')
        require(m['output_dir'] == (SELECTION_OUTPUT_DIR if phase == 'selection' else ACTUAL_OUTPUT_DIR),
                'ACTUAL_OUTPUT')
        if phase == 'actual':
            require(m['identities']['selection']['path'] == SELECTION_OUTPUT_DIR + '/report.json'
                    and m['identities']['selection_review']['path'] == SELECTION_REVIEW_PATH,
                    'ACTUAL_SELECTION_PATHS')
    else:
        forbidden = {FIXED_IDENTITIES[r]['sha256'] for r in ('image', 'index', 'caller')}
        require(all(r['sha256'] not in forbidden for r in records)
                and m['target']['sha256'] not in forbidden, 'SYNTHETIC_ACTUAL_IDENTITY')
    return m
