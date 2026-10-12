"""Constructed CHAININFO snapshots and four-entry checks; no original file IO."""
import copy
import hashlib
import posixpath
import struct
import unittest
from unittest import mock
from test_bcrypt_disassembly_model_016 import identity, make_manifest, model
import bcrypt_selection_016 as selection


def fixture(phase='selection', size=83):
    m = make_manifest(phase)
    t = m['target']
    t.update(runtime_function_index=1, section_index=0, file_start=0x1100,
             file_end=0x1100+size, rva_start=0x6000, rva_end=0x6000+size,
             preferred_vma_start=0x90006000)
    target = b'\x90' * size
    image = bytearray(b'X' * 0x3400)
    image[t['file_start']:t['file_end']] = target
    if phase == 'actual':
        t['sha256'] = hashlib.sha256(target).hexdigest()
        m['tool_argv'] = model.tool_argv(m['identities']['tool']['path'], t)
    parent = {'index': 0, 'section_index': 0, 'section_name': '.text',
              'begin_rva': 0x584C, 'end_rva': 0x58F3, 'begin_file_offset': 0x94C,
              'end_file_offset': 0x9F3, 'unwind_info_rva': 0x8000,
              'table_entry_file_offset': 0x3300}
    child = {'index': 1, 'section_index': 0, 'section_name': '.text',
             'begin_rva': t['rva_start'], 'end_rva': t['rva_end'],
             'begin_file_offset': t['file_start'], 'end_file_offset': t['file_end'],
             'unwind_info_rva': 0x8020, 'table_entry_file_offset': 0x330C}
    chain = {k: v for k, v in parent.items() if k != 'index'}
    chain['table_entry_file_offset'] = 0x3124
    parent_raw = bytes.fromhex('010a04000a3406000a320670')
    child_raw = b'\x21\0\0\0' + struct.pack('<III', parent['begin_rva'], parent['end_rva'], parent['unwind_info_rva'])
    image[0x3100:0x310C] = parent_raw
    image[0x3120:0x3130] = child_raw
    for row in (parent, child):
        offset = row['table_entry_file_offset']
        image[offset:offset+12] = struct.pack('<III', row['begin_rva'], row['end_rva'], row['unwind_info_rva'])
    def section(i, name, offset, rva, size, flags):
        return {'index': i, 'name': name, 'raw_file_offset': offset, 'raw_size': size,
                'file_backed_size': size, 'virtual_address': rva, 'characteristics': flags}
    index = {'schema': 'bcrypt-pe-index-013-v1', 'runtime_execution_observed': False,
             'sha256': hashlib.sha256(image).hexdigest(), 'bytes': len(image),
             'header': {'machine': 34404, 'optional_header_magic': 523,
                        'address_of_entrypoint': 0x5000, 'image_base': 0x90000000},
             'entrypoint': {'rva': 0x5000, 'runtime_function_index': 99},
             'sections': [section(0, '.text', 0x100, 0x5000, 0x3000, 0x60000020),
                          section(1, '.xdata', 0x3100, 0x8000, 0x200, 0x40000040),
                          section(2, '.pdata', 0x3300, 0x9000, 24, 0x40000040)],
             'data_directories': [{'index': 0}, {'index': 1}, {'index': 2},
                                 {'index': 3, 'rva': 0x9000, 'file_offset': 0x3300, 'bytes': 24}],
             'runtime_functions': [parent, child],
             'unwind_info': [
                 {'rva': 0x8000, 'file_offset': 0x3100, 'bytes': 12, 'version': 1,
                  'flags': 0, 'prolog_size': 10, 'code_slots': 4, 'frame_register': 0,
                  'frame_offset': 0, 'raw_hex': parent_raw.hex(), 'handler_rva': None,
                  'handler_file_offset': None, 'chain': None, 'chain_depth': 1},
                 {'rva': 0x8020, 'file_offset': 0x3120, 'bytes': 16, 'version': 1,
                  'flags': 4, 'prolog_size': 0, 'code_slots': 0, 'frame_register': 0,
                  'frame_offset': 0, 'raw_hex': child_raw.hex(), 'handler_rva': None,
                  'handler_file_offset': None, 'chain': chain, 'chain_depth': 2}]}
    report = {'schema': 'bcrypt-pe-index-013-report-v1', 'status': 'complete_index',
              'static_index_admitted': True, 'core': {'schema': 'bcrypt-pe-core-013-v1',
              'status': 'indexed', 'static_index_admitted': True, 'index': index}}
    admission = {'schema': 'bcrypt013-root-joint-runtime-admission-v1',
                 'decision': 'ADMIT_COMPLETE_VERSIONED_STATIC_INDEX_013',
                 'status': 'COMPLETE_INDEX_INDEPENDENTLY_REVIEWED',
                 'concrete_next_static_target': {'runtime_function_index': 169, 'function_bytes': 391}}
    instructions = []
    for source, prefix, offset in ((0x5883, '0f88', 0), (0x5897, '0f85', 29),
                                    (0x58B4, '0f85', 37), (0x58CC, '0f85', 45)):
        raw = bytes.fromhex(prefix) + (t['rva_start']+offset-source-6).to_bytes(4, 'little', signed=True)
        instructions.append({'file_offset': 0x100+source-0x5000, 'rva': source,
                             'preferred_vma': 0x90000000+source, 'length': 6,
                             'raw_hex': raw.hex(), 'assembly': 'jne constructed'})
    caller = {'schema': 'bcrypt-disassembly-report-015-v1', 'status': 'complete_disassembly',
              'native_execution_performed': False, 'outgoing_targets_followed': False,
              'target': {'runtime_function_index': 0, 'section_index': 0,
                         'file_start': parent['begin_file_offset'], 'file_end': parent['end_file_offset'],
                         'rva_start': parent['begin_rva'], 'rva_end': parent['end_rva'],
                         'preferred_vma_start': 0x9000584C, 'sha256': hashlib.sha256(b'P'*167).hexdigest()},
              'instructions': instructions}
    return m, [bytes(image), report, admission, caller], target


def encode_inputs(m, values):
    raws = [values[0]] + [model.encode_json(v, 327680 if i == 1 else 32768)
                          for i, v in enumerate(values[1:], 1)]
    for role, raw in zip(('image', 'index', 'admission', 'caller'), raws):
        m['identities'][role] = identity(m['identities'][role]['path'], raw)
    return raws


def refresh_image(values, image):
    values[0] = bytes(image)
    values[1]['core']['index']['sha256'] = hashlib.sha256(image).hexdigest()
    values[1]['core']['index']['bytes'] = len(image)


def receipts(m):
    inputs = {r: copy.deepcopy(m['identities'][r]) for r in ('image', 'index', 'admission', 'caller')}
    primary = {'schema': 'bcrypt-selection-report-016-v1', 'status': 'complete_selection',
               'call_id': '016-synthetic-primary', 'mode': m['mode'], 'baseline_commit': m['baseline_commit'],
               'source_commit': None, 'manifest_sha256': '2' * 64, 'target': copy.deepcopy(m['target']),
               'input_identities': inputs, 'consumed': True, 'original_image_full_reads': 1,
               'selection_from_verified_snapshot': True, 'tool_processes': 0,
               'identities_before_verified': True, 'identities_after_verified': True,
               'image_snapshot_stat_stable': True, 'completion_requires_clean_owner_exit': True, 'chain_metadata_snapshot_verified': True,
               'chain_metadata_bytes': 52, 'incoming_branches_verified': True}
    raw = model.encode_json(primary, 8192)
    m['identities']['selection'] = identity(m['identities']['selection']['path'], raw)
    review = {'schema': 'bcrypt-selection-review-016-v1', 'decision': 'ADMIT_016_SELECTION_IDENTITY',
              'call_id': '016-synthetic-independent', 'mode': m['mode'], 'baseline_commit': m['baseline_commit'],
              'primary_report': copy.deepcopy(m['identities']['selection']), 'target': copy.deepcopy(m['target']),
              'input_identities': inputs, 'original_image_full_reads': 1,
              'selection_from_verified_snapshot': True, 'independent_of_subject_selector': True,
              'image_snapshot_stat_stable': True, 'primary_owner_exit': 0, 'primary_outer_exit': 0,
              'primary_owner_reaped': True, 'primary_complete_files_verified': True,
              'open_findings': {'C': 0, 'I': 0, 'M': 0}, 'native_execution_performed': False, 'chain_metadata_snapshot_verified': True,
              'chain_metadata_bytes': 52, 'incoming_branches_verified': True}
    source_dir = posixpath.dirname(m['sources']['bcrypt_disassembly_model_016.py']['path'])
    review['primary_manifest_ref'] = {'path': '/synthetic/selection-manifest-016.json',
                                      'bytes': 1000, 'sha256': primary['manifest_sha256']}
    review['primary_manifest_sha256'] = primary['manifest_sha256']
    review['primary_sources'] = {n: copy.deepcopy(m['sources'][n]) if n in m['sources']
                                 else identity(source_dir + '/' + n, b'constructed preparation source')
                                 for n in model.source_names('selection')}
    review['primary_provenance'] = copy.deepcopy(m['provenance'])
    review['primary_outer_source'] = identity(source_dir + '/run_bounded_cli_016.py', b'constructed outer source')
    review['primary_cli_receipt'] = identity('/synthetic/primary-cli-receipt.json', b'constructed transport receipt')
    owner_dir = posixpath.dirname(m['identities']['selection']['path'])
    outer_dir = '/synthetic/cli-selection'
    target_ref = {'path': owner_dir + '/target.bin',
                  'bytes': m['target']['rva_end'] - m['target']['rva_start'], 'sha256': m['target']['sha256']}
    review['primary_files'] = {
        'owner_consumed': identity(owner_dir + '/consumed.json', b'constructed marker'),
        'owner_target': target_ref, 'owner_report': copy.deepcopy(m['identities']['selection']),
        'outer_consumed': identity(outer_dir + '/consumed.json', b'constructed outer marker'),
        'outer_stdout': identity(outer_dir + '/tool-stdout.bin', b''),
        'outer_stderr': identity(outer_dir + '/tool-stderr.bin', b''),
        'outer_report': identity(outer_dir + '/report.json', b'constructed outer report'),
    }
    review['primary_cli_raw'] = {'stdout': identity('/synthetic/primary-cli-stdout.bin', b'constructed summary\n'),
                                 'stderr': identity('/synthetic/primary-cli-stderr.bin', b'')}
    review['completion_requires_clean_verifier_exit'] = True
    return primary, review


def encode_receipts(m, primary, review):
    one = model.encode_json(primary, 8192)
    two = model.encode_json(review, 16384)
    for role, raw in (('selection', one), ('selection_review', two)):
        m['identities'][role] = identity(m['identities'][role]['path'], raw)
    return one, two


class SelectionTests(unittest.TestCase):
    def select(self, m, values):
        return selection.select_target(m, *encode_inputs(m, values))

    def test_legal_chain_has_distinct_table_offsets_matching_logical_tuple(self):
        m, values, target = fixture()
        frozen = copy.deepcopy(m)
        self.assertEqual(self.select(m, values), target)
        self.assertIsNone(m['target']['sha256'])
        self.assertEqual(m['target'], frozen['target'])
        index = values[1]['core']['index']
        parent, chain = index['runtime_functions'][0], index['unwind_info'][1]['chain']
        self.assertNotEqual(parent['table_entry_file_offset'], chain['table_entry_file_offset'])
        self.assertNotEqual(parent, chain)
        self.assertEqual(tuple(parent[k] for k in ('begin_rva','end_rva','unwind_info_rva')),
                         tuple(chain[k] for k in ('begin_rva','end_rva','unwind_info_rva')))
        self.assertEqual(values[2]['concrete_next_static_target']['runtime_function_index'], 169)

    def test_exact_83_end_and_separated_address_domains(self):
        m, values, target = fixture('actual')
        before = copy.deepcopy(values)
        self.assertEqual(self.select(m, values), target)
        self.assertEqual(values, before)
        self.assertEqual(len(target), 83)
        self.assertNotEqual(m['target']['file_start'],m['target']['rva_start'])
        self.assertNotEqual(m['target']['rva_start'],m['target']['preferred_vma_start'])
        m['target']['sha256'] = '0'*64
        with self.assertRaises(model.Refusal): self.select(m, values)

    def test_identity_mismatch_precedes_every_decode(self):
        m, values, _ = fixture()
        raws = encode_inputs(m, values)
        raws[0] += b'changed'
        with mock.patch.object(selection, 'decode_json', side_effect=AssertionError('early decode')):
            with self.assertRaises(model.Refusal): selection.select_target(m, *raws)

    def test_each_of_four_metadata_segments_is_bound_to_same_snapshot(self):
        for offset in (0x330C,0x3120,0x3300,0x3100):
            m, values, _ = fixture()
            image = bytearray(values[0]); image[offset] ^= 1
            refresh_image(values, image)
            with self.subTest(offset=hex(offset)), self.assertRaises(model.Refusal): self.select(m,values)

    def test_wrong_chain_fields_handlers_frames_and_depth_refuse(self):
        changes = [('child','flags',0),('child','flags',5),('child','version',2),
                   ('child','chain_depth',3),('child','code_slots',1),('child','prolog_size',1),
                   ('child','frame_register',1),('child','frame_offset',1),('child','handler_rva',123),
                   ('child','handler_file_offset',123),('child','bytes',15),('child','chain',None),
                   ('parent','flags',4),('parent','chain_depth',2),('parent','chain',{}),
                   ('parent','handler_rva',123),('parent','code_slots',2),('parent','prolog_size',11),
                   ('parent','frame_register',1),('parent','raw_hex','010a04000a3406000a3206'),
                   ('chain','unwind_info_rva',0x8020),('chain','begin_rva',0x584D),
                   ('chain','end_file_offset',0x9F2),('chain','section_index',1),
                   ('chain','table_entry_file_offset',0x3300)]
        for group,key,value in changes:
            m, values, _ = fixture(); index=values[1]['core']['index']
            objects={'parent':index['unwind_info'][0],'child':index['unwind_info'][1],
                     'chain':index['unwind_info'][1]['chain']}
            objects[group][key]=value
            with self.subTest(group=group,key=key),self.assertRaises(model.Refusal):self.select(m,values)

    def test_missing_duplicate_or_wrong_parent_records_refuse(self):
        for case in range(8):
            m,values,_=fixture(); index=values[1]['core']['index']
            if case==0:index['runtime_functions'].pop(0)
            elif case==1:index['runtime_functions'].append(copy.deepcopy(index['runtime_functions'][1]))
            elif case==2:index['unwind_info'].append(copy.deepcopy(index['unwind_info'][1]))
            elif case==3:index['unwind_info'].append(copy.deepcopy(index['unwind_info'][0]))
            elif case==4:index['runtime_functions'][0]['unwind_info_rva']=0x8040
            elif case==5:index['runtime_functions'][0]['index']=1
            elif case==6:index['unwind_info'].pop(0)
            else:index['sections'].append(copy.deepcopy(index['sections'][0]))
            with self.subTest(case=case),self.assertRaises(model.Refusal):self.select(m,values)

    def test_unmapped_truncated_metadata_and_wrong_table_offset_refuse(self):
        for case in range(8):
            m,values,_=fixture(); index=values[1]['core']['index']
            if case==0:index['unwind_info'][1]['file_offset']=0x33FF
            elif case==1:index['unwind_info'][1]['rva']=0xA000
            elif case==2:index['runtime_functions'][0]['table_entry_file_offset']=0x330C
            elif case==3:index['data_directories'][3]['file_offset']=0x33F0
            elif case==4:index['data_directories'][3]['bytes']=12
            elif case==5:index['sections'][1]['file_backed_size']=8
            elif case==6:refresh_image(values,values[0][:0x330D])
            else:index['sections'][1]['virtual_address']=0xC000
            with self.subTest(case=case),self.assertRaises(model.Refusal):self.select(m,values)

    def test_wrong_target_section_mapping_or_header_refuse(self):
        changes=[('header','machine',0),('header','image_base',0),('section','virtual_address',0),
                 ('section','characteristics',0),('function','end_rva',0x6054),
                 ('function','section_index',1),('function','begin_file_offset',0x1101)]
        for group,key,value in changes:
            m,values,_=fixture(); index=values[1]['core']['index']
            {'header':index['header'],'section':index['sections'][0],
             'function':index['runtime_functions'][1]}[group][key]=value
            with self.subTest(group=group,key=key),self.assertRaises(model.Refusal):self.select(m,values)

    def test_exact_four_incoming_branches_reconstructed_from_signed_encoding(self):
        for case in range(7):
            m,values,_=fixture(); row=values[3]['instructions'][2]
            if case==0:row['raw_hex']='0f8500000000'
            elif case==1:row['raw_hex']='0f88'+row['raw_hex'][4:]
            elif case==2:row['length']=5
            elif case==3:row['preferred_vma']+=1
            elif case==4:row['file_offset']+=1
            elif case==5:values[3]['instructions'].append(dict(row))
            else:values[3]['instructions'].pop(0)
            with self.subTest(case=case),self.assertRaises(model.Refusal):self.select(m,values)

    def test_index_and_caller_status_and_image_association_are_required(self):
        for case in range(6):
            m,values,_=fixture()
            if case==0:values[1]['static_index_admitted']=False
            elif case==1:values[1]['core']['static_index_admitted']=False
            elif case==2:values[1]['core']['index']['sha256']='0'*64
            elif case==3:values[1]['core']['index']['runtime_execution_observed']=True
            elif case==4:values[2]['decision']='PENDING'
            else:values[3]['schema']='bcrypt-disassembly-report-014-v1'
            with self.subTest(case=case),self.assertRaises(model.Refusal):self.select(m,values)

    def test_selection_evidence_joins_both_completed_preparations(self):
        m,values,_=fixture('actual');encode_inputs(m,values)
        primary,review=receipts(m)
        self.assertEqual(selection.validate_selection_evidence(m,*encode_receipts(m,primary,review)),primary)

    def test_false_clean_exit_or_chain_verification_cannot_admit(self):
        changes=[('primary_owner_exit',1),('primary_outer_exit',True),('primary_owner_reaped',False),
                 ('independent_of_subject_selector',False),('open_findings',{'C':0,'I':1,'M':0}),
                 ('chain_metadata_snapshot_verified',False),('chain_metadata_bytes',51),
                 ('incoming_branches_verified',False),('completion_requires_clean_verifier_exit',False)]
        for key,value in changes:
            m,values,_=fixture('actual');encode_inputs(m,values);primary,review=receipts(m)
            review[key]=value
            with self.subTest(key=key),self.assertRaises(model.Refusal):
                selection.validate_selection_evidence(m,*encode_receipts(m,primary,review))

    def test_primary_target_report_sources_and_provenance_must_join(self):
        changes=[lambda r:r['primary_report'].__setitem__('sha256','0'*64),
                 lambda r:r['target'].__setitem__('sha256','0'*64),
                 lambda r:r.__setitem__('primary_manifest_sha256','3'*64),
                 lambda r:r['primary_sources']['bcrypt_disassembly_model_016.py'].__setitem__('sha256','3'*64),
                 lambda r:r['primary_sources'].pop('prepare_bcrypt_selection_016.py'),
                 lambda r:r['primary_provenance']['runtime015'].__setitem__('sha256','3'*64),
                 lambda r:r['primary_files']['owner_target'].__setitem__('bytes',84),
                 lambda r:r['primary_cli_raw']['stderr'].__setitem__('bytes',1)]
        for change in changes:
            m,values,_=fixture('actual');encode_inputs(m,values);primary,review=receipts(m);change(review)
            with self.subTest(change=change),self.assertRaises(model.Refusal):
                selection.validate_selection_evidence(m,*encode_receipts(m,primary,review))

    def test_primary_metadata_binding_and_digest_are_required(self):
        for key,value in [('chain_metadata_snapshot_verified',False),('chain_metadata_bytes',53),
                           ('incoming_branches_verified',False),('original_image_full_reads',2)]:
            m,values,_=fixture('actual');encode_inputs(m,values);primary,review=receipts(m);primary[key]=value
            with self.subTest(key=key),self.assertRaises(model.Refusal):
                selection.validate_selection_evidence(m,*encode_receipts(m,primary,review))

    def test_incoming_starts_use_bounded_all_three_address_domains(self):
        m,values,_=fixture('actual');t=m['target']
        rows=[{'file_offset':t['file_start']+i,'rva':t['rva_start']+i,
               'preferred_vma':t['preferred_vma_start']+i,'length':1,
               'raw_hex':'90','assembly':'nop'} for i in range(83)]
        self.assertEqual(selection.validate_incoming_starts(rows,t),[0,29,37,45])
        for field in ('file_offset','rva','preferred_vma'):
            bad=copy.deepcopy(rows);bad[29][field]+=1
            with self.subTest(field=field),self.assertRaises(model.Refusal):selection.validate_incoming_starts(bad,t)
        for bad in (rows[:45],rows+rows[:1],[]):
            with self.assertRaises(model.Refusal):selection.validate_incoming_starts(bad,t)


if __name__ == '__main__':
    unittest.main()
