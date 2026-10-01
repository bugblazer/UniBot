// Chat page. The C++ server serves this page and the API from the same origin.
const API_BASE = '';
const MIN_TYPING_MS = 450; // keeps the typing indicator from flashing on fast replies

const chat = document.getElementById('chat');
const messages = document.getElementById('messages');
const welcome = document.getElementById('welcome');
const form = document.getElementById('composer');
const input = document.getElementById('userInput');
const sendBtn = document.getElementById('sendBtn');
const status = document.getElementById('status');
const statusText = document.getElementById('statusText');

const dateFormat = new Intl.DateTimeFormat(undefined, { weekday: 'short', day: 'numeric', month: 'short' });
const timeFormat = new Intl.DateTimeFormat(undefined, { hour: 'numeric', minute: '2-digit' });

// ---------- DOM helpers ----------

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function icon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('class', 'icon');
  svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  use.setAttribute('href', `#i-${name}`);
  svg.appendChild(use);
  return svg;
}

function botAvatar() {
  const avatar = el('span', 'avatar');
  avatar.appendChild(icon('bot'));
  return avatar;
}

function scrollToEnd() {
  chat.scrollTop = chat.scrollHeight;
}

// "2026-10-15" -> Date at local midnight (avoids the UTC shift of new Date("2026-10-15")).
function parseDate(iso) {
  const [y, m, d] = iso.split('-').map(Number);
  return new Date(y, m - 1, d);
}

function dateCard(schedule) {
  const date = parseDate(schedule.date);
  const card = el('div', 'date-card');

  const badge = el('div', 'date-badge');
  badge.setAttribute('aria-hidden', 'true');
  badge.append(el('span', 'month', date.toLocaleString(undefined, { month: 'short' })), el('span', 'day', String(date.getDate())));

  const body = el('div');
  body.appendChild(el('p', 'title', schedule.title));
  const when = el('p', 'when');
  when.append(icon('clock'), dateFormat.format(date) + (schedule.time ? ` · ${schedule.time}` : ''));
  body.appendChild(when);
  if (schedule.details) body.appendChild(el('p', 'details', schedule.details));

  card.append(badge, body);
  return card;
}

function suggestionButton(question) {
  const button = el('button', 'suggestion', question);
  button.type = 'button';
  button.addEventListener('click', () => ask(question));
  return button;
}

// ---------- messages ----------

function addUserMessage(text) {
  const msg = el('div', 'msg user');
  const wrap = el('div');
  wrap.appendChild(el('div', 'bubble', text));
  wrap.appendChild(el('span', 'time', timeFormat.format(new Date())));
  msg.appendChild(wrap);
  messages.appendChild(msg);
  scrollToEnd();
}

function addTyping() {
  const msg = el('div', 'msg bot');
  msg.setAttribute('aria-label', 'UniBot is typing');
  const bubble = el('div', 'bubble typing');
  bubble.append(el('span'), el('span'), el('span'));
  msg.append(botAvatar(), bubble);
  messages.appendChild(msg);
  scrollToEnd();
  return msg;
}

function addBotMessage(reply) {
  const msg = el('div', `msg bot${reply.type === 'none' ? ' fallback' : ''}`);
  const wrap = el('div');
  const bubble = el('div', 'bubble');

  if (reply.type === 'schedule' && reply.schedule) {
    // The card carries the date, time and details, so the reply text stays short.
    bubble.appendChild(el('p', '', "Here's the date you asked about:"));
    bubble.appendChild(dateCard(reply.schedule));
  } else {
    if (reply.type === 'faq' && reply.question) bubble.appendChild(el('p', 'matched-q', reply.question));
    bubble.appendChild(el('p', '', reply.answer));
  }

  const matched = reply.matched || [];
  if (matched.length) {
    const meta = el('div', 'bubble-meta');
    meta.appendChild(el('span', '', 'Matched'));
    matched.forEach((keyword) => meta.appendChild(el('span', 'chip', keyword)));
    bubble.appendChild(meta);
  }

  const related = reply.related || [];
  if (related.length) {
    const box = el('div', 'related');
    box.appendChild(el('p', 'related-label', reply.type === 'none' ? 'Did you mean' : 'Related questions'));
    related.forEach((q) => box.appendChild(suggestionButton(q)));
    bubble.appendChild(box);
  }

  wrap.append(bubble, el('span', 'time', timeFormat.format(new Date())));
  msg.append(botAvatar(), wrap);
  messages.appendChild(msg);
  scrollToEnd();
}

// ---------- asking ----------

let busy = false;

async function ask(question) {
  question = question.trim();
  if (!question || busy) return;
  busy = true;
  welcome.hidden = true;
  input.value = '';
  resizeInput();
  updateSendState();

  addUserMessage(question);
  const typing = addTyping();
  const started = performance.now();

  let reply;
  try {
    const response = await fetch(`${API_BASE}/ask`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ question }),
    });
    if (!response.ok) throw new Error(`Server returned ${response.status}`);
    reply = await response.json();
    setStatus('online');
  } catch (err) {
    console.error(err);
    reply = { type: 'none', answer: "I can't reach the UniBot server right now. Check that it's running and try again." };
    setStatus('offline');
  }

  const wait = MIN_TYPING_MS - (performance.now() - started);
  if (wait > 0) await new Promise((resolve) => setTimeout(resolve, wait));
  typing.remove();
  addBotMessage(reply);
  busy = false;
  updateSendState();
  input.focus();
}

// ---------- composer ----------

function resizeInput() {
  input.style.height = 'auto';
  input.style.height = `${Math.min(input.scrollHeight, 160)}px`;
}

function updateSendState() {
  sendBtn.disabled = busy || !input.value.trim();
}

input.addEventListener('input', () => {
  resizeInput();
  updateSendState();
});

input.addEventListener('keydown', (event) => {
  if (event.key === 'Enter' && !event.shiftKey && !event.isComposing) {
    event.preventDefault();
    form.requestSubmit();
  }
});

form.addEventListener('submit', (event) => {
  event.preventDefault();
  ask(input.value);
});

// ---------- startup ----------

function setStatus(state) {
  status.dataset.state = state;
  statusText.textContent = state === 'online' ? 'Online' : state === 'offline' ? 'Offline' : 'Connecting…';
}

async function loadJSON(path) {
  const response = await fetch(`${API_BASE}${path}`);
  if (!response.ok) throw new Error(`${path} returned ${response.status}`);
  return response.json();
}

async function init() {
  try {
    await loadJSON('/health');
    setStatus('online');
  } catch {
    setStatus('offline');
    return;
  }

  const [suggestions, upcoming] = await Promise.all([
    loadJSON('/suggestions').catch(() => []),
    loadJSON('/schedules').catch(() => []),
  ]);

  if (suggestions.length) {
    const row = document.getElementById('suggestions');
    suggestions.forEach((q) => row.appendChild(suggestionButton(q)));
    document.getElementById('suggestionsWrap').hidden = false;
  }

  if (upcoming.length) {
    const list = document.getElementById('upcoming');
    upcoming.slice(0, 3).forEach((s) => {
      const item = el('li');
      item.appendChild(dateCard(s));
      list.appendChild(item);
    });
    document.getElementById('upcomingWrap').hidden = false;
  }
}

init();
input.focus();
