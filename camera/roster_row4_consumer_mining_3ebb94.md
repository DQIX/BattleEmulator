# roster row+4 consumer mining: 0x3EBB94 / ミラーシールド

## Fixed pre-consumer baseline

- Hook: overlay26 `0x021E094C`, immediately after `0x021E1958` returns.
- Saved baseline PC: `0x021E0954` (still before the first row initialization/consumer).
- SP: `0x027E31E0`.
- Physical row base: `0x027E333C`.
- Row count: 4; stride: 12 bytes; injected field: physical row `+4`.
- Every mask restores the same saved emulator State before injection.
- Native u32 write readback was verified before every run.

Original physical rows:

| row | actor | class | original row+4 | start | goal | aux | target | route |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | `0x00` | 242 | `0x02392920` | 59 | 255 | 255 | 255 | 0 |
| 1 | `0xC0` | 246 | `0x02392920` | 30 | 255 | 255 | 255 | 0 |
| 2 | `0xC1` | 247 | `0x00000008` | 23 | 255 | 255 | 255 | 0 |
| 3 | `0xC2` | 248 | `0x0000000F` | 33 | 255 | 255 | 255 | 0 |

## Exhaustive four-row result

`bits` are ordered physical row0,row1,row2,row3. `1` means a verified nonzero native u32 was injected; `0` means zero. Node tuples are `start/goal/aux/target/route`.

| mask | bits | row1 decision | row3 decision | after row0 | after row1 | after row2 | after row3 | return |
|---:|:---:|:---|:---|:---|:---|:---|:---|---:|
| 0 | 0000 | zero → `021E1FD8` | zero → `021E1FD8` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 1 | 1000 | zero → `021E1FD8` | zero → `021E1FD8` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 2 | 0100 | nonzero → `021E2664` | zero → `021E1FD8` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 3 | 1100 | nonzero → `021E2664` | zero → `021E1FD8` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 4 | 0010 | zero → `021E1FD8` | zero → `021E1FD8` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 5 | 1010 | zero → `021E1FD8` | zero → `021E1FD8` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 6 | 0110 | nonzero → `021E2664` | zero → `021E1FD8` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 7 | 1110 | nonzero → `021E2664` | zero → `021E1FD8` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/42/255/59/0 | 1 |
| 8 | 0001 | zero → `021E1FD8` | nonzero → `021E2664` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |
| 9 | 1001 | zero → `021E1FD8` | nonzero → `021E2664` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |
| 10 | 0101 | nonzero → `021E2664` | nonzero → `021E2664` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |
| 11 | 1101 | nonzero → `021E2664` | nonzero → `021E2664` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |
| 12 | 0011 | zero → `021E1FD8` | nonzero → `021E2664` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |
| 13 | 1011 | zero → `021E1FD8` | nonzero → `021E2664` | 59/59/255/255/0 | 30/38/255/23/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |
| 14 | 0111 | nonzero → `021E2664` | nonzero → `021E2664` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |
| 15 | 1111 | nonzero → `021E2664` | nonzero → `021E2664` | 59/59/255/255/0 | 30/29/255/255/0 | 23/23/255/255/0 | 33/34/255/255/0 | 1 |

## Confirmed consumer semantics

- Only physical row1 (`C0`) and physical row3 (`C2`) reach the row+4 decision in this exact setup.
- row0 and row2 do not affect the result in any of the 16 masks.
- Each consulted row is independent:
  - zero selects the normal `0x021E1FD8` placement path;
  - nonzero selects the fallback `0x021E2664` path.
- The four semantic outcomes are therefore selected solely by `(row1 != 0, row3 != 0)`.
- The original ROM residue is `1111`, so this captured run selects fallback for both C0 and C2, producing goals 29 and 34 respectively.
