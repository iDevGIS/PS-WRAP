# PS-WRAP Privacy Policy

English · [ภาษาไทย](PRIVACY.th.md)

Effective date: 11 October 2026

PS-WRAP is a free, open-source desktop app for Windows that lets you play your own PlayStation 4 / PlayStation 5 over Remote Play. This policy explains what data the app handles and where it goes. The full source code is public at <https://github.com/iDevGIS/PS-WRAP>, so every statement here can be checked.

## The short version

- **The PS-WRAP app does not collect any data about you.** It has no analytics, telemetry, crash reporting or advertising. It does check GitHub for new versions (see below), which you can turn off.
- Our Discord server has a helpdesk bot, **PS-WRAP-AGENT**. It only handles questions you post to it, and it keeps a log of them for 90 days. See [PS-WRAP-AGENT](#ps-wrap-agent-helpdesk-bot-on-our-discord-server).
- Everything PS-WRAP stores stays **on your PC**.
- PS-WRAP only connects to the services listed below, and most of them only when you turn the feature on.

## Data stored on your PC

| What | Where | Why |
|---|---|---|
| Settings, registered consoles and their Remote Play keys | Windows registry, `HKCU\Software\PS-WRAP` | To remember your consoles and preferences |
| PlayStation Network sign-in tokens (only if you choose *Login to PSN*) | Windows registry, `HKCU\Software\PS-WRAP` | Automatic remote connection over the internet. Remove them any time with *Settings › Remote › Clear PSN Token* |
| Stream keys and API keys for Go Live and live chat | Windows Credential Manager | Sending your live stream and reading your chat |
| Session logs | `%APPDATA%\PS-WRAP\PS-WRAP\log` | Troubleshooting. Stream keys and API keys are never written to logs |
| Recordings, Instant Replays and screenshots | The folder you choose (default: `Videos\PS-WRAP`) | Your captures |
| Images you add to the vertical 9:16 layout | `%APPDATA%\PS-WRAP` (`overlays` folder) | Showing them in your layout |

Your camera and microphone are processed **only on your PC** (facecam, background removal, face effects, noise reduction). They are never uploaded by PS-WRAP, except as part of a live stream or recording that you start yourself.

## Connections PS-WRAP makes

| Service | When | What is sent |
|---|---|---|
| **Your PlayStation console** | When you discover, wake or play | Remote Play traffic: your controller input, microphone (if enabled), and the video/audio stream from the console |
| **Sony / PlayStation Network** (`auth.api.sonyentertainmentnetwork.com`, PSN Remote Play servers) | Only after you choose *Login to PSN*, and when you connect from outside your home network | The sign-in exchange and the connection data needed for remote play. This is governed by [Sony's privacy policy](https://www.playstation.com/legal/privacy-policy/) |
| **Your live-streaming destinations** (YouTube, Twitch, Facebook, Kick or a custom RTMP(S) server) | Only while you use Go Live | Your stream, using the stream key you entered |
| **YouTube Data API** (`googleapis.com`) | Only if you set up YouTube live chat on screen | Requests for your live chat, using the API key you entered |
| **Twitch chat** (`irc.chat.twitch.tv`) | Only if you set up Twitch chat on screen | A read-only, anonymous connection to the channel you entered |
| **Web addresses you paste** (for example a Giphy link) | Only when you add an image from a link | A normal download request for that file |
| **The Discord app on your PC** | When *Discord Status* is on (Settings › General) and Discord is running | See below |
| **GitHub** (`api.github.com`, `github.com`) | 8 seconds after PS-WRAP starts and every 12 hours, if *Check for updates automatically* is on (it is by default); and when you press *Update now* | A request for the list of PS-WRAP releases, and the download of the new version when you choose to update. GitHub sees your IP address, as with any website. Turn it off in the update dialog |

## Discord Status (Rich Presence)

When *Discord Status* is on, PS-WRAP talks to the Discord desktop app on your own PC through Discord's local connection (it does not contact Discord's servers itself). It sends:

- that you are using PS-WRAP, and whether you are on the home screen or streaming;
- the console type (PS4 / PS5) and, if the console reports it and *Show Game On Discord* is on, the name of the game;
- whether you are live or recording, and how long you have been playing.

Discord then shows this on your profile according to your Discord privacy settings and [Discord's privacy policy](https://discord.com/privacy). Turn it off any time in *Settings › General › Discord Status*, or hide just the game name with *Show Game On Discord*. The *PS-WRAP* Discord application is used only for this status; it is not a bot and does not read your messages, servers or account data.

## PS-WRAP-AGENT (helpdesk bot on our Discord server)

PS-WRAP-AGENT is a bot in the `#chatbot-helpdesk` channel of the PS-WRAP Discord server. It is separate from the PS-WRAP app and from the *PS-WRAP* Discord status application described above. It only reads messages posted in that channel and in the threads it opens there. It ignores direct messages and every other channel.

**What happens to your question**

- Before anything else, the bot removes IP addresses, e-mail addresses and anything that looks like a password, token or key from your message.
- It searches PS-WRAP's public documentation on our own computer.
- It sends your question, the matching parts of the documentation and the last few messages of the same thread to Anthropic's Claude, which writes the answer. Anthropic handles this under [Anthropic's privacy policy](https://www.anthropic.com/legal/privacy).
- If you paste a *Copy summary* from PS-WRAP-Diagnostics, it is handled the same way. The summary never contains your PSN sign-in, stream keys or user name.

**What we keep**

| What | Kept for |
|---|---|
| Your question and the bot's answer, after the removal above | 90 days, then the text is deleted and only anonymous counts remain |
| A scrambled, one-way code made from your Discord user ID (not your name) | Same 90 days. Used for rate limits and so `/forget` can find your entries |
| Which documentation the bot used, how long it took, and the *Solved* / *Ask a human* buttons you pressed | 90 days |
| Nightly backups of the above | 14 days |

The log is stored on the PS-WRAP team's own computer and is read by the team to improve answers. A question may be kept longer, without the code from your user ID, if the team turns it into a test question.

**Your choices**

- Don't post passwords, keys or other personal details. The bot never needs them.
- Type `/forget` anywhere on the server to delete every question and answer the bot has linked to you. Backups roll over within 14 days. Messages you posted in Discord itself stay until you delete them.
- Press *Ask a human* to reach the team instead of the bot.

## Children

PS-WRAP is not directed at children under 13 and does not knowingly collect data from anyone.

## Changes and contact

If this policy changes, the new version will be published in the PS-WRAP repository with a new effective date. Questions: open an issue at <https://github.com/iDevGIS/PS-WRAP/issues>.

PS-WRAP is not affiliated with or endorsed by Sony Interactive Entertainment or Discord Inc. "PlayStation", "PS4" and "PS5" are trademarks of Sony Interactive Entertainment Inc.
