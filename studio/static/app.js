"use strict";
const token = document.querySelector('meta[name="studio-token"]').content;
const target = document.getElementById("target");
const editor = document.getElementById("toml");
const status = document.getElementById("state");
const problems = document.getElementById("problems");
const sampleFor = (id) => 'sample-' + id + '.png';
async function call(path, options={}) {
  const res = await fetch('/api/v1/' + path, {
    ...options, headers: {'X-Studio-Token':token,...(options.headers||{})}
  });
  const data = await res.json();
  if (!res.ok && res.status !== 422) throw new Error(data.detail || 'Request failed');
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
    const [{data:t},{data:w}] = await Promise.all([call('targets'),call('workspace')]);
    t.targets.forEach(x => {const o=document.createElement('option');o.value=x.id;
      o.textContent=x.name + ' (' + x.display.join(' × ') + ')';target.append(o);});
    target.value=w.target;editor.value=w.toml;status.textContent='Ready';
  } catch (e) {status.textContent='Cannot connect: ' + e.message;}
}
target.addEventListener('change',()=>{
  editor.value=editor.value.replace(/sample-badger(?:2040|2350)\.png/g,sampleFor(target.value));
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
