"""Finite, synthetic evidence joins. No host reads, decoder or authority generation."""
import copy
import unittest
from unittest import mock

import bcrypt_admission_contract_017 as c
import check_bcrypt_entry_binding_017 as b


def ref(path, raw=b'SYNTHETIC'):
    return dict(path=path, **c.fingerprint(raw))


def module(name, path):
    return dict(file=path, origin=path, cached=None, spec_name=name,
                package='', search=None, path=None)


def proposal(e):
    # Fixture assembly only; the production checker never creates this proposal.
    owner, ctrl, graph = (e[r] for r in b.ROLES)
    files = {}
    for record in [*owner['files'].values(), *ctrl['files'].values(), *graph['nodes'], graph['cache']]:
        files[record['path']] = {k:record[k] for k in ('path','bytes','sha256')}
    aliases = {}
    for p,t in [*owner['aliases'].items(), *ctrl['aliases'].items(),
                *((x['path'],x['target']) for x in graph['aliases'])]:
        if p != t: aliases[p] = t
    absent = set(owner['absent_cached']) | set(ctrl['absent_cached'])
    absent.update(x['path'] for x in graph['absence_checks'])
    return dict(programs=dict(python=owner['python_file'], controller=ctrl['entry_file'],
        decoder=graph['roots']['decoder'], loader=graph['edges'][0]['provider']),
        modules=copy.deepcopy(owner['modules']),
        files={f'f{i:03}':r for i,r in enumerate(files.values())},
        aliases=[dict(path=p,target=t) for p,t in sorted(aliases.items())], absent_paths=sorted(absent))


def specimen():
    e = {}; python='/synthetic/python'; decoder='/synthetic/objdump'; loader='/synthetic/ld.so'
    for role, name in [('owner', b.BOOTSTRAP_NAME), ('controller', b.CONTROLLER_NAME)]:
        path='/synthetic/'+name
        rows={n:module(n,'/synthetic/'+n+'.py') for n in b.SOURCES}
        rows['__main__']=dict(file=path,origin=None,cached=None,spec_name=None,
                              package=None,search=None,path=None)
        rows['empty']=dict(file='/synthetic/empty/__init__.py',origin='/synthetic/empty/__init__.py',
             cached=None,spec_name='empty',package='empty',search=['/synthetic/empty'],path=['/synthetic/empty'])
        files={r['file']:ref(r['file']) for r in rows.values()}
        files['/synthetic/empty/__init__.py']=ref('/synthetic/empty/__init__.py',b'')
        files[python]=ref(python)
        e[role]=dict(schema='bcrypt-entry-preload-evidence-017-v1',role=role,
            status='ENTRY_PRELOAD_RECORDED_NOT_QUALIFIED',phase='before_request_read',
            modules=rows,files=files,python_file=python,entry_file=path,python_version='SYNTHETIC 3.12',
            aliases={p:p for p in files},absent_cached=['/synthetic/missing.pyc'],
            reads=dict(passes=2*len(files),eof_requests=2*len(files),content_bytes=2*sum(r['bytes'] for r in files.values())),
            expected_modules_generated=False,execution_admitted=False,source_independently_reviewed=False,
            runtime_loader_verified=False,writer_domain_qualified=False,full_execution_closure_verified=False,
            request_reads=0,target_reads=0,decoder_processes=0,formal_observations=0)
    e['toolchain']=dict(schema='bcrypt-static-toolchain-017-v1',status='STATIC_TOOLCHAIN_IDENTITY_RECORDED',
        execution_admitted=False,roots=dict(python=python,decoder=decoder),
        nodes=[ref(p) for p in (python,decoder,loader)],node_count=3,edge_count=2,
        edges=[dict(source=p,provider=loader,kind='PT_INTERP') for p in (python,decoder)],
        aliases=[dict(path='/synthetic/loader-alias',target=loader)],
        absence_checks=[dict(path='/synthetic/absent-hook',absent=True)],cache=ref('/synthetic/ld.cache'))
    return e,proposal(e)


def packed(e, runtime):
    raw={r:c.encode(e[r]) for r in b.ROLES};roots={r:c.fingerprint(raw[r]) for r in b.ROLES}
    candidate=c.encode(dict(schema='bcrypt-runtime-binding-candidate-017-v1',basis=copy.deepcopy(roots),runtime=runtime))
    roots['candidate']=c.fingerprint(candidate)
    return candidate,raw,roots


def run(e, runtime):return b.review_binding(*packed(e,runtime))


class EntryBindingTests(unittest.TestCase):
    def setUp(self):self.e,self.runtime=specimen()

    def test_matched_candidate_remains_non_authorizing(self):
        r=run(self.e,self.runtime)
        self.assertEqual(r['status'],'RECORDED_RUNTIME_BINDING_MATCHED_NOT_QUALIFIED')
        self.assertEqual(r['files'],len(self.runtime['files']))
        for key in ('complete_request','current_host_verified','snapshots_atomic','writer_domain_qualified',
                    'preparations_verified','independent_review_signed','execution_admitted','clears_hold'):
            self.assertIs(r[key],False)
        self.assertNotIn('modules',r);self.assertNotIn('runtime',r)

    def test_any_wrong_pin_prevents_every_json_parse(self):
        for role in ('candidate',*b.ROLES):
            candidate,raw,roots=packed(self.e,self.runtime);roots[role]['sha256']='0'*64
            with self.subTest(role=role),mock.patch.object(b,'decode') as decode,self.assertRaises(c.Refused):
                b.review_binding(candidate,raw,roots)
            decode.assert_not_called()

    def test_missing_or_extra_roots_and_unbounded_lengths(self):
        for key in ('owner','extra'):
            candidate,raw,roots=packed(self.e,self.runtime)
            if key=='owner':del roots[key]
            else:roots[key]=roots['owner']
            with self.assertRaises(c.Refused):b.review_binding(candidate,raw,roots)
        for value in (True,-1,0,c.REQUEST_CAP+1):
            candidate,raw,roots=packed(self.e,self.runtime);roots['candidate']['bytes']=value
            with self.assertRaises(c.Refused):b.review_binding(candidate,raw,roots)

    def test_candidate_basis_cannot_substitute_evidence_root(self):
        candidate,raw,roots=packed(self.e,self.runtime);v=c.decode(candidate)
        v['basis']['owner']['sha256']='f'*64;candidate=c.encode(v);roots['candidate']=c.fingerprint(candidate)
        with self.assertRaises(c.Refused):b.review_binding(candidate,raw,roots)

    def test_target_receipts_and_allowlist_flags_are_not_candidate_fields(self):
        for name in ('target','receipts','execution_admitted','allow_unverified'):
            candidate,raw,roots=packed(self.e,self.runtime);v=c.decode(candidate);v[name]=True
            candidate=c.encode(v);roots['candidate']=c.fingerprint(candidate)
            with self.assertRaises(c.Refused):b.review_binding(candidate,raw,roots)

    def test_roles_phase_and_declared_authority_cannot_be_changed(self):
        for name,value in [('role','controller'),('phase','after_execution'),('request_reads',True),
                           ('execution_admitted',True),('full_execution_closure_verified',True)]:
            e=copy.deepcopy(self.e);e['owner'][name]=value
            with self.subTest(name=name),self.assertRaises(c.Refused):run(e,self.runtime)

    def test_main_cannot_be_collector_or_swap_roles(self):
        for role in ('owner','controller'):
            e=copy.deepcopy(self.e);e[role]['entry_file']='/synthetic/check_collector.py'
            e[role]['modules']['__main__']['file']=e[role]['entry_file']
            with self.assertRaises(c.Refused):run(e,self.runtime)

    def test_missing_extra_and_changed_runtime_files(self):
        for case in ('missing','extra','digest','empty','duplicate'):
            runtime=copy.deepcopy(self.runtime);name=next(iter(runtime['files']))
            if case=='missing':del runtime['files'][name]
            elif case=='extra':runtime['files']['new']=ref('/synthetic/invented')
            elif case=='digest':runtime['files'][name]['sha256']='0'*64
            elif case=='empty':runtime['files'][name].update(bytes=0,sha256=c.fingerprint(b'')['sha256'])
            else:runtime['files']['duplicate']=runtime['files'][name]
            with self.subTest(case=case),self.assertRaises(c.Refused):run(self.e,runtime)

    def test_no_silent_winner_for_conflicting_snapshot_file(self):
        e=copy.deepcopy(self.e);e['controller']['files'][e['controller']['python_file']]['sha256']='f'*64
        with self.assertRaises(c.Refused):run(e,proposal(e))

    def test_inode_difference_is_not_cross_host_content_conflict(self):
        for role,device in [('owner',10),('controller',20)]:
            for r in self.e[role]['files'].values():r['state']=dict(dev=device,ino=device+1)
        result=run(self.e,self.runtime)
        self.assertIs(result['snapshots_atomic'],False)

    def test_owner_module_set_and_shared_metadata_are_exact(self):
        runtime=copy.deepcopy(self.runtime);runtime['modules']['extra']=module('extra','/synthetic/extra')
        with self.assertRaises(c.Refused):run(self.e,runtime)
        e=copy.deepcopy(self.e);e['controller']['modules'][b.SOURCES[0]]['package']='changed'
        with self.assertRaises(c.Refused):run(e,self.runtime)

    def test_interpreter_and_loader_edges_cannot_drift(self):
        for case in ('python','version','missing','duplicate','divergent'):
            e=copy.deepcopy(self.e);g=e['toolchain']
            if case=='python':e['controller']['python_file']='/wrong/python'
            elif case=='version':e['controller']['python_version']='other'
            elif case=='missing':g['edges'].pop()
            elif case=='duplicate':g['edges'].append(copy.deepcopy(g['edges'][0]))
            else:g['edges'][1]['provider']=g['roots']['decoder']
            g['edge_count']=len(g['edges'])
            with self.subTest(case=case),self.assertRaises(c.Refused):run(e,self.runtime)

    def test_missing_alias_absence_or_conflicting_alias_refused(self):
        for key in ('aliases','absent_paths'):
            runtime=copy.deepcopy(self.runtime);runtime[key]=[]
            with self.assertRaises(c.Refused):run(self.e,runtime)
        e=copy.deepcopy(self.e);e['owner']['aliases']['/synthetic/loader-alias']=e['owner']['python_file']
        with self.assertRaises(c.Refused):run(e,self.runtime)

    def test_cached_missing_file_cannot_also_be_declared_present(self):
        e=copy.deepcopy(self.e);e['toolchain']['absence_checks'][0]['path']=e['owner']['python_file']
        with self.assertRaises(c.Refused):run(e,self.runtime)

    def test_reads_include_empty_files_and_eof(self):
        for key in ('passes','content_bytes','eof_requests'):
            e=copy.deepcopy(self.e);e['owner']['reads'][key]-=1
            with self.assertRaises(c.Refused):run(e,self.runtime)
        self.assertEqual(run(self.e,self.runtime)['files'],len(self.runtime['files']))

    def test_empty_package_cannot_be_main_or_cached_file(self):
        for role in ('owner','controller'):
            e=copy.deepcopy(self.e);e[role]['modules']['__main__']['file']='/synthetic/empty/__init__.py'
            with self.assertRaises(c.Refused):run(e,self.runtime)
        e=copy.deepcopy(self.e);e['owner']['modules'][b.SOURCES[0]]['cached']='/synthetic/empty/__init__.py'
        with self.assertRaises(c.Refused):run(e,proposal(e))

    def test_root_table_neither_mutated_nor_returned_by_reference(self):
        args=packed(self.e,self.runtime);before=copy.deepcopy(args);r=b.review_binding(*args)
        self.assertEqual(args,before)
        r['evidence']['owner']['sha256']='f'*64
        self.assertEqual(args,before)

    def test_oversized_graph_and_file_projection_are_refused(self):
        e=copy.deepcopy(self.e);e['toolchain']['nodes']*=11
        with self.assertRaises(c.Refused):run(e,self.runtime)
        runtime=copy.deepcopy(self.runtime);runtime['files']={f'x{i}':ref(f'/synthetic/{i}') for i in range(c.FILE_COUNT+1)}
        with self.assertRaises(c.Refused):run(self.e,runtime)


if __name__=='__main__':unittest.main(verbosity=2)
