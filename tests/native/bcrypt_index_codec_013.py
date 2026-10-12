"""Bounded canonical JSON for the separately registered diagnostic 013.

No files, subprocesses, PE inspection or executable input. Exact built-in JSON
types only; callers keep the graph unchanged during a call. Bounds describe
explicit CPython 3.12 / 64-bit object storage, not process RSS or OS limits.
Nodes include keys and repeated aliases. Depth starts at one for the root.
Work charges node visits, input string characters, conservative key comparison
units and bounded byte passes. Decode and its canonical re-encode share work.
This is an accounting model, not an exhaustive instruction or stdlib-internal
pass counter: bounded token finding, slicing and unescaping use a token-byte
unit. A separately owned five-second admission deadline bounds completion.
"""

import json
import sys


CORE_LIMITS = dict(bytes=67108864, nodes=2097152, characters=33554432, depth=16,
                   string_characters=4096, key_characters=64, object_members=64,
                   array_items=65536, work_units=536870912, structure_bytes=536870912)
REPORT_LIMITS = dict(CORE_LIMITS, bytes=67125248, nodes=2101248,
                     characters=33570816, depth=18, structure_bytes=537919488)
MANIFEST_LIMITS = dict(CORE_LIMITS, bytes=32768, nodes=8192, characters=32768,
                       depth=8, array_items=256, work_units=4194304, structure_bytes=2097152)
MARKER_LIMITS = dict(CORE_LIMITS, bytes=1024, nodes=128, characters=1024, depth=4,
                     string_characters=1024, array_items=64, work_units=65536, structure_bytes=32768)
_NAMES = tuple(REPORT_LIMITS)
_CEILINGS = (67125248, 2101248, 33570816, 18, 4096, 64, 64, 65536, 536870912, 537919488)
_TYPES = (dict, list, str, int, bool, type(None))


class CodecError(ValueError):
    def __init__(self, reason):
        self.reason = reason
        super().__init__(reason)


def _require(condition, reason):
    if not condition:
        raise CodecError(reason)


def _limits(value):
    _require(type(value) is dict and set(value) == set(_NAMES), "JSON_LIMITS_INVALID")
    _require(all(type(value[key]) is int and 0 < value[key] <= ceiling
                 for key, ceiling in zip(_NAMES, _CEILINGS)), "JSON_LIMITS_INVALID")
    _require(sys.implementation.name == "cpython" and sys.version_info[:2] == (3, 12)
             and sys.maxsize == (1 << 63) - 1, "JSON_PLATFORM_UNSUPPORTED")
    return value.copy()


class _Budget:
    def __init__(self, limits):
        self.limits, self.work = limits, 0

    def spend(self, amount):
        _require(amount <= self.limits["work_units"] - self.work, "JSON_WORK_LIMIT")
        self.work += amount


class _Measure:
    def __init__(self, limits, budget):
        self.limits, self.budget = limits, budget
        self.size = self.nodes = self.characters = self.structure = 0
        self.active = set()

    def add(self, amount):
        _require(amount <= self.limits["bytes"] - self.size, "JSON_BYTE_LIMIT")
        self.size += amount

    def visit(self, value, depth=1, key=False):
        kind, limits = type(value), self.limits
        _require(kind in _TYPES, "JSON_TYPE_INVALID")
        _require(depth <= limits["depth"], "JSON_DEPTH_LIMIT")
        self.budget.spend(1)
        self.nodes += 1
        _require(self.nodes <= limits["nodes"], "JSON_NODE_LIMIT")
        self.structure += sys.getsizeof(value)
        _require(self.structure <= limits["structure_bytes"], "JSON_STRUCTURE_LIMIT")
        if kind is str:
            maximum = limits["key_characters" if key else "string_characters"]
            _require(len(value) <= maximum, "JSON_KEY_LIMIT" if key else "JSON_STRING_LIMIT")
            self.characters += len(value)
            _require(self.characters <= limits["characters"], "JSON_CHARACTER_LIMIT")
            self.budget.spend(len(value))
            # At most 12*4096+2 ASCII characters; never encodes the whole graph.
            self.add(len(json.encoder.encode_basestring_ascii(value)))
        elif kind is int:
            _require(-(1 << 63) <= value < (1 << 64), "JSON_INTEGER_RANGE")
            self.add(len(str(value)))
        elif kind in (bool, type(None)):
            self.add(4 if value is None or value is True else 5)
        else:
            _require(id(value) not in self.active, "JSON_CYCLE")
            self.active.add(id(value))
            if kind is dict:
                _require(len(value) <= limits["object_members"], "JSON_MEMBER_LIMIT")
                _require(all(type(item) is str for item in value), "JSON_TYPE_INVALID")
                longest = max(map(len, value), default=0)
                _require(longest <= limits["key_characters"], "JSON_KEY_LIMIT")
                self.budget.spend(len(value) * (len(value).bit_length() + 1) * longest)
                self.add(2 + len(value) + max(0, len(value) - 1))
                for name, item in value.items():
                    self.visit(name, depth + 1, key=True)
                    self.visit(item, depth + 1)
            else:
                _require(len(value) <= limits["array_items"], "JSON_ITEM_LIMIT")
                self.add(2 + max(0, len(value) - 1))
                for item in value:
                    self.visit(item, depth + 1)
            self.active.remove(id(value))


def _encode(value, limits, budget):
    before = budget.work
    measure = _Measure(limits, budget)
    measure.visit(value)
    # Reserve the second traversal/sort plus zero-fill, ASCII conversion,
    # slice copies and final immutable-bytes copy before allocating the buffer.
    budget.spend(budget.work - before + 4 * measure.size)
    output, position = bytearray(measure.size), 0
    encoder = json.JSONEncoder(ensure_ascii=True, allow_nan=False,
                               sort_keys=True, separators=(",", ":"))
    chunk_limit = 12 * max(limits["string_characters"], limits["key_characters"]) + 4
    try:
        for chunk in encoder.iterencode(value):
            _require(len(chunk) <= chunk_limit, "JSON_ENCODING_CHANGED")
            end = position + len(chunk)
            _require(end <= measure.size, "JSON_ENCODING_CHANGED")
            output[position:end] = chunk.encode("ascii")
            position = end
    except (TypeError, OverflowError, RecursionError, UnicodeError) as error:
        raise CodecError("JSON_ENCODING_CHANGED") from error
    _require(position == measure.size, "JSON_ENCODING_CHANGED")
    return bytes(output)


class _Scan:
    """Bounded predecode grammar scan; retains only at most depth small key sets."""
    def __init__(self, text, limits, budget):
        self.text, self.limits, self.budget = text, limits, budget
        self.position = self.nodes = self.characters = 0

    def enter(self, depth):
        _require(depth <= self.limits["depth"], "JSON_DEPTH_LIMIT")
        self.budget.spend(1)
        self.nodes += 1
        _require(self.nodes <= self.limits["nodes"], "JSON_NODE_LIMIT")

    def take(self, token):
        _require(self.text.startswith(token, self.position), "JSON_SYNTAX")
        self.budget.spend(len(token))
        self.position += len(token)

    def string(self, key=False):
        start, maximum = self.position, self.limits["key_characters" if key else "string_characters"]
        reason = "JSON_KEY_LIMIT" if key else "JSON_STRING_LIMIT"
        _require(self.text.startswith('"', start), "JSON_SYNTAX")
        stop = min(len(self.text), start + 12 * maximum + 2)
        cursor = start + 1
        while True:
            last = self.text.find('"', cursor, stop)
            _require(last >= 0, reason if stop < len(self.text) else "JSON_SYNTAX")
            slash = last
            while slash > start and self.text[slash - 1] == "\\":
                slash -= 1
            if (last - slash) % 2 == 0:
                break
            cursor = last + 1
        self.budget.spend(last + 1 - start)
        try:
            value, end = json.decoder.scanstring(self.text[start:last + 1], 1, True)
        except ValueError as error:
            raise CodecError("JSON_SYNTAX") from error
        _require(end == last + 1 - start, "JSON_SYNTAX")
        _require(len(value) <= maximum, reason)
        self.characters += len(value)
        _require(self.characters <= self.limits["characters"], "JSON_CHARACTER_LIMIT")
        self.position = last + 1
        return value

    def value(self, depth=1):
        self.enter(depth)
        _require(self.position < len(self.text), "JSON_SYNTAX")
        first = self.text[self.position]
        if first == '"':
            self.string()
        elif first == "{":
            self.take("{")
            keys = set()
            if self.text.startswith("}", self.position):
                self.take("}")
                return
            while True:
                _require(len(keys) < self.limits["object_members"], "JSON_MEMBER_LIMIT")
                self.enter(depth + 1)
                key = self.string(key=True)
                _require(key not in keys, "JSON_DUPLICATE_KEY")
                keys.add(key)
                self.take(":")
                self.value(depth + 1)
                if self.text.startswith("}", self.position):
                    self.take("}")
                    break
                self.take(",")
        elif first == "[":
            self.take("[")
            count = 0
            if self.text.startswith("]", self.position):
                self.take("]")
                return
            while True:
                _require(count < self.limits["array_items"], "JSON_ITEM_LIMIT")
                self.value(depth + 1)
                count += 1
                if self.text.startswith("]", self.position):
                    self.take("]")
                    break
                self.take(",")
        elif first in "tfn":
            self.take({"t": "true", "f": "false", "n": "null"}[first])
        else:
            start = self.position
            end = start + (first == "-")
            while end < len(self.text) and "0" <= self.text[end] <= "9":
                end += 1
                _require(end - start <= 21, "JSON_INTEGER_RANGE")
            token = self.text[start:end]
            _require(token not in ("", "-") and not (token.lstrip("-").startswith("0")
                     and len(token.lstrip("-")) > 1), "JSON_SYNTAX")
            self.budget.spend(len(token))
            value = int(token)
            _require(-(1 << 63) <= value < (1 << 64), "JSON_INTEGER_RANGE")
            self.position = end


def encode(value, *, limits=CORE_LIMITS):
    """Measure before buffer allocation; return canonical ASCII JSON bytes."""
    limits = _limits(limits)
    return _encode(value, limits, _Budget(limits))


def decode(raw, *, limits=CORE_LIMITS):
    """Preflight before json.loads, then same-codec canonical validation."""
    limits = _limits(limits)
    _require(type(raw) is bytes, "JSON_BYTES_REQUIRED")
    _require(len(raw) <= limits["bytes"], "JSON_BYTE_LIMIT")
    budget = _Budget(limits)
    budget.spend(len(raw))  # Bounded ASCII text copy.
    try:
        text = raw.decode("ascii")
    except UnicodeError as error:
        raise CodecError("JSON_SYNTAX") from error
    scan = _Scan(text, limits, budget)
    scan.value()
    _require(scan.position == len(text), "JSON_SYNTAX")
    # For fresh exact CPython JSON objects: container bases <=128, dict entries
    # <=64 per member, list slots <=16; strings <=80+4*characters, scalars <=48.
    # Each edge is charged at most64, hence this deliberately looser tree bound.
    _require(192 * scan.nodes + 4 * scan.characters <= limits["structure_bytes"],
             "JSON_STRUCTURE_LIMIT")
    budget.spend(len(raw) + scan.nodes)  # JSON construction byte/node pass.
    try:
        value = json.loads(text)
    except ValueError as error:
        raise CodecError("JSON_SYNTAX") from error
    canonical = _encode(value, limits, budget)  # Same cumulative work budget.
    _require(canonical == raw, "JSON_NON_CANONICAL")
    return value
