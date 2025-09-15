import os
import re
import json
import requests
import cv2
import numpy as np
from flask import Flask, request, render_template, jsonify, send_from_directory
from werkzeug.utils import secure_filename
from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas
from dotenv import load_dotenv
import google.generativeai as genai

# ------------------------------
# Load environment variables
# ------------------------------
load_dotenv()
API_KEY = os.getenv("GEMINI_API_KEY")

if not API_KEY:
    raise ValueError("❌ No API key found. Please set GEMINI_API_KEY in .env or environment.")

# Flask app
app = Flask(__name__)
app.config["UPLOAD_FOLDER"] = "uploads"
app.config["REPORT_FOLDER"] = "reports"
os.makedirs(app.config["UPLOAD_FOLDER"], exist_ok=True)
os.makedirs(app.config["REPORT_FOLDER"], exist_ok=True)

# ------------------------------
# Chatbot (Gemini REST API)
# ------------------------------
GEMINI_ENDPOINT = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-001:generateContent?key={API_KEY}"

@app.route("/ask", methods=["POST"])
def ask():
    user_msg = request.json.get("message", "")
    farming_prompt = f"You are Kalpataru, an AI farming assistant. Answer clearly and practically. The user asks: {user_msg}"

    try:
        response = requests.post(
            GEMINI_ENDPOINT,
            headers={"Content-Type": "application/json"},
            json={"contents": [{"parts": [{"text": farming_prompt}]}]}
        )
        data = response.json()
        print("🔎 Gemini raw response:", data)

        bot_reply = None
        if "candidates" in data and data["candidates"]:
            first = data["candidates"][0]
            content = first.get("content")
            if content:
                parts = content.get("parts", [])
                if parts:
                    bot_reply = parts[0].get("text")

        if not bot_reply:
            err = data.get("error")
            if err:
                return jsonify({"reply": f"⚠️ Error from Gemini API: {err}"}), 500
            return jsonify({"reply": "⚠️ Gemini returned no usable text."})

        return jsonify({"reply": bot_reply})

    except Exception as e:
        return jsonify({"reply": f"⚠️ Error: {str(e)}"}), 500


# ------------------------------
# Leaf analysis (OpenCV + Gemini JSON mode)
# ------------------------------
genai.configure(api_key=API_KEY)
model = genai.GenerativeModel(
    "gemini-2.0-flash",
    generation_config={"response_mime_type": "application/json"}
)

last_uploaded_path = None

def analyze_leaf_health(image_path):
    try:
        img = cv2.imread(image_path)
        if img is None:
            return {"error": "Invalid image"}

        hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)

        lower_yellow = np.array([20, 100, 100])
        upper_yellow = np.array([30, 255, 255])
        mask_yellow = cv2.inRange(hsv, lower_yellow, upper_yellow)

        lower_brown = np.array([10, 100, 20])
        upper_brown = np.array([20, 255, 200])
        mask_brown = cv2.inRange(hsv, lower_brown, upper_brown)

        total_pixels = img.shape[0] * img.shape[1]
        yellow_percent = round((np.sum(mask_yellow > 0) / total_pixels) * 100, 2)
        brown_percent = round((np.sum(mask_brown > 0) / total_pixels) * 100, 2)

        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        _, thresh = cv2.threshold(gray, 127, 255, cv2.THRESH_BINARY)
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        solidity = 0
        if contours:
            c = max(contours, key=cv2.contourArea)
            area = cv2.contourArea(c)
            hull = cv2.convexHull(c)
            hull_area = cv2.contourArea(hull)
            if hull_area > 0:
                solidity = round((area / hull_area) * 100, 2)

        return {"yellow_spots": yellow_percent, "brown_spots": brown_percent, "solidity": solidity}

    except Exception as e:
        return {"error": str(e)}

def parse_json_response(raw_text):
    raw_text = raw_text.strip()
    if raw_text.startswith("```"):
        raw_text = re.sub(r"^```json", "", raw_text, flags=re.IGNORECASE).strip()
        raw_text = raw_text.strip("`").strip()
    match = re.search(r"\{.*\}", raw_text, re.DOTALL)
    if match:
        raw_text = match.group(0)
    return json.loads(raw_text)

def get_gemini_analysis(image_path, yellow, brown, solidity, override_plant=None):
    with open(image_path, "rb") as f:
        image_data = f.read()

    if override_plant:
        plant_info = f"The user confirms this is a {override_plant} leaf."
    else:
        plant_info = "Identify the plant type from the image."

    prompt = f"""
    You are a plant pathology expert.
    {plant_info}
    Leaf stats:
    - Yellow spots: {yellow}%
    - Brown spots: {brown}%
    - Solidity: {solidity}%

    Return a JSON object with:
    - plant (string)
    - suggestion (string, max 3 lines)
    - thresholds (object with yellow_max, brown_max, solidity_min)
    """

    try:
        response = model.generate_content(
            [prompt, {"mime_type": "image/jpeg", "data": image_data}]
        )
        raw = response.text.strip()
        print("🔎 Gemini raw response:", raw)
        return parse_json_response(raw)

    except Exception as e:
        print("⚠️ Gemini failed:", e)
        return {
            "plant": override_plant if override_plant else "Unknown Plant",
            "suggestion": "Could not analyze properly.",
            "thresholds": {"yellow_max": 10, "brown_max": 5, "solidity_min": 85}
        }

@app.route("/analyze", methods=["POST"])
def analyze():
    try:
        if "file" not in request.files:
            return jsonify({"error": "No file uploaded"}), 400

        file = request.files["file"]
        filename = secure_filename(file.filename)
        filepath = os.path.join(app.config["UPLOAD_FOLDER"], filename)
        file.save(filepath)

        global last_uploaded_path
        last_uploaded_path = filepath

        result = analyze_leaf_health(filepath)
        if "error" in result:
            return jsonify(result), 400

        ai_result = get_gemini_analysis(
            filepath,
            result["yellow_spots"],
            result["brown_spots"],
            result["solidity"]
        )
        result.update(ai_result)
        return jsonify(result)

    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/correct-plant", methods=["POST"])
def correct_plant():
    try:
        data = request.json
        plant = data.get("plant", "Unknown Plant")
        yellow = data.get("yellow_spots")
        brown = data.get("brown_spots")
        solidity = data.get("solidity")

        global last_uploaded_path
        if not last_uploaded_path:
            return jsonify({"error": "No uploaded image found"}), 400

        ai_result = get_gemini_analysis(
            last_uploaded_path,
            yellow, brown, solidity,
            override_plant=plant
        )
        return jsonify(ai_result)

    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/download-report", methods=["POST"])
def download_report():
    try:
        data = request.json
        filename = "report.pdf"
        filepath = os.path.join(app.config["REPORT_FOLDER"], filename)

        c = canvas.Canvas(filepath, pagesize=A4)
        c.drawString(100, 800, "🌱 Leaf Analysis Report")
        c.drawString(100, 770, f"Plant: {data['plant']}")
        c.drawString(100, 750, f"Yellow spots: {data['yellow_spots']}%")
        c.drawString(100, 730, f"Brown spots: {data['brown_spots']}%")
        c.drawString(100, 710, f"Solidity: {data['solidity']}%")
        c.drawString(100, 690, f"Suggestion: {data['suggestion']}")
        c.save()

        return jsonify({"file": filename})

    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/get-report/<filename>")
def get_report(filename):
    return send_from_directory(app.config["REPORT_FOLDER"], filename)

# ------------------------------
# Routes for HTML pages
# ------------------------------
@app.route("/")
def index():
    return render_template("index.html")
@app.route("/weather")
def weather():
    return render_template("weather.html")

@app.route("/leaf")
def leaf():
    return render_template("leaf.html")   # leaf analysis UI

@app.route("/chat")
def chat():
    return render_template("chat.html")   # chatbot UI

# ------------------------------
# Run
# ------------------------------
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
