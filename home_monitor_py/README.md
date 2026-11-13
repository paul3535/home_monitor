# home_monitor (Python rewrite)

Freezer temperatures (DS18B20 over 1-wire) and reed switches (GPIO interrupts), with
escalating alerts by email and/or ntfy push. Replaces the C++ `old_home_manager`.

The core uses only the Python standard library (3.11+). GPIO needs `gpiozero`.

## Run the tests

    PYTHONPATH=src python3 -m unittest discover -s tests

## Test with a fake Pi (recommended before touching real hardware)

    PYTHONPATH=src python3 -m home_monitor -c config.example.toml --fake-pi --time-scale 60

Then open http://127.0.0.1:8099. The panel lets you set each freezer's temperature (or make the
sensor return a CRC error, or look unplugged), close/open each switch, and "bounce" a switch to
check de-bouncing. It shows every alert rule's state and the recent alert events live.

- `--time-scale 60` runs alert timers 60x faster: the 300 s warn and 600 s error timings play out in
  5 s and 10 s. Sensor polling is sped up by the same factor, so "sensor silent" alerts behave.
- The real `watch_switch`, `read_ds18b20`, alert engine and SQLite run unchanged. Only gpiozero and
  the kernel's 1-wire files are faked (the fake files are in the same format, in a temp directory
  printed at startup, so you can also edit them by hand).
- Safe by default: alerts go to the log only (nobody is emailed), and the database and logs go to a
  temp directory, not your real ones.

### Testing real notifications from the fake Pi

Enable the channels you want in `config.toml` (see "Notifications" below), then:

    export HOME_MONITOR_SMTP_USER=you@gmail.com
    read -rs HOME_MONITOR_SMTP_PASSWORD; export HOME_MONITOR_SMTP_PASSWORD   # a Gmail app password
    python -m home_monitor -c config.toml --fake-pi --time-scale 60 \
        --fake-send-to you@gmail.com my-private-test-topic

- `--fake-send-to TARGET...` really sends through your enabled channels, but **only to the targets you
  give**: an email address goes to the email channel, a topic name goes to the ntfy channel (subscribe
  your phone to that topic in the ntfy app first). A channel with no matching target sends nothing.
  Subjects start with `[FAKE-PI TEST]`, and each body ends with the names of who would really have been told.
- `--fake-send-real` sends to the **real recipients** on every enabled channel, to check the per-level
  lists. It prints who will be messaged and makes you type `send` first.
- Either way, at most 10 messages a minute go out per channel, so a fast `--time-scale` cannot flood an
  inbox or a phone (the rest are logged as "not sent").
- The panel has a **Notifications** section: *Send test message* and *Send daily report now* send one
  message immediately and show the result per channel, or the error (SMTP, HTTP), with no staged alert needed.
- The fake washing machine lid starts down, so expect a "watch" message at startup (your config
  notifies at watch).

## Install on the Pi

    sudo apt install python3-gpiozero python3-lgpio python3-venv
    python3 -m venv --system-site-packages .venv && .venv/bin/pip install -e .
    cp config.example.toml config.toml        # then edit contacts, sensor ids, thresholds
    .venv/bin/python -m home_monitor -c config.toml --check-config

1-wire: add `dtoverlay=w1-gpio,gpiopin=4,pullup=on` to `/boot/firmware/config.txt`, reboot, and
list your sensors with `ls /sys/bus/w1/devices`. (`pullup=on` replaces the C++ pull-up request.)

Email: create a Gmail app password and put it in `/etc/home_monitor.env` (chmod 600):

    HOME_MONITOR_SMTP_USER=you@example.com
    HOME_MONITOR_SMTP_PASSWORD=xxxxxxxxxxxxxxxx

Run as a service: edit paths in `deploy/home-monitor.service`, then
`sudo cp deploy/home-monitor.service /etc/systemd/system/ && sudo systemctl enable --now home-monitor`.

## Notifications

Who is told and how they are told are separate:

- **Who**: `[notify.levels]` maps each alert level (watch / warn / error) to contacts or groups, and an
  alert can override that with `recipients = { error = [...] }`.
- **How**: every enabled `[notify.channels.<name>]` gets every message it accepts, **in parallel**, so a
  failing channel (SMTP down) never delays another (an ntfy push). Each channel retries on its own, and a
  log channel is always on, so there is a record of everything sent.

A contact is an email address, or a table with an address per channel; each channel picks its own address
and skips people who have none:

    [contacts]
    mum  = "mum@example.com"                                   # email only
    paul = { email = "paul@example.com", ntfy = "paul-8f3k2x" } # email and an ntfy topic

| Channel | Address key | Notes |
|---|---|---|
| `email` | `email` | SMTP. Login from `user`/`password` (private file) or `HOME_MONITOR_SMTP_USER`/`_PASSWORD`. |
| `ntfy`  | `ntfy` (topic) | Push to the ntfy app. JSON publish with priority (watch 3, warn 4, error 5) and tags; `token` for a protected server. Skips the daily report unless `kinds` includes it. |

On any channel: `levels = ["warn", "error"]` limits which alert levels it carries, and
`kinds = ["event", "report", "test", "startup"]` limits what it carries. A phone can have different sounds
per level by giving the levels different contacts (so different topics), which the ntfy app then configures
per topic. `--check-config` shows how many people each level reaches on each channel, so a contact without a
topic is visible before an alert is missed:

    channels: email (smtp.gmail.com:587), ntfy (https://ntfy.sh)
    who is told: ... | error -> family (3 people: email 3, ntfy 2)

### Mute and status from the phone (ntfy buttons)

With `command_topic` and `command_secret` on the ntfy channel (put both in `private.toml`), every alert push
gets buttons: **Mute 1h**, **Mute 8h** and **Status**. Tapping one makes the phone post a signed command to
the command topic, which the monitor polls every few seconds, runs, and answers on *the topic of whoever
tapped it* (mute replies carry an **Unmute** button). Nothing is opened on your router and no VPN is needed.

| Command | Does |
|---|---|
| `status` | latest temperatures, device states, and any alert that is not OK or is muted |
| `mute <alert\|all> <30m\|2h\|1d>` | silences that alert's warn/error messages (recoveries still come through; reminders resume when the mute ends) |
| `unmute <alert\|all>` | undoes it |

Why this is safe enough for something that can silence an alarm:

- Every command is signed (HMAC-SHA256) with `command_secret`, which only the monitor knows, and **expires**
  (`command_ttl`, default 24 h). Knowing or guessing the command topic is not enough; forged or garbled
  messages are ignored without a reply and logged.
- A mute is capped (`max_mute`, default 24 h), is logged at WARNING level, and a replayed old button can only
  repeat the same action within its expiry.
- Messages already on the topic at startup are skipped, so a restart never replays old taps.
- On a protected ntfy server, allow anonymous *write-only* on the command topic: the phone posts without a login
  and the signature is the credential. The monitor reads it with its own `token`.
- Keep the email channel for errors even if you mute from a phone: it does not depend on the app.

In `--fake-pi` mode the buttons use a separate `<command_topic>-fake` topic, so a tap on a fake alert can never
reach a real monitor running under the same alert names. The panel has **Status** and **Unmute all** buttons
that run the same commands directly.

**Adding a channel** (Telegram, SMS, a webhook...): subclass `Channel` in `src/home_monitor/notify/`, set `kind`
and `option_names`, implement `async send(notification)` (use `self.destinations(n)` to get its addresses), and
list the class in `registry.py`. Contacts then get a key with the same name. Nothing else changes.

The old `[notify.email]` / `[notify.ntfy]` sections are now `[notify.channels.email]` / `[notify.channels.ntfy]`;
loading a config with the old form says so.

## Keeping addresses private, and running several configs

Real email addresses, ntfy topics, groups and logins live in one `private.toml` that is **not in git**
(`private.toml` and `*.private.toml` are in `.gitignore`). Each config refers to people by name, so the
configs themselves hold no secrets and can be committed.

    cp private.example.toml private.toml && chmod 600 private.toml   # then edit

    # ~/home_monitor/
    #   private.toml            contacts, groups, logins  (never committed)
    #   config.paul.toml        error = ["paul"]
    #   config.parents.toml     error = ["family"]
    #   config.test.toml        error = ["tester"]

Point a config at it with `private_file = "private.toml"` under `[system]` (relative to the config), or
`--private FILE`, or the `HOME_MONITOR_PRIVATE` environment variable. Precedence is command line, then
environment, then config. A named file that is missing is an error, never a silent fallback.

- `[contacts]` maps names to addresses; `[groups]` lets one name stand for several contacts
  (`family = ["paul", "mum", "dad"]`). Duplicates are removed. A contact in both the config and the private
  file takes the private value, so the placeholders in `config.example.toml` still work on their own.
- `[notify.channels.<name>]` in the private file is merged over the same channel in the config, so the login,
  tokens and `from_addr` can live there. Contacts (with their ntfy topics, which act like passwords) live there too. *Who is told at each level* stays in each
  config, which is what makes them differ.
- A private file readable by other users gets a warning (`chmod 600`).
- `--check-config` shows who is told at each level as names and counts, never addresses, so it is safe to paste:

      OK: 2 sensors, 3 switches, 2 devices, 8 alerts
          private file: /home/pi/home_monitor/private.toml (4 contacts, 1 groups)
          channels: email (smtp.gmail.com:587), ntfy (https://ntfy.sh)
          who is told: watch -> paul (1 person: email 1, ntfy 1) | ... | error -> family (3 people: email 3, ntfy 2)

The SMTP login can also come from `HOME_MONITOR_SMTP_USER` / `HOME_MONITOR_SMTP_PASSWORD`.

## How it maps from the C++ version

| C++                                   | Python                                            |
|---------------------------------------|---------------------------------------------------|
| `HomeSystem::runSystem` polling loop  | asyncio: sensors push events, a 1 s tick advances alert timers |
| `EA_BoundsThreshold`                  | `type = "bounds"` (`greater_than`, `less_than`, ... and new `outside`) |
| `EA_StateThreshold`                   | `type = "state"`                                  |
| WAIT/WATCH/WARN/ERROR/ERROR_RETRY     | same timings: `watch_s`, `warn_s`, `error_s`, `error_retry_s` |
| per-level email address lists         | `[notify.levels]`, optional per-alert `recipients` |
| mute                                  | `AlertEngine.mute()` (not yet exposed to users)   |
| polled `Device_GPIOSwitch`            | interrupt-driven `watch_switch` with 50 ms debounce |
| `Device_GarageDoor`, `Device_WashingMachine` | `[[devices]]` (`garage_door`, `washing_machine`) |
| `EA_DoorClosingFailure`               | `type = "door_failed_to_close"`                    |
| `getTemperatureC` returning -1/-3/-4  | `None` on failure, plus a `stale` alert if a sensor goes quiet |
| CSV logs + PHP pages                  | SQLite + `report.py` daily plain-text report     |
| JSON file + `send_email*.py`          | `notify/` channels (email via `smtplib`, ntfy); logins from the private file or env vars |

## Garage door and washing machine

Ported from `monitor_gpio.json`; see the `[[switches]]`, `[[devices]]` and `[[alerts]]` sections of
`config.example.toml`. A device derives its state from switches (closed = magnet near = pin LOW):

- `garage_door`: `up`, `down`, `neither` (moving) or `unknown` (both switches closed).
- `washing_machine`: `up` or `down` (the lid_up switch).

Behaviour that differs from the C++ version:

- **Settling.** A derived state must hold for `settle_s` (default 1 s) before it counts. With
  interrupts every edge is seen, so two switches a few ms out of step would otherwise raise an
  instant "unknown" alert. Your JSON notifies at "watch" with a 0 s watch time, so it would have emailed.
- **Door failed to close** (`type = "door_failed_to_close"`) now means what the old code intended: the
  door goes up -> neither -> up without reaching down. The C++ compared the door's state with ON/OFF,
  which a garage door never reports, so as far as I can tell it could not fire.
- **Watch notifications.** As in your JSON (`notify_watch`, watch time 0), you are emailed the moment the
  door goes up or the lid goes down, and again at warn and error. Remove "watch" from `notify_on` to quiet that.
- Not ported: the washing machine's `UNKNOWN_STATE` check (with one switch there is no impossible state)
  and `CLOSED_TOO_LONG` (disabled in your JSON).

## Not ported yet

- Relays and the variable-speed pump.
- Email-in commands (`status`, `mute 2h`): replaced by the ntfy buttons above. The commands live in
  `commands.py` and are independent of ntfy, so a REST API for an Android app can reuse them.
