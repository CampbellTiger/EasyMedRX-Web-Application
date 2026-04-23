from collections import defaultdict
from celery import shared_task
from django.contrib.auth.models import User
from django.db.models import Q
from django.utils import timezone
from django.core.mail import send_mail
from django.conf import settings

from .models import Prescription, PrescriptionLogging, ErrorLog, NotificationPreference, DoseTime
from .consumers import notify_user


def _mask(text, keep=2):
    """Return first `keep` chars + X padding, e.g. 'Aspirin' → 'AsXXXXX'."""
    if not text:
        return text
    visible = text[:keep]
    return visible + 'X' * max(0, len(text) - keep)


@shared_task
def reset_prescription_ready_flags():
    """Reset ready=True prescriptions to False each midnight so they can fire again the next day."""
    updated = Prescription.objects.filter(ready=True).update(ready=False)
    return f"Reset {updated} prescription(s) to ready=False"


@shared_task
def send_due_prescription_notifications():
    now   = timezone.localtime()
    today = now.date()

    # Match DoseTime records whose time_of_day hour:minute equals right now,
    # for active prescriptions not yet notified at this exact time today.
    # Deduplication is via PrescriptionLogging (not prescription.ready, which
    # is for MCU dispensing and stays True after the first dose fires).
    due_dose_times = (
        DoseTime.objects
        .select_related('prescription__user')
        .filter(
            time_of_day__hour=now.hour,
            time_of_day__minute=now.minute,
            prescription__start_date__lte=today,
        )
        .filter(
            Q(prescription__end_date__isnull=True) | Q(prescription__end_date__gte=today)
        )
        .exclude(
            prescription__logs__event_type='REMINDER_SENT',
            prescription__logs__scheduled_time__date=today,
            prescription__logs__scheduled_time__hour=now.hour,
            prescription__logs__scheduled_time__minute=now.minute,
        )
    )

    for dt in due_dose_times:
        p = dt.prescription
        p.ready = True
        p.last_notified = now
        p.save(update_fields=['ready', 'last_notified'])

        prefs, _ = NotificationPreference.objects.get_or_create(user=p.user)

        masked_med = _mask(p.medication_name)

        # WebSocket / desktop notification
        if prefs.prescription_reminder_enabled and prefs.desktop_notification_enabled:
            print(f"[TASK] Sending notification for {p.medication_name} to user_id={p.user.id}")
            try:
                notify_user(p.user.id, f"Time to take {masked_med}!")
                print(f"[TASK] notify_user succeeded")
            except Exception as e:
                print(f"[TASK] notify_user FAILED: {e}")

        # Prescription logging — record the specific dose time that fired
        dose_time_aware = timezone.make_aware(
            timezone.datetime.combine(today, dt.time_of_day)
        )
        PrescriptionLogging.objects.create(
            user=p.user,
            prescription=p,
            event_type="REMINDER_SENT",
            scheduled_time=dose_time_aware,
        )

        # Email — only if enabled and the user has an address on file
        if prefs.prescription_reminder_enabled and prefs.email_enabled and p.user.email:
            display_name  = _mask(p.user.first_name or p.user.username)
            app_url       = getattr(settings, 'APP_BASE_URL', 'http://localhost:8000')
            dose_label    = f" ({dt.label})" if dt.label else ""
            end_date_line = f"End date:   {p.end_date}\n" if p.end_date else ""
            body = (
                f"Hello {display_name},\n\n"
                f"This is a reminder to take your medication.\n\n"
                f"Medication:  {masked_med}{dose_label}\n"
                f"Dosage:      {_mask(p.dosage)}\n"
                f"Doses/day:   {p.doses_per_day}\n"
                f"Scheduled:   {dt.time_of_day.strftime('%I:%M %p')}\n"
                f"Start date:  {p.start_date}\n"
                f"{end_date_line}"
                f"Doctor:      {_mask(p.prescribing_doctor)}\n"
            )
            if p.instructions:
                body += f"\nInstructions:\n{_mask(p.instructions)}\n"

            body += (
                f"\n─────────────────────────────\n"
                f"Open EasyMedRX: {app_url}\n"
            )

            send_mail(
                subject=f"Medication Reminder: {masked_med}",
                message=body,
                from_email=settings.DEFAULT_FROM_EMAIL,
                recipient_list=[p.user.email],
                fail_silently=True,
            )

    # ── Early window open notification ────────────────────────────────────────
    # Fire when now == dose_time - window_before_minutes for any active dose.
    # Skipped when window_before_minutes == 0.
    early_dose_times = (
        DoseTime.objects
        .select_related('prescription__user', 'prescription')
        .filter(
            prescription__start_date__lte=today,
        )
        .filter(
            Q(prescription__end_date__isnull=True) | Q(prescription__end_date__gte=today)
        )
        .exclude(prescription__window_before_minutes=0)
    )

    for dt in early_dose_times:
        p = dt.prescription
        dose_time_aware = timezone.make_aware(
            timezone.datetime.combine(today, dt.time_of_day)
        )
        open_at = dose_time_aware - timezone.timedelta(minutes=p.window_before_minutes)
        if open_at.hour != now.hour or open_at.minute != now.minute:
            continue

        already_sent = PrescriptionLogging.objects.filter(
            prescription=p,
            event_type='REMINDER_SENT',
            scheduled_time__date=today,
            scheduled_time__hour=dt.time_of_day.hour,
            scheduled_time__minute=dt.time_of_day.minute,
        ).exists()
        if already_sent:
            continue

        prefs, _ = NotificationPreference.objects.get_or_create(user=p.user)
        if prefs.prescription_reminder_enabled:
            label_suffix = f' ({dt.label})' if dt.label else ''
            masked_med   = _mask(p.medication_name)
            if prefs.desktop_notification_enabled:
                try:
                    notify_user(
                        p.user.id,
                        f"Medication window open: {masked_med}{label_suffix} "
                        f"can be taken now (scheduled {dt.time_of_day.strftime('%I:%M %p')})."
                    )
                except Exception:
                    pass
            if prefs.email_enabled and p.user.email:
                app_url       = getattr(settings, 'APP_BASE_URL', 'http://localhost:8000')
                display_name  = _mask(p.user.first_name or p.user.username)
                end_date_line = f"End date:   {p.end_date}\n" if p.end_date else ""
                body = (
                    f"Hello {display_name},\n\n"
                    f"Your medication window is now open — you can take your dose.\n\n"
                    f"Medication:  {masked_med}{label_suffix}\n"
                    f"Dosage:      {_mask(p.dosage)}\n"
                    f"Scheduled:   {dt.time_of_day.strftime('%I:%M %p')}\n"
                    f"Window:      {p.window_minutes} minutes\n"
                    f"Start date:  {p.start_date}\n"
                    f"{end_date_line}"
                    f"Doctor:      {_mask(p.prescribing_doctor)}\n"
                    f"\n─────────────────────────────\n"
                    f"Open EasyMedRX: {app_url}\n"
                )
                send_mail(
                    subject=f"Medication Window Open: {masked_med}",
                    message=body,
                    from_email=settings.DEFAULT_FROM_EMAIL,
                    recipient_list=[p.user.email],
                    fail_silently=True,
                )

    # ── Window warning (5 min before expiry) and missed notifications ─────────
    # Query all active DoseTimes directly — no dependency on REMINDER_SENT so
    # these fire even if the initial reminder had a delivery hiccup.
    # A dose is considered dispensed when the MCU logs DISPENSE_SENT within
    # the full dose window (window_before_minutes before → window_minutes after).
    all_active_dose_times = (
        DoseTime.objects
        .select_related('prescription__user', 'prescription')
        .filter(
            prescription__start_date__lte=today,
        )
        .filter(
            Q(prescription__end_date__isnull=True) | Q(prescription__end_date__gte=today)
        )
    )

    for dt in all_active_dose_times:
        p = dt.prescription
        dose_time_aware = timezone.make_aware(
            timezone.datetime.combine(today, dt.time_of_day)
        )
        window_open  = dose_time_aware - timezone.timedelta(minutes=p.window_before_minutes)
        missed_at    = dose_time_aware + timezone.timedelta(minutes=p.window_minutes)
        warn_minutes = max(p.window_minutes - 5, 0)
        warn_at      = dose_time_aware + timezone.timedelta(minutes=warn_minutes)

        # Skip if the MCU already dispensed this dose within the full window
        already_dispensed = PrescriptionLogging.objects.filter(
            prescription=p,
            event_type='DISPENSE_SENT',
            scheduled_time__gte=window_open,
            scheduled_time__lt=missed_at,
        ).exists()
        if already_dispensed:
            continue

        prefs, _ = NotificationPreference.objects.get_or_create(user=p.user)
        if not prefs.missed_dose_alert_enabled:
            continue

        masked_med   = _mask(p.medication_name)
        label_suffix = f' ({dt.label})' if dt.label else ''

        # 5-minute window warning — only meaningful when window > 5 min
        if p.window_minutes > 5 and warn_at.hour == now.hour and warn_at.minute == now.minute:
            already_warned = PrescriptionLogging.objects.filter(
                prescription=p,
                event_type='WARNING',
                scheduled_time__date=today,
                scheduled_time__hour=dt.time_of_day.hour,
                scheduled_time__minute=dt.time_of_day.minute,
            ).exists()
            if not already_warned:
                try:
                    notify_user(
                        p.user.id,
                        f"5 minutes left to take {masked_med}{label_suffix}!",
                    )
                except Exception:
                    pass
                PrescriptionLogging.objects.create(
                    user=p.user,
                    prescription=p,
                    event_type='WARNING',
                    scheduled_time=dose_time_aware,
                )
                if prefs.email_enabled and p.user.email:
                    app_url       = getattr(settings, 'APP_BASE_URL', 'http://localhost:8000')
                    display_name  = _mask(p.user.first_name or p.user.username)
                    end_date_line = f"End date:   {p.end_date}\n" if p.end_date else ""
                    body = (
                        f"Hello {display_name},\n\n"
                        f"You have 5 minutes left to take your medication before the window closes.\n\n"
                        f"Medication:  {masked_med}{label_suffix}\n"
                        f"Dosage:      {_mask(p.dosage)}\n"
                        f"Scheduled:   {dt.time_of_day.strftime('%I:%M %p')}\n"
                        f"Window closes at: {missed_at.strftime('%I:%M %p')}\n"
                        f"Start date:  {p.start_date}\n"
                        f"{end_date_line}"
                        f"Doctor:      {_mask(p.prescribing_doctor)}\n"
                        f"\n─────────────────────────────\n"
                        f"Open EasyMedRX: {app_url}\n"
                    )
                    send_mail(
                        subject=f"5 Minutes Left: {masked_med}",
                        message=body,
                        from_email=settings.DEFAULT_FROM_EMAIL,
                        recipient_list=[p.user.email],
                        fail_silently=True,
                    )

        # Missed notification — fires at dose_time + window_minutes if not dispensed
        if missed_at.hour == now.hour and missed_at.minute == now.minute:
            already_missed = PrescriptionLogging.objects.filter(
                prescription=p,
                event_type='MISSED',
                scheduled_time__date=today,
                scheduled_time__hour=dt.time_of_day.hour,
                scheduled_time__minute=dt.time_of_day.minute,
            ).exists()
            if not already_missed:
                try:
                    notify_user(
                        p.user.id,
                        f"Missed dose: {masked_med}{label_suffix} — window has closed.",
                    )
                except Exception:
                    pass

                PrescriptionLogging.objects.create(
                    user=p.user,
                    prescription=p,
                    event_type='MISSED',
                    scheduled_time=dose_time_aware,
                )

                # Email alert for missed dose
                if prefs.email_enabled and p.user.email:
                    display_name  = _mask(p.user.first_name or p.user.username)
                    app_url       = getattr(settings, 'APP_BASE_URL', 'http://localhost:8000')
                    end_date_line = f"End date:   {p.end_date}\n" if p.end_date else ""
                    body = (
                        f"Hello {display_name},\n\n"
                        f"A scheduled dose was NOT dispensed and the time window has closed.\n\n"
                        f"Medication:  {masked_med}{label_suffix}\n"
                        f"Dosage:      {_mask(p.dosage)}\n"
                        f"Scheduled:   {dt.time_of_day.strftime('%I:%M %p')}\n"
                        f"Window:      {p.window_minutes} minutes\n"
                        f"Start date:  {p.start_date}\n"
                        f"{end_date_line}"
                        f"Doctor:      {_mask(p.prescribing_doctor)}\n"
                        f"\nPlease contact your caregiver or take the medication as soon as possible "
                        f"if it is still safe to do so.\n"
                        f"\n─────────────────────────────\n"
                        f"Open EasyMedRX: {app_url}\n"
                    )
                    send_mail(
                        subject=f"Missed Dose Alert: {masked_med}",
                        message=body,
                        from_email=settings.DEFAULT_FROM_EMAIL,
                        recipient_list=[p.user.email],
                        fail_silently=True,
                    )


@shared_task
def send_daily_report():
    """Send an end-of-day summary email to all staff with an email address."""
    now   = timezone.localtime()
    today = now.date()
    day_start = timezone.make_aware(
        timezone.datetime.combine(today, timezone.datetime.min.time())
    )
    day_end = now

    # ── Gather data ───────────────────────────────────────────────────────────

    logs = (
        PrescriptionLogging.objects
        .select_related('user', 'prescription')
        .filter(event_time__gte=day_start, event_time__lte=day_end)
        .order_by('user__username', 'event_time')
    )

    errors = (
        ErrorLog.objects
        .select_related('user')
        .filter(timestamp__gte=day_start, timestamp__lte=day_end)
        .order_by('timestamp')
    )

    prescriptions = (
        Prescription.objects
        .select_related('user')
        .all()
        .order_by('user__username', 'container')
    )

    # ── Build report ──────────────────────────────────────────────────────────

    sep  = '─' * 52
    lines = [
        'EasyMedRX — Daily Report',
        f'Date : {today.strftime("%A, %B %d %Y")}',
        f'Generated : {now.strftime("%I:%M %p")}',
        sep,
    ]

    # Per-user activity
    users_seen = {}
    for log in logs:
        users_seen.setdefault(log.user, []).append(log)

    if users_seen:
        lines.append('\nACTIVITY BY PATIENT')
        lines.append(sep)
        for user, entries in users_seen.items():
            name = f'{user.first_name} {user.last_name}'.strip() or user.username
            lines.append(f'\n{name} ({user.username})')
            for e in entries:
                ts  = timezone.localtime(e.event_time).strftime('%I:%M %p')
                med = e.prescription.medication_name
                qty = f'  ×{e.quantity}' if e.quantity else ''
                lines.append(f'  {ts}  {e.get_event_type_display():<22}  {med}{qty}')
    else:
        lines.append('\nACTIVITY BY PATIENT')
        lines.append(sep)
        lines.append('  No activity recorded today.')

    # Dispense summary
    dispenses = [l for l in logs if l.event_type == 'DISPENSE_SENT']
    lines.append(f'\nDISPENSE SUMMARY  ({len(dispenses)} total)')
    lines.append(sep)
    if dispenses:
        tally = {}
        for d in dispenses:
            key = (d.user.username, d.prescription.medication_name)
            tally[key] = tally.get(key, 0) + (d.quantity or 1)
        for (username, med), count in tally.items():
            lines.append(f'  {username:<20}  {med:<24}  {count} pill(s) dispensed')
    else:
        lines.append('  No dispenses today.')

    # TAKEN / MISSED
    taken  = [l for l in logs if l.event_type == 'TAKEN']
    missed = [l for l in logs if l.event_type == 'MISSED']
    lines.append(f'\nTAKEN: {len(taken)}   MISSED: {len(missed)}')
    lines.append(sep)
    for l in taken + missed:
        ts   = timezone.localtime(l.event_time).strftime('%I:%M %p')
        name = l.user.username
        lines.append(f'  [{l.event_type:<6}]  {ts}  {name:<16}  {l.prescription.medication_name}')

    # Current stock
    lines.append('\nCURRENT STOCK LEVELS')
    lines.append(sep)
    if prescriptions:
        for p in prescriptions:
            container = f'Container {p.container}' if p.container else 'Unassigned'
            add       = f'  (+{p.add_stock} pending)' if p.add_stock else ''
            lines.append(
                f'  {container}  {p.medication_name:<24}'
                f'  {p.stock_count} pills{add}'
                f'  [{p.user.username}]'
            )
    else:
        lines.append('  No prescriptions on file.')

    # Errors
    lines.append(f'\nERRORS  ({errors.count()} today)')
    lines.append(sep)
    if errors:
        for e in errors:
            ts   = timezone.localtime(e.timestamp).strftime('%I:%M %p')
            user = e.user.username if e.user else 'unknown'
            lines.append(f'  {ts}  [{e.error_type}]  {e.detail}  (device: {e.device_id}, user: {user})')
    else:
        lines.append('  No errors today.')

    lines.append(f'\n{sep}')
    lines.append('EasyMedRX Automated Report')

    body = '\n'.join(lines)

    # ── Send to all staff with an email address ────────────────────────────────

    recipients = list(
        User.objects.filter(is_staff=True)
        .exclude(email='')
        .values_list('email', flat=True)
    )

    if not recipients:
        return 'No staff email addresses configured — report not sent.'

    send_mail(
        subject=f'EasyMedRX Daily Report — {today.strftime("%b %d %Y")}',
        message=body,
        from_email=settings.DEFAULT_FROM_EMAIL,
        recipient_list=recipients,
        fail_silently=False,
    )

    return f'Daily report sent to {recipients}'


@shared_task
def send_weekly_report():
    """Send a weekly summary email to all staff covering the past 7 days."""
    now        = timezone.localtime()
    week_end   = now
    week_start = timezone.make_aware(
        timezone.datetime.combine(
            (now - timezone.timedelta(days=6)).date(),
            timezone.datetime.min.time(),
        )
    )
    start_label = week_start.strftime('%b %d')
    end_label   = now.strftime('%b %d %Y')

    # ── Gather data ───────────────────────────────────────────────────────────

    logs = (
        PrescriptionLogging.objects
        .select_related('user', 'prescription')
        .filter(event_time__gte=week_start, event_time__lte=week_end)
        .order_by('user__username', 'event_time')
    )

    errors = (
        ErrorLog.objects
        .select_related('user')
        .filter(timestamp__gte=week_start, timestamp__lte=week_end)
        .order_by('timestamp')
    )

    prescriptions = (
        Prescription.objects
        .select_related('user')
        .all()
        .order_by('user__username', 'container')
    )

    # ── Build report ──────────────────────────────────────────────────────────

    sep   = '─' * 52
    lines = [
        'EasyMedRX — Weekly Report',
        f'Period    : {start_label} – {end_label}',
        f'Generated : {now.strftime("%I:%M %p")}',
        sep,
    ]

    # Per-user activity grouped by day
    by_user_day = defaultdict(lambda: defaultdict(list))
    for log in logs:
        day = timezone.localtime(log.event_time).date()
        by_user_day[log.user][day].append(log)

    if by_user_day:
        lines.append('\nACTIVITY BY PATIENT')
        lines.append(sep)
        for user, days in by_user_day.items():
            name = f'{user.first_name} {user.last_name}'.strip() or user.username
            lines.append(f'\n{name} ({user.username})')
            for day in sorted(days):
                lines.append(f'  {day.strftime("%A, %b %d")}')
                for e in days[day]:
                    ts  = timezone.localtime(e.event_time).strftime('%I:%M %p')
                    med = e.prescription.medication_name
                    qty = f'  ×{e.quantity}' if e.quantity else ''
                    lines.append(f'    {ts}  {e.get_event_type_display():<22}  {med}{qty}')
    else:
        lines.append('\nACTIVITY BY PATIENT')
        lines.append(sep)
        lines.append('  No activity recorded this week.')

    # Dispense summary
    dispenses = [l for l in logs if l.event_type == 'DISPENSE_SENT']
    lines.append(f'\nDISPENSE SUMMARY  ({len(dispenses)} total this week)')
    lines.append(sep)
    if dispenses:
        tally = {}
        for d in dispenses:
            key = (d.user.username, d.prescription.medication_name)
            tally[key] = tally.get(key, 0) + (d.quantity or 1)
        for (username, med), count in sorted(tally.items()):
            lines.append(f'  {username:<20}  {med:<24}  {count} pill(s) dispensed')
    else:
        lines.append('  No dispenses this week.')

    # TAKEN / MISSED totals + per-day breakdown
    taken  = [l for l in logs if l.event_type == 'TAKEN']
    missed = [l for l in logs if l.event_type == 'MISSED']
    lines.append(f'\nTAKEN: {len(taken)}   MISSED: {len(missed)}  (week total)')
    lines.append(sep)

    # Group taken/missed by day
    tm_by_day = defaultdict(list)
    for l in taken + missed:
        tm_by_day[timezone.localtime(l.event_time).date()].append(l)
    for day in sorted(tm_by_day):
        lines.append(f'  {day.strftime("%A, %b %d")}')
        for l in tm_by_day[day]:
            ts   = timezone.localtime(l.event_time).strftime('%I:%M %p')
            lines.append(f'    [{l.event_type:<6}]  {ts}  {l.user.username:<16}  {l.prescription.medication_name}')

    # Adherence rate
    reminders = [l for l in logs if l.event_type == 'REMINDER_SENT']
    if reminders:
        rate = round(len(taken) / len(reminders) * 100)
        lines.append(f'\n  Adherence this week: {rate}%  ({len(taken)} taken / {len(reminders)} reminders sent)')

    # Current stock snapshot
    lines.append('\nCURRENT STOCK LEVELS')
    lines.append(sep)
    if prescriptions:
        for p in prescriptions:
            container = f'Container {p.container}' if p.container else 'Unassigned'
            add       = f'  (+{p.add_stock} pending)' if p.add_stock else ''
            lines.append(
                f'  {container}  {p.medication_name:<24}'
                f'  {p.stock_count} pills{add}'
                f'  [{p.user.username}]'
            )
    else:
        lines.append('  No prescriptions on file.')

    # Errors
    lines.append(f'\nERRORS  ({errors.count()} this week)')
    lines.append(sep)
    if errors:
        for e in errors:
            ts   = timezone.localtime(e.timestamp).strftime('%a %b %d  %I:%M %p')
            user = e.user.username if e.user else 'unknown'
            lines.append(f'  {ts}  [{e.error_type}]  {e.detail}  (device: {e.device_id}, user: {user})')
    else:
        lines.append('  No errors this week.')

    lines.append(f'\n{sep}')
    lines.append('EasyMedRX Automated Weekly Report')

    body = '\n'.join(lines)

    # ── Send to all staff with an email address ───────────────────────────────

    recipients = list(
        User.objects.filter(is_staff=True)
        .exclude(email='')
        .values_list('email', flat=True)
    )

    if not recipients:
        return 'No staff email addresses configured — weekly report not sent.'

    send_mail(
        subject=f'EasyMedRX Weekly Report — {start_label} – {end_label}',
        message=body,
        from_email=settings.DEFAULT_FROM_EMAIL,
        recipient_list=recipients,
        fail_silently=False,
    )

    return f'Weekly report sent to {recipients}'
