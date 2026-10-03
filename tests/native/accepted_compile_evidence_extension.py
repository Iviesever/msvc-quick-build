"""Exact reversible test extension for same-invocation compile-cache evidence.

Only the reviewed complete native program is admitted. Remove the documented
forward declaration, call, extra prerequisite assertion and appended cases, then
verify the complete predecessor Git blob. No test/path wildcard is excluded.
This is a historical-test view only; it never modifies product or test files.
"""
import hashlib

TEST = 'cpp/tests/msvc/compiler/incremental_loop_tests.cpp'
CURRENT = '85e4b996c769dcbf03e65d21ec1a75c2c50a2f69'
PRIOR = '3943fa92457f0a0f3e73de1cf110372ad4db6637'
BEGIN = '// BEGIN MQB_COMPILE_CACHE_EVIDENCE_CASES\n'
END = '// END MQB_COMPILE_CACHE_EVIDENCE_CASES\n'
LINES = (
    'namespace compile_cache_evidence_cases { int run(const mqb::msvc::MsvcToolchain&, mqb::process::ProcessRunner&); }\n',
    '    failures += compile_cache_evidence_cases::run(toolchain, runner);\n',
    '    expect(cached.has_value(), "header invalidation requires persisted cache metadata");\n',
)


def git_blob(text):
    raw = text.encode('utf-8')
    return hashlib.sha1(b'blob ' + str(len(raw)).encode('ascii') + b'\0' + raw).hexdigest()


def original_program(text):
    if git_blob(text) != CURRENT:
        raise ValueError('compile evidence extension bytes changed')
    if text.count(BEGIN) != 1 or text.count(END) != 1 or not text.endswith(END):
        raise ValueError('compile evidence extension boundary changed')
    original = text.split(BEGIN, 1)[0]
    for line in LINES:
        if original.count(line) != 1:
            raise ValueError('compile evidence extension anchor changed')
        original = original.replace(line, '')
    if git_blob(original) != PRIOR:
        raise ValueError('original incremental loop assertions changed')
    return original


def legacy_view(values):
    result = dict(values)
    if TEST in result:
        value = result[TEST]
        binary = isinstance(value, bytes)
        text = value.decode('utf-8') if binary else value
        original = original_program(text)
        result[TEST] = original.encode('utf-8') if binary else original
    return result
