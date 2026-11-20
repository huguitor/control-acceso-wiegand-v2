#!/usr/bin/env python3
"""Firma comandos de licencia para el controlador (FW >= 2.4.0).

  generar <clave_licencia.h>              crea una clave nueva (no la muestra)
  firmar  <clave_licencia.h> <comando>    imprime la línea CMD lista para enviar
      comando: SUSPEND:<seq> | RESUME:<seq> | RENEW:<dias>:<seq> | FREE:<seq>
  prueba                                   verifica SipHash con el vector oficial

MAC = SipHash-2-4(clave, "<comando>") como 16 dígitos hex.
"""
import os, re, sys

M = (1 << 64) - 1

def _rotl(x, b):
    return ((x << b) | (x >> (64 - b))) & M

def siphash24(key: bytes, msg: bytes) -> int:
    k0 = int.from_bytes(key[:8], "little"); k1 = int.from_bytes(key[8:16], "little")
    v = [k0 ^ 0x736F6D6570736575, k1 ^ 0x646F72616E646F6D,
         k0 ^ 0x6C7967656E657261, k1 ^ 0x7465646279746573]
    def rnd():
        v[0] = (v[0] + v[1]) & M; v[1] = _rotl(v[1], 13) ^ v[0]; v[0] = _rotl(v[0], 32)
        v[2] = (v[2] + v[3]) & M; v[3] = _rotl(v[3], 16) ^ v[2]
        v[0] = (v[0] + v[3]) & M; v[3] = _rotl(v[3], 21) ^ v[0]
        v[2] = (v[2] + v[1]) & M; v[1] = _rotl(v[1], 17) ^ v[2]; v[2] = _rotl(v[2], 32)
    fin = len(msg) - len(msg) % 8
    for i in range(0, fin, 8):
        w = int.from_bytes(msg[i:i + 8], "little")
        v[3] ^= w; rnd(); rnd(); v[0] ^= w
    b = (len(msg) & 0xFF) << 56 | int.from_bytes(msg[fin:], "little")
    v[3] ^= b; rnd(); rnd(); v[0] ^= b
    v[2] ^= 0xFF
    for _ in range(4): rnd()
    return v[0] ^ v[1] ^ v[2] ^ v[3]

def leer_clave(path: str) -> bytes:
    txt = open(path).read()
    txt = txt[txt.index("{") + 1:txt.index("}")]
    vals = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", txt)]
    if len(vals) != 16:
        sys.exit("la clave debe tener 16 bytes")
    if not any(vals):
        sys.exit("la clave es la de la plantilla (todo en cero): generar una real")
    return bytes(vals)

PATRON = re.compile(r"^(SUSPEND:\d+|RESUME:\d+|RENEW:\d+:\d+|FREE:\d+)$")

def firmar(key: bytes, comando: str) -> str:
    if not PATRON.match(comando):
        sys.exit("comando inválido: " + comando)
    return "CMD:%s:%016x" % (comando, siphash24(key, comando.encode()))

def main(a):
    if len(a) == 1 and a[0] == "prueba":
        key = bytes(range(16)); msg = bytes(range(15))
        assert siphash24(key, msg) == 0xA129CA6149BE45E5, "SipHash no coincide"
        print("SipHash-2-4 OK (vector oficial)")
    elif len(a) == 2 and a[0] == "generar":
        if os.path.exists(a[1]):
            sys.exit(a[1] + " ya existe: no se pisa una clave")
        k = os.urandom(16)
        filas = ",\n  ".join(", ".join("0x%02X" % x for x in k[i:i + 8]) for i in (0, 8))
        fd = os.open(a[1], os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write("#pragma once\n// Clave de licencia de ESTE equipo. No subir a Git.\n"
                    "#include <avr/pgmspace.h>\n\n"
                    "const uint8_t CLAVE_LICENCIA[16] PROGMEM = {\n  %s,\n};\n" % filas)
        print("clave creada en", a[1])
    elif len(a) == 3 and a[0] == "firmar":
        print(firmar(leer_clave(a[1]), a[2]))
    else:
        sys.exit(__doc__)

if __name__ == "__main__":
    main(sys.argv[1:])
