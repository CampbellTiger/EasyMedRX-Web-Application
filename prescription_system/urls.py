from django.contrib import admin
from django.urls import path, include
from django.contrib.auth.views import LogoutView

admin.site.site_header = "EasyMedRX Administration"
admin.site.site_title = "EasyMedRX Admin Portal"
admin.site.index_title = "Welcome to EasyMedRX Admin Portal"

urlpatterns = [
    path('admin/', admin.site.urls),
    path('', include('prescriptions.urls')),


]
