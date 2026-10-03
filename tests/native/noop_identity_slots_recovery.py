"""Read-only, evidence-bound admission for one preparation-failure recovery.

No process launch, dispatch, retry, measurement or qualification decision lives here.
An owner permit must be published AFTER code review and BEFORE the new run. This
module deliberately contains no permit and does not grant one by being installed.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import stat
import urllib.request
from zipfile import ZipFile

import noop_identity_slots as slots

REPOSITORY = 'Iviesever/msvc-quick-build'
API = 'https://api.github.com/repos/' + REPOSITORY
WORKFLOW = '.github/workflows/noop-identity-slots-study.yml'
WORKFLOW_ID = 372355819
FAILED_RUN = 36948961360
FAILED_HEAD = 'a07ce0ca2fd371129c90d7550cad31ededcad6d3'
FAILED_ARTIFACT = 11203257232
FAILED_BYTES = 11226368
FAILED_SHA = '70fba5cb2fe61f196614e81fee9c34229e6af30caea25ef22d91e92090625a3a'
FAILED_PREFIX = 'msvc-quick-build/msvc-quick-build/slot-execution-out/'
ORIGINAL_MEMBER = FAILED_PREFIX + 'download/mqb-performance-evidence.zip'
RECOVERY = 'slot001-preparation-recovery-001'
PERMIT_PREFIX = 'MQB_SLOT_RECOVERY_PERMIT_V1\n'
NEXT_NUMBER = 2
need = slots.need
same = slots.same


def loads(raw):
    return json.loads(raw, object_pairs_hook=slots.b.unique)


def inspect_failure(path: Path) -> dict:
    """Authenticate ALL failed bytes first. Never extract untrusted ZIP members."""
    need(path.is_file() and not path.is_symlink() and path.stat().st_size == FAILED_BYTES,
         'wrong preparation-failure ZIP size/type')
    raw = path.read_bytes()
    need(hashlib.sha256(raw).hexdigest() == FAILED_SHA, 'wrong preparation-failure ZIP digest')
    expected = {FAILED_PREFIX + name for name in (
        'admission.json', 'request.json', 'source.zip', 'execution-workflow.yml',
        'entry-intent.json', 'entry.stdout', 'entry.stderr', 'wrapper-result.json',
        'download/mqb-performance-evidence.zip')}
    with ZipFile(io.BytesIO(raw)) as z:
        need(len(z.namelist()) == len(expected) and set(z.namelist()) == expected,
             'unexpected failure prefix; measurement absence not established')
        for item in z.infolist():
            p = PurePosixPath(item.filename)
            need(not p.is_absolute() and '..' not in p.parts and '\\' not in item.filename and
                 not item.is_dir() and not stat.S_ISLNK(item.external_attr >> 16), 'unsafe failure member')
        need(z.testzip() is None, 'failed ZIP CRC error')
        request = loads(z.read(FAILED_PREFIX+'request.json').decode('utf-8-sig'))
        admission = loads(z.read(FAILED_PREFIX+'admission.json').decode('utf-8-sig'))
        for key, value in dict(GITHUB_RUN_ID=str(FAILED_RUN), GITHUB_RUN_NUMBER='1',
            GITHUB_RUN_ATTEMPT='1', GITHUB_REPOSITORY=REPOSITORY, GITHUB_SHA=FAILED_HEAD,
            GITHUB_WORKFLOW_SHA=FAILED_HEAD, REVIEWED_COMMIT=FAILED_HEAD,
            ALLOCATION='pr232-slot-001', EXECUTE_REVIEWED='true').items():
            need(request[key] == value, 'failed request binding differs: '+key)
        need(same(request, admission['context']) and admission['may_clear_hold'] is False,
             'failed admission binding differs')
        result = loads(z.read(FAILED_PREFIX+'wrapper-result.json').decode('utf-8-sig'))
        need(same(result, dict(status='failed_or_incomplete', entry_requested=True, entry_exit_code=1,
            error='RuntimeError: Pinned entry failed with exit code 1', original_decision='HOLD',
            may_clear_hold=False)), 'failed outcome differs')
        need(hashlib.sha256(z.read(FAILED_PREFIX+'entry.stderr')).hexdigest() ==
             '24040e72b4386f9034f5e66d28200ac98308d53bfad574ddeb5eb1c12d8a1c6d',
             'original multi-path error differs')
        need(hashlib.sha256(z.read(ORIGINAL_MEMBER)).hexdigest() == slots.ARCHIVE_SHA,
             'nested original 819 input differs')
        need(hashlib.sha256(z.read(FAILED_PREFIX+'source.zip')).hexdigest() ==
             '02096769eb7cd7b69dd68d80b91b7bd6fb1e5a4f936a6d5c9663a8913e44ac0b',
             'failed source differs')
    return dict(run_id=FAILED_RUN, run_attempt=1, artifact_id=FAILED_ARTIFACT,
        artifact_sha256=FAILED_SHA, status='consumed_failed_before_measurement',
        inference_basis='pinned error, source control flow and retained prefix; not OS process census')


def expected_permit(commit: str, tree: str, workflow_sha: str) -> dict:
    """Schema only. Constructing this object does NOT publish or approve a permit."""
    return dict(schema=1, repository=REPOSITORY, workflow_id=WORKFLOW_ID,
        workflow_path=WORKFLOW, reviewed_commit=commit, reviewed_tree=tree,
        workflow_sha256=workflow_sha, allocation='pr232-slot-001', recovery_id=RECOVERY,
        run_number=NEXT_NUMBER, run_attempt=1, failed_run_id=FAILED_RUN, failed_attempt=1,
        failed_artifact_id=FAILED_ARTIFACT, failed_artifact_sha256=FAILED_SHA,
        original_819_sha256=slots.ARCHIVE_SHA, maximum_mqb_calls=48, prime_calls=16,
        measured_calls=32, decision='authorize_one_recovery_after_review', may_clear_hold=False)


def timestamp(value):
    need(isinstance(value, str) and re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ', value),
         'invalid server timestamp')
    return datetime.strptime(value, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=timezone.utc)


def validate_permit(comment, context, tree, workflow_sha, run_created):
    need(type(comment['id']) is int and str(comment['id']) == context['PERMIT_COMMENT_ID'],
         'permit comment id differs')
    need(comment['issue_url'] == API+'/issues/198' and comment['author_association'] == 'OWNER' and
         comment['user']['login'] == 'Iviesever', 'permit is not an owner record in issue 198')
    need(comment['created_at'] == comment['updated_at'] and
         timestamp(comment['created_at']) < timestamp(run_created), 'permit missing prior immutable publication')
    body = comment['body']
    need(isinstance(body, str) and body.startswith(PERMIT_PREFIX) and len(body) < 16384,
         'explicit recovery permit body required')
    value = loads(body[len(PERMIT_PREFIX):])
    need(same(value, expected_permit(context['REVIEWED_COMMIT'], tree, workflow_sha)),
         'permit source/workflow/failure/protocol differs')
    return value


def validate_history(history, context):
    need(type(history['total_count']) is int and history['total_count'] == 2 and
         len(history['workflow_runs']) == 2, 'unexpected existing diagnostic runs; no retries')
    runs = history['workflow_runs']
    need(all(type(r['id']) is int for r in runs) and
         {r['id'] for r in runs} == {FAILED_RUN, int(context['GITHUB_RUN_ID'])}, 'run identity differs')
    for r in runs:
        old = r['id'] == FAILED_RUN
        need(r['workflow_id'] == WORKFLOW_ID and r['path'] == WORKFLOW and
             r['event'] == 'workflow_dispatch' and r['head_branch'] == 'main' and
             type(r['run_attempt']) is int and r['run_attempt'] == 1 and
             type(r['run_number']) is int and r['run_number'] == (1 if old else NEXT_NUMBER) and
             r['head_sha'] == (FAILED_HEAD if old else context['REVIEWED_COMMIT']),
             'workflow history source/attempt differs')
        if old:
            need(r['status'] == 'completed' and r['conclusion'] == 'failure', 'original failure state changed')
        else:
            need(r['status'] == 'in_progress' and r['conclusion'] is None, 'recovery run not currently active')
    return next(r for r in runs if r['id'] != FAILED_RUN)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError('API redirects are refused')


def read_api(path, token=''):
    allowed = (path == '/actions/workflows/'+str(WORKFLOW_ID)+'/runs?per_page=100' or
               path == '/git/ref/heads/main' or re.fullmatch(r'/issues/comments/[1-9][0-9]*', path))
    need(allowed, 'unexpected API resource')
    headers = {'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
    if token:
        headers['Authorization'] = 'Bearer '+token
    with urllib.request.build_opener(NoRedirect()).open(
            urllib.request.Request(API+path, headers=headers), timeout=20) as response:
        raw = response.read(2097153)
    need(len(raw) <= 2097152, 'API response budget exceeded')
    return raw


def authorize(context, repo: Path, tree: str, failure_zip: Path, out: Path, token='') -> dict:
    """Authenticate evidence and read server state once, outside measurement windows."""
    need(context['GITHUB_RUN_NUMBER'] == str(NEXT_NUMBER) and context['GITHUB_RUN_ATTEMPT'] == '1' and
         context['RECOVERY_ID'] == RECOVERY and context['ALLOCATION'] == 'pr232-slot-001' and
         context['EXECUTE_REVIEWED'] == 'true' and
         re.fullmatch('[1-9][0-9]*', context['GITHUB_RUN_ID']) and
         int(context['GITHUB_RUN_ID']) != FAILED_RUN and
         re.fullmatch('[1-9][0-9]*', context['PERMIT_COMMENT_ID']), 'invalid recovery request')
    for value in (context['REVIEWED_COMMIT'], tree):
        need(isinstance(value, str) and re.fullmatch('[0-9a-f]{40}', value), 'exact source required')
    failure = inspect_failure(failure_zip)
    workflow_sha = hashlib.sha256(slots.canonical((repo/WORKFLOW).read_bytes())).hexdigest()
    snapshots = {}
    for name, path, auth in (
        ('history', '/actions/workflows/'+str(WORKFLOW_ID)+'/runs?per_page=100', token),
        ('permit', '/issues/comments/'+context['PERMIT_COMMENT_ID'], ''),
        ('main', '/git/ref/heads/main', token)):
        raw = read_api(path, auth)
        with (out/('recovery-'+name+'-response.json')).open('xb') as stream:
            stream.write(raw)
        snapshots[name] = loads(raw.decode('utf-8'))
    current = validate_history(snapshots['history'], context)
    need(snapshots['main']['object']['sha'] == context['REVIEWED_COMMIT'], 'main moved since review')
    permit = validate_permit(snapshots['permit'], context, tree, workflow_sha, current['created_at'])
    receipt = dict(status='owner_permit_and_failed_prefix_checked', failure=failure,
        permit_comment_id=int(context['PERMIT_COMMENT_ID']), permit=permit,
        run_id=context['GITHUB_RUN_ID'], reviewed_commit=context['REVIEWED_COMMIT'],
        recovery_id=RECOVERY, remote_allocation_verified=False, may_clear_hold=False)
    slots.b.write_new(out/'recovery-admission.json', receipt)
    return receipt
