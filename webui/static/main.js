async function saveConfig() {
  const body = {
    input_file: document.getElementById('input_file').value,
    output_file: document.getElementById('output_file').value,
    topk: document.getElementById('topk').value,
    time_range: document.getElementById('time_range').value,
    work_type: document.getElementById('work_type').value,
    normalize: document.getElementById('normalize').value,
  };
  const res = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  await res.json();
  const log = document.getElementById('runLog');
  log.textContent = '配置已保存';
}

async function runHotwords() {
  const log = document.getElementById('runLog');
  log.textContent = '正在分析...';
  const res = await fetch('/api/run', { method: 'POST' });
  const data = await res.json();
  if (data.ok) {
    log.textContent = '分析已完成';
  } else {
    log.textContent = '运行失败';
  }
  const out = document.getElementById('outputPreview');
  if (out) out.textContent = data.output_preview || '';
  // Mirror run stdout/stderr to terminal area for file模式 as well
  const term = document.getElementById('consoleOut');
  if (term) {
    const s = (data.stdout || '').trim();
    const e = (data.stderr || '').trim();
    term.textContent = [s, e].filter(Boolean).join('\n');
  }
  await loadSnapshots();
}

// Run analysis from the file input section: first save config, then run
const runConfigBtn = document.getElementById('runConfigBtn');
if (runConfigBtn) {
  runConfigBtn.addEventListener('click', async () => {
    const body = {
      input_file: document.getElementById('input_file').value,
      output_file: document.getElementById('output_file').value,
      topk: document.getElementById('topk').value,
      time_range: document.getElementById('time_range').value,
      work_type: '1',
      normalize: document.getElementById('normalize').value,
    };
    await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    await runHotwords();
  });
}

// Auto-save and re-run when time_range changes, so visualization reflects the new window immediately
const timeRangeEl = document.getElementById('time_range');
if (timeRangeEl) {
  timeRangeEl.addEventListener('change', async () => {
    const body = {
      input_file: document.getElementById('input_file').value,
      output_file: document.getElementById('output_file').value,
      topk: document.getElementById('topk').value,
      time_range: document.getElementById('time_range').value,
      work_type: '1',
      normalize: document.getElementById('normalize').value,
    };
    await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    await runHotwords();
  });
}

document.getElementById('downloadBtn').addEventListener('click', () => {
  window.location.href = '/download/output';
});

// Upload input file
async function uploadInputFile() {
  const fileInput = document.getElementById('inputFilePicker');
  const log = document.getElementById('uploadLog');
  if (!fileInput.files || fileInput.files.length === 0) {
    log.textContent = 'No file selected.';
    return;
  }
  const formData = new FormData();
  formData.append('file', fileInput.files[0]);
  const res = await fetch('/api/upload_input', { method: 'POST', body: formData });
  const data = await res.json();
  if (data.ok) {
    log.textContent = `已上传并设为输入：${data.config?.input_file || ''}`;
  } else {
    log.textContent = '上传失败';
  }
  if (data.ok && data.config && data.config.input_file) {
    const val = data.config.input_file;
    document.getElementById('input_file').value = val;
    const disp = document.getElementById('input_file_display');
    if (disp) disp.textContent = val;
  }
}

// Auto upload when choosing a file, and update displayed name
const filePicker = document.getElementById('inputFilePicker');
if (filePicker) {
  filePicker.addEventListener('change', async () => {
    if (filePicker.files && filePicker.files[0]) {
      const name = filePicker.files[0].name;
      document.getElementById('input_file').value = name;
      const disp = document.getElementById('input_file_display');
      if (disp) disp.textContent = name;
      await uploadInputFile();
    }
  });
}

// --- Snapshot gallery with Chart.js ---
let snapshots = [];
let snapIdx = 0;
let chart;
let forceGoLatestOnNextLoad = false; // after console queries, jump to newest snapshot

async function loadSnapshots() {
  const res = await fetch('/api/output_parsed');
  const data = await res.json();
  if (!data.ok) return;
  snapshots = data.snapshots || [];
  let usedForce = false;
  if (forceGoLatestOnNextLoad && snapshots.length > 0) {
    snapIdx = snapshots.length - 1;
    forceGoLatestOnNextLoad = false;
    usedForce = true;
  } else {
    // Keep current index if possible; clamp within bounds
    if (snapIdx >= snapshots.length) {
      snapIdx = Math.max(0, snapshots.length - 1);
    }
  }
  renderSnapshot();
  if (usedForce) {
    const infoEl = document.getElementById('snapInfo');
    if (infoEl && infoEl.scrollIntoView) {
      infoEl.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }
  }
}

function renderSnapshot() {
  const info = document.getElementById('snapInfo');
  const canvas = document.getElementById('chart');
  if (snapshots.length === 0) {
    info.textContent = '没有可视化快照，请先运行分析';
    if (chart) { chart.destroy(); chart = undefined; }
    return;
  }
  const snap = snapshots[snapIdx];
  const total = snapshots.length;
  const page = snapIdx + 1;
  info.textContent = `第 ${page}/${total} 张 | 查询时间: T = ${snap.time} min`;
  const labels = snap.items.map(it => it.word);
  const data = snap.items.map(it => it.count);
  if (chart) chart.destroy();
  if (window.Chart && window.ChartDataLabels) {
    Chart.register(window.ChartDataLabels);
  }
  const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
  const labelColor = prefersDark ? '#1f2937' : '#111111'; // 深色标注，按主题选择更深的颜色
  chart = new Chart(canvas, {
    type: 'bar',
    data: {
      labels,
      datasets: [{
        label: '词频',
        data,
        backgroundColor: '#0a84ff',
        borderRadius: 6,
        borderSkipped: 'bottom'
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      layout: { padding: { left: 40, right: 40 } },
      plugins: { 
        legend: { display: false },
        datalabels: {
          anchor: 'end',
          align: 'top',
          color: labelColor,
          formatter: (value) => value,
          font: { weight: '600' }
        }
      },
      scales: {
        x: { ticks: { color: '#6b7280' } },
        y: { ticks: { color: '#6b7280' }, beginAtZero: true, grace: '10%' }
      }
    }
  });
}

// (trends UI removed)

const prevEl = document.getElementById('prevArrow') || document.querySelector('.nav-arrow.left');
const nextEl = document.getElementById('nextArrow') || document.querySelector('.nav-arrow.right');
if (prevEl) prevEl.addEventListener('click', () => {
  if (snapshots.length === 0) return;
  snapIdx = (snapIdx - 1 + snapshots.length) % snapshots.length;
  renderSnapshot();
});
if (nextEl) nextEl.addEventListener('click', () => {
  if (snapshots.length === 0) return;
  snapIdx = (snapIdx + 1) % snapshots.length;
  renderSnapshot();
});

// initial load (if output exists)
loadSnapshots();

// ---------------------- Console interaction (work_type=2) ----------------------
let consoleTimer;
let consoleRunning = false;
let pendingSnapshotRefresh = false; // trigger chart refresh after console outputs are written

async function consoleStart() {
  const res = await fetch('/api/console/start', { method: 'POST' });
  const data = await res.json();
  const out = document.getElementById('consoleOut');
  out.textContent = data.ok ? '终端已启动，等待输入...' : ('启动失败: ' + (data.error || ''));
  if (data.ok) {
    consoleRunning = true;
    updateConsoleToggle(true);
    if (consoleTimer) clearInterval(consoleTimer);
    consoleTimer = setInterval(consolePoll, 700);
  }
}

async function consolePoll() {
  const res = await fetch('/api/console/output');
  const data = await res.json();
  if (!data.ok) return;
  document.getElementById('consoleOut').textContent = (data.lines || []).join('\n');
  // If a console command was just sent (e.g., QUERY), refresh snapshots after output arrives
  if (pendingSnapshotRefresh) {
    pendingSnapshotRefresh = false;
    // Slight delay to let file flush complete
    setTimeout(loadSnapshots, 150);
  }
}

async function consoleSend() {
  const val = document.getElementById('consoleInput').value;
  if (!val) return;
  const res = await fetch('/api/console/input', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text: val })
  });
  const data = await res.json();
  if (data.ok) document.getElementById('consoleInput').value = '';
  // Only refresh chart for QUERY commands
  if (/\[ACTION\]\s*QUERY\s*K\s*=\s*\d+/i.test(val)) {
    // Defer snapshot refresh until backend writes output
    pendingSnapshotRefresh = true;
    forceGoLatestOnNextLoad = true;
    // Fallback refresh in case polling lags
    setTimeout(loadSnapshots, 500);
  }
}

async function consoleStop() {
  const res = await fetch('/api/console/stop', { method: 'POST' });
  const data = await res.json();
  if (consoleTimer) clearInterval(consoleTimer);
  document.getElementById('consoleOut').textContent = data.ok ? '终端已停止' : '停止失败';
  consoleRunning = false;
  updateConsoleToggle(false);
}

document.getElementById('consoleSend').addEventListener('click', consoleSend);
// Toggle button for start/stop
const toggleBtn = document.getElementById('consoleToggle');
function updateConsoleToggle(running){
  if (!toggleBtn) return;
  toggleBtn.textContent = running ? '停止终端' : '启动终端';
}
toggleBtn.addEventListener('click', () => {
  if (consoleRunning) consoleStop(); else consoleStart();
});

// Send on Ctrl+Enter inside textarea
const consoleInputEl = document.getElementById('consoleInput');
consoleInputEl.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    consoleSend();
  }
});

// Real-time window size adjust in console mode
async function consoleApplyWindowSize() {
  const el = document.getElementById('console_time_range');
  if (!el) return;
  const val = (el.value || '').trim();
  const n = parseInt(val, 10);
  if (!Number.isFinite(n) || n <= 0) {
    const out = document.getElementById('consoleOut');
    if (out) out.textContent = '无效的窗口大小，请输入正整数';
    return;
  }
  if (!consoleRunning) {
    await consoleStart();
  }
  // Send WINDOW_SIZE command to backend console
  await fetch('/api/console/input', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ line: `WINDOW_SIZE = ${n}` })
  });
  // Not a query, but still may change future visualization; no immediate refresh required
}

const consoleApplyBtn = document.getElementById('consoleApplyTime');
if (consoleApplyBtn) {
  consoleApplyBtn.addEventListener('click', consoleApplyWindowSize);
}

// Query time (minute) -> send "[ACTION] QUERY K=t" and show in terminal
async function consoleQueryTime() {
  const el = document.getElementById('console_query_min');
  if (!el) return;
  const val = (el.value || '').trim();
  const t = parseInt(val, 10);
  if (!Number.isFinite(t) || t < 0) {
    const out = document.getElementById('consoleOut');
    if (out) out.textContent = '无效的查询时间，请输入非负整数分钟数';
    return;
  }
  if (!consoleRunning) {
    await consoleStart();
  }
  await fetch('/api/console/input', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ line: `[ACTION] QUERY K=${t}` })
  });
  // Mark for snapshot refresh upon next poll; also add a fallback timer
  pendingSnapshotRefresh = true;
  forceGoLatestOnNextLoad = true;
  setTimeout(consolePoll, 200);
  setTimeout(loadSnapshots, 600);
}

const consoleQueryBtn = document.getElementById('consoleQueryBtn');
if (consoleQueryBtn) {
  consoleQueryBtn.addEventListener('click', consoleQueryTime);
}
