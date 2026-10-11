# PS-WRAP Privacy Policy

English · [ภาษาไทย](PRIVACY.th.md)

Effective date: 11 October 2026

PS-WRAP is a free, open-source desktop app for Windows that lets you play your own PlayStation 4 / PlayStation 5 over Remote Play. This policy explains what data the app handles and where it goes. The full source code is public at <https://github.com/iDevGIS/PS-WRAP>, so every statement here can be checked.

## Who we are

PS-WRAP is made by the BudToZai team and published by iDevGIS on GitHub. We are responsible for the PS-WRAP-AGENT helpdesk bot described below. For privacy questions or requests, see [Your rights and how to contact us](#your-rights-and-how-to-contact-us).

## The short version

- **The PS-WRAP app does not send data about you to us.** It has no analytics, telemetry, crash reporting or advertising.
- The app does connect to other services, and some of them see your IP address or details you enter. The full list is in [Connections PS-WRAP makes](#connections-ps-wrap-makes). Some happen without you turning anything on: the update check (on by default) and Discord Status (on by default, local only).
- Our Discord server has a helpdesk bot, **PS-WRAP-AGENT**. It only handles questions you post to it, and it keeps a log of them for 90 days. See [PS-WRAP-AGENT](#ps-wrap-agent-helpdesk-bot-on-our-discord-server).
- Everything the PS-WRAP app stores stays **on your PC**.

## Data stored on your PC

| What | Where | Why |
|---|---|---|
| Settings, registered consoles and their Remote Play keys | Windows registry, `HKCU\Software\PS-WRAP` | To remember your consoles and preferences |
| PlayStation Network sign-in tokens and your PSN Account ID (only if you choose *Login to PSN*) | Windows registry, `HKCU\Software\PS-WRAP`. They are not encrypted by PS-WRAP, so other programs running under your Windows account can read them | Automatic remote connection over the internet. *Settings › Remote › Clear PSN Token* removes them |
| Data from Sony's sign-in page (cookies and cache of the sign-in window) | `%APPDATA%\PS-WRAP\PS-WRAP\QtWebEngine\psn-token` | Keeping you signed in to Sony's page. *Clear PSN Token* does **not** remove this; delete the folder while PS-WRAP is closed |
| Stream keys and API keys for Go Live and live chat | Windows Credential Manager | Sending your live stream and reading your chat |
| Session logs | `%APPDATA%\PS-WRAP\PS-WRAP\log` | Troubleshooting. Stream keys and API keys are never written to logs |
| Recordings, Instant Replays and screenshots | The folder you choose (default: `Videos\PS-WRAP`) | Your captures |
| Images you add to the vertical 9:16 layout | `%APPDATA%\PS-WRAP` (`overlays` folder) | Showing them in your layout |

If chiaki-ng is installed on the same PC, PS-WRAP copies its settings on first launch, including registered consoles and any PSN sign-in tokens. chiaki-ng's own copy is left untouched.

Your camera and microphone are processed **only on your PC** (facecam, background removal, face effects, noise reduction). They are never uploaded by PS-WRAP, except as part of a live stream or recording that you start yourself.

## Connections PS-WRAP makes

Every service below sees your IP address when PS-WRAP connects to it. The *What is sent* column lists anything sent on top of that.

| Service | When | What is sent |
|---|---|---|
| **Your PlayStation console** | When you discover, wake or play | Remote Play traffic: your controller input, microphone (if enabled), and the video/audio stream from the console |
| **Sony / PlayStation Network** (servers under `sonyentertainmentnetwork.com`, `playstation.net` and `playstation.com`) | Only after you choose *Login to PSN*, and when you connect from outside your home network | The sign-in exchange, your PSN account details needed for Remote Play, and connection data. Governed by [Sony's privacy policy](https://www.playstation.com/legal/privacy-policy/) |
| **PSN account lookup** (`psn.flipscreen.games`, a third-party service not run by Sony or by us) | Only if you press *Public Lookup* when registering a console, instead of signing in to PSN | The PSN username you type. The service returns your Account ID. Its operator can see your username together with your IP address |
| **STUN server list** (`raw.githubusercontent.com`, the public `always-online-stun` list on GitHub) | When you connect from outside your home network | A request for the current list of public STUN servers |
| **Public STUN servers** (run by third parties; taken from the list above, or built-in servers from Google and moonlight-stream.org if the list cannot be loaded) | When you connect from outside your home network | Small network packets used to find your public address and a route to your console |
| **Your home router (UPnP)** | When you connect from outside your home network | PS-WRAP asks your router for its public address and to open UDP ports for the session. The ports are removed when the session ends normally. If PS-WRAP closes unexpectedly, they can stay open until you remove them on your router or the router restarts |
| **Certificate authorities** | Rarely: only if a secure connection to PSN arrives without its full certificate chain | A request for the missing certificate, at the address named in the certificate |
| **Your live-streaming destinations** (YouTube, Twitch, Facebook, Kick or a custom RTMP(S) server) | Only while you use Go Live | Your stream, using the stream key you entered |
| **YouTube Data API** (`googleapis.com`) | Only if you set up YouTube live chat on screen | Requests for your live chat, using the API key you entered |
| **Twitch chat** (`irc.chat.twitch.tv`) | Only if you set up Twitch chat on screen | A read-only, anonymous connection to the channel you entered |
| **Web addresses you paste** (for example a Giphy link) | Only when you add an image from a link | A normal download request to whichever site hosts that file |
| **The Discord app on your PC** | When *Discord Status* is on (it is by default) and Discord is running | See below |
| **GitHub** (`api.github.com`, `github.com`) | 8 seconds after PS-WRAP starts and every 12 hours, if *Check for updates automatically* is on (it is by default); and when you press *Update now* | A request for the list of PS-WRAP releases, and the download of the new version when you choose to update. Turn it off in the update dialog |

Most of these services are run by companies outside Thailand, so using them sends the data above to other countries.

## Discord Status (Rich Presence)

*Discord Status* and *Show Game On Discord* are both **on by default**. While they are on and Discord is running, PS-WRAP talks to the Discord desktop app on your own PC through Discord's local connection (it does not contact Discord's servers itself). It sends:

- that you are using PS-WRAP, and whether you are on the home screen or streaming;
- the console type (PS4 / PS5) and, if the console reports it and *Show Game On Discord* is on, the name of the game;
- whether you are live or recording, and how long you have been playing.

Discord then shows this on your profile according to your Discord privacy settings and [Discord's privacy policy](https://discord.com/privacy). Turn it off any time in *Settings › General › Discord Status*, or hide just the game name with *Show Game On Discord*. The *PS-WRAP* Discord application is used only for this status; it is not a bot and does not read your messages, servers or account data.

## PS-WRAP-AGENT (helpdesk bot on our Discord server)

PS-WRAP-AGENT is a bot in the `#chatbot-helpdesk` channel of the PS-WRAP Discord server. It is separate from the PS-WRAP app and from the *PS-WRAP* Discord status application described above. It only reads messages posted in that channel and in the threads it opens there. It ignores direct messages and every other channel.

**What happens to your question**

- Before anything else, the bot automatically removes IP addresses, e-mail addresses and anything that looks like a password, token or key from your message. This is automatic and can miss things, such as names, phone numbers or details inside pasted text, and it does not look inside images.
- It searches PS-WRAP's public documentation on our own computer.
- It sends your question, the matching parts of the documentation and the last few messages of the same thread to Anthropic's Claude, which writes the answer. Those messages can include ones posted by other people in the thread. Anthropic handles this under [Anthropic's privacy policy](https://www.anthropic.com/legal/privacy).
- If you paste a *Copy summary* from PS-WRAP-Diagnostics, it is handled the same way. The summary never contains your PSN sign-in, stream keys or user name.

**What we keep**

| What | Kept for |
|---|---|
| Your question and the bot's answer, after the removal above | 90 days, then the text is deleted and only anonymous counts remain |
| A one-way code made from your Discord user ID (not your name). It is not your ID, but treat it as linked to you | Same 90 days. Used for rate limits and so `/forget` can find your entries |
| Which documentation the bot used, how long it took, and the *Solved* / *Ask a human* buttons you pressed | 90 days |
| Nightly backups of the above | 14 days |
| Questions the team chooses as test questions, with the code from your user ID removed | Until the team removes them from its test set |

The log is stored on the PS-WRAP team's own computer and is read only by the team, to improve answers. Because test questions no longer carry the code from your user ID, `/forget` cannot find them; contact us if you want one removed.

**Your choices**

- Don't post passwords, keys or other personal details. The bot never needs them.
- Type `/forget` anywhere on the server to delete every question and answer the bot has linked to you. Backups roll over within 14 days. Messages you posted in Discord itself stay until you delete them.
- Press *Ask a human* to reach the team instead of the bot.

## Your rights and how to contact us

You can ask us to tell you what the helpdesk bot keeps about you, give you a copy, correct it, delete it, or stop using it. Most of it you can delete yourself with `/forget`. The PS-WRAP app does not send us any data, so we hold nothing about you from the app itself.

To make a request privately, e-mail <!-- TODO: add a privacy contact e-mail --> or send a direct message to a team member on our Discord server. Please don't put personal details in a public GitHub issue. If you are in Thailand you can also complain to the Personal Data Protection Committee (PDPC).

## Children

PS-WRAP is not directed at children under 13. The app itself does not send us any data. If a child under 13 has used the helpdesk bot, use `/forget` or contact us and we will delete what it kept.

## Changes

If this policy changes, the new version will be published in the PS-WRAP repository with a new effective date and announced on our Discord server. General questions: open an issue at <https://github.com/iDevGIS/PS-WRAP/issues>.

PS-WRAP is not affiliated with or endorsed by Sony Interactive Entertainment or Discord Inc. "PlayStation", "PS4" and "PS5" are trademarks of Sony Interactive Entertainment Inc.
