#!/usr/bin/env python
import smtplib
from email.mime.multipart import MIMEMultipart
from email.mime.base import MIMEBase
from email.mime.text import MIMEText
from email.utils import COMMASPACE, formatdate
from email import encoders
import os
import sys
import getopt
import json

def parseAddresses( a, to_address ):
    # check for space separated or comma separated (neither are allowed in addresses) and deal with it
    if a.strip().find(",") != -1:
        comma_list = a.strip().split(",")
        for i in comma_list:
            to_address.append( i )
    elif a.strip().find(" ") != -1:
        space_list = a.strip().split(" ")
        for i in space_list:
            to_address.append( i )
    else:
        to_address.append( a )

def sendMail(to, subject, text, use_html, username, password, files=[] ):
    assert type(to)==list
    assert type(files)==list

    msg = MIMEMultipart()
    msg['From'] = username
    msg['To'] = COMMASPACE.join(to)
    msg['Date'] = formatdate(localtime=True)
    msg['Subject'] = subject
    
    if ( use_html ):
        msg.attach( MIMEText(text, 'html') )
    else:
        msg.attach( MIMEText(text, 'plain') )

    for file in files:
        part = MIMEBase('application', "octet-stream")
        part.set_payload( open(file,"rb").read() )
        Encoders.encode_base64(part)
        part.add_header('Content-Disposition', 'attachment; filename="%s"'
                       % os.path.basename(file))
        msg.attach(part)

    server = smtplib.SMTP('smtp.gmail.com:587')
    server.ehlo_or_helo_if_needed()
    server.starttls()
    server.ehlo_or_helo_if_needed()
    server.login(username,password)
    success = True
    try :
        server.sendmail(username, to, msg.as_string())
    except :
        success = False
        server.quit()
        sys.exit(3)

    server.quit()


if ( len( sys.argv ) < 2 ):
    print ("Usage: ", sys.argv[0], "-e email_address -a attachment -b \"body text\" -s \"subject\" -t \"plain_text\" -j \"json_file\" -u \"username\" -p \"password\"\n")
    sys.exit(1)

to_address = [];
attachment = [];
body = "";
username = "";
password = "";
try:
  opts, args = getopt.getopt(sys.argv[1:], 'e:b:s:a:u:p:j:h', ["email=","body=","subject=","attachment=","username=","password=","json=","help"])
  #-l is one of many arguments I'm looking for
except getopt.error as err:
  print (str(err))
  sys.exit(2)

use_html = True
for o, a in opts:
    if o in ("-b", "--body"):  #body
        body = a
        print(body)
    if o in ("-e", "--email"):  #email_address
        parseAddresses( a, to_address )
    if o in ("-s", "--subject"):  #subject
        subject = a
    if o in ("-a", "--attachment"):  #attachment
        attachment.append( a )
    if o in ("-j", "--json"):
        json_file = a
        with open(a, 'r') as f:
            data = json.load(f)
        to_json = data["to"]
        for address in to_json:
            to_address.append( address )
        subject = data["subject"]
        if "attachment" in data:
            attachment_json = data["attachment"]
            for attach in attachment_json:
                attachment.append( attach )
        body = data["body"]
        if "html" in data:
            use_html = data["html"]
    if o in ("-u", "--username"):
        username = a
    if o in ("-p", "--password"):
        password = a

#print( "Goign to send an email to: ", to_address)
#print( " with subject: ", subject)
#print( " with body: ", body)
#print( " with attachment: ", attachment)
#print( " with username: ", username)
#print( " with password: ", password)

sendMail( to_address,
          subject,
          body,
          use_html,
          username,
          password,
          attachment
        )
