GUI application to compare responses of 2 SmartMet server instanses.
(GTKMM)[https://gtkmm.gnome.org/en/index.html] version 3 is used for GUI.
Application may get requests
- by reading them from text file (one request per line with host part removed)
- by fetching last requests of specified number of minutes from SmartMet server (note requires admin access to backend server, which should normally be blocked outside local network)

Sample screenshot of application:
<img width="1912" height="1166" alt="image" src="doc/smartmet-server-compare-screenshot.png" />
