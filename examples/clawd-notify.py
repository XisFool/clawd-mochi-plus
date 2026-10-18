#!/usr/bin/env python3
"""Clawd Mochi Plus — Claude Code hook -> expression bridge (example).

Claude Code hooks pass a JSON payload on stdin (fields include
``hook_event_name``, ``session_id``, ``tool_name``). This script maps the
event to an expression key and calls the device:

    GET http://<device>/cmd?k=<key>[&lease=<ms>]

Device address comes from the ``CLAWD_URL`` environment variable and
defaults to the mDNS name ``http://clawd-mochi.local``. For first-boot AP
provisioning, set ``CLAWD_URL=http://192.168.4.1``.

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

The script uses a short 0.4 s network timeout so an offline device cannot
stall the session; all connection errors are ignored.
"""

import json
import os
import sys
import urllib.error
import urllib.request

# Claude Code hook event -> expression query (see the README table for faces).
# `lease` keeps long-running states on screen (500..1800000 ms on the device).
EVENT_ACTIONS = {
    "UserPromptSubmit": "t&lease=600000",       # think
    "PreToolUse": "e&lease=1800000",            # executing
    "PostToolUse": "t&lease=600000",            # think
    "SubagentStart": "t&lease=600000",          # think
    "SubagentStop": "t&lease=600000",           # main agent resumes thinking
    "PostToolUseFailure": "x",                  # dizzy
    "PermissionRequest": "m&lease=600000",      # embarrassed / asking
    "Notification": "m&lease=600000",           # embarrassed / attention
    "Stop": "l",                                # heart (task done)
    "StopFailure": "c",                         # crying (task failed)
    "SessionEnd": "n",                          # back to normal eyes
}

TIMEOUT_S = 0.4


def send(url: str):
    """Return the HTTP status, or None on any network error."""
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_S) as resp:
            resp.read()
            return resp.status
    except urllib.error.HTTPError as exc:
        return exc.code
    except Exception:
        return None


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        payload = {}

    action = EVENT_ACTIONS.get(payload.get("hook_event_name", ""))
    if not action:
        return 0

    base = os.environ.get("CLAWD_URL", "http://clawd-mochi.local").rstrip("/")
    url = f"{base}/cmd?k={action}"

    send(url)
    return 0


if __name__ == "__main__":
    sys.exit(main())
