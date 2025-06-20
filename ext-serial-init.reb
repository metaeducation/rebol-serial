Rebol [
    title: "Serial Port Extension"
    name: Serial
    type: module
    options: [isolate]
    version: 1.0.0
    license: "Apache 2.0"
]

port-spec-serial: make system.standard.port-spec-head [
    speed: 115200
    data-size: 8
    parity: 'none
    stop-bits: 1
    flow-control: 'none  ; not supported on all systems
]

sys.util/make-scheme [
    title: "Serial Port"
    name: 'serial
    spec: port-spec-serial

    actor: serial-actor/

    init: func [port <local> path speed] [
        if url? port.spec.ref [
            parse port.spec.ref [
                thru ":" repeat [0 2] slash
                path: across [to slash | <end>] one
                speed: across to <end>
            ]
            port.spec.speed: try to integer! speed
            port.spec.path: to file! path
        ]
    ]
]
