Import("env")

import gzip
from pathlib import Path


project_dir = Path(env.subst("$PROJECT_DIR"))
input_path = project_dir / "src" / "web" / "PanelPage.html"
output_path = project_dir / "src" / "web" / "PanelPageGz.h"
compressed = gzip.compress(input_path.read_bytes(), compresslevel=9, mtime=0)

lines = [
    "#pragma once",
    "",
    "#include <Arduino.h>",
    "",
    "static const uint8_t PanelHtmlGz[] PROGMEM = {",
]
for offset in range(0, len(compressed), 16):
    chunk = compressed[offset : offset + 16]
    lines.append("  " + ", ".join("0x{:02x}".format(value) for value in chunk) + ",")
lines.extend(
    [
        "};",
        "static constexpr size_t PanelHtmlGzLength = sizeof(PanelHtmlGz);",
        "",
    ]
)
generated = "\n".join(lines)
if not output_path.exists() or output_path.read_text(encoding="ascii") != generated:
    output_path.write_text(generated, encoding="ascii", newline="\n")
print("Embedded web panel: {} -> {} bytes (gzip)".format(input_path.stat().st_size, len(compressed)))
