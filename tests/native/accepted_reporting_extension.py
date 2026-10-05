"""Recognize only PR236's accepted additive reporting test extension.

The pre-236 whole-program fingerprints remain unchanged. Validate BOTH complete
current files, reverse exactly two added lines, then validate the whole old file.
This is not a wildcard exclusion and cannot admit altered old or new assertions.
Source anchor: main b5a8b9543f8cb44047a9e9b2db989d5e21361f43 / PR236.
The independent compile-evidence extension is checked by its own exact inverse
before historical whole-program fingerprints are evaluated.
"""
import hashlib
import accepted_module_target_cache_extension as target_cache_extension
import accepted_module_cache_evidence_extension as module_evidence_extension
import accepted_compile_evidence_extension as compile_evidence_extension
import accepted_pch_cache_evidence_extension as pch_evidence_extension

REPORT = 'cpp/tests/app/diagnostics/reporting_tests.cpp'
HELPER = 'cpp/tests/app/diagnostics/storage_report_format_cases.hpp'
CURRENT = '7a0fec63cc04f587f5c8cbe5099368834a3fa322b09b8da71ecf0c52b66cda68'
ADDITION = 'ac4862f7f41597961d7bef05d7f6f2f1f653d927df9f0f34d6590622381ae0c4'
PRIOR = '5c66b1f335864eab02537bcac0f6f4c040447ee4a863bcf009d959a0fed77fa9'
LINES = ('#include "storage_report_format_cases.hpp"\n',
         '    failures += storage_report_format_cases::run();\n')


def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def legacy_view(values, helper):
    """Return a copy for historical fingerprinting; never mutate the source map."""
    value=values[REPORT]
    binary=isinstance(value, bytes)
    text=value.decode('utf-8') if binary else value
    if digest(text) != CURRENT or digest(helper) != ADDITION:
        raise ValueError('accepted reporting extension bytes changed')
    for line in LINES:
        if text.count(line) != 1:
            raise ValueError('accepted reporting extension anchor changed')
        text=text.replace(line, '')
    if digest(text) != PRIOR:
        raise ValueError('original reporting assertions changed')
    result=dict(values)
    result[REPORT]=text.encode('utf-8') if binary else text
    return target_cache_extension.legacy_view(module_evidence_extension.legacy_view(pch_evidence_extension.legacy_view(compile_evidence_extension.legacy_view(result))))
