import os
import re

Import("env")

version_file = os.path.join(env["PROJECT_SRC_DIR"], "version.h")
fw_version = "0.0.0"

if os.path.exists(version_file):
    with open(version_file, "r", encoding="utf-8") as f:
        match = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read())
        if match:
            fw_version = match.group(1)

env.Append(CPPDEFINES=[("PROJECT_VER", env.StringifyMacro(fw_version))])
print(f"[sync_version] Embedded PROJECT_VER = {fw_version}")