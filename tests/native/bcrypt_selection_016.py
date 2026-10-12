"""Pure 016 two-level CHAININFO selection; supplied snapshots only, no IO."""
import hashlib
import posixpath
from bcrypt_disassembly_model_016 import (
    Refusal, require, decode_json, validate_manifest, _integer, _hex, _identity,
    _target, _call_id, _keys, _path, source_names, BASELINE_COMMIT, EVIDENCE_ROOT,
)

INCOMING_OFFSETS = (0, 29, 37, 45)
_BRANCHES = ((0x5883, '0f88', 0, '0f88b3820000'),
             (0x5897, '0f85', 29, '0f85bc820000'),
             (0x58B4, '0f85', 37, '0f85a7820000'),
             (0x58CC, '0f85', 45, '0f8597820000'))
_RUNTIME_KEYS = ('begin_rva', 'end_rva', 'begin_file_offset', 'end_file_offset',
                 'section_index', 'section_name', 'unwind_info_rva', 'table_entry_file_offset')
_PARENT_RAW = '010a04000a3406000a320670'


def _raw_identity(raw, record, cap):
    require(type(raw) is bytes and 0 < len(raw) <= cap and len(raw) == record['bytes']
            and hashlib.sha256(raw).hexdigest() == record['sha256'], 'INPUT_IDENTITY')


def _matches(record, expected, code):
    require(type(record) is dict, code)
    for key, value in expected.items():
        require(key in record and type(record[key]) is type(value) and record[key] == value, code)


def _unique(records, predicate, code):
    found = None
    for record in records:
        require(type(record) is dict, code)
        if predicate(record):
            require(found is None, code)
            found = record
    require(found is not None, code)
    return found


def _sections(index, image_size):
    sections = index['sections']
    require(type(sections) is list and 1 <= len(sections) <= 96, 'INDEX_SECTIONS')
    for number, section in enumerate(sections):
        require(type(section) is dict, 'INDEX_SECTIONS')
        for key in ('index','raw_file_offset','raw_size','file_backed_size','virtual_address','characteristics'):
            _integer(section[key], 0, 0xFFFFFFFF)
        require(section['index'] == number and type(section['name']) is str
                and 0 <= section['file_backed_size'] <= section['raw_size']
                and section['raw_file_offset'] + section['raw_size'] <= image_size,
                'SECTION_RANGE')
    return sections


def _map(sections, rva, size, image_size, executable=False):
    _integer(rva, 1, 0xFFFFFFFF)
    _integer(size, 1, image_size)
    require(rva + size <= 0x100000000, 'MAPPED_RANGE')
    section = _unique(sections, lambda s: s['virtual_address'] <= rva
                      and rva + size <= s['virtual_address'] + s['file_backed_size'], 'MAPPED_RANGE')
    offset = section['raw_file_offset'] + rva - section['virtual_address']
    require(0 <= offset < offset + size <= image_size, 'MAPPED_RANGE')
    require(not executable or section['name'] == '.text'
            and section['characteristics'] & 0x20000000, 'EXECUTABLE_RANGE')
    return offset, section


def _tuple(record):
    for key in ('begin_rva', 'end_rva', 'unwind_info_rva'):
        _integer(record[key], 1, 0xFFFFFFFF)
    return tuple(record[k] for k in ('begin_rva', 'end_rva', 'unwind_info_rva'))


def _runtime(record, sections, image_size, indexed):
    _keys(record, _RUNTIME_KEYS + (('index',) if indexed else ()))
    begin, end, unwind = _tuple(record)
    for key in ('begin_file_offset','end_file_offset','section_index','table_entry_file_offset'):
        _integer(record[key], 0, 0xFFFFFFFF)
    require(begin < end and unwind % 4 == 0, 'FUNCTION_RANGE')
    offset, section = _map(sections, begin, end - begin, image_size, True)
    require(record['section_name'] == section['name']
            and record['section_index'] == section['index']
            and record['begin_file_offset'] == offset
            and record['end_file_offset'] == offset + end - begin, 'FUNCTION_MAPPING')
    return section


def _unwind(infos, record, sections, image_size, child):
    info = _unique(infos, lambda r: r.get('rva') == record['unwind_info_rva'], 'UNWIND_ASSOCIATION')
    for key in ('rva','file_offset','bytes','version','flags','prolog_size','code_slots',
                'frame_register','frame_offset','chain_depth'):
        _integer(info[key], 0, 0xFFFFFFFF)
    expected = {'version':1, 'flags':4 if child else 0, 'bytes':16 if child else 12,
                'prolog_size':0 if child else 10, 'code_slots':0 if child else 4,
                'frame_register':0,'frame_offset':0,'chain_depth':2 if child else 1,
                'handler_rva':None,'handler_file_offset':None}
    _matches(info, expected, 'UNWIND_ENVELOPE')
    require(info['prolog_size'] <= record['end_rva'] - record['begin_rva'], 'UNWIND_PROLOG')
    _hex(info['raw_hex'], 2 * info['bytes'])
    offset, _ = _map(sections, info['rva'], info['bytes'], image_size)
    require(offset == info['file_offset'], 'UNWIND_MAPPING')
    require(type(info['chain']) is dict if child else info['chain'] is None, 'UNWIND_CHAIN')
    if not child:
        require(info['raw_hex'] == _PARENT_RAW, 'PARENT_UNWIND')
    return info


def _mapping(index, target, image, actual):
    image_size = len(image)
    header = index['header']
    for key in ('machine','optional_header_magic','image_base','address_of_entrypoint'):
        _integer(header[key])
    require(header['machine'] == 34404 and header['optional_header_magic'] == 523
            and header['image_base'] + target['rva_start'] == target['preferred_vma_start'], 'HEADER_MAPPING')
    sections = _sections(index, image_size)
    functions, infos = index['runtime_functions'], index['unwind_info']
    require(type(functions) is list and 1 <= len(functions) <= 16384
            and type(infos) is list and 1 <= len(infos) <= 16384, 'INDEX_LISTS')
    for number, row in enumerate(functions):
        _matches(row, {'index':number}, 'FUNCTION_INDEX')
    child_index = target['runtime_function_index']
    require(child_index < len(functions), 'FUNCTION_INDEX')
    child = functions[child_index]
    _runtime(child, sections, image_size, True)
    _matches(child, {'section_index':target['section_index'],'section_name':'.text',
                    'begin_rva':target['rva_start'],'end_rva':target['rva_end'],
                    'begin_file_offset':target['file_start'],'end_file_offset':target['file_end']}, 'TARGET_MAPPING')
    _unique(functions, lambda r: _tuple(r) == _tuple(child), 'CHILD_ASSOCIATION')
    child_info = _unwind(infos, child, sections, image_size, True)
    chain = child_info['chain']
    _runtime(chain, sections, image_size, False)
    require(chain['table_entry_file_offset'] == child_info['file_offset'] + 4
            and chain['unwind_info_rva'] != child_info['rva'], 'CHAIN_EMBEDDED_LOCATION')
    parent = _unique(functions, lambda r: _tuple(r) == _tuple(chain), 'PARENT_ASSOCIATION')
    require(parent['index'] != child_index, 'CHAIN_CYCLE')
    _runtime(parent, sections, image_size, True)
    for key in _RUNTIME_KEYS:
        if key != 'table_entry_file_offset':
            require(type(chain[key]) is type(parent[key]) and chain[key] == parent[key], 'CHAIN_PARENT_MAPPING')
    parent_info = _unwind(infos, parent, sections, image_size, False)
    require(parent_info['rva'] != child_info['rva'], 'CHAIN_CYCLE')
    parent_tuple = b''.join(v.to_bytes(4, 'little') for v in _tuple(parent))
    require(child_info['raw_hex'] == (b'\x21\0\0\0' + parent_tuple).hex(), 'CHAIN_RAW_TUPLE')
    directories = index['data_directories']
    require(type(directories) is list and 4 <= len(directories) <= 16, 'EXCEPTION_DIRECTORY')
    directory = directories[3]
    _matches(directory, {'index':3, 'bytes':12 * len(functions)}, 'EXCEPTION_DIRECTORY')
    _integer(directory['rva'], 1, 0xFFFFFFFF)
    _integer(directory['file_offset'], 0, image_size)
    require(directory['rva'] % 4 == 0, 'EXCEPTION_DIRECTORY')
    table_offset, _ = _map(sections, directory['rva'], directory['bytes'], image_size)
    require(table_offset == directory['file_offset'], 'EXCEPTION_DIRECTORY')
    segments = []
    for row, info in ((child, child_info), (parent, parent_info)):
        offset = table_offset + row['index'] * 12
        require(row['table_entry_file_offset'] == offset, 'PDATA_LOCATION')
        segments.append((offset, b''.join(v.to_bytes(4, 'little') for v in _tuple(row))))
        segments.append((info['file_offset'], bytes.fromhex(info['raw_hex'])))
    require(parent['table_entry_file_offset'] != chain['table_entry_file_offset'], 'CHAIN_TABLE_LOCATION')
    for number, (offset, expected) in enumerate(segments):
        end = offset + len(expected)
        require(0 <= offset < end <= image_size and image[offset:end] == expected, 'METADATA_SNAPSHOT')
        require(end <= target['file_start'] or offset >= target['file_end'], 'METADATA_TARGET_ALIAS')
        for previous, prior in segments[:number]:
            require(end <= previous or offset >= previous + len(prior), 'METADATA_ALIAS')
    if actual:
        _matches(child, {'index':252,'unwind_info_rva':0x1E61C,
                         'table_entry_file_offset':0x22BD0}, 'ACTUAL_CHILD')
        _matches(child_info, {'rva':0x1E61C,'file_offset':0x1E61C,
                              'raw_hex':'210000004c580000f358000078dd0100'}, 'ACTUAL_CHILD_UNWIND')
        _matches(parent, {'index':105,'begin_rva':0x584C,'end_rva':0x58F3,
                          'begin_file_offset':0x584C,'end_file_offset':0x58F3,'section_index':0,
                          'unwind_info_rva':0x1DD78,'table_entry_file_offset':0x224EC}, 'ACTUAL_PARENT')
        _matches(parent_info, {'rva':0x1DD78,'file_offset':0x1DD78}, 'ACTUAL_PARENT_UNWIND')
        require(chain['table_entry_file_offset'] == 0x1E620, 'ACTUAL_CHAIN_LOCATION')
    return header['image_base'], parent


def _caller(caller, target, image_base, parent, actual):
    _matches(caller, {'schema':'bcrypt-disassembly-report-015-v1','status':'complete_disassembly',
                      'native_execution_performed':False,'outgoing_targets_followed':False}, 'CALLER_STATUS')
    _matches(caller['target'], {'runtime_function_index':parent['index'], 'section_index':parent['section_index'],
                              'file_start':parent['begin_file_offset'],'file_end':parent['end_file_offset'],
                              'rva_start':parent['begin_rva'],'rva_end':parent['end_rva'],
                              'preferred_vma_start':image_base + parent['begin_rva']}, 'CALLER_PARENT')
    _hex(caller['target']['sha256'])
    require(type(caller['instructions']) is list and 4 <= len(caller['instructions']) <= 167, 'CALLER_LIST')
    for source, prefix, offset, actual_hex in _BRANCHES:
        row = _unique(caller['instructions'], lambda r: r.get('rva') == source, 'CALLER_INSTRUCTION')
        _keys(row, ('file_offset','rva','preferred_vma','length','raw_hex','assembly'))
        for key in ('file_offset','rva','preferred_vma','length'):
            _integer(row[key])
        _hex(row['raw_hex'], 12)
        raw = bytes.fromhex(row['raw_hex'])
        require(parent['begin_rva'] <= source < source + 6 <= parent['end_rva']
                and row['file_offset'] == parent['begin_file_offset'] + source - parent['begin_rva']
                and row['preferred_vma'] == image_base + source
                and row['length'] == 6 and raw[:2].hex() == prefix, 'CALLER_ENCODING')
        displacement = int.from_bytes(raw[2:], 'little', signed=True)
        require(source + 6 + displacement == target['rva_start'] + offset
                and row['preferred_vma'] + 6 + displacement == target['preferred_vma_start'] + offset
                and offset < target['rva_end'] - target['rva_start'], 'CALLER_TARGET')
        if actual:
            require(row['raw_hex'] == actual_hex and row['file_offset'] == source, 'ACTUAL_CALLER')
    if actual:
        require(caller['target']['sha256'] == 'c750f7d9829f95406b7608601da74f8cd8e84710dcdcdd0e304a0a5be762b7e1',
                'ACTUAL_CALLER_TARGET')


def select_target(manifest, image_bytes, index_bytes, admission_bytes, caller_bytes):
    """Bind only four declared metadata segments and target within one verified snapshot."""
    m = validate_manifest(manifest)
    for role, raw in (('image', image_bytes), ('index', index_bytes),
                      ('admission', admission_bytes), ('caller', caller_bytes)):
        _raw_identity(raw, m['identities'][role], m['limits'][role + '_bytes'])
    report = decode_json(index_bytes, m['limits']['index_bytes'])
    admission = decode_json(admission_bytes, m['limits']['admission_bytes'])
    caller = decode_json(caller_bytes, m['limits']['caller_bytes'])
    try:
        _matches(report, {'schema':'bcrypt-pe-index-013-report-v1','status':'complete_index',
                          'static_index_admitted':True}, 'INDEX_STATUS')
        core = report['core']
        _matches(core, {'schema':'bcrypt-pe-core-013-v1','status':'indexed',
                        'static_index_admitted':True}, 'INDEX_STATUS')
        index = core['index']
        _matches(index, {'schema':'bcrypt-pe-index-013-v1','runtime_execution_observed':False,
                         'sha256':m['identities']['image']['sha256'],'bytes':len(image_bytes)}, 'INDEX_IMAGE')
        _matches(admission, {'schema':'bcrypt013-root-joint-runtime-admission-v1',
                             'decision':'ADMIT_COMPLETE_VERSIONED_STATIC_INDEX_013',
                             'status':'COMPLETE_INDEX_INDEPENDENTLY_REVIEWED'}, 'INDEX_ADMISSION')
        image_base, parent = _mapping(index, m['target'], image_bytes, m['mode'] == 'actual')
        _caller(caller, m['target'], image_base, parent, m['mode'] == 'actual')
    except (KeyError, TypeError, IndexError, ValueError, OverflowError) as error:
        raise Refusal('INDEX_SHAPE') from error
    target = image_bytes[m['target']['file_start']:m['target']['file_end']]
    require(len(target) == m['target']['rva_end'] - m['target']['rva_start'], 'SLICE_LENGTH')
    if m['phase'] == 'actual':
        require(hashlib.sha256(target).hexdigest() == m['target']['sha256'], 'TARGET_DIGEST')
    return target


def validate_incoming_starts(instructions, target):
    """At most 83 contiguous records; four fixed starts in every address domain."""
    _target(target, 'actual')
    require(type(instructions) is list and 1 <= len(instructions) <= 83, 'INCOMING_START')
    starts, cursor = set(), 0
    for row in instructions:
        _keys(row, ('file_offset','rva','preferred_vma','length','raw_hex','assembly'))
        _integer(row['length'], 1, 15)
        for key in ('file_offset','rva','preferred_vma'):
            _integer(row[key])
        require(row['file_offset'] == target['file_start'] + cursor
                and row['rva'] == target['rva_start'] + cursor
                and row['preferred_vma'] == target['preferred_vma_start'] + cursor,
                'INCOMING_START')
        starts.add(cursor)
        cursor += row['length']
        require(cursor <= target['rva_end'] - target['rva_start'], 'INCOMING_START')
    require(cursor == target['rva_end'] - target['rva_start']
            and all(offset in starts for offset in INCOMING_OFFSETS), 'INCOMING_START')
    return list(INCOMING_OFFSETS)


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
    source_dir = posixpath.dirname(m['sources']['bcrypt_disassembly_model_016.py']['path'])
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
    require(review['primary_outer_source']['path'] == source_dir + '/run_bounded_cli_016.py',
            'SELECTION_OUTER_SOURCE')
    _identity(review['primary_cli_receipt'], m['limits']['selection_bytes'])


def _selection_files(m, review):
    files = review['primary_files']
    caps = {'owner_consumed': 1024, 'owner_target': 83, 'owner_report': 8192,
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
        evidence = EVIDENCE_ROOT + '/evidence-016'
        require(outer_dir == evidence + '/cli-selection-016'
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
        _matches(primary, {'schema': 'bcrypt-selection-report-016-v1', 'status': 'complete_selection',
                           'mode': m['mode'], 'baseline_commit': BASELINE_COMMIT, 'source_commit': None,
                           'consumed': True, 'original_image_full_reads': 1, 'tool_processes': 0,
                           'selection_from_verified_snapshot': True, 'identities_before_verified': True,
                           'identities_after_verified': True, 'image_snapshot_stat_stable': True,
                           'completion_requires_clean_owner_exit': True, 'chain_metadata_snapshot_verified': True,
                           'chain_metadata_bytes': 52, 'incoming_branches_verified': True}, 'PRIMARY_SELECTION')
        _call_id(primary['call_id'], m['mode'], '016-selection-primary-1')
        _hex(primary['manifest_sha256'])
        _matches(review, {'schema': 'bcrypt-selection-review-016-v1', 'decision': 'ADMIT_016_SELECTION_IDENTITY',
                          'mode': m['mode'], 'baseline_commit': BASELINE_COMMIT, 'original_image_full_reads': 1,
                          'selection_from_verified_snapshot': True, 'independent_of_subject_selector': True,
                          'image_snapshot_stat_stable': True, 'primary_owner_exit': 0, 'primary_outer_exit': 0,
                          'primary_owner_reaped': True, 'primary_complete_files_verified': True,
                          'native_execution_performed': False,
                          'completion_requires_clean_verifier_exit': True, 'chain_metadata_snapshot_verified': True,
                          'chain_metadata_bytes': 52, 'incoming_branches_verified': True}, 'INDEPENDENT_SELECTION')
        _call_id(review['call_id'], m['mode'], '016-selection-independent-1')
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
