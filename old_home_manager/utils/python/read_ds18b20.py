#!/usr/bin/python
# This file is: /usr/share/cacti/site/scripts/flow_temps.py

#cat /sys/bus/w1/devices/28-0000072a0843/w1_slave
#18 ff 4b 46 7f ff 08 10 8b : crc=8b YES
#18 ff 4b 46 7f ff 08 10 8b t=-14500

import os, time, sys, datetime
from os.path import exists

#Set up the location of the DS18B20 sensors in the system
device_folder = '/sys/bus/w1/devices/'
device_file = '/w1_slave'

device_to_check = device_folder + sys.argv[1] + device_file

def read_temp_raw(): #A function that grabs the raw temp data from the sensors
    f_1 = open(device_to_check, 'r')
    lines_1 = f_1.readlines()
    f_1.close()
    outfile_str = '/tmp/' + sys.argv[1]
    file_object = open(outfile_str, 'a')
    now = datetime.datetime.now()
    cur_time = now.strftime("%Y%m%d %H:%M:%H")
    file_object.write( cur_time )
    file_object.write( '\n' )
    file_object.write( ''.join( lines_1 ) )
    file_object.close()
    return lines_1

def read_temp(): #A function to check the connection was good and strip out the temperature
	lines = read_temp_raw()
	while lines[0].strip()[-3:] != 'YES':
		time.sleep(0.2)
		lines = read_temp_raw()
	equals_pos = lines[1].find('t=')
	temp = float(lines[1][equals_pos+2:])/1000
	return temp

if exists( device_to_check ) :
    try :
        temp = read_temp() #get the temp
    except BaseException :
        temp = 99999
    print(temp)
else :
    print("99999")
