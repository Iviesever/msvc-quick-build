"""Own-task capability preflight regression; no real child, signal or prctl."""
import errno
import os
import signal
from types import SimpleNamespace
import unittest
from unittest import mock

import bcrypt_writer_scope_017 as s


class ChildInterfacePreflight(unittest.TestCase):
    def policy(self, failure=None, children=()):
        tasks=mock.MagicMock()
        tasks.__enter__.return_value=iter([SimpleNamespace(name=str(os.getpid()))])
        for patch in (mock.patch.object(s.os,'scandir',return_value=tasks),
                      mock.patch.object(s.signal,'getsignal',return_value=signal.SIG_DFL),
                      mock.patch.object(s,'_children',side_effect=failure,return_value=list(children))):
            patch.start();self.addCleanup(patch.stop)

    def test_missing_interface_refuses_before_policy_change(self):
        self.policy(FileNotFoundError(errno.ENOENT,'synthetic'))
        scope=s.DescendantScope()
        with mock.patch.object(s,'_peek') as peek,mock.patch.object(s,'_prctl') as prctl:
            with self.assertRaisesRegex(s.Refused,'SCOPE_CHILD_INTERFACE_UNAVAILABLE'):scope.activate()
        peek.assert_not_called();prctl.assert_not_called()
        self.assertTrue(scope.used);self.assertFalse(scope.active)

    def test_denied_interface_has_no_alternate_path(self):
        self.policy(PermissionError(errno.EACCES,'synthetic'))
        with mock.patch.object(s.os,'pidfd_open') as pidfd:
            with self.assertRaisesRegex(s.Refused,'SCOPE_CHILD_INTERFACE_UNAVAILABLE'):s._policy()
        pidfd.assert_not_called()

    def test_other_io_failure_is_refused(self):
        self.policy(OSError(errno.EIO,'synthetic'))
        with self.assertRaisesRegex(s.Refused,'SCOPE_CHILD_INTERFACE_UNAVAILABLE'):s._policy()

    def test_malformed_contents_keep_original_refusal(self):
        self.policy(s.Refused('SCOPE_CHILD_LIST'))
        with self.assertRaisesRegex(s.Refused,'^SCOPE_CHILD_LIST$'):s._policy()

    def test_empty_text_does_not_replace_wait_ownership_check(self):
        self.policy()
        with mock.patch.object(s,'_peek',return_value=None),mock.patch.object(s,'_prctl') as prctl:
            with self.assertRaisesRegex(s.Refused,'SCOPE_PREEXISTING_CHILD'):s.DescendantScope().activate()
        prctl.assert_not_called()

    def test_live_candidates_do_not_fail_capability_only_check(self):
        self.policy(children=(401,))
        s._policy()  # Normal during drain: require availability, not emptiness.

    def test_readable_interface_and_echild_preserve_normal_activation(self):
        self.policy()
        with mock.patch.object(s,'_peek',side_effect=ChildProcessError()), \
             mock.patch.object(s,'_prctl',return_value=object()), \
             mock.patch.object(s,'_get',return_value=0),mock.patch.object(s,'_set') as setting, \
             mock.patch.object(s.signal,'signal'):
            scope=s.DescendantScope();scope.activate()
        self.assertTrue(scope.active);self.assertEqual(setting.call_args.args[1],1)

    def test_refused_owner_cannot_retry_activation(self):
        self.policy(FileNotFoundError(errno.ENOENT,'synthetic'))
        scope=s.DescendantScope()
        with self.assertRaises(s.Refused):scope.activate()
        with self.assertRaisesRegex(s.Refused,'REUSED'):scope.activate()


if __name__=='__main__':unittest.main(verbosity=2)
