# Error handling and recovery

[Public contract](../src/as608.h) · [Status definitions](../src/as608_types.h)

## Interpret the result in order

Admission status -> scheduler progress -> result availability -> host
status -> sensor confirmation -> command-specific output guarantee.
Neither COMPLETE nor get_result returning true is a success flag.
Raw confirmation is meaningful only when host status is OK.

| Host status | Typical origin |
| --- | --- |
| OK | Successful local operation or validated terminal sensor result |
| INVALID_ARG | Rejected argument or invalid lifecycle call |
| NOT_INITIALIZED | Start on uninitialized context |
| BUSY | Active job owns context |
| IO_ERROR | Port error, invalid callback count, stream failure |
| TIMEOUT | Whole-job deadline exceeded |
| FRAME_ERROR | Invalid start/address/kind/encoded length or storage bound |
| CHECKSUM_ERROR | Full received frame checksum mismatch |
| UNEXPECTED_REPLY | Direction, phase limit, reply semantics or inconsistent metadata |
| RECOVERY_REQUIRED | Quarantine blocks admission; also stored result after abort |

Admission rejection is not a terminal result of the rejected job. During
BUSY, a transport/protocol fault records a host error and enters
DESYNCHRONIZED. An ordinary valid negative ACK enters DONE and preserves
the raw confirmation, without automatic retry. Address provisioning
quarantines on any reply other than its required ACK_OK. A terminal
burn result can require recovery even with host status OK.

## Outcome and side effects

| Event | What the application may infer |
| --- | --- |
| Ordinary validated ACK_OK | Only the documented command guarantee |
| Negative ACK | Sensor rejected or failed the command; do not assume rollback |
| Standalone download drain | Readiness ACK plus physical TX completion only |
| Timeout or abort | Sensor effects unknown; bytes may still be queued |
| Failed receive stream | Partial caller bytes or sink side effects may exist |
| Failed provisioning | Retained host address may differ from sensor |
| Burn terminal response | Preserve result, require external recovery |

Output scalars and committed lengths follow the API-specific contract.
Do not use stale values from a previous job as current success. Do not
automatically resend destructive commands or infer success from a
timeout. Readback or inventory reconciliation is a separate authorized
job after recovery where appropriate.

## Recovery procedure

```text
Stop admission and snapshot result
    -> stop/cancel outstanding TX and DMA/IT
    -> recover sensor and UART (reset/power-cycle where required)
    -> wait for verified target readiness
    -> purge stale RX/TX, clear latched errors, restore compatible settings
    -> as608_confirm_reset()
    -> re-authenticate / verify parameters if needed
    -> resume authorized jobs
```

The application owns this state machine; its waiting must not block
unrelated work. Physical reset may not undo completed flash changes.
Order hardware operations to ensure no stale producer can refill queues
after purge. Do not call confirm_reset until the physical link is fresh.
The API does not flush UART, cancel DMA, wait for boot or authenticate.
It clears software state and preserves port, device address and timeout.

For uncertain baud/address changes determine actual settings externally.
If the verified address differs from retained configuration, retire the
old context and initialize a fresh zeroed context after physical recovery.
Do not edit private fields or call init again to bypass quarantine.

## Diagnostics

Log application job ID, operation name, elapsed time, host status,
confirmation only when valid, and sanitized transport counters.
Never log passwords, biometric bytes or raw context/frame dumps.
Record firmware identity as available without assuming a unique serial.
Distinguish test traces from actual hardware captures. The checksum
does not authenticate the sender and no automatic secure erasure is promised.
