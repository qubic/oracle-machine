#!/usr/bin/env python3
"""Mock bob JSON-RPC node for the read_qubic_log selftest.
Serves canned qubic_getTransactionReceipt responses, keyed by the 60-char lowercase Qubic
identity of the tx hash (like real bob). rawData mirrors real bob: for contract-emitted log
types it INCLUDES the core-stamped 8-byte prefix (contractIndex u32 LE | type u32 LE).
--tamper flips one payload byte.
"""

import argparse
import json
import os
import struct
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from read_qubic_log_service import qubic_identity

TX_A = qubic_identity(bytes.fromhex("aa" * 31 + "01"))  # executed, 2 logs
TX_C = qubic_identity(bytes.fromhex("aa" * 31 + "03"))  # failed
TX_P = qubic_identity(bytes.fromhex("aa" * 31 + "05"))  # pending
TX_E = qubic_identity(bytes.fromhex("aa" * 31 + "06"))  # executed, oversized log body

# Contract-emitted body = 8-byte core prefix (contractIndex 29 | contract-defined type 1) + payload
PREFIX = struct.pack("<II", 29, 1).hex()
PAYLOAD_A = "51524944" + "47455f57" + "cc" * 120  # sample 128-byte contract payload


def receipts(tamper: bool):
    payload = PAYLOAD_A if not tamper else PAYLOAD_A[:-2] + "dd"
    return {
        TX_A: {
            "hash": TX_A,
            "tick": 12345678,
            "epoch": 230,
            "status": "success",
            "executed": True,
            "logs": [
                {
                    "logId": 6390173,
                    "logIndex": 0,
                    "logType": 0,
                    "logTypeName": "QU_TRANSFER",
                    "rawData": "0x" + "ab" * 72,
                },
                {
                    "logId": 6390174,
                    "logIndex": 1,
                    "logType": 6,
                    "logTypeName": "CONTRACT_INFO",
                    "contractIndex": 29,
                    "rawData": "0x" + PREFIX + payload,
                },
            ],
            "logCount": 2,
        },
        TX_C: {
            "hash": TX_C,
            "tick": 12345678,
            "epoch": 230,
            "status": "failed",
            "executed": False,
            "logs": [],
            "logCount": 0,
        },
        TX_P: {
            "hash": TX_P,
            "tick": 12345699,
            "epoch": 230,
            "status": "pending",
            "executed": None,
            "logs": [],
            "logCount": 0,
        },
        TX_E: {
            "hash": TX_E,
            "tick": 12345678,
            "epoch": 230,
            "status": "success",
            "executed": True,
            "logs": [
                {
                    "logId": 6390200,
                    "logIndex": 0,
                    "logType": 6,
                    "logTypeName": "CONTRACT_INFO",
                    "contractIndex": 29,
                    "rawData": "0x" + PREFIX + "ee" * 300,  # 308-byte body > 256 cap
                },
            ],
            "logCount": 1,
        },
    }


class Handler(BaseHTTPRequestHandler):
    tamper = False

    def log_message(self, fmt, *args):  # quiet
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        request = json.loads(self.rfile.read(length))
        method = request.get("method")
        params = request.get("params", [])
        result = None
        if method == "qubic_getTransactionReceipt":
            result = receipts(self.tamper).get(params[0])
        body = json.dumps({"jsonrpc": "2.0", "id": request.get("id"), "result": result}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--tamper", action="store_true")
    args = parser.parse_args()
    Handler.tamper = args.tamper
    HTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
