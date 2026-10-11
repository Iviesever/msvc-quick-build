"""Bounded fresh-image preparation for diagnostic 010, never a producer seal.

Only the new child is written. The writer-to-read transition keeps an open
attributes anchor to the same file object; final content checks occur under
read-only pins. No pathname retry, deletion, fallback or rights change exists.
"""

from __future__ import annotations

from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import hashlib
import os
from pathlib import Path

from producer_loader_snaps_pe import inspect_loader_snaps_image, verify_loader_snaps_pair


MAX_IMAGE_BYTES = 8 * 1024 * 1024
CHUNK_BYTES = 65536
MAX_FULL_READS = 6
MAX_API_RECORDS = 2048
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_READ_ATTRIBUTES = 0x80
FILE_SHARE_READ = 1
FILE_SHARE_WRITE = 2
FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
CREATE_NEW = 1
OPEN_EXISTING = 3


class _FileIdInfo(ctypes.Structure):
    _fields_ = [("volume", ctypes.c_uint64), ("file_id", ctypes.c_ubyte * 16)]


class _FileStandardInfo(ctypes.Structure):
    _fields_ = [("allocation", ctypes.c_int64), ("size", ctypes.c_int64),
                ("links", ctypes.c_uint32), ("delete_pending", ctypes.c_ubyte),
                ("directory", ctypes.c_ubyte)]


class _FileAttributeTagInfo(ctypes.Structure):
    _fields_ = [("attributes", ctypes.c_uint32), ("reparse_tag", ctypes.c_uint32)]


def _file_key(value: dict) -> tuple[str, str]:
    return value["volume_serial_decimal"], value["file_id_hex"]


class _ImageFiles:
    def __init__(self, record: dict):
        if os.name != "nt" or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError("Image preparation requires 64-bit Windows")
        if (ctypes.sizeof(_FileIdInfo), ctypes.sizeof(_FileStandardInfo),
                ctypes.sizeof(_FileAttributeTagInfo)) != (24, 24, 8):
            raise RuntimeError("Unexpected Windows image-information ABI")
        self.record = record
        self.handles: dict[str, int] = {}
        self.full_reads = 0
        self.copy_writes = 0
        self.patch_writes = 0
        self.close_failed = False
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateFileW": ([wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE),
            "ReOpenFile": ([wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                wintypes.DWORD], wintypes.HANDLE),
            "GetHandleInformation": ([wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            "GetFileType": ([wintypes.HANDLE], wintypes.DWORD),
            "GetFileInformationByHandleEx": ([wintypes.HANDLE, ctypes.c_int,
                ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
            "SetFilePointerEx": ([wintypes.HANDLE, ctypes.c_int64,
                ctypes.POINTER(ctypes.c_int64), wintypes.DWORD], wintypes.BOOL),
            "ReadFile": ([wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
            "WriteFile": ([wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
            "FlushFileBuffers": ([wintypes.HANDLE], wintypes.BOOL),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (arguments, returned) in signatures.items():
            function = getattr(self.kernel, name)
            function.argtypes = arguments
            function.restype = returned

    def log(self, operation: str, label: str, ok: bool, error: int, **fields) -> None:
        if len(self.record["operations"]) >= MAX_API_RECORDS:
            raise RuntimeError("Image API record bound exceeded")
        self.record["operations"].append({"sequence": len(self.record["operations"]) + 1,
            "operation": operation, "label": label, "ok": ok, "error": error, **fields})
        if not ok:
            raise OSError(error, f"{operation}: {label}")

    def acquired(self, handle: int | None, operation: str, label: str, **fields) -> int:
        valid = handle is not None and handle != ctypes.c_void_p(-1).value
        error = 0 if valid else ctypes.get_last_error()
        # Own a successful handle before recording or validation can throw.
        if valid:
            self.handles[label] = handle
        self.log(operation, label, valid, error,
                 handle_decimal=str(handle) if valid else None, **fields)
        flags = wintypes.DWORD()
        ok = bool(self.kernel.GetHandleInformation(handle, ctypes.byref(flags)))
        error = 0 if ok else ctypes.get_last_error()
        self.log("GetHandleInformation", label, ok, error, flags=flags.value)
        if flags.value & 1:
            raise RuntimeError("Image/anchor handles must not be inheritable")
        return handle

    def open(self, path: Path, label: str, *, create: bool) -> int:
        access = GENERIC_READ | GENERIC_WRITE if create else GENERIC_READ
        share = FILE_SHARE_READ | FILE_SHARE_WRITE if create else FILE_SHARE_READ
        disposition = CREATE_NEW if create else OPEN_EXISTING
        handle = self.kernel.CreateFileW(str(path), access, share, None,
            disposition, FILE_FLAG_OPEN_REPARSE_POINT, None)
        return self.acquired(handle, "CreateFileW", label, path=str(path),
            desired_access=access, share_mode=share, creation_disposition=disposition,
            flags=FILE_FLAG_OPEN_REPARSE_POINT, security_attributes_null=True)

    def reopen(self, original: int, label: str, *, attributes_only: bool) -> int:
        access = FILE_READ_ATTRIBUTES if attributes_only else GENERIC_READ
        share = FILE_SHARE_READ | FILE_SHARE_WRITE if attributes_only else FILE_SHARE_READ
        handle = self.kernel.ReOpenFile(original, access, share, FILE_FLAG_OPEN_REPARSE_POINT)
        return self.acquired(handle, "ReOpenFile", label, original_handle_decimal=str(original),
            desired_access=access, share_mode=share, flags=FILE_FLAG_OPEN_REPARSE_POINT)

    def snapshot(self, handle: int, label: str, *, allow_empty: bool = False) -> dict:
        file_type = self.kernel.GetFileType(handle)
        error = ctypes.get_last_error() if file_type == 0 else 0
        self.log("GetFileType", label, file_type == 1, error, file_type=file_type)
        information = (_FileIdInfo(), _FileStandardInfo(), _FileAttributeTagInfo())
        for kind, value in zip((18, 1, 9), information):
            ok = bool(self.kernel.GetFileInformationByHandleEx(handle, kind,
                      ctypes.byref(value), ctypes.sizeof(value)))
            error = 0 if ok else ctypes.get_last_error()
            self.log("GetFileInformationByHandleEx", label, ok, error,
                     information_class=kind, buffer_bytes=ctypes.sizeof(value))
        identifier, standard, attributes = information
        result = {"handle_decimal": str(handle),
            "volume_serial_decimal": str(identifier.volume),
            "file_id_hex": bytes(identifier.file_id).hex(), "size_decimal": str(standard.size),
            "allocation_bytes_decimal": str(standard.allocation), "links": standard.links,
            "delete_pending": bool(standard.delete_pending), "directory": bool(standard.directory),
            "attributes": attributes.attributes, "reparse_tag": attributes.reparse_tag}
        self.record["file_observations"].append({"label": label, **result})
        if (not (0 if allow_empty else 1) <= standard.size <= MAX_IMAGE_BYTES
                or standard.links != 1 or standard.delete_pending or standard.directory
                or attributes.attributes & (0x10 | 0x400) or attributes.reparse_tag != 0):
            raise RuntimeError("Image is not a bounded, regular, single-link non-reparse file")
        return result

    def seek(self, handle: int, offset: int, label: str) -> None:
        position = ctypes.c_int64()
        ok = bool(self.kernel.SetFilePointerEx(handle, offset, ctypes.byref(position), 0))
        error = 0 if ok else ctypes.get_last_error()
        self.log("SetFilePointerEx", label, ok, error, requested_offset=offset,
                 returned_offset=position.value)
        if position.value != offset:
            raise RuntimeError("Unexpected image file position")

    def read_complete(self, handle: int, label: str) -> tuple[bytes, dict]:
        if self.full_reads >= MAX_FULL_READS:
            raise RuntimeError("Held image full-read pass bound exceeded")
        self.full_reads += 1
        before = self.snapshot(handle, label + "-before-read")
        size = int(before["size_decimal"])
        self.seek(handle, 0, label)
        data = bytearray()
        for offset in range(0, size, CHUNK_BYTES):
            requested = min(CHUNK_BYTES, size - offset)
            buffer = ctypes.create_string_buffer(requested)
            count = wintypes.DWORD()
            ok = bool(self.kernel.ReadFile(handle, buffer, requested, ctypes.byref(count), None))
            error = 0 if ok else ctypes.get_last_error()
            self.log("ReadFile", label, ok, error, offset=offset,
                     requested_bytes=requested, returned_bytes=count.value)
            if count.value != requested:
                raise RuntimeError("Incomplete bounded image read; no retry")
            data.extend(buffer.raw)
        extra = ctypes.create_string_buffer(1)
        count = wintypes.DWORD()
        ok = bool(self.kernel.ReadFile(handle, extra, 1, ctypes.byref(count), None))
        error = 0 if ok else ctypes.get_last_error()
        self.log("ReadFile", label + "-EOF", ok, error,
                 requested_bytes=1, returned_bytes=count.value)
        if count.value != 0:
            raise RuntimeError("Image exceeded its complete observed size")
        after = self.snapshot(handle, label + "-after-read")
        if _file_key(before) != _file_key(after) or before["size_decimal"] != after["size_decimal"]:
            raise RuntimeError("Image identity or size changed during full read")
        result = bytes(data)
        self.record["full_reads"].append({"label": label, "bytes": len(result),
            "sha256": hashlib.sha256(result).hexdigest(), "identity": after, "EOF_verified": True})
        return result, after

    def write(self, handle: int, data: bytes, offset: int, label: str, *, patch: bool) -> None:
        if patch:
            if self.patch_writes != 0 or data != b"\x02\x00\x00\x00":
                raise RuntimeError("Only one exact four-byte loader flag write is allowed")
            self.patch_writes += 1
        else:
            if self.copy_writes >= MAX_IMAGE_BYTES // CHUNK_BYTES or not 1 <= len(data) <= CHUNK_BYTES:
                raise RuntimeError("Image copy write bound exceeded")
            self.copy_writes += 1
        buffer = ctypes.create_string_buffer(data, len(data))
        count = wintypes.DWORD()
        ok = bool(self.kernel.WriteFile(handle, buffer, len(data), ctypes.byref(count), None))
        error = 0 if ok else ctypes.get_last_error()
        self.log("WriteFile", label, ok, error, offset=offset,
                 requested_bytes=len(data), returned_bytes=count.value, loader_flag_patch=patch)
        if count.value != len(data):
            raise RuntimeError("Incomplete bounded image write; no retry")

    def flush(self, handle: int) -> None:
        ok = bool(self.kernel.FlushFileBuffers(handle))
        error = 0 if ok else ctypes.get_last_error()
        self.log("FlushFileBuffers", "patched-child-writer", ok, error)

    def close(self, label: str) -> None:
        handle = self.handles.pop(label)
        ok = bool(self.kernel.CloseHandle(handle))
        error = 0 if ok else ctypes.get_last_error()
        try:
            self.log("CloseHandle", label, ok, error, handle_decimal=str(handle))
        except Exception:
            self.close_failed = True
            raise

    def close_remaining(self) -> None:
        for label in list(reversed(self.handles)):
            try:
                self.close(label)
            except Exception as error:
                self.record["errors"].append(f"{type(error).__name__}: {error}")


@contextmanager
def hold_loader_snaps_images(parent_path: Path, child_path: Path, record: dict):
    """Yield exact child identity only after held full-content readback.

    Six complete held reads are fixed: parent initial, child before patch,
    child writer after patch, child read pin, parent final, child final.
    Artifact inventory reads are separate and must also be bounded by caller.
    """
    record.update({"schema": 1, "diagnostic_only": True, "operations": [],
        "file_observations": [], "full_reads": [], "errors": [],
        "ready_for_native": False, "pins_released": False, "post_capture_verified": False,
        "parent_path": str(parent_path), "child_path": str(child_path),
        "image_bytes_limit": MAX_IMAGE_BYTES, "held_read_pass_limit": MAX_FULL_READS,
        "read_chunk_limit": CHUNK_BYTES, "copy_write_limit": 128, "patch_write_limit": 1,
        "pins_are_producer_A4_authority": False})
    files = _ImageFiles(record)
    parent = child = None
    parent_bytes = child_bytes = None
    parent_identity = child_identity = None
    try:
        parent = files.open(parent_path, "parent-read-pin", create=False)
        parent_bytes, parent_identity = files.read_complete(parent, "parent-initial")
        parent_pe = inspect_loader_snaps_image(parent_bytes, expected_flags_set=0,
                                              max_image_bytes=MAX_IMAGE_BYTES)
        record["parent_pe"] = parent_pe
        writer = files.open(child_path, "child-writer", create=True)
        created = files.snapshot(writer, "child-created", allow_empty=True)
        if (created["size_decimal"] != "0" or _file_key(created) == _file_key(parent_identity)
                or created["volume_serial_decimal"] != parent_identity["volume_serial_decimal"]):
            raise RuntimeError("Fresh child must be empty, distinct and on the parent volume")
        for offset in range(0, len(parent_bytes), CHUNK_BYTES):
            files.write(writer, parent_bytes[offset:offset + CHUNK_BYTES], offset,
                        "child-copy", patch=False)
        copied, copied_identity = files.read_complete(writer, "child-before-patch")
        if copied != parent_bytes or _file_key(copied_identity) != _file_key(created):
            raise RuntimeError("Fresh copy differs from held parent before patch")
        record["copy_before_patch_equal"] = True
        field = parent_pe["load_config"]["global_flags_set_file_offset"]
        files.seek(writer, field, "child-loader-flags")
        files.write(writer, b"\x02\x00\x00\x00", field, "child-loader-flags", patch=True)
        files.flush(writer)
        patched, patched_identity = files.read_complete(writer, "child-writer-after-patch")
        record["writer_pair_verification"] = verify_loader_snaps_pair(parent_bytes, patched,
                                                     max_image_bytes=MAX_IMAGE_BYTES)
        if _file_key(patched_identity) != _file_key(created):
            raise RuntimeError("Patched writer identity differs from fresh child")
        anchor = files.reopen(writer, "child-attributes-anchor", attributes_only=True)
        anchor_identity = files.snapshot(anchor, "child-attributes-anchor")
        if (_file_key(anchor_identity) != _file_key(created)
                or anchor_identity["size_decimal"] != str(len(patched))):
            raise RuntimeError("Attributes anchor does not bind the patched child")
        files.close("child-writer")
        child = files.reopen(anchor, "child-read-pin", attributes_only=False)
        child_bytes, child_identity = files.read_complete(child, "child-pinned-before-native")
        record["pinned_pair_verification"] = verify_loader_snaps_pair(parent_bytes, child_bytes,
                                                     max_image_bytes=MAX_IMAGE_BYTES)
        if child_bytes != patched or _file_key(child_identity) != _file_key(created):
            raise RuntimeError("Read pin does not bind complete patched child")
        files.close("child-attributes-anchor")
        record["parent_identity"] = parent_identity
        record["child_identity"] = child_identity
        record["ready_for_native"] = True
        yield {"parent_pe": parent_pe, "parent_identity": parent_identity,
               "child_identity": child_identity,
               "child_sha256": hashlib.sha256(child_bytes).hexdigest()}
    except Exception as error:
        record["errors"].append(f"{type(error).__name__}: {error}")
        raise
    finally:
        # If the native capture raises, preserve final observations while both
        # read pins still exist. Never claim a later pathname hash is this pin.
        if record["ready_for_native"]:
            checks = []
            for handle, original, expected, label in (
                (parent, parent_bytes, parent_identity, "parent-final"),
                (child, child_bytes, child_identity, "child-final"),
            ):
                try:
                    observed, identifier = files.read_complete(handle, label)
                    same = observed == original and _file_key(identifier) == _file_key(expected)
                    if not same:
                        raise RuntimeError(f"{label}: held image changed")
                    checks.append(True)
                except Exception as error:
                    checks.append(False)
                    record["errors"].append(f"{type(error).__name__}: {error}")
            record["post_capture_verified"] = len(checks) == 2 and all(checks)
        files.close_remaining()
        record["full_read_passes"] = files.full_reads
        record["copy_writes"] = files.copy_writes
        record["patch_writes"] = files.patch_writes
        record["pins_released"] = not files.handles and not files.close_failed
