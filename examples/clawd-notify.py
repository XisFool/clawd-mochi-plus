#!/usr/bin/env python3
"""Clawd Mochi Plus — Claude Code hook -> expression bridge (example).

Claude Code hooks pass a JSON payload on stdin (fields include
``hook_event_name``, ``session_id``, ``tool_name``). This script maps the
event to an expression key and calls the device:

    GET http://<device>/cmd?k=<key>

Device address comes from the ``CLAWD_URL`` environment variable and
defaults to the standalone AP hotspot (http://192.168.4.1).

Wire it up in ``~/.claude/settings.json``, e.g. for every tool call:

    {
      "hooks": {
        "PreToolUse": [
          { "matcher": "*",
            "hooks": [ { "type": "command",
                         "command": "python /path/to/examples/clawd-notify.py",
                         "timeout": 5 } ] }
        ]
      }
    }

The script never blocks Claude Code: 2 s network timeout, all errors
ignored (an offline device must not slow down your session).
"""

import json
import os
import sys
import urllib.request

# Claude Code hook event -> expression key (see the README table for faces)
EVENT_KEYS = {
    "UserPromptSubmit": "t",      # think
    "PreToolUse": "e",            # executing
    "PostToolUseFailure": "x",    # dizzy
    "SubagentStart": "t",         # think
    "PermissionRequest": "m",     # embarrassed (asking for permission)
    "Stop": "l",                  # heart (task done)
    "StopFailure": "c",           # crying (task failed)
    "SessionEnd": "n",            # silently back to normal eyes
}


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        payload = {}

    key = EVENT_KEYS.get(payload.get("hook_event_name", ""))
    if not key:
        return 0

    base = os.environ.get("CLAWD_URL", "http://192.168.4.1").rstrip("/")
    try:
        with urllib.request.urlopen(f"{base}/cmd?k={key}", timeout=2) as resp:
            resp.read()
    except Exception:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
