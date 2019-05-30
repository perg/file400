# file400

IBM i record level access for Python 3 

Today we use iseries Python 2.7, and we have a lot of python modules that access the database using the file400 module.

We are in the process of converting to Python 3.6 pase for i.
The biggest concern was the database access, and the only option was the ibm-db-dbi moudle.

We have an Ubuntu box with postgres, that runs the same python programs, but with a layer on top of psycopg2 that emulates fie400. The db2 database changes is copied to the Ubuntu postgres database using journals, during the week, and when the IBM i is unavailable during full backup a few hours every week, this Ubunto box takes over.

I decided to use this same layer on top of ibm-db-dbi on the IBM i but the performance hit was to big for us.

I then converted the db2 module and used that instead of ibm-db-dbi, but it didn't make big difference.

Now I have rewritten the file400 module, and with this the performance is at least as good as before.
