"""
Генерирует корректный, полный C++ header с таблицами keys_a_ps5 и
keys_b_ps5 (по 3584 байта каждая) из локального ctrl_auth_keys.py.

Запускать в той же папке, где лежит ctrl_auth_keys.py.
Результат: файл ps5_auth_keys.h - вставьте его содержимое взамен
текущих (обрубленных) массивов keys_a_ps5 / keys_b_ps5 в crypto.cpp.
"""

import ctrl_auth_keys as auth_keys

EXPECTED_LEN = 0x70 * 0x20  # 3584


def format_array(name: str, data: bytes) -> str:
    if len(data) != EXPECTED_LEN:
        raise ValueError(
            f"{name}: expected {EXPECTED_LEN} bytes, got {len(data)} "
            f"(source table is itself incomplete!)"
        )

    lines = []
    lines.append(f"static const uint8_t {name}[{EXPECTED_LEN}] = {{")

    per_line = 16
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        hex_values = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_values},")

    lines.append("};")
    lines.append("")
    lines.append(
        f"static_assert(sizeof({name}) == {EXPECTED_LEN}, "
        f"\"{name} must contain exactly {EXPECTED_LEN} bytes\");"
    )

    return "\n".join(lines)


def main():
    print("keys_a_ps5 length:", len(auth_keys.keys_a_ps5))
    print("keys_b_ps5 length:", len(auth_keys.keys_b_ps5))

    out_a = format_array("keys_a_ps5", auth_keys.keys_a_ps5)
    out_b = format_array("keys_b_ps5", auth_keys.keys_b_ps5)

    with open("ps5_auth_keys.h", "w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("#include <cstdint>\n")
        f.write("#include <cstddef>\n\n")
        f.write(out_a)
        f.write("\n\n")
        f.write(out_b)
        f.write("\n")

    print()
    print("Written ps5_auth_keys.h successfully.")
    print("keys_a_ps5 initializer count:", len(auth_keys.keys_a_ps5))
    print("keys_b_ps5 initializer count:", len(auth_keys.keys_b_ps5))


if __name__ == "__main__":
    main()
