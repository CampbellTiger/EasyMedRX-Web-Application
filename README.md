# EasyMedRX-Web-Application

To launch the code. Open CMD prompt

Use cd to change the directory to where these files are located. Such as "cd Desktop\EasyMedRX" which is what I use when its on my desktop. 
Run code 
"venv\Scripts\activate"
This opens up a virtual python environment

You should see the interface go to \venv\.

Finally use the command
"py manage.py runserver"

When editing the code, you need to migrate all the new code to the manage.py file. To do this, go into venv.
Then type code
"py manage.py makemigrations"
"py manage.py migrate"

Afterwards, you can use the runserver command again.
