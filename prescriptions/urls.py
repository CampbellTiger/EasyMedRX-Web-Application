from django.urls import path
from . import views
from django.contrib.auth.views import LogoutView
urlpatterns = [
    path('', views.prescription_calendar, name='calendar'),       # Homepage = calendar
    path('api/events/', views.prescription_events, name='prescription_events'),  # JSON feed
    path('logout/', LogoutView.as_view(next_page='/'), name='logout'),  # Logout path
]
