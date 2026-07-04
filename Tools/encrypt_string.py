#!/usr/bin/env python3
"""
Encrypt a string for use with StrEncrypt::Decrypt().
Output is a C byte-array suitable for SECURE_SECTION placement.

Usage:
  python encrypt_string.py "plaintext string"
  python encrypt_string.py --key 0xAB "plaintext string"
  echo "string" | python encrypt_string.py
"""

import sys


def encrypt(plain: str, key: int = None) -> list[int]:
    if key is None:
        key = (len(plain) + 0xA3) & 0xFF
    result = [key]
    for i, ch in enumerate(plain):
        result.append(ord(ch) ^ ((key + i) & 0xFF))
    return result


def format_output(name: str, data: list[int], comment: str = "") -> str:
    lines = []
    if comment:
        lines.append(f"// {comment}")
    # Wrap at ~12 bytes per line for readability
    chunked = [data[i:i + 12] for i in range(0, len(data), 12)]
    hex_strs = [", ".join(f"0x{b:02X}" for b in chunk) for chunk in chunked]
    joined = ",\n    ".join(hex_strs)
    lines.append(f"SECURE_SECTION static const uint8_t {name}[] = {{\n    {joined}\n}};")
    lines.append(f"static const size_t {name}_len = sizeof({name});")
    return "\n".join(lines)


if __name__ == "__main__":
    key = None
    args = list(sys.argv[1:])

    if not args:
        plain = sys.stdin.read()
    else:
        if "--key" in args:
            idx = args.index("--key")
            key = int(args[idx + 1], 0) & 0xFF
            args = args[:idx] + args[idx + 2:]
        plain = " ".join(args)

    if not plain:
        print("Usage: encrypt_string.py [--key 0xAB] <string>", file=sys.stderr)
        sys.exit(1)

    data = encrypt(plain, key)
    name = "kEnc_" + ("".join(c if c.isalnum() else "_" for c in plain[:16]))
    print(format_output(name, data, comment=plain[:48]))
