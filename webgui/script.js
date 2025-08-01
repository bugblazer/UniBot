// Configuration
const API_BASE = 'http://localhost:18080';
const API_ENDPOINT = '/ask';

// DOM Elements
const userInput = document.getElementById("userInput");
const sendBtn = document.getElementById("sendBtn");
const chatbox = document.getElementById("chatbox");
const themeToggle = document.getElementById("themeToggle");
const adminLoginBtn = document.getElementById("adminLogin");
const statusIndicator = document.getElementById("statusIndicator");
const connectionStatus = document.getElementById("connectionStatus");

// Append messages to chatbox
function appendMessage(sender, text) {
  const msgDiv = document.createElement("div");
  msgDiv.className = `message ${sender}`;
  msgDiv.innerText = text;
  chatbox.appendChild(msgDiv);
  chatbox.scrollTop = chatbox.scrollHeight;
}

// Send user message to backend
async function sendMessage() {
  const question = userInput.value.trim();
  if (!question) return;

  appendMessage("user", question);
  userInput.value = "";

  try {
    const response = await fetch(`${API_BASE}${API_ENDPOINT}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ question }),
    });

    if (!response.ok) {
      throw new Error(`Server returned ${response.status}`);
    }

    const data = await response.json();
    appendMessage("bot", data.answer);
  } catch (err) {
    appendMessage("bot", "Failed to get a response. Is the server running?");
    console.error(err);
  }
}

// Event listeners
sendBtn.onclick = sendMessage;
userInput.addEventListener("keypress", function (e) {
  if (e.key === "Enter") sendMessage();
});

// Theme toggle
themeToggle.onclick = () => {
  document.body.classList.toggle("dark");
  document.body.classList.toggle("light");

  const isDark = document.body.classList.contains("dark");
  themeToggle.innerText = isDark ? "☀️" : "🌙";
};

// Admin panel navigation
adminLoginBtn.onclick = () => {
  window.location.href = 'admin.html';
};

// Connectivity indicator
async function checkConnection() {
  try {
    const res = await fetch(`${API_BASE}${API_ENDPOINT}`, {
      method: "OPTIONS",
    });
  } catch (err) {
    console.warn("Could not connect to backend:", err);
  }
}

// Check connection on load
checkConnection();