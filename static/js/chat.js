// app.js

// If frontend and backend are deployed together (same domain), use relative paths
const API_URL = window.location.origin.includes("localhost")
  ? "http://127.0.0.1:5000"  // Local dev
  : window.location.origin;   // Works for ngrok / Render / any cloud host

// Example usage
async function analyzeLeaf(file) {
  const formData = new FormData();
  formData.append("file", file);

  const res = await fetch(`${API_URL}/analyze`, {
    method: "POST",
    body: formData
  });

  return res.json();
}


const chatBox = document.getElementById("chat-box");
const userInput = document.getElementById("user-input");
const sendBtn = document.getElementById("send-btn");
const voiceBtn = document.getElementById("voice-btn");
const stopBtn = document.getElementById("stop-btn");

let currentUtterance = null;
let currentFetchController = null;

// -------------------- Helpers --------------------

// Toggle library panel
function toggleLibrary() {
  const lib = document.getElementById("library");
  lib.style.display = lib.style.display === "block" ? "none" : "block";
}

// Load chat history
function loadHistory() {
  let history = JSON.parse(localStorage.getItem("chatHistory")) || [];
  const list = document.getElementById("history");
  if (!list) return;

  list.innerHTML = "";
  history.forEach(item => {
    const li = document.createElement("li");
    li.textContent = `${item.sender === "user" ? "👨‍🌾" : "🤖"} ${item.text}`;
    list.appendChild(li);
  });
}

// Save chat history
function saveHistory(sender, text) {
  let history = JSON.parse(localStorage.getItem("chatHistory")) || [];
  history.push({ sender, text });
  localStorage.setItem("chatHistory", JSON.stringify(history));
  loadHistory();
}

// Format text with line breaks
function formatText(text) {
  return text.replace(/\n/g, "<br>");
}

// Remove emojis (clean for TTS)
function removeEmojis(text) {
  return text.replace(/([\u2700-\u27BF]|[\uE000-\uF8FF]|[\uD83C-\uDBFF\uDC00-\uDFFF])/g, '');
}

// -------------------- UI --------------------

function addMessage(sender, text, isBot = false) {
  const msg = document.createElement("div");
  msg.classList.add("message");
  msg.classList.add(isBot ? "bot-msg" : "user-msg");

  if (isBot) {
    msg.innerHTML = `<b>${sender}:</b> ${formatText(text)} 
        <button class="listen-btn">🔊 Hear</button>`;
  } else {
    msg.innerHTML = `<b>${sender}:</b> ${text}`;
  }

  chatBox.appendChild(msg);
  chatBox.scrollTop = chatBox.scrollHeight;

  saveHistory(isBot ? "bot" : "user", text);

  if (isBot) {
    const listenBtn = msg.querySelector(".listen-btn");
    listenBtn.addEventListener("click", () => {
      toggleSpeak(text, listenBtn);
    });
  }
}

// -------------------- Chat --------------------

async function sendMessage(message) {
  if (!message) {
    message = userInput.value.trim();
    if (!message) return;
    userInput.value = "";
  }

  message = removeEmojis(message);
  addMessage("👨‍🌾 You", message);

  try {
    currentFetchController = new AbortController();

    const res = await fetch(`${API_URL}/ask`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ message }),
      signal: currentFetchController.signal
    });

    const data = await res.json();
    addMessage("🤖 Kalpataru", data.reply, true);
    currentFetchController = null;
  } catch (error) {
    addMessage("🤖 Kalpataru", "⚠️ Error: Cannot connect to server.", true);
  }
}

// -------------------- Voice Input --------------------

if ("webkitSpeechRecognition" in window) {
  const recognition = new webkitSpeechRecognition();
  recognition.lang = "en-IN";
  recognition.continuous = false;

  voiceBtn.addEventListener("click", () => {
    recognition.start();
    voiceBtn.classList.add("listening");
    voiceBtn.innerText = "🎙️";
  });

  recognition.onresult = function (event) {
    const transcript = event.results[0][0].transcript;
    userInput.value = transcript;
    sendMessage(transcript);
    voiceBtn.classList.remove("listening");
    voiceBtn.innerText = "🎤";
  };

  recognition.onerror = function () {
    voiceBtn.classList.remove("listening");
    voiceBtn.innerText = "🎤";
  };
} else {
  console.warn("Speech Recognition not supported in this browser.");
}

// -------------------- Text-to-Speech --------------------

function toggleSpeak(text, btn) {
  if (speechSynthesis.speaking && currentUtterance) {
    speechSynthesis.cancel();
    currentUtterance = null;
    btn.textContent = "🔊 Hear";
  } else {
    currentUtterance = new SpeechSynthesisUtterance(removeEmojis(text));
    currentUtterance.lang = "en-IN";
    currentUtterance.onend = () => {
      btn.textContent = "🔊 Hear";
      currentUtterance = null;
    };
    btn.textContent = "⏹ Stop";
    speechSynthesis.speak(currentUtterance);
  }
}

// -------------------- UI Events --------------------

// Send button
sendBtn.addEventListener("click", () => {
  sendMessage();
});

// Enter key
userInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && !e.shiftKey) {
    e.preventDefault();
    sendMessage();
  }
});

// Stop button
stopBtn.addEventListener("click", () => {
  if (speechSynthesis.speaking) {
    speechSynthesis.cancel();
    currentUtterance = null;
  }
  if (currentFetchController) {
    currentFetchController.abort();
    currentFetchController = null;
  }
  addMessage("🤖 Kalpataru", "⏹ Generation stopped by user.", true);
});

// Load chat history
window.onload = loadHistory;
