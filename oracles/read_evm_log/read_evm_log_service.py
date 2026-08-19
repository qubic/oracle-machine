#!/usr/bin/env python3
"""read_evm_log oracle service (interface index 3): generic single-log EVM read.

Query 48B: chainId u64 LE | txHash 32B | logIndex u64 LE (receipt-local).
Reply 440B: code u64 | address 32B | topicCount u64 | topics 4x32B | dataLen u64 | data 256B.
Only finalized logs are answered; all configured RPC endpoints must agree byte-for-byte,
otherwise the service abstains (oracle error flag, no reply payload).

Speaks the OM node <-> service protocol of oracles/core/base_oracle_service.cpp:
  query:  header 8B (size LE24 | type=190 | dejavu u32) | OracleMachineQuery 16B | payload 48B
  reply:  header 8B (size LE24 | type=191 | dejavu=0)   | OracleMachineReply 16B | payload 440B

Env: READ_EVM_LOG_SERVICE_PORT (default 9005), EVM_RPC_URLS_<chainId> (comma-separated),
EVM_MIN_CONFIRMATIONS_<chainId> (optional: latest-N instead of the "finalized" tag).
"""

import json
import os
import socket
import socketserver
import struct
import sys
import threading
import urllib.request

QUERY_SIZE = 48
REPLY_SIZE = 440
OM_QUERY_TYPE = 190
OM_REPLY_TYPE = 191
MAX_RPC_RESPONSE_BYTES = 8 * 1024 * 1024
RPC_TIMEOUT_S = 10

# Result codes (mirror core's OI::EvmLogRead)
SUCCESS = 0
BAD_QUERY = 1
CHAIN_UNSUPPORTED = 2
TX_NOT_FOUND = 3
TX_NOT_FINALIZED = 4
LOG_INDEX_OUT_OF_RANGE = 5
LOG_DATA_TOO_LARGE = 6

# Oracle error flags (mirror core's network_messages/common_def.h)
NO_ERROR = 0x0
ORACLE_UNAVAIL = 0x2
INVALID_ARG = 0x10

SUPPORTED_CHAIN_IDS = [1, 10, 56, 137, 250, 8453, 43114, 42161, 11155111]
INTERFACE_INDEX = 3


def hexb(s):
    """0x-prefixed hex string -> bytes; raises on anything else."""
    if not isinstance(s, str) or not s.startswith("0x"):
        raise ValueError(f"expected 0x-prefixed hex, got {s!r}")
    return bytes.fromhex(s[2:])


def load_chains():
    chains = {}
    for chain_id in SUPPORTED_CHAIN_IDS:
        urls = os.environ.get(f"EVM_RPC_URLS_{chain_id}", "")
        endpoints = [u.strip() for u in urls.split(",") if u.strip()]
        if not endpoints:
            continue
        conf = os.environ.get(f"EVM_MIN_CONFIRMATIONS_{chain_id}", "")
        chains[chain_id] = {
            "endpoints": endpoints,
            "min_confirmations": max(0, int(conf)) if conf else 0,
        }
    return chains


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


def normalize_receipt(result):
    """Canonical receipt: None if not found; raises on malformed data."""
    if result is None:
        return None
    receipt = {
        "status": int(result["status"], 16),
        "blockNumber": int(result["blockNumber"], 16),
        "blockHash": hexb(result["blockHash"]),
        "logs": [
            {
                "address": hexb(log["address"]),
                "topics": [hexb(t) for t in log["topics"]],
                "data": hexb(log["data"]),
            }
            for log in result["logs"]
        ],
    }
    for log in receipt["logs"]:
        if len(log["address"]) != 20 or any(len(t) != 32 for t in log["topics"]):
            raise ValueError("malformed log entry (address/topic length)")
    return receipt


def finalized_head(endpoint, chain):
    if chain["min_confirmations"]:
        latest = rpc(endpoint, "eth_getBlockByNumber", ["latest", False])
        return int(latest["number"], 16) - chain["min_confirmations"]
    block = rpc(endpoint, "eth_getBlockByNumber", ["finalized", False])
    return int(block["number"], 16)


def get_receipt(chain, tx_hash_hex):
    """All-endpoints byte-agreement receipt fetch. Returns (receipt|None, finalized)."""
    receipt = None
    finalized = True
    for i, endpoint in enumerate(chain["endpoints"]):
        one = normalize_receipt(rpc(endpoint, "eth_getTransactionReceipt", [tx_hash_hex]))
        if i == 0:
            receipt = one
        elif one != receipt:
            raise ValueError("provider disagreement")
        if one is not None and one["blockNumber"] > finalized_head(endpoint, chain):
            finalized = False
    if receipt is None:
        finalized = False
    return receipt, finalized


def code_reply(code):
    return struct.pack("<Q", code) + bytes(REPLY_SIZE - 8)


def handle_query(chains, payload):
    """48B query payload -> (errorFlags, 440B reply payload). Abstains via non-zero flags."""
    if len(payload) != QUERY_SIZE:
        return INVALID_ARG, bytes(REPLY_SIZE)
    chain_id, tx_hash, log_index = struct.unpack("<Q32sQ", payload)

    if tx_hash == bytes(32):
        return NO_ERROR, code_reply(BAD_QUERY)
    if chain_id not in chains:
        return NO_ERROR, code_reply(CHAIN_UNSUPPORTED)

    try:
        receipt, finalized = get_receipt(chains[chain_id], "0x" + tx_hash.hex())
    except Exception as e:
        print(f"[ReadEvmLog] abstain: {e}", file=sys.stderr)
        return ORACLE_UNAVAIL, bytes(REPLY_SIZE)

    if receipt is None:
        return NO_ERROR, code_reply(TX_NOT_FOUND)
    if not finalized or receipt["status"] != 1:
        return NO_ERROR, code_reply(TX_NOT_FINALIZED)
    if log_index >= len(receipt["logs"]):
        return NO_ERROR, code_reply(LOG_INDEX_OUT_OF_RANGE)

    log = receipt["logs"][log_index]
    if len(log["data"]) > 256:
        return NO_ERROR, code_reply(LOG_DATA_TOO_LARGE)
    if len(log["topics"]) > 4:
        return ORACLE_UNAVAIL, bytes(REPLY_SIZE)  # impossible on honest EVM (LOG0..LOG4)

    reply = struct.pack("<Q", SUCCESS)
    reply += bytes(12) + log["address"]  # 20-byte emitter right-aligned into 32 bytes
    reply += struct.pack("<Q", len(log["topics"]))
    for topic in log["topics"]:
        reply += topic
    reply += bytes(32) * (4 - len(log["topics"]))
    reply += struct.pack("<Q", len(log["data"]))
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
                flags, reply_payload = handle_query(self.server.chains, body[16:])
            except Exception as e:  # structural backstop: never kill the OM connection
                print(f"[ReadEvmLog] abstain (unexpected): {e}", file=sys.stderr)
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
    chains = load_chains()
    if not chains:
        print("No chains configured! Set EVM_RPC_URLS_<chainId>.", file=sys.stderr)
        return 1
    port = int(os.environ.get("READ_EVM_LOG_SERVICE_PORT", "9005"))
    for chain_id, chain in chains.items():
        finality = (f"{chain['min_confirmations']} confirmations"
                    if chain["min_confirmations"] else '"finalized" tag')
        print(f"[ReadEvmLog] chain {chain_id}: {len(chain['endpoints'])} endpoint(s), {finality}")
    server = Server(("0.0.0.0", port), Handler)
    server.chains = chains
    print(f"[ReadEvmLog] listening on {port}")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
