# Wire protocol and supported profiles

[Capabilities and command inventory](../FEATURES.md) · [Packet contract](../src/as608_packet.h)

## Framing

All multi-byte wire fields use big-endian order.

| Offset | Field | Bytes |
| ---: | --- | ---: |
| 0 | Start (EF 01) | 2 |
| 2 | Exact device address | 4 |
| 6 | Packet kind | 1 |
| 7 | Length L | 2 |
| 9 | Payload | N |
| 9 + N | Additive checksum | 2 |

L = N + 2, total frame = N + 11. Checksum is
(kind + length high byte + length low byte + sum of payload bytes) modulo
65536. Start, address and checksum itself are excluded. The checksum
is neither a CRC nor cryptographic integrity protection.

| Kind | Meaning | Driver direction |
| --- | --- | --- |
| 01 | COMMAND | Host to sensor |
| 02 | DATA | Either |
| 07 | ACK | Sensor to host |
| 08 | END_DATA | Either |

The default address FFFFFFFF is exact, never a wildcard. The wire length
field can represent payloads up to 65533 bytes, but the transport
storage limits each payload to 256 bytes. A zero-length payload is
structurally representable; command/stream handlers impose semantic
length rules. Do not serialize host structs or enum representations.

## Independent examples

GetImage command at the default address:

```text
EF 01 FF FF FF FF 01 00 03 01 00 05
```

A successful simple ACK:

```text
EF 01 FF FF FF FF 07 00 03 00 00 0A
```

A no-finger ACK:

```text
EF 01 FF FF FF FF 07 00 03 02 00 0C
```

The examples are mathematical wire vectors, not captured hardware traces.

## ACK semantics

The first ACK payload byte is the raw confirmation. Simple ACK length
is one; typed ACK handlers accept a permitted shape and require the
full success payload for ACK_OK. A negative confirmation may terminate
a structurally accepted reply without committing scalar outputs.
Unknown confirmation values are preserved when accepted by the handler.

| Success response | ACK payload bytes (including confirmation) |
| --- | ---: |
| Simple, readiness | 1 |
| GPIO echoed state | 2 |
| 16-bit score/count/PageID | 3 |
| PageID + score or random code | 5 |
| System parameters | 17 |
| Index or notepad | 33 |

Wrong direction, unexpected length/semantic value or bad checksum is
a host fault, not a negative sensor confirmation. For provisioning and
firmware maintenance, see the stricter rules in FEATURES.md.

## Stream profiles

Sensor-to-host: command -> readiness ACK -> DATA fragments -> END_DATA.
Verified data is delivered or copied incrementally. Exact length is
required for information pages, feature and image streams; templates
accept actual 512 or 768 bytes. A fault may leave partial output bytes.

Host-to-sensor ordinary download: ReadSysPara -> download command ->
readiness ACK -> DATA/END_DATA -> physical TX drain. The local profile
does not consume a final sensor ACK. Do not enable it until the target
is verified to produce no trailing ACK. Template/feature chunks are
32/64/128/256 bytes; image chunks are 64/128/256 bytes and exact total
length must be divisible by the selected size. No final sensor acceptance
is inferred from drain.

Composite write appends StoreChar after drain and consumes its ACK.
A trailing DownChar ACK can masquerade as that StoreChar ACK; target
verification is a prerequisite, not an optional improvement. BurnCode
has a distinct experimental profile and recovery gate; no firmware
programming procedure is established by host tests.

## Configuration and target uncertainties

Packet sizes follow WriteReg codes 0–3. ReadSysPara's conflicting prose
for code 1 is resolved to 64 bytes in the implemented profile.
Address provisioning expects default TX address and the requested new
RX address; only valid ACK_OK commits the local configuration.

Descriptor offsets, index bit order, notepad capacity, variant-specific
opcodes and image dimensions must be checked against the actual firmware.
The related R503 manual is evidence for the index-bit-order assumption,
not direct AS608 validation.

## References

- Synochip, AS60x SOC Communication Manual V1.0, April 2010 (Chinese),
  printed pp. 8–10 command list, pp. 11–28 commands, pp. 29–34 workflows.
- [AS60x MCU User's Guide V1.0 (English)](https://cdn.sparkfun.com/assets/e/c/d/a/1/MCU_SOC_Manual_EN.pdf),
  §§4.1–4.2 framing, printed pp. 16–17 and 31 DownChar discrepancy.
- See [FEATURES.md](../FEATURES.md#references) for other reference
  identifiers and scope caveats. No new target measurements are claimed.
