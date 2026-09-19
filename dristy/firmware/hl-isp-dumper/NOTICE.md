# Notices

The ISP protocol, SLIP implementation, and SPI-flash driver are derived from
[loboris/ktool](https://github.com/loboris/ktool), Copyright 2020 LoBo,
licensed under Apache License 2.0.

The build uses the Kendryte Standalone SDK, Copyright 2018 Canaan Inc.,
licensed under Apache License 2.0. The pinned SDK commit is
`02576ba67e8797444f3ee3f34c625b5ed048e707`.

The vendored minimal BSP/build subset follows `loboris/ktool` commit
`0345aa90d9b3830641373fb4e3ce4edf45d0a46f`; only the K210 startup, clock,
FPIOA, UART, and timing support needed by this SRAM program is included.
