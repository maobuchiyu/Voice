"""Embed local web assets in flash; no filesystem upload or internet required."""
import gzip
from pathlib import Path

Import("env")
root = Path(env.subst("$PROJECT_DIR"))
generated = root / ".pio" / "generated"
generated.mkdir(parents=True, exist_ok=True)
assets = [("index.html", "text/html; charset=utf-8"),
          ("styles.css", "text/css; charset=utf-8"),
          ("protocol.js", "application/javascript; charset=utf-8"),
          ("app.js", "application/javascript; charset=utf-8")]
parts = ["#pragma once\n#include <Arduino.h>\n"]
for index, (name, mime) in enumerate(assets):
    data = gzip.compress((root / "web" / name).read_bytes(), mtime=0)
    parts.append("static const uint8_t asset_%d[] PROGMEM = {%s};\n" %
                 (index, ",".join(str(b) for b in data)))
parts.append("struct WebAsset { const char* path; const char* mime; const uint8_t* data; size_t size; };\n")
parts.append("static const WebAsset webAssets[] = {\n")
for index, (name, mime) in enumerate(assets):
    parts.append('{"/%s", "%s", asset_%d, sizeof(asset_%d)},\n' % (name, mime, index, index))
parts.append("};\n")
output = generated / "web_assets.h"
content = "".join(parts)
if not output.exists() or output.read_text(encoding="utf-8") != content:
    output.write_text(content, encoding="utf-8")
env.Append(CPPPATH=[str(generated)])
