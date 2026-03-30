from django.contrib.admin.views.decorators import staff_member_required
from django.contrib.auth import authenticate
from django.contrib.auth.decorators import login_required
from django.contrib.auth.models import User
from django.shortcuts import render, redirect, get_object_or_404
from django.utils.dateparse import parse_datetime
from .models import Prescription, ErrorLog, PrescriptionLogging, UserProfile
from .forms import PrescriptionForm
import calendar
from django.utils.timezone import now, make_aware, is_naive, localtime
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
from rest_framework.decorators import api_view
from rest_framework.response import Response

from .consumers import notify_user


@login_required
def prescription_ready_view(request, prescription_id):
    prescription = get_object_or_404(Prescription, id=prescription_id)
    notify_user(prescription.user_id, f"Prescription {prescription.medication_name} is ready!")
    return JsonResponse({"status": "notification sent", "id": prescription.id})

COLOR_PALETTE = [
    "#1E90FF",  # blue
    "#28a745",  # green
    "#FFC107",  # yellow
    "#FF5733",  # orange-red
    "#6f42c1",  # purple
    "#fd7e14",  # orange
]


def _serialize_user_prescriptions(user):
    """Return a user dict with all their prescriptions for UpdateMCU admin responses.
    Relies on the caller having used prefetch_related('prescriptions') so that
    user.prescriptions.all() hits the prefetch cache instead of the database."""
    return {
        'username':    user.username,
        'first_name':  user.first_name,
        'last_name':   user.last_name,
        'prescriptions': [
            {
                'id':                p.id,
                'medication_name':   p.medication_name,
                'dosage':            p.dosage,
                'dose_count':        p.dose_count,
                'stock_count':       p.stock_count,
                'frequency':         p.frequency,
                'instructions':      p.instructions,
                'prescribing_doctor': p.prescribing_doctor,
                'start_date':        p.start_date.isoformat(),
                'end_date':          p.end_date.isoformat() if p.end_date else None,
                'scheduled_time':    p.scheduled_time.strftime('%H:%M'),
                'time_window':       _scheduled_seconds(p),
            }
            for p in user.prescriptions.all()
        ],
    }


def _scheduled_seconds(prescription):
    """Return the prescription's scheduled time-of-day as seconds since midnight (int32).
    The MCU uses this value to know when to dispense each medication."""
    t = prescription.scheduled_time
    return t.hour * 3600 + t.minute * 60


def _flat_mcu_response(uid, user, prescriptions):
    """Build the flat script0-3 / dose0-3 / stock0-3 / time0-3 response the MCU expects."""
    slots = list(prescriptions[:4])  # MCU supports up to 4 slots
    resp  = {
        'type':  'UpdateMCU',
        'uid':   uid,
        'user':  user.username,
        'email': user.email,
        'phone': getattr(getattr(user, 'profile', None), 'phone', ''),
    }
    for i in range(4):
        if i < len(slots):
            p = slots[i]
            resp[f'script{i}'] = p.medication_name
            resp[f'dose{i}']   = p.dose_count
            resp[f'stock{i}']  = p.stock_count
            resp[f'time{i}']   = _scheduled_seconds(p)
        else:
            resp[f'script{i}'] = ''
            resp[f'dose{i}']   = 0
            resp[f'stock{i}']  = 0
            resp[f'time{i}']   = 0
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
@api_view(['POST'])
def device_push(request):
    """
    Single endpoint for all MCU → web app communication.

    Every request must include:
        uid   — device identifier  (MCU sends lowercase; UID uppercase also accepted)
        type  — message type       (MCU sends lowercase; Type uppercase also accepted)
        user  — username for authentication
        pword — password for authentication

    ── type: UpdateMCU ─────────────────────────────────────────────────────
    MCU requests a data sync. Authenticates the user and returns that user's
    prescriptions in the flat MCU format (script0-3, dose0-3, stock0-3,
    time0-3). Staff/admin credentials return all users in nested format.

    MCU sends:
        {
            "type":    "UpdateMCU",
            "uid":     "11223344",
            "user":    "Bugs",
            "pword":   "Bunny",
            "email":   "DaphyDuck@gmail.com",
            "phone":   "904-393-9032",
            "script0": "Tic-Tacs",  "dose0": 10,  "stock0": 5000,
            "script1": "Gobstoppers","dose1": 10, "stock1": 1000,
            "script2": "Atomic Fireballs","dose2":100,"stock2":3000,
            "script3": "Skittles",  "dose3": 70,  "stock3": 170,
            "time":    1711234567
        }

    Web app returns (regular user):
        {
            "type": "UpdateMCU", "uid": "11223344", "user": "Bugs",
            "email": "...", "phone": "...",
            "script0": "Tic-Tacs",  "dose0": 10, "stock0": 5000, "time0": 28800,
            "script1": "Gobstoppers","dose1": 10, "stock1":1000,  "time1": 52200,
            "script2": "",          "dose2":  0, "stock2":    0, "time2":     0,
            "script3": "",          "dose3":  0, "stock3":    0, "time3":     0
        }

    Web app returns (staff/admin credentials — admin mode):
        {
            "status": "ok", "uid": "...", "admin": true,
            "users": [ { "username":..., "prescriptions": [...] }, ... ]
        }

    ── type: ErrorWebApp ───────────────────────────────────────────────────
    MCU sends when it encounters a device error. Web app logs it, notifies
    the user, and returns confirmation.

    MCU sends:
        {
            "type":    "ErrorWebApp",
            "uid":     "11223344",
            "user":    "plaintext_username",
            "pword":   "plaintext_password",
            "email":   "helloworld@gmail.com",
            "phone":   "000-000-0000",
            "message": "Unable to connect to web application"
        }

    Web app returns:
        { "status": "confirmed", "uid": "...", "type": "ErrorWebApp", "message": "Error logged" }

    ── type: updateWebApp ──────────────────────────────────────────────────
    MCU sends batched offline events. Web app logs each event and confirms.

    MCU sends:
        {
            "type": "updateWebApp", "uid": "...",
            "user": "...", "pword": "...",
            "events": [
                { "user": "john_doe", "event_type": "TAKEN",
                  "medication_name": "Metformin", "timestamp": "2026-03-21T08:05:00" },
                { "user": "jane_doe", "event_type": "MISSED",
                  "medication_name": "Lisinopril","timestamp": "2026-03-21T12:00:00" }
            ]
        }

    Web app returns:
        { "status": "confirmed", "uid": "...", "processed": 2, "failed": 0, "message": "All events logged" }
    """
    # Accept both lowercase (MCU native) and uppercase (legacy) key names
    uid        = request.data.get('uid') or request.data.get('UID')
    event_type = request.data.get('type') or request.data.get('Type')
    username   = request.data.get('user')
    password   = request.data.get('pword')
    email      = request.data.get('email', '')
    phone      = request.data.get('phone', '')

    if not uid or not event_type:
        return Response({'error': 'uid and type are required'}, status=400)

    event_lower = event_type.lower()

    # Look up user by username (no password check)
    try:
        auth_user = User.objects.get(username=username) if username else None
    except User.DoesNotExist:
        auth_user = None

    # # Update email / phone from device if supplied. Dont need right now. MCU shouldn't update web application's stuff?
    # if auth_user:
    #     _sync_contact_info(auth_user, email, phone)

    # ── UpdateMCU ────────────────────────────────────────────────────────────
    if event_lower == 'updatemcu':
        if not auth_user:
            return Response({'status': 'error', 'detail': 'unknown user'}, status=400)
        # Update any stock counts the MCU reports for this user's prescriptions
        prescriptions = list(
            Prescription.objects.filter(user=auth_user).order_by('scheduled_time')
        )
        for i in range(4):
            stock_val = request.data.get(f'stock{i}')
            if stock_val is not None and i < len(prescriptions):
                prescriptions[i].stock_count = int(stock_val)
                prescriptions[i].save(update_fields=['stock_count'])

        if auth_user and (auth_user.is_staff or auth_user.is_superuser):
            # Admin mode — return all non-staff users with full prescription detail
            users = (
                User.objects
                .filter(prescriptions__isnull=False, is_staff=False, is_superuser=False)
                .prefetch_related('prescriptions')
                .distinct()
            )
            return Response({
                'status': 'ok',
                'uid':    uid,
                'admin':  True,
                'users':  [_serialize_user_prescriptions(u) for u in users],
            }, status=200)

        # Regular user — return flat MCU format
        return Response(_flat_mcu_response(uid, auth_user, prescriptions), status=200)

    # ── ErrorWebApp ──────────────────────────────────────────────────────────
    elif event_lower == 'errorwebapp':
        message = request.data.get('message', 'No message provided')
        ErrorLog.objects.create(
            device_id=uid,
            error_type='DEVICE_ERROR',
            detail=message,
            user=auth_user,
        )
        if auth_user:
            notify_user(auth_user.id, f"[Device Error] {message}")
        return Response({
            'status':  'confirmed',
            'uid':     uid,
            'type':    'ErrorWebApp',
            'message': 'Error logged',
        }, status=200)

    # ── updateWebApp ─────────────────────────────────────────────────────────
    elif event_lower == 'updatewebapp':
        events = request.data.get('events')

        if not isinstance(events, list) or len(events) == 0:
            return Response({'error': "'events' must be a non-empty list"}, status=400)

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

        return Response({
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
        return Response({
            'error':           f"Unknown type '{event_type}'",
            'supported_types': ['UpdateMCU', 'ErrorWebApp', 'updateWebApp'],
        }, status=400)


def prescription_events(request):
    prescriptions = Prescription.objects.select_related('user').all()
    events      = []
    user_colors = {}
    color_index = 0

    for prescription in prescriptions:
        uid = prescription.user_id
        if uid not in user_colors:
            user_colors[uid] = COLOR_PALETTE[color_index % len(COLOR_PALETTE)]
            color_index += 1

        events.append({
            'title':        prescription.medication_name,
            'instructions': prescription.instructions,
            'user':         f'{prescription.user.first_name} {prescription.user.last_name}',
            'start':        prescription.start_date.isoformat(),
            'end':          prescription.end_date.isoformat() if prescription.end_date else None,
            'time':         localtime(prescription.scheduled_time).strftime('%H:%M'),
            'allDay':       True,
            'color':        user_colors[uid],
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

