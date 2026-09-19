"""Pure-data release-body contracts; no GitHub calls or executable launches."""
from __future__ import annotations

import json
from pathlib import Path
import re
import tempfile
import unittest

import render_release_notes as n

ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = 'Iviesever/msvc-quick-build'
COMMIT = 'a' * 40


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'source'
        self.root.mkdir()
        (self.root / 'VERSION').write_text('5.6.0\n', encoding='utf-8')
        for suffix in ('.md', '_ZH.md'):
            source = Path('docs/V5_6_RELEASE_BOUNDARY' + suffix)
            text = (ROOT / source).read_text(encoding='utf-8')
            (self.root / source).parent.mkdir(parents=True, exist_ok=True)
            (self.root / source).write_text(text, encoding='utf-8')
            for target in re.findall(r'\]\(([^\s()]+)\)', text):
                if not target.startswith('https:'):
                    p = (self.root / source.parent / target.split('#')[0]).resolve()
                    p.parent.mkdir(parents=True, exist_ok=True)
                    if not p.exists():
                        p.write_text('link target\n', encoding='utf-8')
        self.out = Path(self.temp.name) / 'output'

    def render(self):
        return n.render(self.root, REPOSITORY, COMMIT)

    def change(self, old, new, suffix='.md'):
        p = self.root / ('docs/V5_6_RELEASE_BOUNDARY' + suffix)
        text = p.read_text(encoding='utf-8')
        self.assertIn(old, text)
        p.write_text(text.replace(old, new), encoding='utf-8')

    def test_bilingual_body_and_six_risks(self):
        body, metadata = self.render()
        text = body.decode('utf-8')
        self.assertEqual(metadata['version'], '5.6.0')
        for risk in n.RISK_IDS:
            self.assertEqual(text.count(f'| `{risk}` |'), 2)
        self.assertNotIn('VERSION` is still', text)
        self.assertNotIn('## Remaining release handoff', text)
        self.assertIn('no general latency/throughput guarantee', text)
        self.assertIn('不作普遍延迟', text)

    def test_all_selected_sections_preserved(self):
        body, _ = self.render()
        for language, suffix in (('English', '.md'), ('简体中文', '_ZH.md')):
            path = Path('docs/V5_6_RELEASE_BOUNDARY' + suffix)
            text = (self.root / path).read_text(encoding='utf-8')
            for i, heading in enumerate(n.SECTIONS[language]):
                expected = n.section(text, heading)
                if i == 2:
                    expected = n.risk_body(expected)
                expected = n.pin_links(expected, self.root, path, REPOSITORY, COMMIT)
                self.assertIn(expected.encode('utf-8'), body)

    def test_relative_links_pinned_to_exact_commit(self):
        body, _ = self.render()
        for target in re.findall(r'\]\(([^\s()]+)\)', body.decode('utf-8')):
            self.assertTrue(target.startswith('https://'), target)
        self.assertIn(f'/blob/{COMMIT}/cpp/src/app/targets/BuildCompletion.cpp'.encode(), body)

    def test_missing_risk_rejected(self):
        self.change('`cold-tails`', '`missing-risk`')
        with self.assertRaisesRegex(ValueError, 'risk row'):
            self.render()

    def test_duplicate_risk_rejected(self):
        self.change('`cold-tails`', '`private129`')
        with self.assertRaisesRegex(ValueError, 'risk row'):
            self.render()

    def test_chinese_missing_risk_rejected(self):
        self.change('`msvc-c1041`', '`missing-risk`', '_ZH.md')
        with self.assertRaisesRegex(ValueError, 'risk row'):
            self.render()

    def test_missing_section_rejected(self):
        self.change('## Stop and rollback conditions', '## Other conditions')
        with self.assertRaisesRegex(ValueError, 'missing section'):
            self.render()

    def test_duplicate_heading_rejected(self):
        self.change('## Stop and rollback conditions', '## Changes since v5.5.0')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.render()

    def test_empty_section_rejected(self):
        with self.assertRaisesRegex(ValueError, 'empty section'):
            n.section('## A\n\n## B\ntext', 'A')

    def test_table_without_limits_rejected(self):
        table = '| Risk | Decision |\n|---|---|\n' + '\n'.join(f'| `{r}` | X |' for r in n.RISK_IDS)
        with self.assertRaises(ValueError):
            n.risk_body(table)

    def test_invalid_versions_rejected(self):
        for value in ('', 'v5.6.0', '5.6', '5.6.0\n6.0.0', '5.6.0; echo bad'):
            with self.subTest(value=value):
                (self.root / 'VERSION').write_text(value, encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'invalid VERSION'):
                    self.render()

    def test_prerelease_uses_root_version(self):
        (self.root / 'VERSION').write_text('5.6.0-rc.1\n', encoding='utf-8')
        body, metadata = self.render()
        self.assertTrue(body.startswith(b'# MQB 5.6.0-rc.1\n'))
        self.assertEqual(metadata['version'], '5.6.0-rc.1')

    def test_missing_next_series_document_rejected(self):
        (self.root / 'VERSION').write_text('5.7.0\n', encoding='utf-8')
        with self.assertRaises(FileNotFoundError):
            self.render()

    def test_bad_identity_rejected(self):
        for repository, commit in (('other/repo', COMMIT), (REPOSITORY, 'main'),
                                   (REPOSITORY, 'A' * 40), (REPOSITORY, 'a' * 39)):
            with self.subTest(repository=repository, commit=commit):
                with self.assertRaises(ValueError):
                    n.render(self.root, repository, commit)

    def test_bad_links_rejected(self):
        for target in ('../../../outside', 'file:///secret', '//evil.example/a', 'unknown.md'):
            with self.subTest(target=target):
                with self.assertRaises(ValueError):
                    n.pin_links(f'[bad]({target})', self.root, Path('docs/V5_6_RELEASE_BOUNDARY.md'), REPOSITORY, COMMIT)

    def test_crlf_equivalence(self):
        before = self.render()
        for p in self.root.rglob('*.md'):
            p.write_bytes(p.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        (self.root / 'VERSION').write_bytes(b'5.6.0\r\n')
        self.assertEqual(before, self.render())

    def test_roundtrip_and_no_overwrite(self):
        n.write_notes(self.root, REPOSITORY, COMMIT, self.out)
        n.verify_notes(self.root, REPOSITORY, COMMIT, self.out)
        with self.assertRaises(FileExistsError):
            n.write_notes(self.root, REPOSITORY, COMMIT, self.out)

    def test_body_tampering_rejected(self):
        n.write_notes(self.root, REPOSITORY, COMMIT, self.out)
        p = self.out / 'body.md'
        p.write_bytes(p.read_bytes().replace(b'cold-tails', b'hidden'))
        with self.assertRaisesRegex(ValueError, 'body differs'):
            n.verify_notes(self.root, REPOSITORY, COMMIT, self.out)

    def test_provenance_tampering_rejected(self):
        n.write_notes(self.root, REPOSITORY, COMMIT, self.out)
        p = self.out / 'manifest.json'
        data = json.loads(p.read_text(encoding='utf-8'))
        data['commit'] = 'b' * 40
        p.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'provenance'):
            n.verify_notes(self.root, REPOSITORY, COMMIT, self.out)

    def test_source_change_after_render_rejected(self):
        n.write_notes(self.root, REPOSITORY, COMMIT, self.out)
        self.change('no general latency/throughput guarantee', 'a latency guarantee')
        with self.assertRaisesRegex(ValueError, 'body differs'):
            n.verify_notes(self.root, REPOSITORY, COMMIT, self.out)

    def test_actual_repository_documents(self):
        body, _ = n.render(ROOT, REPOSITORY, COMMIT)
        self.assertIn(b'earlier-abba-flags', body)


if __name__ == '__main__':
    unittest.main()
