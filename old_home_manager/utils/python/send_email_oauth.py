import os
import sys
import json
import base64

from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from google.auth.transport.requests import Request
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from email.mime.text import MIMEText
import argparse

# Scopes for Gmail API
SCOPES = [
    'https://www.googleapis.com/auth/gmail.send',
    'https://www.googleapis.com/auth/gmail.modify'
]

# Get the credentials directory from an environment variable or default
CREDENTIALS_DIR = os.getenv('GMAIL_CREDENTIALS_DIR', '/home/pi/credentials')
CREDENTIALS_PATH = os.path.join(CREDENTIALS_DIR, 'credentials.json')
TOKEN_PATH = os.path.join(CREDENTIALS_DIR, 'token.json')

def authenticate_gmail():
    print("authenticate_gmail")
    creds = None
    if os.path.exists(TOKEN_PATH):
        creds = Credentials.from_authorized_user_file(TOKEN_PATH, SCOPES)
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            flow = InstalledAppFlow.from_client_secrets_file(CREDENTIALS_PATH, SCOPES)
            flow.redirect_uri = "http://localhost"
            auth_url, _ = flow.authorization_url(prompt='consent')
            print(f"Please go to this URL: {auth_url}")
            code = input("Enter the authorization code: ")
            flow.fetch_token(code=code)
            creds = flow.credentials
            with open(TOKEN_PATH, 'w') as token:
                token.write(creds.to_json())
    return creds

def create_message(sender, to, subject, body):
    print("create_message")
    """Create a message for an email."""
    message = MIMEText(body, 'html')
    message['to'] = ', '.join(to)
    message['from'] = sender
    message['subject'] = subject
    return {'raw': base64.urlsafe_b64encode(message.as_bytes()).decode()}

def send_email(subject, body, to, cc=None, bcc=None, attachment=None):
    print("send_email")
    creds = authenticate_gmail()

    try:
        # Build the Gmail API service
        service = build('gmail', 'v1', credentials=creds)

        # Sender's email address is taken from the OAuth credentials
        sender = creds.token_uri

        # Create the email message
        message = create_message(sender, to, subject, body)

        # Send the message using the Gmail API
        send_message = service.users().messages().send(userId='me', body=message).execute()
        print(f"Message sent successfully: {send_message['id']}")

    except HttpError as error:
        print(f"An error occurred: {error}")
        return None
        
if __name__ == "__main__":
   # Parse command-line arguments
    parser = argparse.ArgumentParser(description="Send an email via Gmail.")
    parser.add_argument('-s', '--subject', help='Email subject')
    parser.add_argument('-b', '--body', help='Email body')
    parser.add_argument('-t', '--to', help='Recipient email addresses', nargs='+')
    parser.add_argument('--cc', help='CC email addresses', nargs='+')
    parser.add_argument('--bcc', help='BCC email addresses', nargs='+')
    parser.add_argument('--attachment', help='Path to file attachment(s)', nargs='+')
    parser.add_argument('--json', help='Path to JSON file with email details')

    args = parser.parse_args()

    # Initialize variables
    subject = args.subject
    body = args.body
    to = args.to
    cc = args.cc
    bcc = args.bcc

    # If JSON file is provided, override the arguments with its content
    if args.json:
        with open(args.json, 'r') as f:
            email_data = json.load(f)
        subject = email_data.get('subject', subject)
        body = email_data.get('body', body)
        to = email_data.get('to', to)
        cc = email_data.get('cc', cc)
        bcc = email_data.get('bcc', bcc)

    # Check if required parameters are present
    if not subject or not body or not to:
        print("Error: Missing required email information. Please provide subject, body, and recipient addresses.")
        sys.exit(1)

    # Send the email using Gmail API
    send_email(subject, body, to, cc, bcc, args.attachment)
