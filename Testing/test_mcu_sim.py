"""
MCU Simulator — test_mcu_sim.py

Simulates all MCU → web app message types in the exact wire format
the MCU uses: backslash-escaped JSON sent as text/plain.

Usage:
    python test_mcu_sim.py                  # runs all tests
    python test_mcu_sim.py profile          # updateMCUProfile only
    python test_mcu_sim.py stock            # updateMCUStock only
    python test_mcu_sim.py uids             # trustedUIDs only
    python test_mcu_sim.py error            # Error only
    python test_mcu_sim.py offline          # updateWebApp only
"""

import sys
import json
import requests

BASE_URL = "http://10.250.88.97:8000"
ENDPOINT = f"{BASE_URL}/api/device/push"

# ── Device / credentials ─────────────────────────────────────────────────────
DEVICE_UID = "11223344"
USERNAME   = "developer"
PASSWORD   = "EasyMedRX"


def mcu_encode(data: dict) -> str:
    """Encode a dict in the MCU wire format: standard JSON with every \" escaped."""
    return json.dumps(data, separators=(',', ':')).replace('"', '\\"')


def send(payload: dict, label: str):
    """Send one MCU-format request and print the response."""
    body = mcu_encode(payload)
    print(f"\n{'='*50}")
    print(f">> {label}")
    print(f"   Sending: {body[:120]}{'...' if len(body) > 120 else ''}")

    try:
        resp = requests.post(
            ENDPOINT,
            data=body,
            headers={"Content-Type": "text/plain"},
            timeout=5,
        )
        print(f"   Status : {resp.status_code}")
        # Server responds in the same escaped format — unescape for display
        raw = resp.text
        try:
            parsed = json.loads(raw.replace('\\"', '"'))
            print("   Response:")
            print("   " + json.dumps(parsed, indent=4).replace("\n", "\n   "))
        except (ValueError, json.JSONDecodeError):
            print(f"   Raw: {raw}")

    except requests.exceptions.RequestException as e:
        print(f"   FAILED: {e}")


# ── Test functions ────────────────────────────────────────────────────────────

def test_profile():
    """updateMCUProfile — profile + prescription sync."""
    send({
        "type":  "updateMCUProfile",
        "uid":   DEVICE_UID,
        "user":  USERNAME,
        "pword": PASSWORD,
        "email": "DaphyDuck@gmail.com",
        "phone": "904-393-9032",
    }, "updateMCUProfile (regular user)")


def test_stock():
    """updateMCUStock — MCU reports current pill counts (no auth)."""
    send({
        "type":      "updateMCUStock",
        "uid":       DEVICE_UID,
        "medicine0": "Tic-Tacs",       "stock0": 5000,
        "medicine1": "Gobstoppers",    "stock1": 1000,
        "medicine2": "Atomic Fireballs","stock2": 3000,
        "medicine3": "Skittles",       "stock3": 170,
    }, "updateMCUStock")


def test_trusted_uids():
    """trustedUIDs — MCU requests authorized RFID card UIDs (no auth)."""
    send({
        "type": "trustedUIDs",
        "uid":  DEVICE_UID,
        # Template fields the MCU sends but web app ignores:
        "trustedUID0": "", "trustedUID1": "", "trustedUID2": "", "trustedUID3": "",
        "trustedUID4": "", "trustedUID5": "", "trustedUID6": "", "trustedUID7": "",
    }, "trustedUIDs")


def test_error():
    """Error — MCU reports a device fault (no auth)."""
    send({
        "type":    "Error",
        "uid":     DEVICE_UID,
        "message": "Compartment 1 jam detected",
    }, "Error")


def test_offline_events():
    """updateWebApp — MCU syncs events recorded while offline (auth required)."""
    send({
        "type":  "updateWebApp",
        "uid":   DEVICE_UID,
        "user":  USERNAME,
        "pword": PASSWORD,
        "events": [
            {
                "user":            USERNAME,
                "event_type":      "TAKEN",
                "medication_name": "Tic-Tacs",
                "timestamp":       "2026-04-10T08:05:00",
            },
            {
                "user":            USERNAME,
                "event_type":      "MISSED",
                "medication_name": "Gobstoppers",
                "timestamp":       "2026-04-10T12:00:00",
            },
        ],
    }, "updateWebApp (offline events)")


# ── Entry point ───────────────────────────────────────────────────────────────

TESTS = {
    "profile": test_profile,
    "stock":   test_stock,
    "uids":    test_trusted_uids,
    "error":   test_error,
    "offline": test_offline_events,
}

if __name__ == "__main__":
    arg = sys.argv[1].lower() if len(sys.argv) > 1 else "all"
    if arg == "all":
        for fn in TESTS.values():
            fn()
    elif arg in TESTS:
        TESTS[arg]()
    else:
        print(f"Unknown test '{arg}'. Options: {', '.join(TESTS)} or 'all'")
