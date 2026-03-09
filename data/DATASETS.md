# Real Datasets — Download Instructions

Place downloaded CSV files in `data/real/`. The training script auto-detects the format.

---

## Dataset 1 — Beijing Multi-Site Air Quality (RECOMMENDED)

**Why**: Has real PM2.5, PM10, temperature, pressure, humidity, CO — closest to your sensors.

**File names you will download:**
```
PRSA_Data_Aotizhongxin_20130301-20170228.csv
PRSA_Data_Changping_20130301-20170228.csv
PRSA_Data_Dingling_20130301-20170228.csv
PRSA_Data_Dongsi_20130301-20170228.csv
PRSA_Data_Guanyuan_20130301-20170228.csv
PRSA_Data_Gucheng_20130301-20170228.csv
PRSA_Data_Huairou_20130301-20170228.csv
PRSA_Data_Nongzhanguan_20130301-20170228.csv
PRSA_Data_Shunyi_20130301-20170228.csv
PRSA_Data_Tiantan_20130301-20170228.csv
PRSA_Data_Wanliu_20130301-20170228.csv
PRSA_Data_Wanshouxigong_20130301-20170228.csv
```

**Where to download:**
1. Go to: https://www.kaggle.com/datasets/sid321axn/beijing-multisite-airquality-data-set
2. Click **Download** (you need a free Kaggle account)
3. Extract the ZIP — you get 12 CSV files (one per monitoring station)
4. Copy all 12 CSV files into `data/real/`

**To train with it:**
```bash
python src/train.py --beijing data/real/PRSA_Data_Aotizhongxin_20130301-20170228.csv
```

**Columns used:**  PM2.5, PM10, TEMP, PRES, DEWP, CO
**Columns synthesised:** PM1.0, gas resistance, MQ_analog (from CO), MQ_digital

---

## Dataset 2 — UCI Air Quality Dataset

**File name:** `AirQualityUCI.csv`

**Where to download:**
1. Go to: https://archive.ics.uci.edu/dataset/360/air+quality
2. Click **Download** → extract → find `AirQualityUCI.csv`
3. Copy to `data/real/`

**Note:** This dataset uses semicolons (`;`) as separators.
The loader handles this automatically.

**Columns available:** CO, NOx, NO2, temperature, humidity
**Missing:** PM2.5, PM10 (will be estimated from CO/NOx levels)

---

## Dataset 3 — Real Hardware Data (Best Option)

When your hardware is running, CSV files are saved to the SD card at:
```
/sd/data/YYYY-MM-DD.csv
```

Copy them off the SD card and place them in `data/real/`. These files match
the exact format this project uses — no adaptation needed.

To train with hardware data:
```bash
python src/train.py --hardware data/real/2026-02-22.csv
```

---

## Combining Multiple Sources (Recommended for best results)

```bash
# Step 1: Generate synthetic baseline data
python src/train.py --generate

# Step 2: Add Beijing real PM data
python src/train.py --beijing data/real/PRSA_Data_Dongsi_20130301-20170228.csv

# Step 3: When hardware data is available, retrain
python src/train.py --hardware data/real/2026-03-01.csv
```

---

## What Each Column Maps To

| CSV Column   | Sensor         | Unit    |
|-------------|----------------|---------|
| PM1.0       | PMS5003        | µg/m³   |
| PM2.5       | PMS5003        | µg/m³   |
| PM10        | PMS5003        | µg/m³   |
| TVOC        | ENS160 (off)   | ppb     |
| eCO2        | ENS160 (off)   | ppm     |
| temperature | BME680         | °C      |
| humidity    | BME680         | %RH     |
| pressure    | BME680         | hPa     |
| gas         | BME680         | Ohms    |
| MQ_analog   | MQ gas sensor  | 0.0–1.0 |
| MQ_digital  | MQ gas sensor  | 0 or 1  |
