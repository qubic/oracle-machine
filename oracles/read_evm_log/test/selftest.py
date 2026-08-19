#!/usr/bin/env python3
"""Selftest for read_evm_log_service.py against mock_evm_rpc.py. Run:
    python3 test/mock_evm_rpc.py --port 18545 &
    python3 test/mock_evm_rpc.py --port 18546 &
    python3 test/mock_evm_rpc.py --port 18547 --tamper &
    python3 test/selftest.py           (exit 0 = all pass)
"""

import os
import struct
import sys
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))

import read_evm_log_service as svc

TX_A = bytes.fromhex("aa" * 31 + "01")  # finalized, one Transfer log
TX_B = bytes.fromhex("aa" * 31 + "02")  # unknown
TX_C = bytes.fromhex("aa" * 31 + "03")  # not yet finalized
TX_E = bytes.fromhex("aa" * 31 + "06")  # oversized log data
TX_F = bytes.fromhex("aa" * 31 + "07")  # malformed 5-topic log
TX_D = bytes.fromhex("aa" * 31 + "04")  # reverted
TRANSFER_TOPIC = bytes.fromhex("ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef")
EMITTER = bytes.fromhex("dac17f958d2ee523a2206206994597c13d831ec7")
VALUE_1M = bytes(28) + bytes.fromhex("000f4240")

failures = 0


def check(cond, what):
    global failures
    print(f"  [{'PASS' if cond else 'FAIL'}] {what}")
    if not cond:
        failures += 1


def query(chain_id, tx_hash, log_index):
    return struct.pack("<Q32sQ", chain_id, tx_hash, log_index)


def main():
    os.environ["EVM_RPC_URLS_1"] = "http://127.0.0.1:18545,http://127.0.0.1:18546"
    chains = svc.load_chains()

    print("SUCCESS (raw log echoed):")
    flags, r = svc.handle_query(chains, query(1, TX_A, 0))
    check(flags == svc.NO_ERROR, "finalized tx: service answers")
    check(len(r) == 440, "reply is 440 bytes")
    check(struct.unpack("<Q", r[0:8])[0] == svc.SUCCESS, "code == SUCCESS")
    check(r[8:40] == bytes(12) + EMITTER, "emitter right-aligned, upper 12 zero")
    check(struct.unpack("<Q", r[40:48])[0] == 3, "topicCount == 3")
    check(r[48:80] == TRANSFER_TOPIC, "topics[0] byte-exact")
    check(r[80:112] == bytes(12) + bytes.fromhex("aa" * 20), "topics[1] byte-exact")
    check(r[112:144] == bytes(12) + bytes.fromhex("bb" * 20), "topics[2] byte-exact")
    check(r[144:176] == bytes(32), "unused topics[3] zero-padded")
    check(struct.unpack("<Q", r[176:184])[0] == 32, "dataLen == 32")
    check(r[184:216] == VALUE_1M, "data byte-exact")
    check(r[216:440] == bytes(224), "data tail zero-padded")

    print("Result codes:")
    flags, r = svc.handle_query(chains, query(1, TX_B, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.TX_NOT_FOUND, "unknown tx: code 3")
    flags, r = svc.handle_query(chains, query(1, TX_C, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.TX_NOT_FINALIZED, "unfinalized tx: code 4")
    flags, r = svc.handle_query(chains, query(1, TX_D, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.TX_NOT_FINALIZED and r[8:] == bytes(432),
          "reverted tx: code 4, log never served")
    flags, r = svc.handle_query(chains, query(1, TX_A, 5))
    check(flags == svc.NO_ERROR and r[0] == svc.LOG_INDEX_OUT_OF_RANGE, "log index 5: code 5")
    flags, r = svc.handle_query(chains, query(2, TX_A, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.CHAIN_UNSUPPORTED, "chain 2: code 2")
    flags, r = svc.handle_query(chains, query(1, bytes(32), 0))
    check(flags == svc.NO_ERROR and r[0] == svc.BAD_QUERY, "zero tx hash: code 1")
    flags, r = svc.handle_query(chains, query(1, TX_E, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.LOG_DATA_TOO_LARGE, "oversized data: code 6")

    print("Malformed 5-topic log (abstain):")
    flags, r = svc.handle_query(chains, query(1, TX_F, 0))
    check(flags != svc.NO_ERROR, "5-topic log: no answer (oracle error flag)")

    print("Provider agreement (abstain):")
    os.environ["EVM_RPC_URLS_1"] = "http://127.0.0.1:18545,http://127.0.0.1:18547"
    flags, r = svc.handle_query(svc.load_chains(), query(1, TX_A, 0))
    check(flags != svc.NO_ERROR, "tampered provider: no answer (oracle error flag)")

    print("Endpoint down (abstain):")
    os.environ["EVM_RPC_URLS_1"] = "http://127.0.0.1:18545,http://127.0.0.1:1"
    flags, r = svc.handle_query(svc.load_chains(), query(1, TX_A, 0))
    check(flags != svc.NO_ERROR, "dead endpoint: no answer (oracle error flag)")

    print(f"{'SELFTEST FAILED' if failures else 'SELFTEST PASSED'} ({failures} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
