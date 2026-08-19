# read_qubic_log oracle service

Generic **Qubic &rarr; anywhere** oracle. Given `(tick, txHash, logId)` it returns exactly one
Qubic log event — type, emitting contract index and raw body — as **raw bytes**, mirroring
`read_evm_log`. No semantics are baked in; the querying smart contract decodes its own layout.

Implemented in Python (`read_qubic_log_service.py`, stdlib only). It listens on
`READ_QUBIC_LOG_SERVICE_PORT` and speaks the OM node <-> service protocol; it reads transaction
receipts from **per-operator bob nodes** (`qubic_getTransactionReceipt`).

## Wire format

Mirrors core's `OI::QubicLogRead` (`src/oracle_interfaces/QubicLogRead.h`).

- **Query — fixed 48 bytes:** `tick u64 LE | txHash 32B | logId u64 LE`. `logId` is the **global**
  log id (counted from the chain's first log event); the service scans the tx receipt's logs for
  the entry whose `logId` matches.
- **Reply — fixed 288 bytes:** `code u64 | contractIndex u64 | logType u64 | dataLen u64 | data 256B`
  (raw log body, zero-padded). On any non-zero code all value fields are zero. For contract-emitted
  log types (4..7) the body begins with the core-stamped 8-byte prefix
  (`contractIndex u32 LE | contract-defined type u32 LE`), so the usable contract payload is at
  most **248 bytes**.

When querying bob, the 32-byte tx hash is encoded as Qubic's **60-char lowercase identity**
(`core/src/four_q.h getIdentity`, `isLowerCase=true`) — Qubic does not address tx hashes as hex.
The service implements the encoding (KangarooTwelve checksum) in pure Python, verified
byte-identical to core.

| code | meaning |
|-----:|---------|
| 0 | success — log returned verbatim |
| 1 | bad query (zero txHash / zero tick) |
| 2 | tx not found |
| 3 | tx pending or failed (no logs exist) |
| 4 | tx exists but not in the queried tick |
| 5 | no log with the queried logId in this tx |
| 6 | log body exceeds the 256-byte reply field |

A `CONTRACT_INFORMATION_MESSAGE` (logType 6) can only be emitted by code running inside a
contract, and the core stamps `contractIndex` itself — so `(logType, contractIndex)`
authenticates the emitter the same way `(address, topic0)` does for an EVM log. A plain qu
transfer can only ever appear as `QU_TRANSFER` (logType 0).

## Config knobs

| env var                            | purpose |
|------------------------------------|---------|
| `READ_QUBIC_LOG_SERVICE_HOST/PORT` | host/port of the interface slot (default port 9006) |
| `BOB_URLS`                         | comma-separated bob RPC URLs — run your OWN bob |

Run: `python3 read_qubic_log_service.py` (Python 3.8+, no third-party packages).

## Determinism guarantees

- **Immutable history.** A log is a fact of an executed tick — byte-identical whenever read.
- **Executed only.** Pending or failed transactions are never served (code 3).
- **Multi-bob byte-agreement.** All configured bobs must return byte-identical receipts.
- **Abstain on failure.** Bob unreachable, malformed data, or disagreement &rArr; oracle error
  flag, no commit. Never point the service at a bob you do not run: a shared third-party bob
  could forge log content for everyone reading it.

## Selftest

```bash
python3 test/mock_bob.py --port 28545 &
python3 test/mock_bob.py --port 28546 &
python3 test/mock_bob.py --port 28547 --tamper &
python3 test/selftest.py     # exit 0 = all scenarios pass
```
