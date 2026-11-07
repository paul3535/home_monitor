import os
import sys
import json
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from google.auth.transport.requests import Request
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
import argparse

# Gmail API read-only scope
SCOPES = [
    'https://www.googleapis.com/auth/gmail.send',
    'https://www.googleapis.com/auth/gmail.modify'
]

# Get the credentials directory from an environment variable or default
CREDENTIALS_DIR = os.getenv('GMAIL_CREDENTIALS_DIR', '/home/pi/credentials')
CREDENTIALS_PATH = os.path.join(CREDENTIALS_DIR, 'credentials.json')
TOKEN_PATH = os.path.join(CREDENTIALS_DIR, 'token.json')

def authenticate_gmail():
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

def get_unread_messages(tag=None):
    creds = authenticate_gmail()

    try:
        # Build the Gmail API service
        service = build('gmail', 'v1', credentials=creds)

        # Query for unread messages
        query = 'is:unread'
        if tag:
            query += f' label:{tag}'

        results = service.users().messages().list(userId='me', q=query).execute()
        messages = results.get('messages', [])

        unread_emails = []
        if messages:
            for message in messages:
                msg = service.users().messages().get(userId='me', id=message['id']).execute()
                #print(f"Whole message:\n{msg}")
                # Extract email details
                sender = next(header['value'] for header in msg['payload']['headers'] if header['name'] == 'From')
                subject = next(header['value'] for header in msg['payload']['headers'] if header['name'] == 'Subject')
                body = msg['snippet']

                email_details = {
                    'sender': sender,
                    'subject': subject,
                    'body': body,
                }
                unread_emails.append(email_details)

                # Mark message as read
                service.users().messages().modify(userId='me', id=message['id'], body={'removeLabelIds': ['UNREAD']}).execute()

        return unread_emails

    except HttpError as error:
        print(f"An error occurred: {error}")
        return None

if __name__ == "__main__":
    # Parse command-line arguments
    parser = argparse.ArgumentParser(description="Read unread emails via Gmail.")
    parser.add_argument('--tag', help='Gmail label to filter messages by', default=None)
    parser.add_argument('--mark-read', help='Mark messages as read after retrieving', action='store_true')

    args = parser.parse_args()

    # Get unread emails
    unread_emails = get_unread_messages(args.tag)

    if unread_emails:
        print(json.dumps(unread_emails))
