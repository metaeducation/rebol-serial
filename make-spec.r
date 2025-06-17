Rebol [
    name: Serial
    notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'no

sources: %mod-serial.c

depends: compose [
    (switch platform-config.os-base [
        'Windows [
            [%serial-windows.c]
        ]
    ] else [
        [%serial-posix.c]
    ])
]
