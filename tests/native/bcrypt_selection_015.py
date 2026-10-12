"""Pure 015 selection and evidence association; no files or processes."""
import hashlib
import posixpath
from bcrypt_disassembly_model_015 import (
    Refusal, require, decode_json, validate_manifest, _integer, _hex, _identity,
    _target, _call_id, _keys, _path, source_names, BASELINE_COMMIT, EVIDENCE_ROOT,
)


def _raw_identity(raw, record, cap):
    require(type(raw) is bytes and 0 < len(raw) <= cap and len(raw) == record['bytes']
            and hashlib.sha256(raw).hexdigest() == record['sha256'], 'INPUT_IDENTITY')


def _matches(record, expected, code):
    require(type(record) is dict, code)
    for key, value in expected.items():
        require(key in record and type(record[key]) is type(value) and record[key] == value, code)


def _mapping(index, target, image_size, actual):
    header = index['header']
    for key in ('machine', 'optional_header_magic', 'image_base', 'address_of_entrypoint'):
        _integer(header[key])
    require(header['machine'] == 34404 and header['optional_header_magic'] == 523
            and header['image_base'] + target['rva_start'] == target['preferred_vma_start'], 'HEADER_MAPPING')
    require(type(index['sections']) is list and type(index['runtime_functions']) is list, 'INDEX_LISTS')
    section = index['sections'][target['section_index']]
    function = index['runtime_functions'][target['runtime_function_index']]
    for key in ('index', 'raw_file_offset', 'raw_size', 'file_backed_size', 'virtual_address', 'characteristics'):
        _integer(section[key])
    require(section['index'] == target['section_index'] and section['name'] == '.text'
            and section['characteristics'] & 0x20000000
            and 0 < section['file_backed_size'] <= section['raw_size']
            and section['raw_file_offset'] + section['raw_size'] <= image_size
            and section['raw_file_offset'] <= target['file_start'] < target['file_end']
            <= section['raw_file_offset'] + section['file_backed_size'], 'SECTION_RANGE')
    for rva, offset in ((target['rva_start'], target['file_start']), (target['rva_end'], target['file_end'])):
        require(rva >= section['virtual_address']
                and section['raw_file_offset'] + rva - section['virtual_address'] == offset, 'SECTION_MAPPING')
    _matches(function, {'index': target['runtime_function_index'], 'section_index': target['section_index'],
                       'section_name': '.text', 'begin_rva': target['rva_start'], 'end_rva': target['rva_end'],
                       'begin_file_offset': target['file_start'], 'end_file_offset': target['file_end']}, 'FUNCTION_MAPPING')
    _integer(function['unwind_info_rva'], 1)
    _integer(function['table_entry_file_offset'])
    require(type(index['unwind_info']) is list, 'UNWIND_LIST')
    infos = [r for r in index['unwind_info'] if type(r) is dict and r.get('rva') == function['unwind_info_rva']]
    require(len(infos) == 1, 'UNWIND_ASSOCIATION')
    info = infos[0]
    for key in ('rva', 'file_offset', 'bytes', 'version', 'flags', 'prolog_size',
                'code_slots', 'frame_register', 'frame_offset', 'chain_depth'):
        _integer(info[key])
    require(info['version'] == 1 and info['flags'] == 0
            and info['prolog_size'] <= target['rva_end'] - target['rva_start']
            and info['code_slots'] <= 255 and info['bytes'] == 4 + ((info['code_slots'] + 1) & ~1) * 2
            and info['handler_rva'] is None and info['handler_file_offset'] is None
            and info['chain'] is None and info['chain_depth'] == 1, 'UNWIND_ENVELOPE')
    _hex(info['raw_hex'], 2 * info['bytes'])
    if actual:
        _matches(function, {'unwind_info_rva': 0x1DD78, 'table_entry_file_offset': 0x224EC}, 'ACTUAL_FUNCTION')
        _matches(info, {'rva': 0x1DD78, 'file_offset': 0x1DD78, 'bytes': 12, 'version': 1, 'flags': 0,
                        'prolog_size': 10, 'code_slots': 4, 'raw_hex': '010a04000a3406000a320670'}, 'ACTUAL_UNWIND')
    return header['image_base']


def _caller(caller, target, image_base, actual):
    _matches(caller, {'schema': 'bcrypt-disassembly-report-014-v1', 'status': 'complete_disassembly',
                      'native_execution_performed': False, 'outgoing_targets_followed': False}, 'CALLER_STATUS')
    require(type(caller['instructions']) is list, 'CALLER_LIST')
    calls = [r for r in caller['instructions'] if type(r) is dict and r.get('rva') == 0xA612]
    require(len(calls) == 1, 'CALLER_INSTRUCTION')
    call = calls[0]
    require(set(call) == {'file_offset', 'rva', 'preferred_vma', 'length', 'raw_hex', 'assembly'}, 'CALLER_FIELDS')
    for key in ('file_offset', 'rva', 'preferred_vma', 'length'):
        _integer(call[key])
    _hex(call['raw_hex'], 10)
    raw = bytes.fromhex(call['raw_hex'])
    require(call['length'] == 5 and raw[0] == 0xE8
            and call['preferred_vma'] == image_base + call['rva'], 'CALLER_ENCODING')
    displacement = int.from_bytes(raw[1:], 'little', signed=True)
    require(call['rva'] + 5 + displacement == target['rva_start']
            and call['preferred_vma'] + 5 + displacement == target['preferred_vma_start'], 'CALLER_TARGET')
    if actual:
        require(call['raw_hex'] == 'e835b2ffff' and call['file_offset'] == 0xA612, 'ACTUAL_CALLER')


def select_target(manifest, image_bytes, index_bytes, admission_bytes, caller_bytes):
    """Select only after all supplied snapshots match; never parse original PE bytes."""
    m = validate_manifest(manifest)
    for role, raw in (('image', image_bytes), ('index', index_bytes),
                      ('admission', admission_bytes), ('caller', caller_bytes)):
        _raw_identity(raw, m['identities'][role], m['limits'][role + '_bytes'])
    report = decode_json(index_bytes, m['limits']['index_bytes'])
    admission = decode_json(admission_bytes, m['limits']['admission_bytes'])
    caller = decode_json(caller_bytes, m['limits']['caller_bytes'])
    try:
        _matches(report, {'schema': 'bcrypt-pe-index-013-report-v1', 'status': 'complete_index',
                          'static_index_admitted': True}, 'INDEX_STATUS')
        core = report['core']
        _matches(core, {'schema': 'bcrypt-pe-core-013-v1', 'status': 'indexed',
                        'static_index_admitted': True}, 'INDEX_STATUS')
        index = core['index']
        _matches(index, {'schema': 'bcrypt-pe-index-013-v1', 'runtime_execution_observed': False,
                         'sha256': m['identities']['image']['sha256'], 'bytes': len(image_bytes)}, 'INDEX_IMAGE')
        _matches(admission, {'schema': 'bcrypt013-root-joint-runtime-admission-v1',
                             'decision': 'ADMIT_COMPLETE_VERSIONED_STATIC_INDEX_013',
                             'status': 'COMPLETE_INDEX_INDEPENDENTLY_REVIEWED'}, 'INDEX_ADMISSION')
        # The old entrypoint and concrete_next_static_target are not helper permissions.
        image_base = _mapping(index, m['target'], len(image_bytes), m['mode'] == 'actual')
        _caller(caller, m['target'], image_base, m['mode'] == 'actual')
    except (KeyError, TypeError, IndexError) as error:
        raise Refusal('INDEX_SHAPE') from error
    target = image_bytes[m['target']['file_start']:m['target']['file_end']]
    require(len(target) == m['target']['rva_end'] - m['target']['rva_start'], 'SLICE_LENGTH')
    if m['phase'] == 'actual':
        require(hashlib.sha256(target).hexdigest() == m['target']['sha256'], 'TARGET_DIGEST')
    return target


def _artifact_ref(record, cap, allow_empty=False):
    _keys(record, ('path', 'bytes', 'sha256'))
    _path(record['path'])
    _integer(record['bytes'], 0 if allow_empty else 1, cap)
    _hex(record['sha256'])
    if record['bytes'] == 0:
        require(record['sha256'] == hashlib.sha256(b'').hexdigest(), 'EMPTY_ARTIFACT')


def _selection_lineage(m, primary, review):
    _identity(review['primary_manifest_ref'], m['limits']['manifest_bytes'])
    _hex(review['primary_manifest_sha256'])
    require(primary['manifest_sha256'] == review['primary_manifest_ref']['sha256']
            == review['primary_manifest_sha256'], 'SELECTION_MANIFEST_JOIN')
    sources = review['primary_sources']
    _keys(sources, source_names('selection'))
    source_dir = posixpath.dirname(m['sources']['bcrypt_disassembly_model_015.py']['path'])
    for name, record in sources.items():
        _identity(record, m['limits']['source_bytes'])
        require(record['path'] == source_dir + '/' + name, 'SELECTION_SOURCE_PATH')
        if name in m['sources']:
            require(record == m['sources'][name], 'SELECTION_SOURCE_JOIN')
    _keys(review['primary_provenance'], m['provenance'])
    for record in review['primary_provenance'].values():
        _identity(record, m['limits']['provenance_bytes'])
    require(review['primary_provenance'] == m['provenance'], 'SELECTION_PROVENANCE_JOIN')
    _identity(review['primary_outer_source'], m['limits']['source_bytes'])
    require(review['primary_outer_source']['path'] == source_dir + '/run_bounded_cli_015.py',
            'SELECTION_OUTER_SOURCE')
    _identity(review['primary_cli_receipt'], m['limits']['selection_bytes'])


def _selection_files(m, review):
    files = review['primary_files']
    caps = {'owner_consumed': 1024, 'owner_target': 167, 'owner_report': 8192,
            'outer_consumed': 1024, 'outer_stdout': 1, 'outer_stderr': 66, 'outer_report': 8192}
    _keys(files, caps)
    owner_dir = posixpath.dirname(m['identities']['selection']['path'])
    for role, cap in caps.items():
        _artifact_ref(files[role], cap, role in ('outer_stdout', 'outer_stderr'))
    outer_dir = posixpath.dirname(files['outer_consumed']['path'])
    names = {'owner_consumed': 'consumed.json', 'owner_target': 'target.bin',
             'outer_consumed': 'consumed.json', 'outer_stdout': 'tool-stdout.bin',
             'outer_stderr': 'tool-stderr.bin', 'outer_report': 'report.json'}
    for role, name in names.items():
        parent = owner_dir if role.startswith('owner_') else outer_dir
        require(files[role]['path'] == parent + '/' + name, 'SELECTION_FILE_PATH')
    require(files['owner_report'] == m['identities']['selection'], 'SELECTION_FILE_JOIN')
    require(owner_dir != outer_dir and review['primary_cli_receipt']['path']
            not in {record['path'] for record in files.values()}, 'SELECTION_TRANSPORT_PATH')
    require(files['owner_target']['bytes'] == m['target']['rva_end'] - m['target']['rva_start']
            and files['owner_target']['sha256'] == m['target']['sha256'], 'SELECTION_TARGET_FILE')
    require(files['outer_stdout']['bytes'] == files['outer_stderr']['bytes'] == 0,
            'SELECTION_OWNER_STREAMS')
    cli_raw = review['primary_cli_raw']
    _keys(cli_raw, ('stdout', 'stderr'))
    _artifact_ref(cli_raw['stdout'], 1024)
    _artifact_ref(cli_raw['stderr'], 65, True)
    require(cli_raw['stderr']['bytes'] == 0, 'SELECTION_OUTER_STDERR')
    if m['mode'] == 'actual':
        evidence = EVIDENCE_ROOT + '/evidence-015'
        require(outer_dir == evidence + '/cli-selection-015'
                and review['primary_cli_receipt']['path'] == evidence + '/selection-primary-cli-receipt.json'
                and all(cli_raw[r]['path'] == evidence + '/selection-primary-cli-' + r + '.bin'
                        for r in ('stdout', 'stderr')), 'ACTUAL_SELECTION_FILES')


def validate_selection_evidence(manifest, selection_raw, review_raw):
    """Join a primary result to an independently pinned clean-completion receipt."""
    m = validate_manifest(manifest)
    require(m['phase'] == 'actual', 'SELECTION_EVIDENCE_PHASE')
    for role, raw in (('selection', selection_raw), ('selection_review', review_raw)):
        _raw_identity(raw, m['identities'][role], m['limits'][role + '_bytes'])
    primary = decode_json(selection_raw, m['limits']['selection_bytes'])
    review = decode_json(review_raw, m['limits']['selection_review_bytes'])
    try:
        _matches(primary, {'schema': 'bcrypt-selection-report-015-v1', 'status': 'complete_selection',
                           'mode': m['mode'], 'baseline_commit': BASELINE_COMMIT, 'source_commit': None,
                           'consumed': True, 'original_image_full_reads': 1, 'tool_processes': 0,
                           'selection_from_verified_snapshot': True, 'identities_before_verified': True,
                           'identities_after_verified': True, 'image_snapshot_stat_stable': True,
                           'completion_requires_clean_owner_exit': True}, 'PRIMARY_SELECTION')
        _call_id(primary['call_id'], m['mode'], '015-selection-primary-1')
        _hex(primary['manifest_sha256'])
        _matches(review, {'schema': 'bcrypt-selection-review-015-v1', 'decision': 'ADMIT_015_SELECTION_IDENTITY',
                          'mode': m['mode'], 'baseline_commit': BASELINE_COMMIT, 'original_image_full_reads': 1,
                          'selection_from_verified_snapshot': True, 'independent_of_subject_selector': True,
                          'image_snapshot_stat_stable': True, 'primary_owner_exit': 0, 'primary_outer_exit': 0,
                          'primary_owner_reaped': True, 'primary_complete_files_verified': True,
                          'native_execution_performed': False,
                          'completion_requires_clean_verifier_exit': True}, 'INDEPENDENT_SELECTION')
        _call_id(review['call_id'], m['mode'], '015-selection-independent-1')
        require(review['call_id'] != primary['call_id'], 'INDEPENDENT_CALL_ID')
        _identity(review['primary_report'], m['limits']['selection_bytes'])
        require(review['primary_report'] == m['identities']['selection'], 'PRIMARY_REPORT_IDENTITY')
        findings = review['open_findings']
        require(type(findings) is dict and set(findings) == {'C', 'I', 'M'}
                and all(type(v) is int and v == 0 for v in findings.values()), 'SELECTION_FINDINGS')
        expected = {r: m['identities'][r] for r in ('image', 'index', 'admission', 'caller')}
        for record in (primary, review):
            _target(record['target'], 'actual')
            require(record['target'] == m['target'], 'SELECTION_TARGET')
            inputs = record['input_identities']
            require(type(inputs) is dict and set(inputs) == set(expected), 'SELECTION_INPUTS')
            for role, identity in inputs.items():
                _identity(identity, m['limits'][role + '_bytes'])
            require(inputs == expected, 'SELECTION_INPUTS')
        _selection_lineage(m, primary, review)
        _selection_files(m, review)
    except (KeyError, TypeError, IndexError) as error:
        raise Refusal('SELECTION_EVIDENCE_SHAPE') from error
    return primary
