"use strict";
// Prefer the private launch fragment; cached credentials may be from a
// previous server process. Only persist credentials after API verification.
const fragmentToken = new URLSearchParams(window.location.hash.slice(1)).get("token") || "";
if (fragmentToken) history.replaceState(null, "", window.location.pathname + window.location.search);
function cachedToken() {
  try { return sessionStorage.getItem("studio-token") || ""; } catch (_) { return ""; }
}
function cacheToken(value) {
  try { sessionStorage.setItem("studio-token", value); } catch (_) { /* storage disabled */ }
}
function clearToken() {
  try { sessionStorage.removeItem("studio-token"); } catch (_) { /* storage disabled */ }
}
let token = fragmentToken || cachedToken();
const $ = id => document.getElementById(id);
const target = $("target");
const editor = $("toml");
const status = $("state");
const problems = $("problems");
const sampleFor = id => "sample-" + id + ".png";
const personLabels = {name:"Name",title:"Job title",affiliation:"Affiliation",event:"Event",interests:"Interests (separate with commas)"};
const qrLabels = {show:"QR mode",link:"HTTPS link",caption:"QR caption"};
const contactLabels = {type:"Type",label:"Label",value:"Value",hidden:"Keep slot hidden"};
const projectLabels = {title:"Project name",tagline:"Tagline",description:"Description",status:"Status",banner:"Teaser banner",link:"HTTPS link"};
const contactTypes = ["email","phone","web","github","discord","text"];
const qrModes = ["none","link","vcard","vcard-from-contacts"];
let activeView = "visual", model = null, dirty = false, savedToml = "";

async function call(path, options={}) {
  const res = await fetch("/api/v1/" + path, {
    ...options, headers: {"X-Studio-Token":token,...(options.headers||{})}
  });
  let data = {};
  try { data = await res.json(); } catch (_) { /* Error responses can be non-JSON */ }
  if (!res.ok && res.status !== 422) {
    const detail = data.detail;
    const error = new Error(typeof detail === "string" ? detail :
      (detail ? JSON.stringify(detail) : res.status + " " + res.statusText));
    error.httpStatus = res.status;
    if (res.status === 403 && detail === "Invalid session token") {
      lockSession("Session expired or token rejected. Paste the latest private launch URL.");
    }
    throw error;
  }
  return {res,data};
}
function note(text) {status.textContent = text;}
function showResult(data) {
  problems.replaceChildren();
  note(data.ok ? "Validated and saved" : "Not saved");
  (data.problems||[]).forEach(msg=>{
    const p=document.createElement("p");p.textContent=msg;problems.append(p);
  });
  (data.notes||[]).forEach(msg=>{
    const p=document.createElement("p");p.textContent="Note: "+msg;problems.append(p);
  });
}
function changed() {dirty=true;note("Unsaved changes");}
function input(label, value, onChange, choices=null, multiline=false, check=false) {
  const wrap=document.createElement("label");wrap.className="field";
  const name=document.createElement("span");name.textContent=label;wrap.append(name);
  let el;
  if (choices) {
    el=document.createElement("select");
    for (const option of choices) {
      const opt=document.createElement("option");opt.value=option;opt.textContent=option;
      el.append(opt);
    }
    el.value=value;
  } else if (multiline) {
    el=document.createElement("textarea");el.rows=3;el.value=value;
  } else {
    el=document.createElement("input");el.type=check?"checkbox":"text";
    if (check) el.checked=Boolean(value); else el.value=value;
  }
  el.addEventListener("input",()=>{onChange(check?el.checked:el.value);changed();});
  wrap.append(el);return wrap;
}
function fields(parent, data, labels, mode) {
  parent.replaceChildren();
  for (const [key,label] of Object.entries(labels)) {
    const choices=key==="show"?qrModes:key==="type"?contactTypes:null;
    const value=key==="interests"?(Array.isArray(data[key])?data[key].join(", "):data[key]):data[key];
    parent.append(input(label,value,v=>{
      data[key]=key==="interests"?(Array.isArray(data[key])?v.split(",").map(s=>s.trim()).filter(Boolean):v):v;
    },choices,key==="description",key==="hidden"));
  }
}
function list(section, singular, labels, max) {
  const container=$(section+"-list");container.replaceChildren();
  model[section].forEach((row,index)=>{
    const card=document.createElement("article");card.className="entry";
    const header=document.createElement("div");header.className="entry-head";
    const title=document.createElement("strong");title.textContent=singular+" "+(index+1);
    header.append(title);
    for(const [label,delta] of [["↑",-1],["↓",1]]) {
      const b=document.createElement("button");b.className="tiny";b.type="button";
      b.textContent=label;b.setAttribute("aria-label","Move "+singular+" "+(index+1)+(delta<0?" up":" down"));
      b.disabled=index+delta<0||index+delta>=model[section].length;
      b.addEventListener("click",()=>{
        [model[section][index],model[section][index+delta]]=[model[section][index+delta],model[section][index]];
        changed();render();
      });header.append(b);
    }
    const remove=document.createElement("button");remove.type="button";remove.className="tiny danger";
    remove.textContent="Remove";remove.addEventListener("click",()=>{
      model[section].splice(index,1);changed();render();
    });header.append(remove);card.append(header);
    const grid=document.createElement("div");grid.className="fields";
    fields(grid,row,labels,section);card.append(grid);container.append(card);
  });
  $(section==="contacts"?"contact-add":"project-add").disabled=model[section].length>=max;
}
function render() {
  if(!model)return;
  fields($("person-fields"),model.person,personLabels,"person");
  fields($("qr-fields"),model.qr,qrLabels,"qr");
  list("contacts","Contact",contactLabels,6);
  list("projects","Project",projectLabels,12);
}
function showTab(tab) {
  activeView=tab;
  $("visual").classList.toggle("hidden",tab!=="visual");
  $("advanced").classList.toggle("hidden",tab!=="toml");
  $("visual-tab").classList.toggle("active",tab==="visual");
  $("toml-tab").classList.toggle("active",tab==="toml");
}
async function visualToToml() {
  const {res,data}=await call("compose",{method:"PUT",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({target:target.value,toml:editor.value,fields:model})});
  if(!res.ok) {showResult(data);return false;}
  editor.value=data.toml;
  return true;
}
async function tomlToVisual() {
  const {res,data}=await call("parse",{method:"PUT",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({target:target.value,toml:editor.value})});
  if(!res.ok) {showResult(data);return false;}
  model=data.fields;render();return true;
}
$("visual-tab").addEventListener("click",async()=>{
  if(activeView==="visual")return;
  try {if(await tomlToVisual())showTab("visual");}catch(e){note("Error: "+e.message);}
});
$("toml-tab").addEventListener("click",async()=>{
  if(activeView==="toml")return;
  try {if(await visualToToml())showTab("toml");}catch(e){note("Error: "+e.message);}
});
$("contact-add").addEventListener("click",()=>{
  if(model.contacts.length>=6)return;
  model.contacts.push({type:"text",label:"",value:"",hidden:false});changed();render();
});
$("project-add").addEventListener("click",()=>{
  if(model.projects.length>=12)return;
  model.projects.push({title:"",tagline:"",description:"",status:"",banner:"",link:""});changed();render();
});
target.addEventListener("change",()=>{
  const names=Array.from(target.options,o=>sampleFor(o.value));
  editor.value=editor.value.replace(/sample-[a-z0-9_-]+\.png/g,
    name=>names.includes(name)?sampleFor(target.value):name);
  changed();
});
editor.addEventListener("input",changed);
$("save").addEventListener("click",async()=>{
  note("Validating…");
  try {
    if(activeView==="visual" && !await visualToToml())return;
    const {res,data}=await call("workspace",{method:"PUT",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({target:target.value,toml:editor.value})});
    showResult(data);
    if(res.ok){dirty=false;savedToml=editor.value;}
  } catch(e){note("Error: "+e.message);}
});
window.addEventListener("beforeunload",e=>{if(dirty){e.preventDefault();e.returnValue="";}});
function lockSession(message) {
  token = "";
  clearToken();
  document.body.classList.add("locked");
  $("connection").classList.remove("hidden");
  $("connection-message").textContent = message;
}

function tokenFromInput(value) {
  const raw = value.trim();
  let candidate = raw;
  if (/^https?:\/\//i.test(raw)) {
    const url = new URL(raw);
    if (url.origin !== window.location.origin || url.pathname !== "/" || url.search) {
      throw new Error("Use the private launch URL for this exact local address and port.");
    }
    candidate = new URLSearchParams(url.hash.slice(1)).get("token") || "";
  } else if (raw.startsWith("#")) {
    candidate = new URLSearchParams(raw.slice(1)).get("token") || "";
  }
  if (!/^[a-zA-Z0-9_-]{32,128}$/.test(candidate)) {
    throw new Error("Paste the complete private link printed by the current Studio server.");
  }
  return candidate;
}

async function connect(candidate) {
  token = candidate;
  $("connection-message").textContent = "Checking connection…";
  try {
    const {data:t} = await call("targets");
    if (!model || !dirty) {
      const {data:w} = await call("workspace");
      target.replaceChildren();
      t.targets.forEach(x=>{
        const opt=document.createElement("option");opt.value=x.id;
        opt.textContent=x.name+" ("+x.display.join(" × ")+")";target.append(opt);
      });
      target.value=w.target;editor.value=w.toml;savedToml=w.toml;
      if (!await tomlToVisual()) throw new Error("Could not parse the saved badge form");
      dirty = false;
      showTab("visual");
    }
    cacheToken(candidate);
    document.body.classList.remove("locked");
    $("connection").classList.add("hidden");
    $("connection-url").value = "";
    note(dirty ? "Reconnected; unsaved changes preserved" : "Ready");
    return true;
  } catch (e) {
    const message = e.httpStatus === 403 && e.message === "Invalid session token"
      ? "Token rejected. Copy the newest link from the terminal running Studio. An older link cannot unlock a restarted server."
      : "Unable to connect: " + e.message;
    lockSession(message);
    return false;
  }
}

$("connection-form").addEventListener("submit",async event=>{
  event.preventDefault();
  try { await connect(tokenFromInput($("connection-url").value)); }
  catch (e) { $("connection-message").textContent = e.message; }
  finally { $("connection-url").value = ""; }
});

if (token) connect(token);
else lockSession("Paste the private launch URL printed by the running Studio server.");
