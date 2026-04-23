import os
import time
import json as _json
from datetime import datetime

from django.contrib.admin.views.decorators import staff_member_required
from django.contrib.auth import authenticate
from django.contrib.auth.decorators import login_required
from django.contrib.auth.models import User
from django.shortcuts import render, redirect, get_object_or_404
from django.utils.dateparse import parse_datetime
from .models import Prescription, ErrorLog, PrescriptionLogging, UserProfile, NotificationPreference, DoseTime, MCUSession
from .forms import PrescriptionForm, CompartmentEditForm, NotificationPreferenceForm, DoseTimeFormSet
import calendar
from django.utils.timezone import now, make_aware, is_naive, localtime
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
from rest_framework.decorators import api_view
from rest_framework.response import Response

_BASE_DIR          = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_MCU_TRAFFIC_LOG   = os.path.join(_BASE_DIR, 'logs', 'mcu_traffic.log')
_MCU_HEARTBEAT_FILE = os.path.join(_BASE_DIR, 'logs', 'mcu_heartbeat.txt')

MAX_STOCK = 30


def _mcu_log(direction, data):
    """Append one traffic entry to mcu_traffic.log."""
    try:
        ts   = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        body = _json.dumps(data, indent=2) if isinstance(data, dict) else repr(data)
        entry = f"[{ts}] {direction}\n{body}\n{'─' * 60}\n"
        os.makedirs(os.path.dirname(_MCU_TRAFFIC_LOG), exist_ok=True)
        with open(_MCU_TRAFFIC_LOG, 'a') as f:
            f.write(entry)
    except Exception:
        pass


def _mcu_resp(data, status=200):
    """Log the outgoing response then return a JsonResponse."""
    _mcu_log('SERVER → MCU', data)
    return JsonResponse(data, status=status)


# ── MCU connection flag ───────────────────────────────────────────────────────
# Uses a small file (logs/mcu_heartbeat.txt) containing a Unix timestamp.
# Written on every POST; read when checking connection status.
# File I/O is the same mechanism already proven by mcu_traffic.log.

def _is_mcu_connected():
    """Return True if the MCU sent a request within the last 60 seconds."""
    try:
        with open(_MCU_HEARTBEAT_FILE) as f:
            last = float(f.read().strip())
        return (time.time() - last) < 60
    except Exception:
        return False


def _record_mcu_heartbeat():
    """Write the current Unix timestamp to the heartbeat file."""
    was_connected = _is_mcu_connected()
    try:
        os.makedirs(os.path.dirname(_MCU_HEARTBEAT_FILE), exist_ok=True)
        with open(_MCU_HEARTBEAT_FILE, 'w') as f:
            f.write(str(time.time()))
    except Exception:
        pass
    if not was_connected:
        _mcu_log('MCU STATUS', {
            'status':  'CONNECTED',
            'message': 'MCU is now sending requests',
        })


from .consumers import notify_user


def _mask(text, keep=2):
    """Return first `keep` chars + X padding for notification privacy."""
    if not text:
        return text
    return text[:keep] + 'X' * max(0, len(text) - keep)


@login_required
def prescription_ready_view(request, prescription_id):
    prescription = get_object_or_404(Prescription, id=prescription_id)
    notify_user(prescription.user_id, f"Prescription {_mask(prescription.medication_name)} is ready!")
    return JsonResponse({"status": "notification sent", "id": prescription.id})


@staff_member_required
def test_notification(request, kind):
    """Trigger a notification type immediately. Staff only. Used for testing via browser console."""
    from .tasks import (
        send_due_prescription_notifications,
        send_daily_report,
        send_weekly_report,
    )

    if kind == 'websocket':
        # Send a test WebSocket push to the requesting user
        notify_user(request.user.id, "EasyMedRX test — WebSocket notification working!")
        return JsonResponse({"status": "websocket sent", "user": request.user.username})

    if kind == 'desktop':
        # WebSocket message that the client will show as a desktop notification
        notify_user(request.user.id, "EasyMedRX test — Desktop notification working!")
        return JsonResponse({"status": "desktop triggered via websocket"})

    if kind == 'email-reminder':
        # Force the reminder task to run right now
        result = send_due_prescription_notifications.apply()
        return JsonResponse({"status": "email-reminder task run", "result": str(result.result)})

    if kind == 'daily-report':
        result = send_daily_report.apply()
        return JsonResponse({"status": "daily report task run", "result": str(result.result)})

    if kind == 'weekly-report':
        result = send_weekly_report.apply()
        return JsonResponse({"status": "weekly report task run", "result": str(result.result)})

    return JsonResponse({"error": f"Unknown notification kind: {kind}"}, status=400)


@login_required
def notification_preferences(request):
    prefs, _ = NotificationPreference.objects.get_or_create(user=request.user)
    if request.method == 'POST':
        form = NotificationPreferenceForm(request.POST, instance=prefs)
        if form.is_valid():
            form.save()
            return redirect('/notifications/preferences/?saved=1')
    else:
        form = NotificationPreferenceForm(instance=prefs)
    return render(request, 'prescriptions/notification_preferences.html', {'form': form})


COLOR_PALETTE = [
    "#1E90FF",  # blue
    "#28a745",  # green
    "#FFC107",  # yellow
    "#FF5733",  # orange-red
    "#6f42c1",  # purple
    "#fd7e14",  # orange
]



def _flat_mcu_response(uid, user, prescriptions, request_data=None):
    """Build the updateMCUProfile response the MCU expects.

    Fields per slot (0-based, maps to container 1-4):
      script{i}  — medication name
      dose{i}    — pills per dose
      stock{i}   — current stock
      window{i}  — pills to dispense right now
      device{i}  — device name the prescription belongs to
    Plus top-level: uid, user, email, phone, time.
    """
    by_container = {
        p.container: p
        for p in prescriptions
        if p.container in (1, 2, 3, 4)
    }

    resp = dict(request_data) if request_data else {}
    resp.update({
        'type':  'updateMCUProfile',
        'uid':   uid,
        'user':  user.username,
        'email': user.email,
        'phone': getattr(getattr(user, 'profile', None), 'phone', ''),
        'time':  localtime(now()).strftime('%Y-%m-%dT%H:%M:%S'),
    })
    for i in range(4):
        p = by_container.get(i + 1)   # container 1 → slot/index 0
        if p:
            resp[f'script{i}']  = p.medication_name
            resp[f'dose{i}']    = p.dose_count
            resp[f'stock{i}']   = p.stock_count
            resp[f'window{i}']  = p.dose_count
            resp[f'device{i}']  = p.device.device_id if p.device else ''
            if p.dose_count > 0:
                PrescriptionLogging.objects.create(
                    prescription=p,
                    user=user,
                    event_type='DISPENSE_SENT',
                    scheduled_time=now(),
                    quantity=p.dose_count,
                )
        else:
            resp[f'script{i}']  = ''
            resp[f'dose{i}']    = 0
            resp[f'stock{i}']   = 0
            resp[f'window{i}']  = 0
            resp[f'device{i}']  = ''
    return resp


def _sync_contact_info(user, email, phone):
    """Update the user's email and phone number if the MCU sent new values."""
    changed = False
    if email and user.email != email:
        user.email = email
        changed = True
    if changed:
        user.save(update_fields=['email'])

    if phone:
        profile, _ = UserProfile.objects.get_or_create(user=user)
        if profile.phone != phone:
            profile.phone = phone
            profile.save(update_fields=['phone'])


def _parse_event_timestamp(ts_string):
    """Parse an ISO timestamp string from the MCU into an aware datetime."""
    if not ts_string:
        return now()
    dt = parse_datetime(ts_string)
    if dt is None:
        return now()
    if is_naive(dt):
        dt = make_aware(dt)
    return dt

@csrf_exempt
def device_push(request):
    """Single endpoint for all MCU → web app communication."""
    if request.method != 'POST':
        return _mcu_resp({'error': 'POST required'}, status=405)

    _record_mcu_heartbeat()   # raise flag as soon as any POST arrives

    raw_body = request.body

    # Parse JSON body directly — no DRF overhead, tolerates null bytes in
    # Content-Type and minor body corruption from SLFS roundtrip.
    try:
        data = _json.loads(raw_body.decode('utf-8'))
    except Exception:
        try:
            text = raw_body.decode('utf-8', errors='ignore')
            data = _json.loads(text[:text.rfind('}') + 1])
        except Exception:
            data = {}

    uid        = data.get('uid') or data.get('UID')
    event_type = data.get('type') or data.get('Type')
    username   = data.get('user')
    password   = data.get('pword')
    email      = data.get('email', '')
    phone      = data.get('phone', '')

    _mcu_log('MCU → SERVER', data)

    if not event_type:
        return _mcu_resp({'error': 'type is required'}, status=400)

    event_lower = event_type.lower()

    # Look up user by username (case-insensitive to tolerate MCU capitalisation).
    # Fallback: look up by RFID UID stored in UserProfile.
    try:
        auth_user = User.objects.get(username__iexact=username) if username else None
    except User.DoesNotExist:
        auth_user = None

    if auth_user is None and uid:
        try:
            auth_user = UserProfile.objects.select_related('user').get(
                rfid_uid__iexact=uid
            ).user
        except UserProfile.DoesNotExist:
            pass


    # ── updateMCUProfile ─────────────────────────────────────────────────────
    # MCU sends this on every RFID scan.  Looks up the user by RFID uid and
    # returns their full prescription data.
    if event_lower == 'updatemcuprofile':
        if not auth_user:
            return _mcu_resp({'status': 'error', 'detail': 'unknown user'}, status=400)

        prescriptions = list(
            Prescription.objects.select_related('device').filter(user=auth_user).order_by('scheduled_time')
        )
        return _mcu_resp(_flat_mcu_response(uid, auth_user, prescriptions, data), status=200)

    # ── trustedUIDs ───────────────────────────────────────────────────────────
    # MCU requests the RFID whitelist. Returns up to 8 UIDs from UserProfile.rfid_uid.
    elif event_lower == 'trusteduids':
        uid_list = list(
            UserProfile.objects.exclude(rfid_uid='').values_list('rfid_uid', flat=True)[:8]
        )
        resp = {'type': 'trustedUIDs', 'uid': uid or ''}
        for i in range(8):
            resp[f'uid{i}'] = uid_list[i] if i < len(uid_list) else ''
        return _mcu_resp(resp, status=200)

    # ── updateWebAppStock / updateMCUStock ────────────────────────────────────
    # MCU reports current pill stock for each compartment. The response always
    # uses the DB's canonical name and stock (web app is authoritative on names).
    # If the MCU sends an old name after a rename, the slot-index fallback ensures
    # the response still carries the updated name and correct stock.
    elif event_lower in ('updatewebappstock', 'updatemcustock'):
        resp         = {'type': 'updateWebAppStock'}
        # Container-keyed map: container 1 = slot 0, etc.
        by_container = {
            p.container: p
            for p in Prescription.objects.all()
            if p.container in (1, 2, 3, 4)
        }

        for i in range(4):
            raw      = data.get(f'medicine{i}')
            medicine = raw if isinstance(raw, str) and raw.strip() else ''
            stock    = data.get(f'stock{i}')

            # Container is the authoritative slot — always look up by container.
            # Never look up by medicine name: the MCU may still know the old name
            # after a rename, which would match the wrong prescription entirely.
            p = by_container.get(i + 1)

            if p and stock is not None:
                if medicine.lower() == p.medication_name.lower():
                    # Names match — MCU is in sync. Apply add_stock bonus then reset, capped at MAX_STOCK.
                    old_stock     = p.stock_count
                    p.stock_count = min(int(stock) + p.add_stock, MAX_STOCK)
                    p.add_stock   = 0
                    p.save(update_fields=['stock_count', 'add_stock'])

                    # Only notify when stock first crosses below threshold (transition)
                    # and only during afternoon hours to avoid night-time spam.
                    if p.stock_count < 5 and old_stock >= 5 and localtime(now()).hour >= 12:
                        from .consumers import notify_user
                        prefs, _ = NotificationPreference.objects.get_or_create(user=p.user)
                        if prefs.low_stock_alert_enabled:
                            notify_user(p.user.id, f"Low stock: {p.medication_name} has {p.stock_count} pill(s) remaining.")
                # If names don't match the MCU is stale; return DB's name so it updates.

            resp[f'medicine{i}'] = p.medication_name if p else medicine
            resp[f'stock{i}']    = p.stock_count if p else 0

        return _mcu_resp(resp, status=200)

    # ── onlineLogin ───────────────────────────────────────────────────────────
    # MCU sends {"type": "onlineLogin", "uid": "<device_uid>"}.
    # Used when a patient has no RFID card — they log in via the MCU Login web
    # page instead.  The server looks up whoever is waiting in an MCUSession for
    # this device (matched by device_id == uid) and returns the full UpdateMCU
    # dispense JSON.  The session is deleted after one successful read so the
    # MCU's regular polling doesn't keep re-triggering it.
    elif event_lower == 'onlinelogin':
        mcu_session = (
            MCUSession.objects.select_related('user').filter(device_id=uid).first()
            if uid else
            MCUSession.objects.select_related('user').first()
        )

        if mcu_session is None:
            return _mcu_resp({'error': 'no MCU session'}, status=200)

        mcu_user = mcu_session.user
        try:
            user_rfid = mcu_user.profile.rfid_uid or ''
        except Exception:
            user_rfid = ''

        prescriptions = Prescription.objects.select_related('device').filter(user=mcu_user).order_by('scheduled_time')
        resp = _flat_mcu_response(user_rfid, mcu_user, prescriptions, data)
        mcu_session.delete()  # one-shot: clear so repeat polls don't re-trigger
        return _mcu_resp(resp, status=200)

    # ── Error ─────────────────────────────────────────────────────────────────
    # MCU sends {"type":"Error", "uid":..., "message":..., "time":...} for
    # device faults. No auth required. Response must include return_code=0
    # so flushErrorBuffer() on the MCU knows to delete the acknowledged file.
    elif event_lower == 'error':
        message = data.get('message', 'No message provided')
        ErrorLog.objects.create(
            device_id=uid,
            error_type='DEVICE_ERROR',
            detail=message,
            user=auth_user,
        )
        if auth_user:
            notify_user(auth_user.id, f"[Device Error] {message}")
        return _mcu_resp({
            'type':        'Error',
            'uid':         uid,
            'return_code': 0,
            'message':     'Error logged',
        }, status=200)

    # ── ErrorWebApp ──────────────────────────────────────────────────────────
    elif event_lower == 'errorwebapp':
        message = data.get('message', 'No message provided')
        ErrorLog.objects.create(
            device_id=uid,
            error_type='DEVICE_ERROR',
            detail=message,
            user=auth_user,
        )
        if auth_user:
            notify_user(auth_user.id, f"[Device Error] {message}")
        return _mcu_resp({
            'status':      'confirmed',
            'uid':         uid,
            'type':        'ErrorWebApp',
            'return_code': 0,
            'message':     'Error logged',
        }, status=200)

    # ── updateWebApp ─────────────────────────────────────────────────────────
    elif event_lower == 'updatewebapp':
        events = data.get('events')

        if not isinstance(events, list) or len(events) == 0:
            return _mcu_resp({'error': "'events' must be a non-empty list"}, status=400)

        processed = 0
        failed    = 0
        failures  = []

        # Cache user and prescription lookups to avoid repeated DB hits
        user_cache         = {}  # username → User | None
        prescription_cache = {}  # (user_id, medication_lower) → Prescription | None

        for i, event in enumerate(events):
            username        = event.get('user')
            evt_type        = (event.get('event_type') or '').upper()
            medication_name = event.get('medication_name')
            timestamp       = _parse_event_timestamp(event.get('timestamp'))

            # Resolve user (cached)
            if username not in user_cache:
                try:
                    user_cache[username] = User.objects.get(username=username)
                except User.DoesNotExist:
                    user_cache[username] = None

            user = user_cache[username]
            if user is None:
                failed += 1
                failures.append({'index': i, 'reason': f"User '{username}' not found"})
                continue

            if evt_type in ('TAKEN', 'MISSED'):
                # Resolve prescription (cached)
                cache_key = (user.id, (medication_name or '').lower())
                if cache_key not in prescription_cache:
                    prescription_cache[cache_key] = Prescription.objects.filter(
                        user=user,
                        medication_name__iexact=medication_name,
                    ).first()

                prescription = prescription_cache[cache_key]
                if not prescription:
                    failed += 1
                    failures.append({
                        'index': i,
                        'reason': f"No prescription '{medication_name}' for user '{username}'"
                    })
                    continue

                PrescriptionLogging.objects.create(
                    prescription=prescription,
                    user=user,
                    event_type=evt_type,
                    scheduled_time=timestamp,
                )
                processed += 1

            elif evt_type == 'ERROR':
                error_type = event.get('error_type', 'DEVICE_ERROR')
                detail     = event.get('detail', 'No detail provided')

                ErrorLog.objects.create(
                    device_id=uid,
                    error_type=error_type,
                    detail=detail,
                    user=user,
                )
                notify_user(user.id, f"[Offline Error] {error_type}: {detail}")
                processed += 1

            else:
                failed += 1
                failures.append({'index': i, 'reason': f"Unknown event_type '{evt_type}'"})

        total   = processed + failed
        message = (
            'All events logged' if failed == 0
            else f"{processed} of {total} events logged; {failed} failed"
        )

        return _mcu_resp({
            'status':    'confirmed',
            'uid':       uid,
            'type':      'updateWebApp',
            'processed': processed,
            'failed':    failed,
            'failures':  failures,
            'message':   message,
        }, status=200)

    # ── Unknown type ─────────────────────────────────────────────────────────
    else:
        return _mcu_resp({
            'error':           f"Unknown type '{event_type}'",
            'supported_types': ['updateMCUProfile', 'trustedUIDs',
                                'updateWebAppStock', 'onlineLogin', 'Error', 'ErrorWebApp', 'updateWebApp'],
        }, status=400)


def prescription_events(request):
    from datetime import timedelta

    # Auto-create a DoseTime from scheduled_time for any prescription missing one
    for p in Prescription.objects.filter(dose_times__isnull=True):
        DoseTime.objects.create(
            prescription=p,
            time_of_day=localtime(p.scheduled_time).time(),
            label='',
        )

    qs = DoseTime.objects.select_related('prescription__user')
    if not request.user.is_staff:
        qs = qs.filter(prescription__user=request.user)
    dose_times = qs.order_by('prescription__user_id', 'time_of_day')

    events      = []
    user_colors = {}
    color_index = 0

    for dt in dose_times:
        p   = dt.prescription
        uid = p.user_id
        if uid not in user_colors:
            user_colors[uid] = COLOR_PALETTE[color_index % len(COLOR_PALETTE)]
            color_index += 1

        end_recur = (
            (p.end_date + timedelta(days=1)).isoformat()
            if p.end_date else None
        )
        label_suffix = f' ({dt.label})' if dt.label else ''

        dur_h, dur_m = divmod(p.window_minutes, 60)
        events.append({
            'title':      p.medication_name + label_suffix,
            'startTime':  dt.time_of_day.strftime('%H:%M'),
            'duration':   f'{dur_h:02d}:{dur_m:02d}',
            'startRecur': p.start_date.isoformat(),
            'endRecur':   end_recur,
            'allDay':     False,
            'color':      user_colors[uid],
            'extendedProps': {
                'instructions': p.instructions,
                'user':         f'{p.user.first_name} {p.user.last_name}'.strip() or p.user.username,
                'dose':         f'{p.dose_count} pill(s)',
                'stock':        p.stock_count,
                'time':         dt.time_of_day.strftime('%I:%M %p'),
            },
        })
    return JsonResponse(events, safe=False)

def prescription_calendar(request):
    today = now().date()
    month = today.month
    year = today.year

    cal = calendar.HTMLCalendar(calendar.MONDAY)
    cal_data = cal.formatmonth(year, month)

    prescriptions = Prescription.objects.filter(
        scheduled_time__year=year,
        scheduled_time__month=month
    )

    prescription_days = {prescription.scheduled_time.day for prescription in prescriptions}

    return render(request, 'prescriptions/calendar.html', {
        'calendar': cal_data,
        'prescription_days': prescription_days,
        'month': calendar.month_name[month],
        'year': year,
    })

@login_required
def public_prescription_list(request):
    prescriptions = Prescription.objects.all().order_by('scheduled_time')
    return render(request, 'prescriptions/public_list.html', {
        'prescriptions': prescriptions
    })

@staff_member_required
def prescription_list(request):
    prescriptions = Prescription.objects.filter(user=request.user)
    return render(request, 'prescriptions/list.html', {
        'prescriptions': prescriptions
    })

@staff_member_required
def prescription_create(request):
    if request.method == 'POST':
        form = PrescriptionForm(request.POST)
        if form.is_valid():
            prescription = form.save(commit=False)
            prescription.user = request.user
            prescription.save()
            return redirect('prescription_list')
    else:
        form = PrescriptionForm()

    return render(request, 'prescriptions/form.html', {'form': form})


@staff_member_required
def mcu_log_view(request):
    """Display the MCU traffic log parsed into individual entries."""
    entries = []
    separator = '─' * 60

    try:
        with open(_MCU_TRAFFIC_LOG, encoding='utf-8', errors='replace') as f:
            raw = f.read()

        # Split on separator lines; each block is one log entry.
        blocks = raw.split(separator)
        for block in blocks:
            block = block.strip()
            if not block:
                continue

            lines = block.splitlines()
            if not lines:
                continue

            # First line: "[TIMESTAMP] DIRECTION"
            header = lines[0].strip()
            body   = '\n'.join(lines[1:]).strip()

            timestamp = ''
            direction = header
            if header.startswith('['):
                close = header.find(']')
                if close != -1:
                    timestamp = header[1:close]
                    direction = header[close + 1:].strip()

            # Attempt to pretty-print the body as JSON
            try:
                parsed   = _json.loads(body)
                body_fmt = _json.dumps(parsed, indent=2)
                is_json  = True
            except Exception:
                body_fmt = body
                is_json  = False

            entries.append({
                'timestamp': timestamp,
                'direction': direction,
                'body':      body_fmt,
                'is_json':   is_json,
            })

    except FileNotFoundError:
        pass

    # Newest entries first
    entries.reverse()

    return render(request, 'prescriptions/mcu_log.html', {
        'entries': entries,
        'log_path': _MCU_TRAFFIC_LOG,
    })


@staff_member_required
def rename_medication(request):
    """Full prescription edit form gated by MCU connection (admin only)."""
    from django.contrib.auth.models import User as _User
    from django.utils.timezone import localtime as _localtime

    mcu_connected = _is_mcu_connected()
    success = None
    form    = CompartmentEditForm()  # unbound — populated client-side via JS

    if request.method == 'POST':
        if not mcu_connected:
            pass  # warning shown in template; form stays unbound
        else:
            prescription_id = request.POST.get('prescription_id', '').strip()
            container_num   = int(request.POST.get('container_num', 0) or 0)

            if prescription_id:
                # Edit existing prescription
                p        = get_object_or_404(Prescription, id=prescription_id)
                old_name = p.medication_name
                form     = CompartmentEditForm(request.POST, instance=p)
                action   = 'updated'
            elif container_num:
                # Create new prescription for an empty container
                old_name = None
                form     = CompartmentEditForm(request.POST)
                action   = 'created'
            else:
                form = CompartmentEditForm()

            if form and container_num and form.is_bound:
                if form.is_valid():
                    saved = form.save(commit=False)
                    saved.container = container_num
                    saved.save()
                    form.save_m2m()

                    # Sync DoseTime records from POST data
                    import datetime as _dt
                    doses_per_day = saved.doses_per_day or 1
                    existing_dose_times = list(saved.dose_times.order_by('time_of_day'))

                    # Calculate evenly spaced fallback times from the first dose's time
                    first_time_str = request.POST.get('dose_time_1', '').strip()
                    if not first_time_str and existing_dose_times:
                        t = existing_dose_times[0].time_of_day
                        first_time_str = t.strftime('%H:%M')
                    if first_time_str:
                        fh, fm = map(int, first_time_str.split(':'))
                        first_mins = fh * 60 + fm
                    else:
                        first_mins = 0
                    interval = round(24 * 60 / doses_per_day)

                    for i in range(1, doses_per_day + 1):
                        time_val  = request.POST.get(f'dose_time_{i}', '').strip()
                        label_val = request.POST.get(f'dose_label_{i}', '').strip()
                        if not time_val:
                            mins = (first_mins + (i - 1) * interval) % (24 * 60)
                            time_val = f'{mins // 60:02d}:{mins % 60:02d}'
                        if i <= len(existing_dose_times):
                            dt_obj = existing_dose_times[i - 1]
                            dt_obj.time_of_day = time_val
                            dt_obj.label       = label_val
                            dt_obj.save(update_fields=['time_of_day', 'label'])
                        else:
                            DoseTime.objects.create(
                                prescription=saved,
                                time_of_day=time_val,
                                label=label_val,
                            )
                    # Remove extras if doses_per_day was reduced
                    extra_ids = list(saved.dose_times.order_by('-time_of_day').values_list('id', flat=True)[doses_per_day:])
                    if extra_ids:
                        DoseTime.objects.filter(id__in=extra_ids).delete()

                    # Save RFID UID to the patient's UserProfile
                    rfid_uid = form.cleaned_data.get('rfid_uid', '').strip()
                    profile, _ = UserProfile.objects.get_or_create(user=saved.user)
                    profile.rfid_uid = rfid_uid
                    profile.save(update_fields=['rfid_uid'])
                    _mcu_log('WEB EDIT', {
                        'action':    action,
                        'container': container_num,
                        'old_name':  old_name,
                        'new_name':  saved.medication_name,
                        'stock':     saved.stock_count,
                        'rfid_uid':  rfid_uid,
                        'by':        request.user.username,
                    })
                    success = f'Container {container_num} — "{saved.medication_name}" {action}.'

                    # Notify the patient that their prescription was updated
                    from .consumers import notify_user as _notify
                    prefs, _ = NotificationPreference.objects.get_or_create(user=saved.user)
                    if prefs.prescription_reminder_enabled:
                        _notify(saved.user.id, f"Your prescription for {saved.medication_name} has been updated.")
                    form = CompartmentEditForm()  # reset to unbound after success

    # Build compartments by container number (1-4) — authoritative slot source.
    by_container = {
        p.container: p
        for p in Prescription.objects.select_related('user__profile').all()
        if p.container in (1, 2, 3, 4)
    }
    compartments = [
        {'container': i, 'obj': by_container.get(i)}
        for i in range(1, 5)
    ]

    # Serialize prescription data for JS filldown — one entry per compartment.
    def _p_json(p):
        if p is None:
            return None
        try:
            rfid_uid = p.user.profile.rfid_uid
        except Exception:
            rfid_uid = ''
        return {
            'id':                 p.id,
            'user':               p.user_id,
            'rfid_uid':           rfid_uid,
            'medication_name':    p.medication_name,
            'dosage':             p.dosage,
            'dose_count':         p.dose_count,
            'stock_count':        p.stock_count,
            'add_stock':          p.add_stock,
            'doses_per_day':      p.doses_per_day,
            'dose_times':         [
                {'order': i + 1, 'time': dt.time_of_day.strftime('%H:%M'), 'label': dt.label}
                for i, dt in enumerate(p.dose_times.order_by('time_of_day'))
            ],
            'instructions':       p.instructions,
            'prescribing_doctor': p.prescribing_doctor,
            'start_date':         p.start_date.isoformat() if p.start_date else '',
            'end_date':           p.end_date.isoformat() if p.end_date else '',
            'scheduled_time':     _localtime(p.scheduled_time).strftime('%Y-%m-%dT%H:%M') if p.scheduled_time else '',
            'ready':              p.ready,
            'container':          p.container,
        }

    compartments_json = _json.dumps([
        {'container': c['container'], 'data': _p_json(c['obj'])}
        for c in compartments
    ])

    return render(request, 'prescriptions/rename_medication.html', {
        'compartments':      compartments,
        'compartments_json': compartments_json,
        'form':              form,
        'mcu_connected':     mcu_connected,
        'success':           success,
    })


def mcu_login_view(request):
    """Web page where a patient logs in as the active MCU dispense user for a device.

    One user per device can be logged in at a time.  Logging in for a device
    replaces any existing session for that device.  Multiple devices can each
    have a different active user simultaneously.
    The MCU onlineLogin event (carrying its own uid) reads this to know whose
    prescriptions to dispense.
    """
    from .models import Device as _Device

    all_sessions = MCUSession.objects.select_related('user').order_by('device_id')
    known_devices = list(_Device.objects.values_list('device_id', flat=True).order_by('id'))
    error   = None
    success = None

    if request.method == 'POST':
        action    = request.POST.get('action', '')
        device_id = request.POST.get('device_id', '').strip()

        if action == 'logout' and device_id:
            MCUSession.objects.filter(device_id=device_id).delete()
            return redirect('mcu_login')

        username = request.POST.get('username', '').strip()
        password = request.POST.get('password', '')

        if not device_id:
            error = 'Please enter or select a device ID.'
        else:
            user = authenticate(request, username=username, password=password)
            if user is None or not user.is_active:
                error = 'Invalid username or password.'
            else:
                MCUSession.objects.filter(device_id=device_id).delete()
                MCUSession.objects.create(user=user, device_id=device_id)
                all_sessions = MCUSession.objects.select_related('user').order_by('device_id')
                success = f'{user.username} is now logged in for device "{device_id}".'

    return render(request, 'prescriptions/mcu_login.html', {
        'all_sessions':  all_sessions,
        'known_devices': known_devices,
        'error':         error,
        'success':       success,
    })

