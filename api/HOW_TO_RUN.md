# Ember AI — How to Run Everything
All commands you need, in one place.

---

## Run Locally (Your Computer)

### Step 1 — Install packages (first time only)
```bash
cd "C:\Users\Acer\OneDrive\Desktop\Seneca\sep600 winter 2026\AI RL"
pip install flask flask-cors requests stable-baselines3
```

### Step 2 — Start the server
```bash
cd "C:\Users\Acer\OneDrive\Desktop\Seneca\sep600 winter 2026\AI RL"
python -m api.server
```
Leave this terminal open. Server runs at: http://localhost:5000

### Step 3 — Test the server (open a second terminal)
```bash
cd "C:\Users\Acer\OneDrive\Desktop\Seneca\sep600 winter 2026\AI RL"
python -m api.simulate_hardware
```
This sends 7 fake sensor readings and shows the AI alarm decisions.

### Step 4 — Check in browser (while server is running)
- http://localhost:5000/           ← health check
- http://localhost:5000/status     ← uptime + model version
- http://localhost:5000/history    ← last 50 predictions

### Stop the server
Press `Ctrl + C` in the terminal running the server.

---

## GitHub Commands

### First time push (already done)
```bash
cd "C:\Users\Acer\OneDrive\Desktop\Seneca\sep600 winter 2026\AI RL"
git init
git add .
git commit -m "your message here"
git branch -M main
git remote add origin https://YOUR_TOKEN@github.com/rajini2000/ember-ai.git
git push -u origin main
```

### Push changes after editing files
```bash
cd "C:\Users\Acer\OneDrive\Desktop\Seneca\sep600 winter 2026\AI RL"
git add .
git commit -m "describe what you changed"
git push
```

### Check what files changed
```bash
git status
```

### Check commit history
```bash
git log --oneline
```

---

## Render.com (Live Online Server)

**URL:** https://ember-ai-ews2.onrender.com

### How to redeploy (after pushing changes to GitHub)
Render.com automatically redeploys when you push to GitHub.
Or manually: Render dashboard → ember-ai → **Manual Deploy** → **Deploy latest commit**

### Check if server is live
Open in browser: https://ember-ai-ews2.onrender.com/status

### Test the live server from your terminal
```bash
cd "C:\Users\Acer\OneDrive\Desktop\Seneca\sep600 winter 2026\AI RL"
python -m api.simulate_hardware --url https://ember-ai-ews2.onrender.com
```

### Send one test reading to live server (curl)
```bash
curl -X POST https://ember-ai-ews2.onrender.com/predict \
     -H "Content-Type: application/json" \
     -d "{\"PM2.5\": 709, \"PM10\": 812, \"PM1.0\": 500, \"MQ_analog\": 0.439, \"MQ_digital\": 0, \"temperature\": 27.5, \"humidity\": 18.1, \"pressure\": 990.5, \"gas\": 250000, \"TVOC\": 0, \"eCO2\": 0}"
```

### Important note about free tier
Render.com free tier **sleeps after 15 minutes of no traffic**.
First request after sleeping takes ~50 seconds to wake up.
For the demo, open the /status URL a minute before showing the professor.

---

## Full Workflow (making a change and deploying)

1. Edit a file on your computer
2. Test it locally: `python -m api.server` + `python -m api.simulate_hardware`
3. Push to GitHub: `git add . && git commit -m "message" && git push`
4. Render.com auto-deploys in ~3 minutes
5. Test live: open https://ember-ai-ews2.onrender.com/status

---

## Quick Reference — All Endpoints

| Endpoint | Method | What it does |
|---|---|---|
| `/` | GET | Health check |
| `/status` | GET | Uptime + model version |
| `/predict` | POST | Send sensor data → get alarm decision |
| `/history` | GET | Last 50 predictions from database |
