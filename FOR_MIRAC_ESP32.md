# What Mirac Needs to Add to the ESP32 Code

## The One Thing You Need to Do

After every sensor reading, send the data to Rajini's AI server via HTTP POST.

---

## The URL to Send Data To

```
POST https://ember-ai-ews2.onrender.com/predict
```

---

## The JSON Format to Send

```json
{
    "PM1.0":       8,
    "PM2.5":       15,
    "PM10":        18,
    "TVOC":        0,
    "eCO2":        0,
    "temperature": 27.3,
    "humidity":    18.2,
    "pressure":    990.9,
    "gas":         14523678.0,
    "MQ_analog":   0.031,
    "MQ_digital":  1
}
```

---

## The Response You Get Back

```json
{
    "alarm":    "ON",
    "aqi":      500.0,
    "category": "HAZARDOUS",
    "timestamp": "2026-03-12 14:23:05"
}
```

- If `alarm == "ON"` → activate the physical alarm GPIO (PTA2)
- If `alarm == "OFF"` → deactivate the alarm

---

## ESP32 AT Commands to Send HTTP POST

After your normal sensor reading loop, add these AT commands:

```
// Step 1 — open TCP connection to Rajini's server
AT+CIPSTART="TCP","ember-ai-ews2.onrender.com",80

// Step 2 — build the HTTP POST request string
// Replace the values with your actual sensor readings
String body = "{\"PM1.0\":8,\"PM2.5\":15,\"PM10\":18,\"TVOC\":0,\"eCO2\":0,"
              "\"temperature\":27.3,\"humidity\":18.2,\"pressure\":990.9,"
              "\"gas\":14523678,\"MQ_analog\":0.031,\"MQ_digital\":1}";

String request = "POST /predict HTTP/1.1\r\n"
                 "Host: ember-ai-ews2.onrender.com\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: " + String(body.length()) + "\r\n"
                 "Connection: close\r\n\r\n" + body;

// Step 3 — send the length first
AT+CIPSEND=<length of request>

// Step 4 — send the request
<paste the full request string>

// Step 5 — close connection
AT+CIPCLOSE
```

---

## Important Notes

1. **Make sure your phone hotspot has mobile data ON** — the ESP32 needs internet to reach Render.com
2. Send data every ~2.5 seconds (same as your current sensor reading interval)
3. Read the response — if `"alarm":"ON"` → turn on the physical alarm
4. If the server takes too long (first wake-up after sleep can take 50 seconds), just retry

---

## After You Add This

- Rajini can open https://ember-ai-ews2.onrender.com/history and see your live sensor data
- The AI makes alarm decisions automatically
- Both the physical alarm AND Rajini's dashboard will update in real time
