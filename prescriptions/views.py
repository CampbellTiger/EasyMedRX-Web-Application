from django.contrib.admin.views.decorators import staff_member_required
from django.contrib.auth.decorators import login_required
from django.contrib.auth.models import User
from django.shortcuts import render, redirect, get_object_or_404
from django.utils.dateparse import parse_datetime
from .models import Prescription, Device, ErrorLog, PrescriptionLogging
from .forms import PrescriptionForm
import calendar
from django.utils.timezone import now, make_aware, is_naive
from django.http import JsonResponse

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
    """Return a user dict with all their prescriptions for UpdateMCU responses."""
    prescriptions = Prescription.objects.filter(user=user).order_by('scheduled_time')
    return {
        'username':    user.username,
        'first_name':  user.first_name,
        'last_name':   user.last_name,
        'prescriptions': [
            {
                'id':                p.id,
                'medication_name':   p.medication_name,
                'dosage':            p.dosage,
                'frequency':         p.frequency,
                'instructions':      p.instructions,
                'prescribing_doctor': p.prescribing_doctor,
                'start_date':        p.start_date.isoformat(),
                'end_date':          p.end_date.isoformat() if p.end_date else None,
                'scheduled_time':    p.scheduled_time.strftime('%H:%M'),
            }
            for p in prescriptions
        ],
    }


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


@api_view(['POST'])
def device_push(request):
    """
    Single endpoint for all MCU → web app communication.

    Required fields in every request:
        UID  — device identifier
        Type — one of: error | UpdateMCU | updateWebApp

    ── Type: error ─────────────────────────────────────────────────────────
    Sent by the MCU when an error occurs. Web app logs it and notifies the
    user, then returns a confirmation JSON.

    MCU sends:
        {
            "UID":        "device-001",
            "Type":       "error",
            "user":       "john_doe",
            "error_type": "SENSOR_FAILURE",
            "detail":     "Temperature sensor not responding"
        }

    Web app returns:
        {
            "status":  "confirmed",
            "uid":     "device-001",
            "type":    "error",
            "message": "Error logged"
        }

    ── Type: UpdateMCU ─────────────────────────────────────────────────────
    MCU requests a full data sync. Web app returns every user and all their
    prescription details (medication, dosage, frequency, schedule, etc.).

    MCU sends:
        {
            "UID":  "device-001",
            "Type": "UpdateMCU"
        }

    Web app returns:
        {
            "status": "ok",
            "uid":    "device-001",
            "users": [
                {
                    "username":   "john_doe",
                    "first_name": "John",
                    "last_name":  "Doe",
                    "prescriptions": [
                        {
                            "id":                1,
                            "medication_name":   "Metformin",
                            "dosage":            "500mg",
                            "frequency":         "Twice daily",
                            "instructions":      "Take with food",
                            "prescribing_doctor": "Dr. Smith",
                            "start_date":        "2026-03-01",
                            "end_date":          "2026-06-01",
                            "scheduled_time":    "08:00"
                        }
                    ]
                }
            ]
        }

    ── Type: updateWebApp ──────────────────────────────────────────────────
    Sent by the MCU after the web app was offline. Contains all events that
    occurred on the device while the web app was unreachable. Web app logs
    each event to the database, then returns a confirmation JSON.

    MCU sends:
        {
            "UID":  "device-001",
            "Type": "updateWebApp",
            "events": [
                {
                    "user":            "john_doe",
                    "event_type":      "TAKEN",
                    "medication_name": "Metformin",
                    "timestamp":       "2026-03-21T08:05:00"
                },
                {
                    "user":            "jane_doe",
                    "event_type":      "MISSED",
                    "medication_name": "Lisinopril",
                    "timestamp":       "2026-03-21T12:00:00"
                },
                {
                    "user":       "john_doe",
                    "event_type": "ERROR",
                    "error_type": "JAM",
                    "detail":     "Pill dispenser jammed",
                    "timestamp":  "2026-03-21T09:00:00"
                }
            ]
        }

    Web app returns:
        {
            "status":    "confirmed",
            "uid":       "device-001",
            "type":      "updateWebApp",
            "processed": 3,
            "failed":    0,
            "message":   "All events logged"
        }
    """
    uid        = request.data.get('UID')
    event_type = request.data.get('Type')

    if not uid or not event_type:
        return Response({'error': 'UID and Type are required'}, status=400)

    event_lower = event_type.lower()

    # ── error ────────────────────────────────────────────────────────────────
    if event_lower == 'error':
        username   = request.data.get('user')
        error_type = request.data.get('error_type', 'DEVICE_ERROR')
        detail     = request.data.get('detail', 'No detail provided')

        if not username:
            return Response({'error': "'user' is required for type 'error'"}, status=400)

        try:
            user = User.objects.get(username=username)
        except User.DoesNotExist:
            return Response({'error': f"User '{username}' not found"}, status=404)

        ErrorLog.objects.create(
            device_id=uid,
            error_type=error_type,
            detail=detail,
            user=user,
        )
        notify_user(user.id, f"[Device Error] {error_type}: {detail}")

        return Response({
            'status':  'confirmed',
            'uid':     uid,
            'type':    'error',
            'message': 'Error logged',
        }, status=200)

    # ── UpdateMCU ────────────────────────────────────────────────────────────
    elif event_lower == 'updatemcu':
        users = User.objects.prefetch_related('prescriptions').all()
        return Response({
            'status': 'ok',
            'uid':    uid,
            'users':  [_serialize_user_prescriptions(u) for u in users],
        }, status=200)

    # ── updateWebApp ─────────────────────────────────────────────────────────
    elif event_lower == 'updatewebapp':
        events = request.data.get('events')

        if not isinstance(events, list) or len(events) == 0:
            return Response({'error': "'events' must be a non-empty list"}, status=400)

        processed = 0
        failed    = 0
        failures  = []

        for i, event in enumerate(events):
            username        = event.get('user')
            evt_type        = (event.get('event_type') or '').upper()
            medication_name = event.get('medication_name')
            timestamp       = _parse_event_timestamp(event.get('timestamp'))

            # Resolve user
            try:
                user = User.objects.get(username=username)
            except User.DoesNotExist:
                failed += 1
                failures.append({'index': i, 'reason': f"User '{username}' not found"})
                continue

            if evt_type in ('TAKEN', 'MISSED'):
                # Match prescription by medication name
                prescription = Prescription.objects.filter(
                    user=user,
                    medication_name__iexact=medication_name,
                ).first()

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
            'error':           f"Unknown Type '{event_type}'",
            'supported_types': ['error', 'UpdateMCU', 'updateWebApp'],
        }, status=400)


@api_view(['POST'])  #HTTP connection code
def prescription_get(request):
    device_id = request.data.get('device_id')

    if not device_id:
        return Response({'error': "device_id required"}, status=400)

    try:
        device = Device.objects.select_related("patient").get(device_id=device_id)
    except Device.DoesNotExist:
        return Response({"error": "unknown device"}, status=404)

    prescription = (
        Prescription.objects
        .filter(user=device.patient, ready=True)
        .first()
    )

    if not prescription:
        return Response({'command': 'NO_PRESCRIPTION'}, status=200)

    return Response({
        "user_id": device.patient.id,
        "pill": prescription.medication_name,
        "dosage": prescription.dosage,
    }, status=200)

def prescription_events(request):
    prescriptions = Prescription.objects.select_related('user').all()
    events = []

    unique_users = list({p.user.id: p.user for p in prescriptions}.values())

    user_colors = {}
    for i, user in enumerate(unique_users):
        user_colors[user.id] = COLOR_PALETTE[i % len(COLOR_PALETTE)]

    for prescription in prescriptions:
        events.append({
            'title': prescription.medication_name,
            'instructions': prescription.instructions,
            'user': f'{prescription.user.first_name} {prescription.user.last_name}',
            'start': prescription.start_date.isoformat(),
            'end': prescription.end_date.isoformat() if prescription.end_date else None,
            'time': prescription.scheduled_time.strftime('%H:%M'),
            'allDay': True,
            'color': user_colors[prescription.user.id],
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

