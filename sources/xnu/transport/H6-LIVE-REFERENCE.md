# H6 live transport and Upload collection reference

This is a record of the historical procedure, **not an executable live sender**. The source archive provides host-only reconstruction and verification in this directory. No device-specific serial number, local machine path, seed, receipt, RAM capture, or Apple binary is included.

## Send sequence observed for the paired H6 build

1. Before USB transfer, the sender checked a fresh Android identity and boot-state receipt, resident bootloader hash, exact FD/callback/XNU/witness hashes, and frozen independent H3/H4b/H5 A/B evidence. It required one matching Fastboot USB device, VID:PID `18d1:0002`.
2. It claimed interface 0 and used a 64-byte standard, host-to-device, device-recipient control transfer, request `0x30` (`SET_SEL`), to set the next OUT target. The transfer payload had little-endian 64-bit values `1` at offset `0x20`, target physical pointer at `0x30`, and `1` at `0x38`; all other bytes were zero.
3. Each of nine setup steps paired one `SET_SEL` with one exact-length bulk OUT on endpoint `0x02`. The labels and targets were: `prime` to `0x8b000000` with `getvar:product`; `stage` to `0xe81ec280` with the 340-byte callback; `invalidate` to `0x8b000000` with pointer `0xe8016638`; `stage` again; `sync` to `0x8b000000` with `0xe8016658`; `stage` again; `disable_mmu` to `0x8b000000` with `0xe80ac798`; `stage` again; `install_callback` to `0x8b000ff8` with pointer `0x8b000000`. The observed callback pointer slot was `0xe83483f8`. These addresses belong to the tested bootloader build and are not portable device defaults.
4. It sent the high witness as 2,048 consecutive 512-byte OUTs, then the low FD/manifest/XNU/padding stream as 51,712 consecutive 512-byte OUTs. Every nonterminal OUT had to report success and its exact byte count. No short, extra, or out-of-order frame was accepted.
5. It attempted one 8-byte terminal `UEFITERM` at `0x91940000`. A USB disconnect, timeout or completion at this step was treated as uncertain until the subsequent Upload A/B checks. The terminal was never automatically retried. Total setup + high + low + terminal OUT count: 53,770.

This plan records the observed protocol; it does not prove these instructions are safe on another firmware, device revision, or newly rebuilt image. `h6_send_plan.py` emits the exact host-only setup descriptor for a pinned callback stage.

## Historical Upload A/B collection

The original bounded reader expected one Samsung Upload device, VID:PID `04e8:685d`, and verified its probe table reported `SM-N986B`, 64-bit addresses and 12 areas. It read each permitted range **twice**, A then B, without changing the handset between reads. Reads were split into at most 1 MiB chunks. The collector first read the manifest, loader marker and handoff snapshot to bind the fresh session, then the other ranges. It validated the XNU live log pointers before reading the console ring.

| Name | Physical address | Bytes |
| --- | ---: | ---: |
| callback | `0x8b000000` | 340 |
| callback_state | `0x8b000200` | 16 |
| witness | `0x940000000` | 1,048,576 |
| xnu_scratch | `0x940100000` | 4,096 |
| fd_suffix | `0x90049000` | `0x6b7000` |
| manifest | `0x90700000` | 4,096 |
| kernel | `0x90701000` | 19,057,760 |
| loader_marker | `0x95e00000` | 4,096 |
| handoff_snapshot | `0x95d00000` | 4,096 |
| loader_live | `0x905aa1210` | 96 |
| loader_log_head | `0x905b2ca00` | 64 |
| xnu-console | `0x905b2ca14` | 65,536 to 1,572,864, bounded by log header |

The Upload protocol used the command strings `PrEaMbLe`, `PrObE`, address strings, and `DaTaXfEr`, with an acknowledgement after each nonempty data burst. The address commands encoded inclusive start and end as hexadecimal strings with at least eight digits, expanding above 32 bits. These command details are retained to explain the historical reader, but the live USB collector and its native binary are not distributed here.

The stable FD suffix begins at `0x90049000`; the Upload path could overwrite its prefix. The XNU kernel could also be overlaid by S-Boot crash-summary data in exactly `0x91100000..0x91121000` (exclusive end). The historical checker required every kernel byte **outside** that interval to match the pinned Mach-O and, if the interval changed, required the known crash-summary signatures. It did not ignore arbitrary kernel differences.

The A/B checks tied the manifest hash, session tag, witness digest, UEFI handoff snapshot, XNU scratch commit, and 22 ordered H4/H5/H6 console markers to the same run. The H6 marker chain showed two 512-byte `VNOP_STRATEGY` reads and zero writes. The observed block and raw device majors were 1 and 13. An offline check of retained bytes is useful evidence of consistency; it is not a fresh physical test.
