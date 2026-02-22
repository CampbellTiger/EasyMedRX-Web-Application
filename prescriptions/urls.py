from django.urls import path
from . import views
from django.contrib.auth.views import LogoutView
from .views import prescription_get

urlpatterns = [
    path('', views.prescription_calendar, name='calendar'),       # Homepage = calendar
    path('api/events/', views.prescription_events, name='prescription_events'),  # JSON feed
    path('logout/', LogoutView.as_view(next_page='/'), name='logout'),  # Logout path
    path('api/prescription/', prescription_get),
    path('ready/<int:prescription_id>/', views.prescription_ready_view, name='prescription_ready')
]
