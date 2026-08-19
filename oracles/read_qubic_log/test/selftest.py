#!/usr/bin/env python3
"""Selftest for read_qubic_log_service.py against mock_bob.py. Run:
    python3 test/mock_bob.py --port 28545 &
    python3 test/mock_bob.py --port 28546 &
    python3 test/mock_bob.py --port 28547 --tamper &
    python3 test/selftest.py           (exit 0 = all pass)
"""

import os
import struct
import sys
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))

import read_qubic_log_service as svc

TX_A = bytes.fromhex("aa" * 31 + "01")  # executed, 2 logs
TX_B = bytes.fromhex("aa" * 31 + "02")  # unknown
TX_C = bytes.fromhex("aa" * 31 + "03")  # failed
TX_P = bytes.fromhex("aa" * 31 + "05")  # pending
TX_E = bytes.fromhex("aa" * 31 + "06")  # oversized log body
TICK = 12345678
import struct as _struct
PREFIX = _struct.pack("<II", 29, 1)  # core-stamped body prefix (contractIndex | type)
PAYLOAD_A = PREFIX + bytes.fromhex("51524944" + "47455f57" + "cc" * 120)

failures = 0


def check(cond, what):
    global failures
    print(f"  [{'PASS' if cond else 'FAIL'}] {what}")
    if not cond:
        failures += 1


def query(tick, tx_hash, log_id):
    return struct.pack("<Q32sQ", tick, tx_hash, log_id)


def main():
    bobs = ["http://127.0.0.1:28545", "http://127.0.0.1:28546"]

    print("SUCCESS (raw contract log echoed):")
    flags, r = svc.handle_query(bobs, query(TICK, TX_A, 6390174))
    check(flags == svc.NO_ERROR, "executed tx: service answers")
    check(len(r) == 288, "reply is 288 bytes")
    code, contract_index, log_type, data_len = struct.unpack("<QQQQ", r[:32])
    check(code == svc.SUCCESS, "code == SUCCESS")
    check(contract_index == 29, "contractIndex == 29 (core-stamped emitter)")
    check(log_type == 6, "logType == CONTRACT_INFORMATION_MESSAGE")
    check(data_len == 136, "dataLen == 136 (8B core prefix + 128B payload)")
    check(r[32:168] == PAYLOAD_A, "body byte-exact incl core-stamped prefix")
    check(r[32:36] == _struct.pack("<I", 29), "body prefix contractIndex == 29")
    check(r[168:288] == bytes(120), "data tail zero-padded")

    print("Log 0 is the plain QU_TRANSFER (distinguishable):")
    flags, r = svc.handle_query(bobs, query(TICK, TX_A, 6390173))
    code, contract_index, log_type, data_len = struct.unpack("<QQQQ", r[:32])
    check(flags == svc.NO_ERROR and code == svc.SUCCESS, "answers")
    check(log_type == 0 and contract_index == 0, "logType QU_TRANSFER, no contract emitter")

    print("Result codes:")
    flags, r = svc.handle_query(bobs, query(TICK, TX_B, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.TX_NOT_FOUND, "unknown tx: code 2")
    flags, r = svc.handle_query(bobs, query(TICK, TX_C, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.TX_NOT_EXECUTED and r[8:] == bytes(280),
          "failed tx: code 3, nothing served")
    flags, r = svc.handle_query(bobs, query(TICK, TX_P, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.TX_NOT_EXECUTED, "pending tx: code 3")
    flags, r = svc.handle_query(bobs, query(TICK + 1, TX_A, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.TICK_MISMATCH, "wrong tick: code 4")
    flags, r = svc.handle_query(bobs, query(TICK, TX_A, 999999))
    check(flags == svc.NO_ERROR and r[0] == svc.LOG_NOT_FOUND, "unknown logId: code 5")
    flags, r = svc.handle_query(bobs, query(TICK, bytes(32), 0))
    check(flags == svc.NO_ERROR and r[0] == svc.BAD_QUERY, "zero tx hash: code 1")
    flags, r = svc.handle_query(bobs, query(0, TX_A, 0))
    check(flags == svc.NO_ERROR and r[0] == svc.BAD_QUERY, "zero tick: code 1")
    flags, r = svc.handle_query(bobs, query(TICK, TX_E, 6390200))
    check(flags == svc.NO_ERROR and r[0] == svc.LOG_DATA_TOO_LARGE, "oversized body: code 6")

    print("Bob agreement (abstain):")
    flags, r = svc.handle_query(["http://127.0.0.1:28545", "http://127.0.0.1:28547"],
                                query(TICK, TX_A, 6390174))
    check(flags != svc.NO_ERROR, "tampered bob: no answer (oracle error flag)")

    print("Bob down (abstain):")
    flags, r = svc.handle_query(["http://127.0.0.1:28545", "http://127.0.0.1:1"],
                                query(TICK, TX_A, 6390174))
    check(flags != svc.NO_ERROR, "dead bob: no answer (oracle error flag)")

    print(f"{'SELFTEST FAILED' if failures else 'SELFTEST PASSED'} ({failures} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
