from django.urls import path
from . import views

urlpatterns = [
    path('', views.prescription_calendar, name='calendar'),       # Homepage = calendar
    path('api/events/', views.prescription_events, name='prescription_events'),  # JSON feed
]
