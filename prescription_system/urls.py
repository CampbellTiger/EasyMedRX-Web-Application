from django.contrib import admin
from django.urls import path, include
from django.contrib.auth.views import (
    LogoutView,
    PasswordResetView,
    PasswordResetDoneView,
    PasswordResetConfirmView,
    PasswordResetCompleteView,
)

admin.site.site_header = "EasyMedRX Administration"
admin.site.site_title = "EasyMedRX Admin Portal"
admin.site.index_title = "Welcome to EasyMedRX Admin Portal"

urlpatterns = [
    path('admin/', admin.site.urls),
    path('', include('prescriptions.urls')),

    # Password reset flow (also registers 'admin_password_reset' for the admin login link)
    path('admin/password_reset/',         PasswordResetView.as_view(),         name='admin_password_reset'),
    path('admin/password_reset/done/',    PasswordResetDoneView.as_view(),     name='password_reset_done'),
    path('reset/<uidb64>/<token>/',       PasswordResetConfirmView.as_view(),  name='password_reset_confirm'),
    path('reset/done/',                   PasswordResetCompleteView.as_view(), name='password_reset_complete'),
]
