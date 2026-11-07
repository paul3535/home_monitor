# https://developers.google.com/gmail/api/guides/sending#python
# https://fabianlee.org/2019/10/23/python-sending-html-emails-via-gmail-api-or-smtp-relay/
# git clone https://github.com/fabianlee/python-send-html-email.git

import json

import sys
import argparse
import base64
import os
import csv

# mail
import smtplib
import email.utils
import mimetypes
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.mime.image import MIMEImage
from email.mime.base import MIMEBase

SCOPES = ['https://mail.google.com/']
def createHTMLMessage(sender, to, subject, msg_html, msg_plain, attachment_file_list):
    """Create a message for an email.

    Args:
      sender: Email address of the sender.
      to: Email address of the receiver (csv)
      subject: The subject of the email message.
      msg_html: html version of message
      msg_plain: plain text version of message
      attachment_file_list: array of files to be attached

    Returns:
      An email message object
    """

    # outer mime wrapper
    message = MIMEMultipart('mixed')

    # supports multiple recipients if separated by ","
    message['to'] = to
    message['from'] = sender
    message['subject'] = subject
    message['bcc'] = sender
    print("CREATING HTML EMAIL MESSAGE")
    print("From: {}".format(sender))
    print("To: {}".format(to))
    print("Subject: {}".format(subject))

    # text and html versions of message
    message_alt = MIMEMultipart('alternative')
    message_alt.attach(MIMEText(msg_plain, 'plain'))
    message_rel = MIMEMultipart('related')
    message_rel.attach(MIMEText(msg_html, 'html'))
    # we are not adding an embedded 'cid:' image
    #add_embedded_image_to_related(message_rel)

    # on alternative wrapper, add related
    message_alt.attach(message_rel)

    # on outer wrapper, add alternative
    message.attach(message_alt)

    # each attachment
    for attachment_file in (attachment_file_list if attachment_file_list is not None else []):
        print("create_message_with_attachment: file: {}".format(attachment_file))
        content_type, encoding = mimetypes.guess_type(attachment_file)

        if content_type is None or encoding is not None:
            content_type = 'application/octet-stream'
        main_type, sub_type = content_type.split('/', 1)
        # print("main/sub={}/{}".format(main_type,sub_type))

        msg_att = None
        if main_type == 'text':
            fp = open(attachment_file, 'r')
            msg_att = MIMEText(fp.read(), _subtype=sub_type)
            fp.close()
            # DO NOT encode as base64, sent as text
        elif main_type == 'image':
            fp = open(attachment_file, 'rb')
            msg_att = MIMEImage(fp.read(), _subtype=sub_type)
            fp.close()
            # DO NOT encode as base64, already added as such
        else:
            fp = open(attachment_file, 'rb')
            msg_att = MIMEBase(main_type, sub_type)
            msg_att.set_payload(fp.read())
            fp.close()
            # encode as base64
            email.encoders.encode_base64(msg_att)

        # attach to main message
        filename = os.path.basename(attachment_file)
        msg_att.add_header(
            'Content-Disposition', 'attachment', filename=filename)
        message.attach(msg_att)

    return message

def send_message_to_google(creds_file, token_file, message, sender):
    """
    Requires local 'credentials.json' for Gmail API
    https://developers.google.com/gmail/api/quickstart/python
    """

    # googleapi oauth
    # put here in the function so that users doing relay don't require install
    import pickle
    from googleapiclient.discovery import build
    from google_auth_oauthlib.flow import InstalledAppFlow
    from google.auth.transport.requests import Request

    SCOPES = 'https://www.googleapis.com/auth/gmail.send'

    creds = None
    # The file token.pickle stores the user's access and refresh tokens, and is
    # created automatically when the authorization flow completes for the first
    # time.
    if os.path.exists(token_file):
        with open(token_file, 'rb') as token:
            creds = pickle.load(token)
    # If there are no (valid) credentials available, let the user log in.
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            flow = InstalledAppFlow.from_client_secrets_file(
                creds_file, SCOPES)
            creds = flow.run_local_server(port=0)
        # Save the credentials for the next run
        with open(token_file, 'wb') as token:
            pickle.dump(creds, token)

    service = build('gmail', 'v1', credentials=creds)
    msg_raw = {'raw': base64.urlsafe_b64encode(message.as_string().encode()).decode()}
    try:
        message = (service.users().messages().send(userId=sender, body=msg_raw).execute())
        print("Message Id: {}".format(message['id']))
        return message
    except Exception as e:
        print("An error occurred: {}".format(e))
        raise e

to_address = [];
attachment = [];
#try:

parser = argparse.ArgumentParser(description='Send email.')
parser.add_argument('--json', dest='json_file', help='full path to json file')
parser.add_argument('--creds_file', dest='creds_file', help='full path to creds file')
parser.add_argument('--token_file', dest='token_file', help='full path to token file')

args = parser.parse_args()
#sys.exit(2)

with open(args.json_file, 'r') as f:
    data = json.load(f)
to_json = data["to"]
from_address = data["from"]
for address in to_json:
  to_address.append( address )
subject = data["subject"]
body = data["body"]

message = createHTMLMessage( from_address,
                    ",".join(to_address),
                    subject,
                    body,
                    "",
                    attachment
                  )
send_message_to_google( args.creds_file,
                        args.token_file,
                        message,
                        from_address)
