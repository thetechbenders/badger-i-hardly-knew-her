"use strict";
// Token arrives only via the private launch URL fragment, never via HTTP.
const fragmentToken = new URLSearchParams(window.location.hash.slice(1)).get('token');
if (fragmentToken) {
  sessionStorage.setItem('studio-token', fragmentToken);
  history.replaceState(null, '', window.location.pathname);
}
const token = fragmentToken || sessionStorage.getItem('studio-token') || '';
const target = document.getElementById("target");
const editor = document.getElementById("toml");
const status = document.getElementById("state");
const problems = document.getElementById("problems");
const sampleFor = (id) => 'sample-' + id + '.png';
async function call(path, options={}) {
  const res = await fetch('/api/v1/' + path, {
    ...options, headers: {'X-Studio-Token':token,...(options.headers||{})}
  });
  let data = {};
  try { data = await res.json(); } catch (_) { /* HTTP error may be non-JSON */ }
  if (!res.ok && res.status !== 422) {
    const detail = data.detail;
    throw new Error(typeof detail === 'string' ? detail :
      (detail ? JSON.stringify(detail) : res.status + ' ' + res.statusText));
  }
  return {res,data};
}
function present(data) {
  problems.replaceChildren();
  status.textContent = data.ok ? 'Validated and saved' : 'Not saved';
  (data.problems||[]).forEach(msg => {
    const p = document.createElement('p');p.textContent = msg;problems.append(p);
  });
  (data.notes||[]).forEach(msg => {
    const p = document.createElement('p');p.textContent = 'Note: ' + msg;problems.append(p);
  });
}
async function initialize() {
  try {
    if (!token) throw new Error('Open the private launch URL printed by Studio');
    const [{data:t},{data:w}] = await Promise.all([call('targets'),call('workspace')]);
    t.targets.forEach(x => {const o=document.createElement('option');o.value=x.id;
      o.textContent=x.name + ' (' + x.display.join(' × ') + ')';target.append(o);});
    target.value=w.target;editor.value=w.toml;status.textContent='Ready';
  } catch (e) {status.textContent='Cannot connect: ' + e.message;}
}
target.addEventListener('change',()=>{
  // Match any server-provided target, including future additions.
  const oldNames = Array.from(target.options, o => sampleFor(o.value));
  editor.value = editor.value.replace(/sample-[a-z0-9_-]+\.png/g,
    name => oldNames.includes(name) ? sampleFor(target.value) : name);
  status.textContent='Unsaved changes';
});
editor.addEventListener('input',()=>{status.textContent='Unsaved changes';});
document.getElementById('save').addEventListener('click',async()=>{
  status.textContent='Validating…';
  try {
    const {data}=await call('workspace',{method:'PUT',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({target:target.value,toml:editor.value})});
    present(data);
  } catch(e) {status.textContent='Error: '+e.message;}
});
initialize();
