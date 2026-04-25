# EasyMedRX — Operational Manual

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Windows Installation (WSL2)](#windows-installation-wsl2)
3. [Linux Installation (Ubuntu / Raspberry Pi)](#linux-installation-ubuntu--raspberry-pi)
4. [First-Time Setup (Both Platforms)](#first-time-setup-both-platforms)
5. [Starting the Server](#starting-the-server)
6. [Accessing the Web Application](#accessing-the-web-application)
7. [Using the Application](#using-the-application)
8. [MCU Device Integration](#mcu-device-integration)
9. [Troubleshooting](#troubleshooting)

---

## System Overview

EasyMedRX is a Django-based web application that manages automated medication dispensing. It communicates with a physical MCU (microcontroller) pill dispenser over HTTP/HTTPS, sends prescription reminders and alerts via email and browser notifications, and provides a calendar-based dashboard for caregivers and patients.

**Core components:**

| Component | Technology |
|-----------|-----------|
| Web framework | Django 6.0.2 |
| Web/WebSocket server | Daphne 4.2.1 (ASGI) |
| Real-time notifications | Django Channels + Redis |
| Scheduled tasks | Celery + Celery Beat |
| Message broker | Redis |
| Database | SQLite3 |
| Email | Gmail SMTP |

**Ports used:**

| Port | Purpose |
|------|---------|
| 8000 | HTTP |
| 8443 | HTTPS (used by MCU) |
| 6379 | Redis (internal only) |

---

## Windows Installation (WSL2)

### Step 1 — Enable WSL2

Open PowerShell **as Administrator** and run:

```powershell
wsl --install
```

Restart your computer when prompted. After restart, open the Microsoft Store, search for **Ubuntu**, and install it. Launch Ubuntu from the Start Menu and complete the initial user setup (create a username and password).

### Step 2 — Update Ubuntu

Inside the Ubuntu terminal:

```bash
sudo apt update && sudo apt upgrade -y
```

### Step 3 — Install System Dependencies

```bash
sudo apt install -y python3 python3-pip python3-venv git redis-server openssl
```

Verify Python is installed:

```bash
python3 --version
```

You need Python 3.10 or higher.

### Step 4 — Clone or Copy the Repository

If cloning from Git:

```bash
cd ~
git clone https://github.com/CampbellTiger/EasyMedRX-Web-Application.git
cd EasyMedRX-Web-Application
```

If copying from a USB drive or folder, place the `EasyMedRX-Web-Application` folder in your home directory (`/home/<your-username>/`) and navigate into it:

```bash
cd ~/EasyMedRX-Web-Application
```

### Step 5 — Create a Python Virtual Environment

```bash
python3 -m venv venv
source venv/bin/activate
```

You should see `(venv)` at the start of your terminal prompt.

### Step 6 — Install Python Dependencies

```bash
pip install --upgrade pip
pip install -r requirements.txt
```

This installs all required packages including Django, Celery, Channels, Daphne, and Redis bindings.

### Step 7 — Configure Email and Secrets

Run the secrets generator:

```bash
python generate_secrets.py
```

This will prompt you for:
- Gmail address (e.g. `yourapp@gmail.com`)
- Gmail App Password (see note below)
- Admin email for push notifications

It will create a `.env` file with your credentials, a Django secret key, and VAPID keys for browser push notifications.

> **Gmail App Password:** Your regular Gmail password will not work. You must generate an App Password:
> 1. Go to [myaccount.google.com](https://myaccount.google.com)
> 2. Select **Security** → **2-Step Verification** (must be enabled)
> 3. Scroll down to **App Passwords**
> 4. Create a new app password and paste it when prompted

### Step 8 — Initialize the Database

```bash
python manage.py migrate
```

### Step 9 — Create an Admin Account

```bash
python manage.py createsuperuser
```

Enter a username, email, and password. This account is used to log into the web application and access the admin panel.

### Step 10 — Configure Windows Firewall (for MCU access)

The startup script handles this automatically, but if you need to do it manually, open PowerShell **as Administrator**:

```powershell
netsh advfirewall firewall add rule name="EasyMedRX HTTP" dir=in action=allow protocol=TCP localport=8000
netsh advfirewall firewall add rule name="EasyMedRX HTTPS" dir=in action=allow protocol=TCP localport=8443
```

---

## Linux Installation (Ubuntu / Raspberry Pi)

### Step 1 — Update the System

```bash
sudo apt update && sudo apt upgrade -y
```

### Step 2 — Install System Dependencies

**Ubuntu:**
```bash
sudo apt install -y python3 python3-pip python3-venv git redis-server openssl ufw
```

**Raspberry Pi (Raspberry Pi OS):**
```bash
sudo apt install -y python3 python3-pip python3-venv git redis-server openssl ufw
```

Verify Python version (3.10+ required):

```bash
python3 --version
```

If your version is below 3.10, upgrade Python:

```bash
sudo apt install -y python3.11
sudo update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.11 1
```

### Step 3 — Enable Redis on Boot

```bash
sudo systemctl enable redis-server
sudo systemctl start redis-server
```

Verify Redis is running:

```bash
redis-cli ping
```

Expected output: `PONG`

### Step 4 — Configure Firewall

```bash
sudo ufw allow 8000/tcp
sudo ufw allow 8443/tcp
sudo ufw enable
```

### Step 5 — Clone or Copy the Repository

```bash
cd ~
git clone https://github.com/CampbellTiger/EasyMedRX-Web-Application.git
cd EasyMedRX-Web-Application
```

Or copy the folder manually and navigate into it.

### Step 6 — Create a Python Virtual Environment

```bash
python3 -m venv venv
source venv/bin/activate
```

### Step 7 — Install Python Dependencies

```bash
pip install --upgrade pip
pip install -r requirements.txt
```

### Step 8 — Configure Email and Secrets

```bash
python generate_secrets.py
```

Enter your Gmail address, Gmail App Password, and admin email when prompted. See the Gmail App Password instructions in the Windows section above.

### Step 9 — Initialize the Database

```bash
python manage.py migrate
```

### Step 10 — Create an Admin Account

```bash
python manage.py createsuperuser
```

Enter a username, email, and password.

### Step 11 — (Raspberry Pi only) Set a Static IP

For reliable MCU communication, assign your Raspberry Pi a static IP address. Edit the DHCP config:

```bash
sudo nano /etc/dhcpcd.conf
```

Add the following at the bottom (adjust to match your network):

```
interface eth0
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=8.8.8.8
```

Save with `Ctrl+O`, exit with `Ctrl+X`, then reboot:

```bash
sudo reboot
```

---

## First-Time Setup (Both Platforms)

After installation and before starting the server for the first time:

### Collect Static Files

```bash
source venv/bin/activate   # if not already active
python manage.py collectstatic --noinput
```

### Verify Redis is Running

```bash
redis-cli ping
```

Must return `PONG`. If not:
- **WSL2/Ubuntu:** `redis-server --daemonize yes`
- **Raspberry Pi:** `sudo systemctl start redis-server`

### Verify the .env File Exists

```bash
cat .env
```

You should see `SECRET_KEY`, `EMAIL_HOST_USER`, `EMAIL_HOST_PASSWORD`, `VAPID_PUBLIC_KEY`, and `VAPID_PRIVATE_KEY` entries. If the file is missing, re-run `python generate_secrets.py`.

---

## Starting the Server

### Windows (WSL2)

**Option A — Batch file (recommended):**

In Windows File Explorer, navigate to the `EasyMedRX-Web-Application` folder, right-click `Start EasyMedRX.bat`, and select **Run as Administrator**.

This will:
- Open the Ubuntu terminal
- Start Redis, Celery Beat, Celery Worker, and both HTTP/HTTPS Daphne servers
- Configure Windows port forwarding so the MCU can reach the server
- Generate/refresh SSL certificates with the current network IP

**Option B — Manually from the Ubuntu terminal:**

```bash
cd ~/EasyMedRX-Web-Application
source venv/bin/activate
python startup.py
```

### Linux (Ubuntu / Raspberry Pi)

```bash
cd ~/EasyMedRX-Web-Application
source venv/bin/activate
python startup_pi.py
```

This starts:
1. Redis (if not already running)
2. Celery Beat — runs scheduled tasks (reminders, daily/weekly reports)
3. Celery Worker — processes task queue (sends emails, notifications)
4. Daphne HTTP server on port 8000
5. Daphne HTTPS server on port 8443

Logs are written to the `logs/` folder:

| Log file | What it covers |
|----------|---------------|
| `logs/daphne.log` | HTTP server activity |
| `logs/daphne_ssl.log` | HTTPS server activity |
| `logs/celery_worker.log` | Email and notification tasks |
| `logs/celery_beat.log` | Scheduled task triggers |
| `logs/mcu_traffic.log` | All MCU ↔ server communication |

### Stopping the Server

Press `Ctrl+C` in the terminal running the startup script. All background processes (Celery, Daphne) will be terminated automatically.

---

## Accessing the Web Application

Once the server is running, open a browser and go to:

| Platform | URL |
|----------|-----|
| Same machine (HTTP) | `http://localhost:8000` |
| Same machine (HTTPS) | `https://localhost:8443` |
| Another device on the network | `http://<server-ip>:8000` |

> **HTTPS certificate warning:** The server uses a self-signed certificate. Your browser will show a security warning. Click **Advanced → Proceed** (Chrome) or **Accept the Risk** (Firefox) to continue. This is expected behaviour.

Log in with the superuser account created during setup.

---

## Using the Application

### Dashboard (Calendar)

The home page shows a weekly/monthly calendar of all scheduled prescription doses. Click any event to see details including medication name, dosage, stock level, and instructions.

### Managing Prescriptions — Compartment Editor

Navigate to **Rename/Edit Medication** from the navigation menu.

> The MCU must be connected (sending requests within the last 60 seconds) before changes can be saved. A green **MCU Connected** badge appears at the top when it is.

**To edit a compartment:**
1. Click a compartment slot (1–4) on the page
2. Fill in the prescription fields:
   - **Medication Name** — name of the drug
   - **Dosage** — e.g. `500mg`
   - **Dose Count** — pills per dose
   - **Stock Count** — current number of pills in the compartment
   - **Add Stock** — enter a number here to immediately add pills to the current stock (e.g. enter `10` to add 10 pills)
   - **Doses Per Day** — how many times per day the dose should be taken
   - **Dose Times** — specific times for each daily dose
   - **Doctor** — prescribing doctor's name
   - **Start / End Date** — active date range for the prescription
   - **UID** — assign an RFID card to the patient
3. Click **Save**

A green banner will confirm the save. If pills were added via the **Add Stock** field, a separate "Pills added successfully" banner will also appear.

### Adding Pills to a Compartment

1. Open the Compartment Editor
2. Select the compartment
3. Enter the number of pills to add in the **Add Stock** field
4. Click **Save**

The stock count updates immediately. The patient receives a notification that their prescription was updated.

### MCU Login (Online Login)

Used when a patient does not have an RFID card and needs to authenticate via the web app instead.

1. Navigate to **MCU Login** from the menu
2. Enter the patient's username and password and the device ID
3. Click **Login**

The MCU will detect the session on its next poll and proceed with dispensing for that patient. The session remains active until the patient logs out from this page.

### Notification Preferences

Each user can configure their notification settings at `/notifications/preferences/`:

- **Email notifications** — receive emails for reminders, missed doses, low stock, etc.
- **Desktop notifications** — browser push notifications
- **Prescription reminders** — notified when it is time to take a dose
- **Low stock alerts** — notified when a compartment drops below 5 pills
- **Missed dose alerts** — notified when a dose window closes without dispensing
- **Device error alerts** — notified when the MCU reports a hardware error

### MCU Traffic Log

Navigate to **MCU Log** from the menu to view a live record of all communication between the MCU and the web server. Useful for diagnosing connection issues.

### Admin Panel

Navigate to `/admin/` and log in with a staff account to:
- Create and manage user accounts
- View and edit prescriptions directly
- Review error logs and prescription history
- Manage RFID cards and device assignments

---

## MCU Device Integration

### Connecting the MCU

The MCU communicates with the web server via HTTPS POST requests to:

```
https://<server-ip>:8443/api/device/push/
```

The server uses a self-signed SSL certificate. The MCU must have the `cert.der` file flashed to it to trust this certificate. This file is automatically generated in the project root each time the startup script runs.

If the MCU is on a separate network from the server, ensure port 8443 is forwarded through any routers between them.

### SSL Certificate for the MCU

After each startup, copy `cert.der` from the project root to the MCU's file system. On Raspberry Pi the file is at:

```
~/EasyMedRX-Web-Application/cert.der
```

On Windows (WSL2), the file is accessible at:

```
\\wsl$\Ubuntu\home\<username>\EasyMedRX-Web-Application\cert.der
```

### MCU Event Types

The MCU sends JSON payloads to `/api/device/push/`. Supported types:

| Type | Direction | Purpose |
|------|-----------|---------|
| `updateMCUProfile` | MCU → Server | RFID scan — fetch user's prescription data |
| `updateWebAppStock` | MCU → Server | Report current pill counts per compartment |
| `updateWebApp` | MCU → Server | Report dispensed pills (`dispPills0`–`dispPills3`) |
| `onlineLogin` | MCU → Server | Request RFID UID of web-logged-in user |
| `trustedUIDs` | MCU → Server | Request whitelist of known RFID UIDs |
| `Error` / `ErrorWebApp` | MCU → Server | Report a device fault |

---

## Troubleshooting

### Server won't start — "Address already in use"

A previous instance is still running. Kill it:

```bash
pkill -f daphne
pkill -f celery
```

Then re-run the startup script.

### Redis connection error

Celery and Channels both require Redis. Start it:

```bash
# Ubuntu / WSL2
redis-server --daemonize yes

# Raspberry Pi
sudo systemctl start redis-server
```

Confirm with `redis-cli ping` → should return `PONG`.

### Emails not sending

1. Confirm `.env` has `EMAIL_HOST_USER` and `EMAIL_HOST_PASSWORD` set
2. The password must be a **Gmail App Password**, not your regular Gmail password
3. Check `logs/celery_worker.log` for SMTP errors
4. Test directly from the browser console (staff only):
   ```
   fetch('/api/test/email-reminder/')
   ```

### MCU not connecting

1. Confirm the server is running and port 8443 is open
2. On Windows, confirm the firewall rule exists and the port proxy is active:
   ```powershell
   netsh interface portproxy show all
   ```
3. Confirm `cert.der` on the MCU matches the current certificate in the project root
4. Check `logs/mcu_traffic.log` — if entries appear, the MCU is reaching the server

### Browser shows "MCU Disconnected"

The MCU has not sent a request within the last 60 seconds. Check:
- MCU is powered on and connected to the network
- MCU has the correct server IP and port configured
- No firewall is blocking port 8443

### WebSocket notifications not working

1. Confirm Redis is running
2. Confirm you are accessing the app over HTTP (not a file:// URL)
3. Open the browser console — look for WebSocket connection errors
4. Restart the Daphne server and reload the page

### Database errors after a code update

Run migrations to apply any new schema changes:

```bash
source venv/bin/activate
python manage.py migrate
```

### Forgot admin password

Reset it from the terminal:

```bash
source venv/bin/activate
python manage.py changepassword <username>
```
