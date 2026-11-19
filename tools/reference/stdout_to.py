"""stdout_to.py OUTPUT SCRIPT ARGS...: runs a Python script and writes what it prints to OUTPUT (the build's
custom commands: m6809make.py prints the core's states)."""
import os
import subprocess
import sys

output, script = sys.argv[1], sys.argv[2]
os.makedirs(os.path.dirname(output), exist_ok=True)
result = subprocess.run([sys.executable, script] + sys.argv[3:], capture_output=True, text=True)
if result.returncode:
    sys.stderr.write(result.stderr)
    sys.exit(result.returncode)
with open(output, "w", newline="\n") as handle:
    handle.write(result.stdout)
