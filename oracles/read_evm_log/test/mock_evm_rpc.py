#!/usr/bin/env python3
"""Minimal mock EVM JSON-RPC server for the read_evm_log oracle service selftest.

Serves canned eth_getTransactionReceipt / eth_getBlockByNumber / eth_getBlockByHash
responses. Run one instance per fake "provider"; --tamper alters one data word so
provider-disagreement handling can be tested.

Usage: mock_evm_rpc.py --port 18545 [--tamper]
"""

import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer

# keccak256("Transfer(address,address,uint256)") — a generic, standard EVM log used purely as
# representative raw-bytes test data. The service is contract-agnostic and reads any log verbatim.
TRANSFER_TOPIC = "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"

EMITTER = "0xdac17f958d2ee523a2206206994597c13d831ec7"
FROM_PADDED = "0x" + "00" * 12 + "aa" * 20
TO_PADDED = "0x" + "00" * 12 + "bb" * 20

TX_A = "0x" + "aa" * 31 + "01"  # finalized, one Transfer log
TX_C = "0x" + "aa" * 31 + "03"  # NOT yet finalized
TX_D = "0x" + "aa" * 31 + "04"  # reverted
TX_E = "0x" + "aa" * 31 + "06"  # finalized, log data > 256 bytes
TX_F = "0x" + "aa" * 31 + "07"  # finalized, malformed 5-topic log

FINALIZED_HEAD = "0x1ff"

VALUE_1M = "0x" + "00" * 28 + "000f4240"  # 1_000_000 as a 32-byte word


def receipts(tamper: bool):
    value = VALUE_1M if not tamper else "0x" + "00" * 28 + "000f4241"
    return {
        TX_A: {
            "status": "0x1",
            "blockNumber": "0x100",
            "blockHash": "0x" + "cc" * 32,
            "logs": [
                {
                    "address": EMITTER,
                    "topics": [TRANSFER_TOPIC, FROM_PADDED, TO_PADDED],
                    "data": value,
                },
            ],
        },
        TX_C: {
            "status": "0x1",
            "blockNumber": "0x300",  # beyond the finalized head 0x1ff
            "blockHash": "0x" + "ce" * 32,
            "logs": [
                {
                    "address": EMITTER,
                    "topics": [TRANSFER_TOPIC, FROM_PADDED, TO_PADDED],
                    "data": VALUE_1M,
                },
            ],
        },
        TX_E: {
            "status": "0x1",
            "blockNumber": "0x100",
            "blockHash": "0x" + "d0" * 32,
            "logs": [
                {
                    "address": EMITTER,
                    "topics": [TRANSFER_TOPIC],
                    "data": "0x" + "ee" * 300,
                },
            ],
        },
        TX_F: {
            "status": "0x1",
            "blockNumber": "0x100",
            "blockHash": "0x" + "d1" * 32,
            "logs": [
                {
                    "address": EMITTER,
                    "topics": [TRANSFER_TOPIC, FROM_PADDED, TO_PADDED, FROM_PADDED, TO_PADDED],
                    "data": VALUE_1M,
                },
            ],
        },
        TX_D: {
            "status": "0x0",  # reverted (finalized block, provider still lists a log)
            "blockNumber": "0x100",
            "blockHash": "0x" + "cd" * 32,
            "logs": [
                {
                    "address": EMITTER,
                    "topics": [TRANSFER_TOPIC, FROM_PADDED, TO_PADDED],
                    "data": VALUE_1M,
                },
            ],
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

        if method == "eth_getTransactionReceipt":
            result = receipts(self.tamper).get(params[0].lower())
        elif method == "eth_getBlockByNumber":
            if params[0] in ("finalized", "latest"):
                result = {"number": FINALIZED_HEAD, "timestamp": "0x64000000"}
        elif method == "eth_getBlockByHash":
            result = {"number": "0x100", "timestamp": "0x64000000"}

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
