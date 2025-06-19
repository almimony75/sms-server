# sms-sse-server

📡 **Offline C++ HTTP Server for Receiving SMS via REST and Broadcasting in Real-Time via Server-Sent Events (SSE)**
Because why just get SMS when you can stream them like a pro? 😎

---

## What Is This?

This project lets you run a lightweight HTTP server on your PC that:

- Receives SMS messages from your phone app via a simple REST **POST** endpoint (`/sms`)
- Stores SMS in memory and a log file (`sms_log.jsonl`)
- Broadcasts incoming SMS to any connected clients in real-time using **Server-Sent Events (SSE)** at `/events`
- Provides REST endpoints to fetch the latest SMS or all stored SMS
- **Important**: You need to install a phone app that forwards your SMS messages by sending them as JSON via REST API to this server. This project does not handle receiving SMS directly from your phone — it only receives forwarded messages from any app.

---

## Why?

Because sometimes you want your phone’s SMS delivered live to your computer, ready for your own creative projects — like AI assistants, dashboards, or just geeky notifications!

---

## How to Build

You'll need a C++17 compiler and [cmake](https://cmake.org/):

```bash
git clone <repo-url>
cd sms-sse-server
mkdir build && cd build
cmake ..
make
```

---

## How to Run

```bash
./sms_server
```

Server listens on `http://0.0.0.0:1001`

---

## API Endpoints

| Method | Path          | Description                     |
|--------|---------------|--------------------------------|
| POST   | `/sms`        | Send SMS JSON `{sender, message}` |
| GET    | `/sms/latest` | Get the last SMS as JSON       |
| GET    | `/sms/all`    | Get all SMS stored as JSON array |
| GET    | `/stats`      | Get stats about SMS and clients |
| GET    | `/events`     | SSE stream for real-time SMS   |

---

## Example: Sending an SMS

```bash
curl -X POST http://localhost:8081/sms \
  -H "Content-Type: application/json" \
  -d '{"sender": "+123456789", "message": "Hello from the phone!"}'
```

---

## Example: Real-time SMS Streaming

Use the provided `sse-client.sh` script:

```bash
chmod +x sse-client.sh
./sse-client.sh
```

Every time your phone sends an SMS, you’ll see it instantly printed!

---

## Notes

- Press `Ctrl+C` to gracefully stop the server.
- Logs are saved to `sms_log.jsonl` in the project folder.
- SSE clients auto-reconnect is your responsibility (or build a fancy front-end).
