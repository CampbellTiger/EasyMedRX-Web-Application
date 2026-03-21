from django.urls import path
from . import views
from django.contrib.auth.views import LogoutView

urlpatterns = [
    path('', views.prescription_calendar, name='calendar'),
    path('api/events/', views.prescription_events, name='prescription_events'),
    path('logout/', LogoutView.as_view(next_page='/'), name='logout'),
    path('api/device/push/', views.device_push, name='device_push'),
    path('api/device/push', views.device_push),
    path('ready/<int:prescription_id>/', views.prescription_ready_view, name='prescription_ready'),
]
