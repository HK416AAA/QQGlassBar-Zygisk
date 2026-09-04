const LABELS = { "com.tencent.mobileqq": "QQ", "com.tencent.tim": "TIM" };
const TARGETS = Object.keys(LABELS);
const DATA = "/data/adb/qfunqaux";
const SCOPE = DATA + "/scope.conf";

let bridge = null, offline = false;
let state = { enabled: true, qauxv: true, qfun: true, pkg: { "com.tencent.mobileqq": true, "com.tencent.tim": true } };
const $ = (id) => document.getElementById(id);

function detect() {
  for (const c of [window.ksu, window.magisk, window.mmrl]) {
    if (typeof c === "object" && c && typeof c.exec === "function") { bridge = c; return true; }
  }
  return false;
}
function exec(cmd) {
  return new Promise((resolve, reject) => {
    const n = "mq_" + Date.now() + "_" + Math.floor(Math.random() * 1e9);
    window[n] = (errno, stdout, stderr) => { delete window[n]; resolve({ errno: errno | 0, stdout: stdout || "", stderr: stderr || "" }); };
    try { bridge.exec(cmd, JSON.stringify({}), n); } catch (e) { delete window[n]; reject(e); }
  });
}
function setStatus(s, err) { const el = $("status"); el.textContent = s; el.style.color = err ? "#e53935" : "#2e7d32"; }
function parse(t) {
  const s = JSON.parse(JSON.stringify(state));
  for (const line of String(t).split("\n")) {
    const m = /^\s*([a-zA-Z0-9.:_-]+)\s*=\s*(1|0)\s*$/.exec(line);
    if (!m) continue;
    const v = m[2] === "1";
    if (m[1] === "enabled") s.enabled = v;
    else if (m[1] === "module:qauxv") s.qauxv = v;
    else if (m[1] === "module:qfun") s.qfun = v;
    else if (m[1] in s.pkg) s.pkg[m[1]] = v;
  }
  return s;
}
function serialize(s) {
  let out = "# merged container scope\nenabled=" + (s.enabled ? 1 : 0) +
    "\nmodule:qauxv=" + (s.qauxv ? 1 : 0) + "\nmodule:qfun=" + (s.qfun ? 1 : 0) + "\n";
  for (const p of TARGETS) out += "pkg:" + p + "=" + (s.pkg[p] ? 1 : 0) + "\n";
  return out;
}
function apply() {
  $("enabled").checked = state.enabled;
  $("mod_qauxv").checked = state.qauxv;
  $("mod_qfun").checked = state.qfun;
  const ul = $("targets"); ul.innerHTML = "";
  for (const p of TARGETS) {
    const li = document.createElement("li");
    const b = document.createElement("div");
    const t = document.createElement("div"); t.className = "t"; t.textContent = LABELS[p];
    const d = document.createElement("div"); d.className = "d"; d.textContent = p;
    b.appendChild(t); b.appendChild(d);
    const sw = document.createElement("label"); sw.className = "switch";
    const inp = document.createElement("input"); inp.type = "checkbox"; inp.checked = state.pkg[p];
    inp.addEventListener("change", (e) => { state.pkg[p] = e.target.checked; });
    const sp = document.createElement("span"); sp.className = "slider";
    sw.appendChild(inp); sw.appendChild(sp);
    li.appendChild(b); li.appendChild(sw); ul.appendChild(li);
  }
}
function collect() {
  state.pkg = { "com.tencent.mobileqq": state.pkg["com.tencent.mobileqq"], "com.tencent.tim": state.pkg["com.tencent.tim"] };
  return { enabled: $("enabled").checked, qauxv: $("mod_qauxv").checked, qfun: $("mod_qfun").checked, pkg: state.pkg };
}
async function load() {
  if (!detect()) { apply(); offline = true; $("conn").classList.remove("hidden"); $("conn").textContent = "未找到 WebUI 桥，请从 KernelSU/KsuWebUI 打开"; setStatus("离线", true); return; }
  try {
    const r = await exec("cat " + SCOPE + " 2>/dev/null || true");
    state = parse(r.stdout);
    offline = false; $("conn").classList.add("hidden"); apply(); setStatus("已连接（root）");
  } catch (e) { apply(); offline = true; }
}
async function save() {
  if (offline) return setStatus("离线无法保存", true);
  const s = collect();
  const b64 = btoa(serialize(s));
  const r = await exec("mkdir -p " + DATA + " && echo '" + b64 + "' | base64 -d > " + SCOPE + " && chmod 644 " + SCOPE);
  setStatus(r.errno === 0 ? "已保存。重启 QQ/TIM 生效。" : "保存失败", r.errno !== 0);
}
async function diag() {
  const el = $("diag");
  if (offline) { el.textContent = "（离线）"; return; }
  const cmd = "for p in com.tencent.mobileqq com.tencent.tim; do " +
    "for m in qaux qfun; do f=/data/user/0/$p/files/.a2q/$m_status.txt; " +
    "if [ -s \"$f\" ]; then echo \"$p/$m: $(head -n1 \"$f\")\"; else echo \"$p/$m: none\"; fi; done; done";
  const r = await exec(cmd);
  el.textContent = r.stdout || "(空)";
}
$("save").addEventListener("click", save);
apply();
load().then(diag, diag);
