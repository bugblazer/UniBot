// Admin page: sign in, then manage FAQs and schedules. Served from the same origin as the API.
const API_BASE = '';
const TOKEN_KEY = 'unibot-admin-token';

const loginView = document.getElementById('loginView');
const dashboard = document.getElementById('dashboard');
const loginForm = document.getElementById('loginForm');
const loginError = document.getElementById('loginError');
const logoutBtn = document.getElementById('logoutBtn');
const toast = document.getElementById('toast');

let faqs = [];
let schedules = [];

// ---------- session ----------

let memoryToken = null;

function getToken() {
  try {
    return sessionStorage.getItem(TOKEN_KEY);
  } catch {
    return memoryToken;
  }
}

function setToken(token) {
  memoryToken = token;
  try {
    if (token) sessionStorage.setItem(TOKEN_KEY, token);
    else sessionStorage.removeItem(TOKEN_KEY);
  } catch {
    // Session storage unavailable; the in-memory token lasts until the page closes.
  }
}

class AuthError extends Error {}

async function api(path, options = {}) {
  const response = await fetch(`${API_BASE}${path}`, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${getToken() ?? ''}`,
      ...options.headers,
    },
  });
  if (response.status === 401) throw new AuthError('Your session expired. Sign in again.');
  if (!response.ok) {
    const body = await response.json().catch(() => ({}));
    throw new Error(body.error || `Request failed (${response.status})`);
  }
  return response.status === 204 ? null : response.json();
}

function handleError(err, showIn) {
  if (err instanceof AuthError) {
    signOut(err.message);
    return;
  }
  console.error(err);
  if (showIn) showFormError(showIn, err.message);
  else showToast(err.message);
}

// ---------- helpers ----------

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

function iconButton(name, label, className = '') {
  const button = el('button', `icon-btn ${className}`.trim());
  button.type = 'button';
  button.setAttribute('aria-label', label);
  button.appendChild(icon(name));
  return button;
}

function showFormError(node, message) {
  node.textContent = message;
  node.hidden = false;
}

function clearFormError(node) {
  node.hidden = true;
  node.textContent = '';
}

let toastTimer;
function showToast(message) {
  toast.textContent = message;
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (toast.hidden = true), 2600);
}

function todayISO() {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
}

function parseDate(iso) {
  const [y, m, d] = iso.split('-').map(Number);
  return new Date(y, m - 1, d);
}

const longDate = new Intl.DateTimeFormat(undefined, { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' });

// ---------- keyword chip input ----------

function chipInput(container) {
  const list = container.querySelector('.chip-list');
  const input = container.querySelector('input');
  let values = [];

  function render() {
    list.replaceChildren(
      ...values.map((value, index) => {
        const chip = el('li', 'chip removable', value);
        const remove = el('button');
        remove.type = 'button';
        remove.setAttribute('aria-label', `Remove keyword ${value}`);
        remove.appendChild(icon('x'));
        remove.addEventListener('click', () => {
          values.splice(index, 1);
          render();
          input.focus();
        });
        chip.appendChild(remove);
        return chip;
      }),
    );
  }

  function commit() {
    input.value
      .toLowerCase()
      .split(/[\s,]+/)
      .filter(Boolean)
      .forEach((word) => {
        if (!values.includes(word)) values.push(word);
      });
    input.value = '';
    render();
  }

  input.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' || event.key === ',') {
      event.preventDefault();
      commit();
    } else if (event.key === 'Backspace' && !input.value && values.length) {
      values.pop();
      render();
    }
  });
  input.addEventListener('blur', commit);
  container.addEventListener('click', (event) => {
    if (event.target === container) input.focus();
  });

  return {
    get: () => {
      commit();
      return [...values];
    },
    set: (next) => {
      values = [...next];
      input.value = '';
      render();
    },
  };
}

const faqKeywords = chipInput(document.getElementById('faqKeywords'));
const scheduleKeywords = chipInput(document.getElementById('scheduleKeywords'));

// ---------- dialogs ----------

document.querySelectorAll('dialog').forEach((dialog) => {
  dialog.querySelectorAll('[data-close]').forEach((button) => button.addEventListener('click', () => dialog.close()));
  // Click on the backdrop closes the dialog.
  dialog.addEventListener('click', (event) => {
    if (event.target === dialog) dialog.close();
  });
});

const confirmDialog = document.getElementById('confirmDialog');
let pendingDelete = null;

function confirmDelete(title, text, action) {
  document.getElementById('confirmTitle').textContent = title;
  document.getElementById('confirmText').textContent = text;
  pendingDelete = action;
  confirmDialog.showModal();
}

document.getElementById('confirmDelete').addEventListener('click', async () => {
  const action = pendingDelete;
  confirmDialog.close();
  if (action) await action();
});

// ---------- tabs ----------

const tabs = [document.getElementById('tabFaqs'), document.getElementById('tabSchedules')];

function selectTab(tab) {
  tabs.forEach((t) => {
    const selected = t === tab;
    t.setAttribute('aria-selected', String(selected));
    t.tabIndex = selected ? 0 : -1;
    document.getElementById(t.getAttribute('aria-controls')).hidden = !selected;
  });
}

tabs.forEach((tab, index) => {
  tab.addEventListener('click', () => selectTab(tab));
  tab.addEventListener('keydown', (event) => {
    if (event.key !== 'ArrowRight' && event.key !== 'ArrowLeft') return;
    const next = tabs[(index + (event.key === 'ArrowRight' ? 1 : tabs.length - 1)) % tabs.length];
    selectTab(next);
    next.focus();
  });
});

// ---------- FAQs ----------

const faqList = document.getElementById('faqList');
const faqSearch = document.getElementById('faqSearch');
const faqCount = document.getElementById('faqCount');
const faqDialog = document.getElementById('faqDialog');
const faqForm = document.getElementById('faqForm');
const faqError = document.getElementById('faqError');
let editingFaq = -1;

function renderFAQs() {
  const query = faqSearch.value.trim().toLowerCase();
  const visible = faqs.filter(
    (faq) =>
      !query ||
      faq.question.toLowerCase().includes(query) ||
      faq.answer.toLowerCase().includes(query) ||
      faq.keywords.some((k) => k.includes(query)),
  );

  faqCount.textContent = query ? `${visible.length} of ${faqs.length} FAQs` : `${faqs.length} FAQs`;

  if (!visible.length) {
    const empty = el('li', 'empty', query ? 'No FAQs match your search.' : 'No FAQs yet. Add the first one with New FAQ.');
    faqList.replaceChildren(empty);
    return;
  }

  faqList.replaceChildren(
    ...visible.map((faq) => {
      const item = el('li', 'item');
      const body = el('div', 'item-body');
      body.append(el('p', 'item-title', faq.question), el('p', 'item-text', faq.answer));
      const meta = el('div', 'item-meta');
      faq.keywords.forEach((k) => meta.appendChild(el('span', 'chip', k)));
      body.appendChild(meta);

      const actions = el('div', 'item-actions');
      const edit = iconButton('edit', `Edit FAQ: ${faq.question}`);
      edit.addEventListener('click', () => openFaq(faq));
      const del = iconButton('trash', `Delete FAQ: ${faq.question}`, 'delete');
      del.addEventListener('click', () =>
        confirmDelete('Delete this FAQ?', `"${faq.question}" will no longer be answered. This can't be undone.`, () => deleteFaq(faq.id)),
      );
      actions.append(edit, del);

      item.append(body, actions);
      return item;
    }),
  );
}

function openFaq(faq) {
  editingFaq = faq ? faq.id : -1;
  document.getElementById('faqDialogTitle').textContent = faq ? 'Edit FAQ' : 'New FAQ';
  document.getElementById('faqQuestion').value = faq?.question ?? '';
  document.getElementById('faqAnswer').value = faq?.answer ?? '';
  faqKeywords.set(faq?.keywords ?? []);
  clearFormError(faqError);
  faqDialog.showModal();
  document.getElementById('faqQuestion').focus();
}

faqForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const payload = {
    question: document.getElementById('faqQuestion').value.trim(),
    answer: document.getElementById('faqAnswer').value.trim(),
    keywords: faqKeywords.get(),
  };
  if (!payload.question || !payload.answer) return showFormError(faqError, 'Add a question and an answer.');
  if (!payload.keywords.length) return showFormError(faqError, 'Add at least one keyword so students can find this answer.');

  try {
    const isEdit = editingFaq >= 0;
    await api(isEdit ? `/admin/faqs/${editingFaq}` : '/admin/faqs', { method: isEdit ? 'PUT' : 'POST', body: JSON.stringify(payload) });
    faqDialog.close();
    showToast(isEdit ? 'FAQ updated' : 'FAQ added');
    await loadAll();
  } catch (err) {
    handleError(err, faqError);
  }
});

async function deleteFaq(id) {
  try {
    await api(`/admin/faqs/${id}`, { method: 'DELETE' });
    showToast('FAQ deleted');
    await loadAll();
  } catch (err) {
    handleError(err);
  }
}

faqSearch.addEventListener('input', renderFAQs);
document.getElementById('addFaqBtn').addEventListener('click', () => openFaq(null));

// ---------- schedules ----------

const scheduleList = document.getElementById('scheduleList');
const scheduleDialog = document.getElementById('scheduleDialog');
const scheduleForm = document.getElementById('scheduleForm');
const scheduleError = document.getElementById('scheduleError');
let editingSchedule = -1;

function renderSchedules() {
  const today = todayISO();
  const sorted = [...schedules].sort((a, b) => a.date.localeCompare(b.date));

  if (!sorted.length) {
    scheduleList.replaceChildren(el('li', 'empty', 'No dates yet. Add deadlines, tests or events with New date.'));
    return;
  }

  scheduleList.replaceChildren(
    ...sorted.map((s) => {
      const past = s.date < today;
      const date = parseDate(s.date);
      const item = el('li', `item${past ? ' past' : ''}`);

      const badge = el('div', 'date-badge');
      badge.setAttribute('aria-hidden', 'true');
      badge.append(el('span', 'month', date.toLocaleString(undefined, { month: 'short' })), el('span', 'day', String(date.getDate())));

      const body = el('div', 'item-body');
      body.appendChild(el('p', 'item-title', s.title));
      const when = el('p', 'when');
      when.append(icon('clock'), longDate.format(date) + (s.time ? ` · ${s.time}` : ''));
      body.appendChild(when);
      if (s.details) body.appendChild(el('p', 'item-text', s.details));
      const meta = el('div', 'item-meta');
      if (past) meta.appendChild(el('span', 'chip past', 'Past'));
      s.keywords.forEach((k) => meta.appendChild(el('span', 'chip', k)));
      body.appendChild(meta);

      const actions = el('div', 'item-actions');
      const edit = iconButton('edit', `Edit date: ${s.title}`);
      edit.addEventListener('click', () => openSchedule(s));
      const del = iconButton('trash', `Delete date: ${s.title}`, 'delete');
      del.addEventListener('click', () =>
        confirmDelete('Delete this date?', `"${s.title}" will be removed from UniBot. This can't be undone.`, () => deleteSchedule(s.id)),
      );
      actions.append(edit, del);

      item.append(badge, body, actions);
      return item;
    }),
  );
}

function openSchedule(s) {
  editingSchedule = s ? s.id : -1;
  document.getElementById('scheduleDialogTitle').textContent = s ? 'Edit date' : 'New date';
  document.getElementById('scheduleTitle').value = s?.title ?? '';
  document.getElementById('scheduleDate').value = s?.date ?? '';
  document.getElementById('scheduleTime').value = s?.time ?? '';
  document.getElementById('scheduleDetails').value = s?.details ?? '';
  scheduleKeywords.set(s?.keywords ?? []);
  clearFormError(scheduleError);
  scheduleDialog.showModal();
  document.getElementById('scheduleTitle').focus();
}

scheduleForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const payload = {
    title: document.getElementById('scheduleTitle').value.trim(),
    date: document.getElementById('scheduleDate').value,
    time: document.getElementById('scheduleTime').value.trim(),
    details: document.getElementById('scheduleDetails').value.trim(),
    keywords: scheduleKeywords.get(),
  };
  if (!payload.title || !payload.date) return showFormError(scheduleError, 'Add a title and a date.');
  if (!payload.keywords.length) return showFormError(scheduleError, 'Add at least one keyword so students can find this date.');

  try {
    const isEdit = editingSchedule >= 0;
    await api(isEdit ? `/admin/schedules/${editingSchedule}` : '/admin/schedules', {
      method: isEdit ? 'PUT' : 'POST',
      body: JSON.stringify(payload),
    });
    scheduleDialog.close();
    showToast(isEdit ? 'Date updated' : 'Date added');
    await loadAll();
  } catch (err) {
    handleError(err, scheduleError);
  }
});

async function deleteSchedule(id) {
  try {
    await api(`/admin/schedules/${id}`, { method: 'DELETE' });
    showToast('Date deleted');
    await loadAll();
  } catch (err) {
    handleError(err);
  }
}

document.getElementById('addScheduleBtn').addEventListener('click', () => openSchedule(null));

// ---------- data ----------

function renderStats() {
  const keywords = new Set(faqs.flatMap((f) => f.keywords));
  const today = todayISO();
  document.getElementById('statFaqs').textContent = faqs.length;
  document.getElementById('statKeywords').textContent = keywords.size;
  document.getElementById('statUpcoming').textContent = schedules.filter((s) => s.date >= today).length;
}

async function loadAll() {
  [faqs, schedules] = await Promise.all([api('/admin/faqs'), api('/admin/schedules')]);
  renderStats();
  renderFAQs();
  renderSchedules();
}

// ---------- sign in / out ----------

function showDashboard() {
  loginView.hidden = true;
  dashboard.hidden = false;
  logoutBtn.hidden = false;
}

function signOut(message) {
  const token = getToken();
  if (token) fetch(`${API_BASE}/admin/logout`, { method: 'POST', headers: { Authorization: `Bearer ${token}` } }).catch(() => {});
  setToken(null);
  dashboard.hidden = true;
  logoutBtn.hidden = true;
  loginView.hidden = false;
  document.getElementById('password').value = '';
  if (message) showFormError(loginError, message);
  else clearFormError(loginError);
  document.getElementById('username').focus();
}

loginForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const username = document.getElementById('username').value.trim();
  const password = document.getElementById('password').value;
  if (!username || !password) return showFormError(loginError, 'Enter your username and password.');

  const button = document.getElementById('loginBtn');
  button.disabled = true;
  button.textContent = 'Signing in…';
  try {
    const response = await fetch(`${API_BASE}/admin/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password }),
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(body.error || "Couldn't sign in.");
    setToken(body.token);
    clearFormError(loginError);
    await loadAll();
    showDashboard();
  } catch (err) {
    showFormError(loginError, err.message === 'Failed to fetch' ? "Can't reach the UniBot server." : err.message);
  } finally {
    button.disabled = false;
    button.textContent = 'Sign in';
  }
});

logoutBtn.addEventListener('click', () => signOut());

// Resume an existing session on reload.
if (getToken()) {
  loadAll()
    .then(showDashboard)
    .catch((err) => handleError(err));
} else {
  document.getElementById('username').focus();
}
