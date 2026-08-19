#!/usr/bin/env python3
"""read_qubic_log oracle service (interface index 4): generic single-log Qubic read.

Query 48B: tick u64 LE | txHash 32B | logId u64 LE (GLOBAL log id, counted from the chain's
first log event; matched against the logId field of each log in the tx receipt).
Reply 288B: code u64 | contractIndex u64 | logType u64 | dataLen u64 | data 256B.
Reads transaction receipts from per-operator bob nodes (qubic_getTransactionReceipt); all
configured bobs must agree byte-for-byte, otherwise the service abstains.

Speaks the OM node <-> service protocol of oracles/core/base_oracle_service.cpp:
  query:  header 8B (size LE24 | type=190 | dejavu u32) | OracleMachineQuery 16B | payload 48B
  reply:  header 8B (size LE24 | type=191 | dejavu=0)   | OracleMachineReply 16B | payload 288B

Env: READ_QUBIC_LOG_SERVICE_PORT (default 9006), BOB_URLS (comma-separated bob RPC URLs).
"""

import json
import os
import socketserver
import struct
import sys
import urllib.request

QUERY_SIZE = 48
REPLY_SIZE = 288
OM_QUERY_TYPE = 190
OM_REPLY_TYPE = 191
MAX_RPC_RESPONSE_BYTES = 8 * 1024 * 1024
RPC_TIMEOUT_S = 10

# Result codes (mirror core's OI::QubicLogRead)
SUCCESS = 0
BAD_QUERY = 1
TX_NOT_FOUND = 2
TX_NOT_EXECUTED = 3
TICK_MISMATCH = 4
LOG_NOT_FOUND = 5
LOG_DATA_TOO_LARGE = 6

# Oracle error flags (mirror core's network_messages/common_def.h)
NO_ERROR = 0x0
ORACLE_UNAVAIL = 0x2
INVALID_ARG = 0x10

INTERFACE_INDEX = 4
U64_MAX = (1 << 64) - 1


# --- KangarooTwelve (Keccak-p[1600,12]) + Qubic identity encoding -------------------------------
# Qubic addresses 32-byte digests as 60-char base-26 identities (core/src/four_q.h getIdentity);
# tx hashes use the lowercase form. Verified byte-identical to core's implementation.

_K12_RC = [0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
           0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
           0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008]
_K12_ROT = [[0, 36, 3, 41, 18], [1, 44, 10, 45, 2], [62, 6, 43, 15, 61],
            [28, 55, 25, 21, 56], [27, 20, 39, 8, 14]]
_MASK64 = (1 << 64) - 1


def _rol(x, n):
    n %= 64
    return ((x << n) | (x >> (64 - n))) & _MASK64


def _keccak_p1600_12(lanes):
    for rnd in range(12):
        c = [lanes[x][0] ^ lanes[x][1] ^ lanes[x][2] ^ lanes[x][3] ^ lanes[x][4] for x in range(5)]
        d = [c[(x - 1) % 5] ^ _rol(c[(x + 1) % 5], 1) for x in range(5)]
        for x in range(5):
            for y in range(5):
                lanes[x][y] ^= d[x]
        b = [[0] * 5 for _ in range(5)]
        for x in range(5):
            for y in range(5):
                b[y][(2 * x + 3 * y) % 5] = _rol(lanes[x][y], _K12_ROT[x][y])
        for x in range(5):
            for y in range(5):
                lanes[x][y] = b[x][y] ^ ((~b[(x + 1) % 5][y]) & b[(x + 2) % 5][y])
        lanes[0][0] ^= _K12_RC[rnd]


def k12(data, out_len):
    """KangarooTwelve for single-chunk inputs (ours are 32 bytes)."""
    s = data + b"\x00"  # right_encode(0) for the empty customization string
    rate = 168
    lanes = [[0] * 5 for _ in range(5)]
    padded = bytearray(s + b"\x07")
    padded += bytes(-len(padded) % rate)
    padded[-1] |= 0x80
    for block_start in range(0, len(padded), rate):
        for i in range(rate // 8):
            lane = int.from_bytes(padded[block_start + i * 8:block_start + i * 8 + 8], "little")
            lanes[i % 5][i // 5] ^= lane
        _keccak_p1600_12(lanes)
    out = b""
    while len(out) < out_len:
        for i in range(rate // 8):
            out += lanes[i % 5][i // 5].to_bytes(8, "little")
    return out[:out_len]


def qubic_identity(digest32, lower=True):
    """32-byte digest -> 60-char base-26 identity (lowercase for tx hashes)."""
    base = ord("a") if lower else ord("A")
    chars = []
    for i in range(4):
        frag = int.from_bytes(digest32[i * 8:(i + 1) * 8], "little")
        for _ in range(14):
            chars.append(chr(base + frag % 26))
            frag //= 26
    checksum = int.from_bytes(k12(digest32, 3), "little") & 0x3FFFF
    for _ in range(4):
        chars.append(chr(base + checksum % 26))
        checksum //= 26
    return "".join(chars)


def load_bobs():
    return [u.strip() for u in os.environ.get("BOB_URLS", "").split(",") if u.strip()]


def rpc(endpoint, method, params):
    """Single JSON-RPC call; returns the result field. Raises on any transport/JSON error."""
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode()
    req = urllib.request.Request(endpoint, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=RPC_TIMEOUT_S) as resp:
        raw = resp.read(MAX_RPC_RESPONSE_BYTES + 1)
    if len(raw) > MAX_RPC_RESPONSE_BYTES:
        raise ValueError("response too large")
    reply = json.loads(raw)
    if "result" not in reply:
        raise ValueError(f"rpc error: {reply.get('error')}")
    return reply["result"]


def hexb(s):
    """0x-prefixed hex string -> bytes; raises on anything else."""
    if not isinstance(s, str) or not s.startswith("0x"):
        raise ValueError(f"expected 0x-prefixed hex, got {s!r}")
    return bytes.fromhex(s[2:])


def u64(v):
    v = int(v)
    if not 0 <= v <= U64_MAX:
        raise ValueError(f"value out of u64 range: {v}")
    return v


def normalize_receipt(result):
    """Canonical receipt: None if not found; raises on malformed data."""
    if result is None:
        return None
    return {
        "tick": u64(result["tick"]),
        "status": str(result["status"]),
        "logs": [
            {
                "logId": u64(log["logId"]),
                "logType": u64(log["logType"]),
                "contractIndex": u64(log.get("contractIndex", 0)),
                "data": hexb(log["rawData"]),
            }
            for log in result["logs"]
        ],
    }


def get_receipt(bobs, tx_hash_hex):
    """All-bobs byte-agreement receipt fetch. Returns the canonical receipt or None."""
    receipt = None
    for i, endpoint in enumerate(bobs):
        one = normalize_receipt(rpc(endpoint, "qubic_getTransactionReceipt", [tx_hash_hex]))
        if i == 0:
            receipt = one
        elif one != receipt:
            raise ValueError("bob disagreement")
    return receipt


def code_reply(code):
    return struct.pack("<Q", code) + bytes(REPLY_SIZE - 8)


def handle_query(bobs, payload):
    """48B query payload -> (errorFlags, 288B reply payload). Abstains via non-zero flags."""
    if len(payload) != QUERY_SIZE:
        return INVALID_ARG, bytes(REPLY_SIZE)
    tick, tx_hash, log_id = struct.unpack("<Q32sQ", payload)

    if tx_hash == bytes(32) or tick == 0:
        return NO_ERROR, code_reply(BAD_QUERY)
    if not bobs:
        return ORACLE_UNAVAIL, bytes(REPLY_SIZE)

    try:
        # Qubic addresses tx hashes as 60-char lowercase identities, not hex.
        receipt = get_receipt(bobs, qubic_identity(tx_hash, lower=True))
    except Exception as e:
        print(f"[ReadQubicLog] abstain: {e}", file=sys.stderr)
        return ORACLE_UNAVAIL, bytes(REPLY_SIZE)

    if receipt is None:
        return NO_ERROR, code_reply(TX_NOT_FOUND)
    if receipt["status"] != "success":
        return NO_ERROR, code_reply(TX_NOT_EXECUTED)
    if receipt["tick"] != tick:
        return NO_ERROR, code_reply(TICK_MISMATCH)

    # logId is a GLOBAL counter: scan the tx's logs for the matching one (never index by position)
    log = next((entry for entry in receipt["logs"] if entry["logId"] == log_id), None)
    if log is None:
        return NO_ERROR, code_reply(LOG_NOT_FOUND)
    if len(log["data"]) > 256:
        return NO_ERROR, code_reply(LOG_DATA_TOO_LARGE)

    reply = struct.pack("<QQQQ", SUCCESS, log["contractIndex"], log["logType"], len(log["data"]))
    reply += log["data"] + bytes(256 - len(log["data"]))
    assert len(reply) == REPLY_SIZE
    return NO_ERROR, reply


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        while True:
            header = recv_exact(self.request, 8)
            if header is None:
                return
            size = header[0] | (header[1] << 8) | (header[2] << 16)
            msg_type = header[3]
            body = recv_exact(self.request, size - 8)
            if body is None:
                return
            if msg_type != OM_QUERY_TYPE or len(body) != 16 + QUERY_SIZE:
                continue  # skip unrelated frames
            query_id, interface_index = struct.unpack("<QI", body[:12])
            if interface_index != INTERFACE_INDEX:
                continue  # not our interface (misrouted config)
            try:
                flags, reply_payload = handle_query(self.server.bobs, body[16:])
            except Exception as e:  # structural backstop: never kill the OM connection
                print(f"[ReadQubicLog] abstain (unexpected): {e}", file=sys.stderr)
                flags, reply_payload = ORACLE_UNAVAIL, bytes(REPLY_SIZE)

            reply_size = 8 + 16 + REPLY_SIZE
            packet = bytes([reply_size & 0xFF, (reply_size >> 8) & 0xFF,
                            (reply_size >> 16) & 0xFF, OM_REPLY_TYPE]) + bytes(4)
            packet += struct.pack("<QH6x", query_id, flags)
            packet += reply_payload
            self.request.sendall(packet)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


def main():
    args = sys.argv[1:]
    if args[:1] == ["--log"] and len(args) == 2:
        log = open(args[1], "a", buffering=1)
        sys.stdout = sys.stderr = log
    elif args:
        print(f"usage: {sys.argv[0]} [--log FILE]")
        return 0 if args[:1] == ["--help"] else 1

    urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
    bobs = load_bobs()
    if not bobs:
        print("No bob nodes configured! Set BOB_URLS.", file=sys.stderr)
        return 1
    port = int(os.environ.get("READ_QUBIC_LOG_SERVICE_PORT", "9006"))
    print(f"[ReadQubicLog] {len(bobs)} bob node(s)")
    server = Server(("0.0.0.0", port), Handler)
    server.bobs = bobs
    print(f"[ReadQubicLog] listening on {port}")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
