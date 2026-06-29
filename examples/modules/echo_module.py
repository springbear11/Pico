import json
import sys


def handle(request):
    context = request.get("context", {})
    inputs = context.get("inputs", {})
    return {
        "outcome": inputs.get("outcome", "Passed"),
        "outputs": inputs,
        "measurements": inputs.get("measurements", {}),
        "errorCode": inputs.get("errorCode", ""),
        "errorMessage": inputs.get("errorMessage", ""),
    }


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    print(json.dumps(handle(json.loads(line)), separators=(",", ":")), flush=True)
