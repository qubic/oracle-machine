# read_evm_log oracle service

Generic **EVM &rarr; Qubic** oracle. Given `(chainId, txHash, logIndex)` it returns exactly one
receipt log — emitter address, topics and data — as **raw bytes**. No event ABI is baked in: any
contract's log is served and the querying Qubic smart contract decodes its own semantics.

Implemented in Python (`read_evm_log_service.py`, stdlib only). It listens on
`READ_EVM_LOG_SERVICE_PORT` and speaks the OM node <-> service protocol; adding a new consumer is a
config change, not a code change.

## Wire format

### Query — 48 bytes (`EvmReadQuery`)

| offset | size | field      | notes                                            |
|-------:|-----:|------------|--------------------------------------------------|
| 0      | 8    | `chainId`  | uint64 little-endian                             |
| 8      | 32   | `txHash`   | transaction hash                                 |
| 40     | 8    | `logIndex` | uint64 LE, **receipt-local** index into the tx's own logs |

### Reply — 440 bytes (`EvmReadReply`)

| offset | size | field        | notes                                             |
|-------:|-----:|--------------|---------------------------------------------------|
| 0      | 8    | `code`       | uint64 LE result code (see below)                 |
| 8      | 32   | `address`    | 20-byte emitter right-aligned; upper 12 bytes zero |
| 40     | 8    | `topicCount` | uint64 LE number of topics (0..4)                 |
| 48     | 128  | `topics`     | `uint8[4][32]`, zero-padded                       |
| 176    | 8    | `dataLen`    | uint64 LE length of `data`                        |
| 184    | 256  | `data`       | log data, zero-padded                             |

On any non-zero `code`, **all value fields are zero** (canonical failure form, so every computor
commits identical bytes).

## Result codes

| code | name                       | meaning                                               |
|-----:|----------------------------|-------------------------------------------------------|
| 0    | `EVM_READ_SUCCESS`         | log returned verbatim                                 |
| 1    | `EVM_READ_BAD_QUERY`       | malformed query (e.g. zero txHash)                    |
| 2    | `EVM_READ_CHAIN_UNSUPPORTED` | no RPC endpoints configured for `chainId`           |
| 3    | `EVM_READ_TX_NOT_FOUND`    | no endpoint knows the tx                              |
| 4    | `EVM_READ_TX_NOT_FINALIZED`| tx is above the finalized head                        |
| 5    | `EVM_READ_LOG_INDEX_OUT_OF_RANGE` | `logIndex >= receipt.logs.size()`              |
| 6    | `EVM_READ_LOG_DATA_TOO_LARGE` | log data exceeds the 256-byte reply field          |

A **transport error / provider disagreement is not a result code**: the service abstains (returns an
oracle error flag and commits nothing) so it never records a reply other computors cannot reproduce.

## Config knobs

| env var                          | purpose                                                            |
|----------------------------------|--------------------------------------------------------------------|
| `READ_EVM_LOG_SERVICE_HOST/PORT` | host/port of the interface slot the OM node reaches this service on (default port 9005) |
| `EVM_RPC_URLS_<chainId>`         | comma-separated JSON-RPC URLs for a chain (>= 2 providers recommended) |
| `EVM_MIN_CONFIRMATIONS_<chainId>`| optional; use `latest - N` instead of the `finalized` block tag    |

The node's `Config::loadFromEnv` registers the interface slot (default index 3) with its host/port;
the node routes an incoming oracle query for that index to this service's socket.

Run: `python3 read_evm_log_service.py` (Python 3.8+, no third-party packages).

## Determinism guarantees

- **Finalized only.** A tx counts as final only if its block is at or below the finalized head
  reported by *every* configured endpoint. The service never reads `latest`.
- **Multi-provider byte-agreement.** Every read queries all endpoints of the chain and requires
  byte-identical normalized results.
- **Abstain on disagreement.** Any endpoint failure or disagreement yields a transport error and no
  commit — the oracle abstains rather than risk a non-reproducible reply.
- **Deterministic facts are answered** with a result code and zeroed value fields (chain
  unsupported, tx not found, not finalized, log index out of range, data too large).

## Adding a consumer (config, not code)

1. Register the interface index in the OM node config (`libs/om_common`) if it is not one of the
   default generic slots, and set its `..._SERVICE_HOST/PORT`.
2. Add `EVM_RPC_URLS_<chainId>` for each chain the consumer queries.

No service code changes are required.

## Selftest

```bash
python3 test/mock_evm_rpc.py --port 18545 &
python3 test/mock_evm_rpc.py --port 18546 &
python3 test/mock_evm_rpc.py --port 18547 --tamper &
python3 test/selftest.py     # exit 0 = all scenarios pass
```
