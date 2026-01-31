from django.contrib.auth.decorators import login_required
from django.contrib.admin.views.decorators import staff_member_required
from django.shortcuts import render, redirect, get_object_or_404
from .models import Prescription
from .forms import PrescriptionForm
import calendar
from datetime import date 
from django.utils.timezone import now
from django.http import JsonResponse

def prescription_events(request):
    prescriptions = Prescription.objects.all()
    events = []
    for prescription in prescriptions:
        events.append({
            'title': prescription.medication_name,
            'instructions': prescription.instructions,
            'user': f'{prescription.user.first_name} {prescription.user.last_name}',
            'start': prescription.start_date.isoformat(),
            'end': prescription.end_date.isoformat(),
            'time': prescription.scheduled_time.time(),
            'allDay': True,
           
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

