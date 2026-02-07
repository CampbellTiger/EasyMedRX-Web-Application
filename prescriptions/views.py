from django.contrib.auth.decorators import login_required
from django.contrib.admin.views.decorators import staff_member_required
from django.shortcuts import render, redirect, get_object_or_404
from .models import Prescription
from .forms import PrescriptionForm
import calendar
from datetime import date 
from django.utils.timezone import now
from django.http import JsonResponse

#rest framework
from rest_framework.decorators import api_view
from rest_framework.response import Response
from rest_framework import status
from .models import Device
from .serializers import DeviceSerializer

COLOR_PALETTE = [
    "#1E90FF",  # blue
    "#28a745",  # green
    "#FFC107",  # yellow
    "#FF5733",  # orange-red
    "#6f42c1",  # purple
    "#fd7e14",  # orange
]

@api_view(['GET'])
def prescription_get(request):
    device_id = request.query_params.get('device_id')

    if not device_id:
        return Response({'error': "device_id required"}, status=800)

    try:
        device = Device.objects.select_related("user").get(device_id=device_id)
    except Device.DoesNotExist:
        return Response({"error": "unknown device"}, status=404)
    
    prescription = (
        Prescription.objects
        .filter(user=device.user, active=True)
        .first()
    )

    if not prescription:
        return Response({'command':'NO_PRESCRIPTION'}, status=200)

    return Response({
        "user_id": device.user.user_id,
        "pill": prescription.medication_name,
        "dosage": prescription.dosage,
    })

def prescription_events(request):
    prescriptions = Prescription.objects.all()
    events = []

    unique_users = list({p.user.id: p.user for p in prescriptions}.values())

    # Map each user ID to a color
    user_colors = {}
    for i, user in enumerate(unique_users):
        user_colors[user.id] = COLOR_PALETTE[i % len(COLOR_PALETTE)]  # cycle colors if more users than colors

    for prescription in prescriptions:
        events.append({
            'title': prescription.medication_name,
            'instructions': prescription.instructions,
            'user': f'{prescription.user.first_name} {prescription.user.last_name}',
            'start': prescription.start_date.isoformat(),
            'end': prescription.end_date.isoformat(),
            'time': prescription.scheduled_time.time(),
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

