"""Pure constructed fixtures for the bounded 014 model; no image/tool access."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest import mock


SOURCE_NAMES = (
    "bcrypt_disassembly_model_014.py", "bcrypt_disassembly_listing_014.py",
    "bcrypt_disassembly_io_014.py", "bcrypt_disassembly_owner_014.py",
    "run_bcrypt_disassembly_014.py",
)
LIMITS = {
    "owner_seconds": 12, "child_cpu_seconds": 2,
    "child_address_space_bytes": 268435456, "child_file_bytes": 131072,
    "child_core_bytes": 0, "child_open_files": 64, "manifest_bytes": 32768,
    "image_bytes": 262144, "index_bytes": 327680, "admission_bytes": 32768,
    "identity_bytes": 67108864, "identity_total_bytes": 67108864,
    "identity_chunk_bytes": 65536, "source_count": 5, "source_bytes": 32768,
    "identity_roles": 14, "alias_count": 12, "absent_path_count": 8,
    "path_chars": 256, "tool_argv_count": 18, "owner_stderr_bytes": 65,
    "owner_stdout_bytes": 0, "target_bytes": 391, "instructions": 391,
    "instruction_bytes": 15, "raw_line_bytes": 512, "assembly_chars": 384,
    "report_bytes": 524288, "marker_bytes": 1024, "report_wrapper_bytes": 16384,
}
ENV = {"LANG": "C", "LC_ALL": "C", "TZ": "UTC", "DEBUGINFOD_URLS": ""}


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("ascii")


def identity(path, data):
    return {"path": path, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def argv(tool, target):
    start = target["preferred_vma_start"]
    end = start + target["rva_end"] - target["rva_start"]
    return [tool, "-D", "-z", "-b", "binary", "-m", "i386:x86-64", "-M",
            "intel", "--insn-width=16", "--wide", "--show-raw-insn",
            "--visualize-jumps=off", "--disassembler-color=off",
            f"--adjust-vma={start:#x}", f"--start-address={start:#x}",
            f"--stop-address={end:#x}", "target.bin"]


def fixture(target_bytes=b"\x90\xc3", file_start=32, rva_start=0x2020,
            image_base=0x180000000):
    """Return manifest/image/index/admission bytes, without any filesystem IO."""
    image = bytes(file_start) + target_bytes + bytes(16)
    length = len(target_bytes)
    target = {"runtime_function_index": 0, "section_index": 0,
              "file_start": file_start, "file_end": file_start + length,
              "rva_start": rva_start, "rva_end": rva_start + length,
              "preferred_vma_start": image_base + rva_start,
              "sha256": hashlib.sha256(target_bytes).hexdigest()}
    raw_start = min(16, file_start)
    section = {"index": 0, "name": ".text", "raw_file_offset": raw_start,
               "raw_size": len(image) - raw_start,
               "file_backed_size": len(image) - raw_start,
               "virtual_address": rva_start - (file_start - raw_start),
               "characteristics": 0x60000020}
    entry = {"file_offset": file_start, "rva": rva_start,
             "loader_init_rva": rva_start, "runtime_function_index": 0,
             "section_index": 0, "section_name": ".text"}
    runtime = {"index": 0, "section_index": 0, "section_name": ".text",
               "begin_file_offset": file_start, "end_file_offset": file_start + length,
               "begin_rva": rva_start, "end_rva": rva_start + length}
    index = {"schema": "bcrypt-pe-index-013-v1", "bytes": len(image),
             "sha256": hashlib.sha256(image).hexdigest(),
             "runtime_execution_observed": False,
             "header": {"machine": 34404, "optional_header_magic": 523,
                        "address_of_entrypoint": rva_start, "image_base": image_base},
             "entrypoint": entry, "sections": [section], "runtime_functions": [runtime]}
    report = {"schema": "bcrypt-pe-index-013-report-v1", "status": "complete_index",
              "static_index_admitted": True,
              "core": {"schema": "bcrypt-pe-core-013-v1", "status": "indexed",
                       "static_index_admitted": True, "index": index}}
    admission = {"schema": "bcrypt013-root-joint-runtime-admission-v1",
                 "decision": "ADMIT_COMPLETE_VERSIONED_STATIC_INDEX_013",
                 "status": "COMPLETE_INDEX_INDEPENDENTLY_REVIEWED",
                 "concrete_next_static_target": {
                     "runtime_function_index": 0, "section_index": 0,
                     "file_range": [file_start, file_start + length],
                     "rva_range": [rva_start, rva_start + length],
                     "function_bytes": length, "function_sha256": target["sha256"],
                     "image_base_preferred": image_base,
                     "preferred_vma_range": [image_base + rva_start,
                                             image_base + rva_start + length]}}
    index_raw, admission_raw = canonical(report), canonical(admission)
    roles = ("image", "index", "admission", "python", "tool", "loader_cache",
             *(f"dep{i:02d}" for i in range(1, 9)))
    identities = {role: identity("/fixture/input/" + role, role.encode()) for role in roles}
    for role, data in (("image", image), ("index", index_raw), ("admission", admission_raw)):
        identities[role] = identity("/fixture/input/" + role, data)
    manifest = {"schema": "bcrypt-disassembly-manifest-014-v1", "mode": "synthetic",
                "call_id": "014-synthetic-model-1", "source_commit": "1" * 40,
                "output_dir": "/fixture/run-014", "identities": identities,
                "sources": {name: identity("/fixture/source/" + name, name.encode())
                            for name in SOURCE_NAMES},
                "aliases": [], "absent_paths": [], "target": target,
                "tool_argv": argv(identities["tool"]["path"], target),
                "environment": dict(ENV), "limits": dict(LIMITS)}
    return manifest, image, index_raw, admission_raw


MODEL_PATH = Path(__file__).with_name(SOURCE_NAMES[0])
model = None
if MODEL_PATH.is_file():
    spec = importlib.util.spec_from_file_location("model_014_test", MODEL_PATH)
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)


class ModelTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(model, "014 bounded model implementation is missing")

    def rejected(self, operation):
        with self.assertRaises(model.Refusal):
            operation()

    def test_frozen_contract_constants(self):
        self.assertEqual(model.LIMITS, LIMITS)
        self.assertEqual(model.ENV, ENV)
        self.assertEqual(tuple(model.SOURCE_NAMES), SOURCE_NAMES)

    def test_refusal_has_fixed_code_and_require_has_no_value(self):
        self.assertIsNone(model.require(True, "CHECK"))
        with self.assertRaises(model.Refusal) as caught:
            model.require(False, "CHECK")
        self.assertEqual(caught.exception.code, "CHECK")

    def test_canonical_json_exact_ascii_and_lf(self):
        value = {"z": [None, True, False, -(1 << 63), (1 << 64) - 1],
                 "a": 'quote" slash\\ \b\f\n\r\t\x00\x7f\u96ea\U0001f600'}
        expected = canonical(value)
        self.assertEqual(model.encode_json(value, len(expected)), expected)
        self.assertEqual(model.decode_json(expected, len(expected)), value)
        self.rejected(lambda: model.encode_json(value, len(expected) - 1))
        self.rejected(lambda: model.decode_json(expected, len(expected) - 1))

    def test_encoder_rejects_before_unbounded_serialization(self):
        for value, cap in (("x" * 1000, 8), ({"x": set()}, 128),
                           ({"x": 1 << 1000}, 128)):
            with self.subTest(value_type=type(value).__name__, cap=cap):
                with mock.patch.object(model.json, "dumps", side_effect=AssertionError("encoded early")):
                    self.rejected(lambda: model.encode_json(value, cap))

    def test_nodes_count_keys_and_containers(self):
        value = {"x": [1]}
        self.assertEqual(model.encode_json(value, 64, 4), b'{"x":[1]}\n')
        self.assertEqual(model.decode_json(b'{"x":[1]}', 64, 4), value)
        self.rejected(lambda: model.encode_json(value, 64, 3))
        self.rejected(lambda: model.decode_json(b'{"x":[1]}', 64, 3))

    def test_decoder_nodes_reject_before_json_allocation(self):
        with mock.patch.object(model.json, "loads", side_effect=AssertionError("decoded early")):
            self.rejected(lambda: model.decode_json(b'[0,0,0,0]', 128, 4))

    def test_depth_and_cycle_limits(self):
        value = 0
        for _ in range(16):
            value = [value]
        self.assertEqual(model.decode_json(model.encode_json(value, 128), 128), value)
        self.rejected(lambda: model.encode_json([value], 128))
        self.rejected(lambda: model.decode_json(b'[' * 17 + b'0' + b']' * 17, 128))
        cycle = []
        cycle.append(cycle)
        self.rejected(lambda: model.encode_json(cycle, 128))

    def test_unsupported_values_and_subclasses_refused(self):
        class OddInt(int):
            pass
        for value in (1.0, float('inf'), float('nan'), b'x', (1,), {1: 2},
                      {'x': '\ud800'}, OddInt(1), -(1 << 63) - 1, 1 << 64):
            with self.subTest(kind=type(value).__name__):
                self.rejected(lambda: model.encode_json(value, 128))

    def test_strict_json_duplicate_nonfinite_numeric_and_syntax(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":{"a":0,"a":1}}', b'NaN',
                    b'Infinity', b'-Infinity', b'1.0', b'1e2', b'18446744073709551616',
                    b'-9223372036854775809', b'01', b'{}{}', b'"\\ud800"',
                    b'"unterminated', b'\xff', b'[}', b''):
            with self.subTest(raw=raw):
                self.rejected(lambda: model.decode_json(raw, 256))

    def test_codec_control_arguments_are_bounded(self):
        for cap, nodes in ((True, 1), (0, 1), (524289, 1), (64, True), (64, 0), (64, 32769)):
            self.rejected(lambda: model.encode_json(None, cap, nodes))
            self.rejected(lambda: model.decode_json(b'null', cap, nodes))
        self.rejected(lambda: model.decode_json('null', 64))

    def test_synthetic_manifest_returns_same_dict(self):
        manifest, *_ = fixture()
        self.assertIs(model.validate_manifest(manifest), manifest)
        self.assertEqual(model.tool_argv(manifest['identities']['tool']['path'], manifest['target']),
                         manifest['tool_argv'])

    def test_manifest_exact_top_fields_and_types(self):
        original, *_ = fixture()
        for field in original:
            changed = copy.deepcopy(original)
            del changed[field]
            self.rejected(lambda: model.validate_manifest(changed))
        for field, value in (("extra", 0), ("schema", "wrong"), ("mode", "unknown"),
                             ("call_id", "014-init-static-1"), ("source_commit", "G" * 40),
                             ("identities", []), ("sources", []), ("target", [])):
            changed = copy.deepcopy(original)
            changed[field] = value
            self.rejected(lambda: model.validate_manifest(changed))

    def test_manifest_paths_are_bounded_canonical_ascii(self):
        for path in ('relative/path', '//double/path', '/trailing/', '/a/../b', '/a/./b',
                     '/x\n', '/\u96ea', '/' + 'x' * 256, '/'):
            manifest, *_ = fixture()
            manifest['output_dir'] = path
            self.rejected(lambda: model.validate_manifest(manifest))

    def test_exact_limit_environment_and_argv_values(self):
        for section, key, value in (("limits", "owner_seconds", 13),
                                    ("limits", "child_core_bytes", False),
                                    ("environment", "PATH", "/bin"),
                                    ("environment", "LANG", "C.UTF-8")):
            manifest, *_ = fixture()
            manifest[section][key] = value
            self.rejected(lambda: model.validate_manifest(manifest))
        for position in range(18):
            manifest, *_ = fixture()
            manifest['tool_argv'][position] += '-changed'
            self.rejected(lambda: model.validate_manifest(manifest))

    def test_identity_roles_shape_size_and_sha(self):
        for field, value in (("bytes", True), ("bytes", -1), ("bytes", 67108865),
                             ("sha256", "A" * 64), ("extra", 0), ("path", '/a/../b')):
            manifest, *_ = fixture()
            manifest['identities']['dep01'][field] = value
            self.rejected(lambda: model.validate_manifest(manifest))
        for role in ('dep08', 'tool'):
            manifest, *_ = fixture()
            del manifest['identities'][role]
            self.rejected(lambda: model.validate_manifest(manifest))
        manifest, *_ = fixture()
        manifest['identities']['extra'] = manifest['identities']['dep01']
        self.rejected(lambda: model.validate_manifest(manifest))
        manifest, *_ = fixture()
        for item in manifest['identities'].values():
            item['bytes'] = 67108864
        self.rejected(lambda: model.validate_manifest(manifest))

    def test_role_specific_size_caps(self):
        for role, limit in (('image', 262144), ('index', 327680), ('admission', 32768)):
            manifest, *_ = fixture()
            manifest['identities'][role]['bytes'] = limit + 1
            self.rejected(lambda: model.validate_manifest(manifest))

    def test_sources_have_exact_names_common_parent_and_size(self):
        for key, value in (('path', '/other/' + SOURCE_NAMES[0]),
                           ('path', '/fixture/source/wrong.py'), ('bytes', 32769)):
            manifest, *_ = fixture()
            manifest['sources'][SOURCE_NAMES[0]][key] = value
            self.rejected(lambda: model.validate_manifest(manifest))
        manifest, *_ = fixture()
        manifest['sources']['extra.py'] = manifest['sources'][SOURCE_NAMES[0]]
        self.rejected(lambda: model.validate_manifest(manifest))

    def test_alias_and_absent_path_relationships(self):
        manifest, *_ = fixture()
        manifest['aliases'] = [{'path': '/alias/tool', 'target': '/fixture/input/tool'}]
        manifest['absent_paths'] = ['/absent/one']
        self.assertIs(model.validate_manifest(manifest), manifest)
        mutations = [lambda m: m['aliases'][0].update(target='/unpinned'),
                     lambda m: m['aliases'][0].update(extra=1),
                     lambda m: m['aliases'].append(dict(m['aliases'][0])),
                     lambda m: m['absent_paths'].append('/absent/one'),
                     lambda m: m['absent_paths'].append('/fixture/input/tool'),
                     lambda m: m['absent_paths'].append('/alias/tool'),
                     lambda m: m['aliases'][0].update(path='/fixture/input/image')]
        for change in mutations:
            changed = copy.deepcopy(manifest)
            change(changed)
            self.rejected(lambda: model.validate_manifest(changed))
        for key, count in (('aliases', 13), ('absent_paths', 9)):
            changed = copy.deepcopy(manifest)
            changed[key] = changed[key] * count
            self.rejected(lambda: model.validate_manifest(changed))

    def test_synthetic_rejects_all_actual_data_digests(self):
        for actual in (model.ACTUAL_IMAGE, model.ACTUAL_INDEX):
            for role in ('image', 'index'):
                manifest, *_ = fixture()
                manifest['identities'][role]['sha256'] = actual['sha256']
                self.rejected(lambda: model.validate_manifest(manifest))
        for role in ('image', 'index'):
            manifest, *_ = fixture()
            manifest['identities'][role]['sha256'] = model.ACTUAL_TARGET_SHA256
            self.rejected(lambda: model.validate_manifest(manifest))
        for digest in (model.ACTUAL_IMAGE['sha256'], model.ACTUAL_INDEX['sha256'],
                       model.ACTUAL_TARGET_SHA256):
            manifest, *_ = fixture()
            manifest['target']['sha256'] = digest
            self.rejected(lambda: model.validate_manifest(manifest))

    def actual_manifest(self):
        manifest, *_ = fixture()
        manifest.update(mode='actual', call_id='014-init-static-1')
        for role, actual in (('image', model.ACTUAL_IMAGE), ('index', model.ACTUAL_INDEX),
                             ('admission', model.ACTUAL_ADMISSION)):
            manifest['identities'][role].update(actual)
        manifest['identities']['tool'].update(path='/usr/bin/x86_64-linux-gnu-objdump',
            bytes=390848, sha256='7fd25a9f02382a952fe983e40bd41aa12127d1bbe3f4dc5c93ea13d03279a2f5')
        manifest['identities']['python'].update(bytes=30894944,
            sha256='fa67443527ed9647f760d807e2a38f26340757123e643c4639cf273ed15d5ea7')
        manifest['target'] = {'runtime_function_index': 169, 'section_index': 0,
            'file_start': 42384, 'file_end': 42775, 'rva_start': 42384,
            'rva_end': 42775, 'preferred_vma_start': 6442493328,
            'sha256': model.ACTUAL_TARGET_SHA256}
        manifest['tool_argv'] = argv(manifest['identities']['tool']['path'], manifest['target'])
        return manifest

    def test_actual_manifest_exact_fixed_identities_and_target(self):
        original = self.actual_manifest()
        self.assertIs(model.validate_manifest(original), original)
        for role in ('image', 'index', 'admission', 'tool', 'python'):
            for field, value in (('bytes', 1), ('sha256', '0' * 64)):
                changed = copy.deepcopy(original)
                changed['identities'][role][field] = value
                self.rejected(lambda: model.validate_manifest(changed))
        for field in original['target']:
            changed = copy.deepcopy(original)
            changed['target'][field] = '0' * 64 if field == 'sha256' else changed['target'][field] + 1
            self.rejected(lambda: model.validate_manifest(changed))
        changed = copy.deepcopy(original)
        changed['identities']['tool']['path'] = '/different/objdump'
        changed['tool_argv'] = argv('/different/objdump', changed['target'])
        self.rejected(lambda: model.validate_manifest(changed))

    def test_target_strict_fields_types_lengths_and_overflow(self):
        for field, value in (('file_start', True), ('runtime_function_index', -1),
                             ('rva_end', 0x2020), ('file_end', 33),
                             ('preferred_vma_start', (1 << 64) - 2), ('extra', 0)):
            manifest, *_ = fixture()
            manifest['target'][field] = value
            self.rejected(lambda: model.validate_manifest(manifest))
        manifest, *_ = fixture(bytes(392))
        self.rejected(lambda: model.validate_manifest(manifest))

    def test_select_target_separates_file_rva_and_preferred_vma(self):
        for length in (1, 2, 15, 391):
            expected = b'\x90' * length
            parts = fixture(expected, file_start=48, rva_start=0x7010, image_base=0x140000000)
            self.assertEqual(model.select_target(*parts), expected)

    def test_select_target_checks_input_digest_before_decode(self):
        parts = list(fixture())
        for position in (1, 2, 3):
            changed = list(parts)
            changed[position] += b'x'
            with mock.patch.object(model.json, 'loads', side_effect=AssertionError('decoded early')):
                self.rejected(lambda: model.select_target(*changed))

    def test_select_target_rejects_changed_target_bytes(self):
        manifest, image, index, admission = fixture()
        changed = bytearray(image)
        changed[manifest['target']['file_start']] ^= 1
        changed = bytes(changed)
        manifest['identities']['image'] = identity('/fixture/input/image', changed)
        parsed = json.loads(index)
        parsed['core']['index']['sha256'] = hashlib.sha256(changed).hexdigest()
        index = canonical(parsed)
        manifest['identities']['index'] = identity('/fixture/input/index', index)
        self.rejected(lambda: model.select_target(manifest, changed, index, admission))

    def test_select_target_rejects_broken_index_associations(self):
        mutations = [lambda r: r.update(status='refused'),
                     lambda r: r['core'].update(status='refused'),
                     lambda r: r['core']['index'].update(runtime_execution_observed=True),
                     lambda r: r['core']['index']['header'].update(image_base=1),
                     lambda r: r['core']['index']['header'].update(machine=332),
                     lambda r: r['core']['index']['entrypoint'].update(runtime_function_index=1),
                     lambda r: r['core']['index']['entrypoint'].update(loader_init_rva=1),
                     lambda r: r['core']['index']['sections'][0].update(raw_file_offset=1),
                     lambda r: r['core']['index']['sections'][0].update(file_backed_size=1),
                     lambda r: r['core']['index']['runtime_functions'][0].update(end_rva=1),
                     lambda r: r['core']['index']['runtime_functions'][0].update(index=True)]
        for mutate in mutations:
            manifest, image, raw, admission = fixture()
            report = json.loads(raw)
            mutate(report)
            raw = canonical(report)
            manifest['identities']['index'] = identity('/fixture/input/index', raw)
            self.rejected(lambda: model.select_target(manifest, image, raw, admission))

    def test_select_target_rejects_unadmitted_or_different_joint_target(self):
        for key, value in (('decision', 'PENDING'), ('status', 'PENDING')):
            manifest, image, raw, admission = fixture()
            parsed = json.loads(admission)
            parsed[key] = value
            admission = canonical(parsed)
            manifest['identities']['admission'] = identity('/fixture/input/admission', admission)
            self.rejected(lambda: model.select_target(manifest, image, raw, admission))
        for key in ('file_range', 'rva_range', 'preferred_vma_range', 'function_sha256'):
            manifest, image, raw, admission = fixture()
            parsed = json.loads(admission)
            parsed['concrete_next_static_target'][key] = '0' * 64 if key == 'function_sha256' else [0, 1]
            admission = canonical(parsed)
            manifest['identities']['admission'] = identity('/fixture/input/admission', admission)
            self.rejected(lambda: model.select_target(manifest, image, raw, admission))


if __name__ == '__main__':
    unittest.main()
