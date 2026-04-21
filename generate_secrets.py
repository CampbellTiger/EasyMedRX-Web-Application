"""
EasyMedRX — one-time secrets generator
Run this once before first launch (or any time .env is missing):

    python generate_secrets.py

It will:
  1. Create .env with all required fields (prompts for email credentials)
  2. Generate a Django SECRET_KEY
  3. Generate VAPID public/private key pair for push notifications
  4. Generate a self-signed SSL certificate (cert.pem / key.pem)

Already-set values are never overwritten, so it's safe to re-run.
"""

import base64
import os
import re
import subprocess
import sys


ENV_PATH  = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
CERT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cert.pem")
KEY_PATH  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "key.pem")

ENV_TEMPLATE = """\
# ── Django ────────────────────────────────────────────────────────────────────
# Auto-generated — do not edit manually
SECRET_KEY=

# ── Email (Gmail) ─────────────────────────────────────────────────────────────
# Create an App Password at: myaccount.google.com → Security → App Passwords
EMAIL_HOST=smtp.gmail.com
EMAIL_PORT=587
EMAIL_USE_TLS=True
EMAIL_HOST_USER=
EMAIL_HOST_PASSWORD=
DEFAULT_FROM_EMAIL=

# ── Web Push (VAPID) ──────────────────────────────────────────────────────────
# Auto-generated — do not edit manually
VAPID_PUBLIC_KEY=
VAPID_PRIVATE_KEY=
VAPID_ADMIN_EMAIL=
"""

# ── Helpers ───────────────────────────────────────────────────────────────────

def read_env():
    with open(ENV_PATH) as f:
        return f.read()

def write_env(contents):
    with open(ENV_PATH, "w") as f:
        f.write(contents)

def get_field(contents, key):
    m = re.search(rf'^{re.escape(key)}=(.*)$', contents, re.MULTILINE)
    return m.group(1).strip() if m else ''

def set_field(contents, key, value):
    if re.search(rf'^{re.escape(key)}=', contents, re.MULTILINE):
        return re.sub(rf'^{re.escape(key)}=.*$', f'{key}={value}', contents, flags=re.MULTILINE)
    return contents + f'\n{key}={value}\n'

def prompt(label, hint='', secret=False):
    suffix = f' ({hint})' if hint else ''
    try:
        if secret:
            import getpass
            return getpass.getpass(f'  {label}{suffix}: ').strip()
        return input(f'  {label}{suffix}: ').strip()
    except (KeyboardInterrupt, EOFError):
        print('\nAborted.')
        sys.exit(1)

def ok(msg):  print(f'  \033[32m✓\033[0m  {msg}')
def skip(msg): print(f'  \033[90m–\033[0m  {msg} (already set)')
def warn(msg): print(f'  \033[33m!\033[0m  {msg}')

# ── Step 1 — ensure .env exists ───────────────────────────────────────────────

print('\n── EasyMedRX Secret Generator ──────────────────────────────────\n')

if not os.path.exists(ENV_PATH):
    write_env(ENV_TEMPLATE)
    print('Created .env from template.')
else:
    print('.env already exists — filling in any blank fields.')

contents = read_env()

# ── Step 2 — Django SECRET_KEY ────────────────────────────────────────────────

print('\n[1/4] Django SECRET_KEY')
if not get_field(contents, 'SECRET_KEY'):
    import secrets as _secrets
    key = 'django-' + _secrets.token_urlsafe(50)
    contents = set_field(contents, 'SECRET_KEY', key)
    write_env(contents)
    ok('SECRET_KEY generated')
else:
    skip('SECRET_KEY')

# ── Step 3 — Email credentials ────────────────────────────────────────────────

print('\n[2/4] Email credentials')
changed = False

if not get_field(contents, 'EMAIL_HOST_USER'):
    val = prompt('Gmail address', 'e.g. yourapp@gmail.com')
    if val:
        contents = set_field(contents, 'EMAIL_HOST_USER', val)
        if not get_field(contents, 'DEFAULT_FROM_EMAIL'):
            contents = set_field(contents, 'DEFAULT_FROM_EMAIL', f'EasyMedRX <{val}>')
        if not get_field(contents, 'VAPID_ADMIN_EMAIL'):
            contents = set_field(contents, 'VAPID_ADMIN_EMAIL', val)
        changed = True
        ok(f'EMAIL_HOST_USER set to {val}')
    else:
        warn('EMAIL_HOST_USER left blank — email sending will not work')
else:
    skip('EMAIL_HOST_USER')

if not get_field(contents, 'EMAIL_HOST_PASSWORD'):
    val = prompt('Gmail App Password', 'myaccount.google.com → Security → App Passwords', secret=True)
    if val:
        contents = set_field(contents, 'EMAIL_HOST_PASSWORD', val)
        changed = True
        ok('EMAIL_HOST_PASSWORD set')
    else:
        warn('EMAIL_HOST_PASSWORD left blank — email sending will not work')
else:
    skip('EMAIL_HOST_PASSWORD')

if not get_field(contents, 'DEFAULT_FROM_EMAIL'):
    user = get_field(contents, 'EMAIL_HOST_USER')
    default = f'EasyMedRX <{user}>' if user else ''
    val = prompt('Display name + address for outgoing mail', default or 'e.g. EasyMedRX <you@gmail.com>')
    contents = set_field(contents, 'DEFAULT_FROM_EMAIL', val or default)
    changed = True
    ok('DEFAULT_FROM_EMAIL set')
else:
    skip('DEFAULT_FROM_EMAIL')

if not get_field(contents, 'VAPID_ADMIN_EMAIL'):
    user = get_field(contents, 'EMAIL_HOST_USER')
    contents = set_field(contents, 'VAPID_ADMIN_EMAIL', user)
    changed = True
    ok('VAPID_ADMIN_EMAIL set')
else:
    skip('VAPID_ADMIN_EMAIL')

if changed:
    write_env(contents)

# ── Step 4 — VAPID keys ───────────────────────────────────────────────────────

print('\n[3/4] VAPID push-notification keys')
pub  = get_field(contents, 'VAPID_PUBLIC_KEY')
priv = get_field(contents, 'VAPID_PRIVATE_KEY')

if pub and priv:
    skip('VAPID keys')
else:
    try:
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives import serialization

        private_key = ec.generate_private_key(ec.SECP256R1())
        public_key  = private_key.public_key()

        pub = base64.urlsafe_b64encode(
            public_key.public_bytes(
                serialization.Encoding.X962,
                serialization.PublicFormat.UncompressedPoint,
            )
        ).decode().rstrip('=')

        priv = base64.urlsafe_b64encode(
            private_key.private_bytes(
                serialization.Encoding.DER,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        ).decode().rstrip('=')

        contents = read_env()
        contents = set_field(contents, 'VAPID_PUBLIC_KEY',  pub)
        contents = set_field(contents, 'VAPID_PRIVATE_KEY', priv)
        write_env(contents)
        ok('VAPID key pair generated')
    except ImportError:
        warn('cryptography package not found — run: pip install cryptography')
    except Exception as ex:
        warn(f'VAPID generation failed: {ex}')

# ── Step 5 — SSL certificate ──────────────────────────────────────────────────

print('\n[4/4] SSL certificate (cert.pem / key.pem)')
if os.path.exists(CERT_PATH) and os.path.exists(KEY_PATH):
    skip('cert.pem and key.pem')
else:
    import socket as _sock
    try:
        s = _sock.socket(_sock.AF_INET, _sock.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        wsl_ip = s.getsockname()[0]
        s.close()
    except Exception:
        wsl_ip = '127.0.0.1'

    san = f'IP:{wsl_ip},IP:127.0.0.1,DNS:easymedrx.local,DNS:localhost'
    result = subprocess.run(
        ['openssl', 'req', '-x509', '-newkey', 'rsa:2048',
         '-keyout', KEY_PATH, '-out', CERT_PATH,
         '-days', '365', '-nodes',
         '-subj', '/CN=easymedrx.local',
         '-addext', f'subjectAltName={san}'],
        capture_output=True, text=True,
    )
    if result.returncode == 0:
        # Export DER format and ca.pem copy for MCU flashing
        base = os.path.dirname(CERT_PATH)
        subprocess.run(['openssl', 'x509', '-in', CERT_PATH, '-outform', 'DER',
                        '-out', os.path.join(base, 'cert.der')], capture_output=True)
        import shutil
        shutil.copy(CERT_PATH, os.path.join(base, 'ca.pem'))
        ok('cert.pem / key.pem generated (valid 365 days); ca.pem + cert.der ready for MCU')
    else:
        warn('openssl failed — is it installed?')
        print(f'    {result.stderr.strip()}')

# ── Done ──────────────────────────────────────────────────────────────────────

print('\n────────────────────────────────────────────────────────────────')
print('Done.  You can now run:  python startup.py')
print('────────────────────────────────────────────────────────────────\n')
