"""Render the reviewed bilingual scope into a versioned Release body.

No network, builds or publication. VERSION remains the only version source.
The dated candidate status and future handoff are deliberately not release text.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import re
from urllib.parse import urlsplit

RISK_IDS = (
    'private129', 'pr188-abba630', 'pr192-abba641',
    'msvc-c1041', 'cold-tails', 'earlier-abba-flags',
)
SECTIONS = {
    'English': ('Changes since v5.5.0', 'Compatibility, installation and migration',
                'Explicit residual-risk disposition', 'Stop and rollback conditions'),
    '简体中文': ('相对 v5.5.0 的变更', '兼容性、安装与迁移',
                 '剩余风险的明确处置', '停止与回退条件'),
}


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise ValueError(reason)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def section(text: str, heading: str) -> str:
    headings = list(re.finditer(r'^## (.+)$', text, re.M))
    names = [m[1] for m in headings]
    require(len(names) == len(set(names)), 'duplicate document heading')
    require(names.count(heading) == 1, f'missing section: {heading}')
    i = names.index(heading)
    end = headings[i + 1].start() if i + 1 < len(headings) else len(text)
    result = text[headings[i].end():end].strip()
    require(bool(result), f'empty section: {heading}')
    return result


def risk_body(text: str) -> str:
    # Keep the full table and all following limitations; omit only the dated
    # candidate-authorization preamble, not any risk row or qualification.
    start = re.search(r'^\|', text, re.M)
    require(start is not None, 'missing risk table')
    result = text[start.start():]
    ids = re.findall(r'^\| `([^`]+)` \|', result, re.M)
    require(tuple(ids) == RISK_IDS, 'missing, duplicate or unexpected risk row')
    require(bool(result[result.rfind('\n|') + 1:].split('\n\n', 1)[-1].strip()),
            'missing risk limitations')
    # The source has a paragraph after the table. Do not accept table-only notes.
    require(re.search(r'\|\s*\n\s*\n[^|\s]', result) is not None, 'missing risk paragraph')
    return result


def pin_links(text: str, root: Path, source: Path, repository: str, commit: str) -> str:
    def replace(match: re.Match[str]) -> str:
        label, target = match[1], match[2]
        url = urlsplit(target)
        if url.scheme:
            require(url.scheme == 'https' and bool(url.netloc), 'unsupported link scheme')
            return match[0]
        require(not url.netloc and not url.query, 'unsupported relative link')
        relative = (posixpath.normpath(posixpath.join(source.parent.as_posix(), url.path))
                    if url.path else source.as_posix())
        require(not relative.startswith(('/', '../')) and relative != '..', 'link escapes repository')
        require((root / relative).is_file(), f'missing linked file: {relative}')
        pinned = f'https://github.com/{repository}/blob/{commit}/{relative}'
        if url.fragment:
            pinned += '#' + url.fragment
        return f'[{label}]({pinned})'
    return re.sub(r'\[([^\]]+)\]\(([^\s()]+)\)', replace, text)


def render(root: Path, repository: str, commit: str) -> tuple[bytes, dict]:
    require(re.fullmatch(r'[0-9a-f]{40}', commit) is not None, 'full lowercase commit SHA required')
    require(repository == 'Iviesever/msvc-quick-build', 'unexpected release repository')
    raw_version = (root / 'VERSION').read_bytes()
    version = raw_version.decode('utf-8').strip()
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?', version)
    require(match is not None, 'invalid VERSION')
    stem = f'docs/V{match[1]}_{match[2]}_RELEASE_BOUNDARY'
    sources = {'VERSION': sha256(raw_version.replace(b'\r\n', b'\n'))}
    blocks = [f'# MQB {version}',
              f'Source / 发布提交: [{commit}](https://github.com/{repository}/commit/{commit})',
              'The scope and limitations below come from the reviewed candidate assessment; '
              'they do not claim that historical failures or their causes have been resolved.\n\n'
              '以下能力与限制来自已审阅的候选评估，不表示历史失败或其根因已经解决。']
    for language, suffix in (('English', '.md'), ('简体中文', '_ZH.md')):
        path = Path(stem + suffix)
        raw = (root / path).read_bytes().replace(b'\r\n', b'\n')
        text = raw.decode('utf-8')
        sources[path.as_posix()] = sha256(raw)
        blocks.append('## ' + language)
        for heading in SECTIONS[language]:
            body = section(text, heading)
            if heading == SECTIONS[language][2]:
                body = risk_body(body)
            blocks.append('### ' + heading + '\n\n' + pin_links(body, root, path, repository, commit))
    body = ('\n\n'.join(blocks) + '\n').encode('utf-8')
    require(len(body) < 100_000, 'release body too large')
    require(all(body.decode('utf-8').count(f'| `{risk}` |') == 2 for risk in RISK_IDS),
            'bilingual risk rows missing')
    metadata = dict(schema=1, repository=repository, commit=commit, version=version,
                    sources_sha256_lf=sources, body_sha256=sha256(body),
                    languages=list(SECTIONS), risk_ids=list(RISK_IDS))
    return body, metadata


def write_notes(root: Path, repository: str, commit: str, output: Path) -> None:
    body, metadata = render(root, repository, commit)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'body.md').write_bytes(body)
    (output / 'manifest.json').write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + '\n',
                                          encoding='utf-8', newline='\n')


def verify_notes(root: Path, repository: str, commit: str, output: Path) -> None:
    body, metadata = render(root, repository, commit)
    require((output / 'body.md').read_bytes() == body, 'release body differs from reviewed sources')
    require(json.loads((output / 'manifest.json').read_text(encoding='utf-8')) == metadata,
            'release body provenance mismatch')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path, default=Path('.'))
    parser.add_argument('--repository', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    function = verify_notes if args.verify else write_notes
    function(args.repo_root.resolve(), args.repository, args.commit, args.output)
    print('Bilingual release body verified.' if args.verify else 'Bilingual release body generated.')


if __name__ == '__main__':
    main()
