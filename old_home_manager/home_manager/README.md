# home_manager

# intructions
curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
sudo apt update
sudo apt install gh
sudo apt install git-crypt
gh auth login
// In Github, go the the profile and developer tools to get an access token.

sudo apt install libcurl4-openssl-dev libgpiod-dev at

Need installed:
    cmake
    
git clone https://github.com/paul3535/home_manager.git
cd home_manager
git submodule init
git submodule update --recursive
# Is this needed?
    cd 3rdParty/WiringPi
    ./build


// Don't forget the _private.json file


pip3 install --upgrade google-auth google-auth-oauthlib google-auth-httplib2 google-api-python-client oauth2client

source .venv/bin/activate

// Oauth stuff
You will need an oauth credentials.json file. Put it in /home/pi, or set the environment variable (check out utils/python/send_email_oauth.py)
You may need to do something special the first time. It will ask you to go to a URL to log in. Log in, then allow it access, then you will be redirected to an invalid URL. That URL will look something like this:

http://localhost/?state=asdf45989ASFdksli4XLD349dldjss&code=4/ASF523AS-asflkj2349asdASDfk239ksASDFek3k29skkdd-lkjasf9234ASFSkeie823kd&scope=https://www.googleapis.com/auth/gmail.send

Take everything between "code=" and "&scope" and put that into the response it is looking for (so, in this case that would be 4/ASF523AS-asflkj2349asdASDfk239ksASDFek3k29skkdd-lkjasf9234ASFSkeie823kd)

After that you shouldn't have to do it again.


TODO:
Garage monitor
send evaluations to mqtt
washing machine monitor
dryer monitor

Checking emails should say what commands it supports. Maybe send "info" and it will respond?

Infinite loop is /home/pi/github/home_manager/config/charing/../../logs/charing/email/ does not exist
