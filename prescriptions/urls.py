from django.urls import path
from . import views
from django.contrib.auth.views import LogoutView, LoginView

urlpatterns = [
    path('', views.prescription_calendar, name='calendar'),
    path('api/events/', views.prescription_events, name='prescription_events'),
    path('login/', LoginView.as_view(template_name='prescriptions/login.html'), name='login'),
    path('logout/', LogoutView.as_view(next_page='/login/'), name='logout'),
    path('api/device/push/', views.device_push, name='device_push'),
    path('api/device/push', views.device_push),
    path('ready/<int:prescription_id>/', views.prescription_ready_view, name='prescription_ready'),
    path('rename/', views.rename_medication, name='rename_medication'),
    path('mcu-log/', views.mcu_log_view, name='mcu_log'),
    path('api/test/<str:kind>/', views.test_notification, name='test_notification'),
    path('notifications/preferences/', views.notification_preferences, name='notification_preferences'),
]
