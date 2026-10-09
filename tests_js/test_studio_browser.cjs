"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const javascript = fs.readFileSync(path.join(root, "studio/static/app.js"), "utf8");
const html = fs.readFileSync(path.join(root, "studio/static/index.html"), "utf8");
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]);
const VALID = "A".repeat(43), STALE = "B".repeat(43);

function fakeNode(tag="div") {
  const classes = new Set();
  return {
    tag, children: [], options: [], listeners: {}, value: "", textContent: "",
    hidden: false, style: {}, min: "", max: "", naturalWidth: 1254, naturalHeight: 1254,
    checked: false, disabled: false,
    classList: {
      add(s) { classes.add(s); },
      remove(s) { classes.delete(s); },
      toggle(s, enable) {
        if (enable === undefined ? !classes.has(s) : enable) classes.add(s);
        else classes.delete(s);
      },
      contains(s) { return classes.has(s); }
    },
    append(...items) {
      this.children.push(...items);
      if (this.tag === "select") this.options.push(...items);
    },
    replaceChildren(...items) {
      this.children = [];
      if (this.tag === "select") this.options = [];
      this.append(...items);
    },
    addEventListener(type, callback) { this.listeners[type] = callback; }
  };
}

function browser({hash="", cached="", serverToken=VALID}={}) {
  const elements = new Map(ids.map(id => [id, fakeNode(id === "target" ? "select" : "div")]));
  const body = fakeNode("body");
  body.classList.add("locked");
  const storage = new Map(cached ? [["studio-token", cached]] : []);
  const requests = [];
  const location = {hash, origin:"http://127.0.0.1:8765", pathname:"/", search:""};
  const window = {location, addEventListener() {}};
  const history = {replaceState(_a,_b,url) {location.hash=""; location.pathname=url;}};
  const form = {
    person:{name:"Alex",title:"Engineer",affiliation:"",event:"",interests:[]},
    qr:{show:"link",link:"https://example.org",caption:"Scan"},
    contacts:[], projects:[]
  };
  const document = {
    body,
    getElementById(id) {return elements.get(id) || null;},
    createElement(tag) {return fakeNode(tag);}
  };
  const fetch = async (url, options) => {
    requests.push({url,token:options.headers["X-Studio-Token"]});
    if (options.headers["X-Studio-Token"] !== serverToken) {
      return {ok:false,status:403,statusText:"Forbidden",
        json:async()=>({detail:"Invalid session token"})};
    }
    let data = {};
    if(url.endsWith("/targets")) data={targets:[{id:"badger2040",name:"Badger 2040",display:[296,128]}]};
    if(url.endsWith("/workspace")) data={target:"badger2040",toml:"form = 1"};
    if(url.endsWith("/parse")) data={ok:true,fields:JSON.parse(JSON.stringify(form))};
    if(url.endsWith("/compose")) data={ok:true,toml:"form = 1"};
    return {ok:true,status:200,statusText:"OK",json:async()=>data};
  };
  class MockURL extends URL { static createObjectURL(){return "blob:portrait-test";} static revokeObjectURL(){} }
  const context = {document,window,history,sessionStorage:{
    getItem(k){return storage.get(k)||null;},
    setItem(k,v){storage.set(k,v);},
    removeItem(k){storage.delete(k);}
  },fetch,URL:MockURL,URLSearchParams,console};
  vm.runInNewContext(javascript,context,{filename:"app.js"});
  return {
    elements,body,storage,requests,context,
    async settle() {
      for(let i=0;i<5;i++) await new Promise(resolve=>setImmediate(resolve));
    },
    async reconnect(link) {
      elements.get("connection-url").value=link;
      await elements.get("connection-form").listeners.submit({preventDefault(){}});
      await this.settle();
    }
  };
}

test("fresh launch URL unlocks Studio; old cached token is overwritten", async()=>{
  const b=browser({hash:"#token="+VALID,cached:STALE});
  await b.settle();
  assert.equal(b.body.classList.contains("locked"),false);
  assert.equal(b.storage.get("studio-token"),VALID);
  assert.equal(b.elements.get("state").textContent,"Ready");
  assert.ok(b.requests.length>=3);
  assert.ok(b.requests.every(r=>r.token===VALID));
  assert.equal(b.elements.get("person-fields").children.length,5);
});

test("rejected cached token shows reconnect instead of an empty editor", async()=>{
  const b=browser({cached:STALE});
  await b.settle();
  assert.equal(b.body.classList.contains("locked"),true);
  assert.equal(b.storage.has("studio-token"),false);
  assert.match(b.elements.get("connection-message").textContent,/token|launch/i);
  await b.reconnect("http://127.0.0.1:8765/#token="+VALID);
  assert.equal(b.body.classList.contains("locked"),false);
  assert.equal(b.storage.get("studio-token"),VALID);
  assert.equal(b.elements.get("state").textContent,"Ready");
  assert.equal(b.elements.get("connection-url").value,"");
});

test("mismatched local port or stale relink is not accepted",async()=>{
  const b=browser();
  await b.reconnect("http://127.0.0.1:8766/#token="+VALID);
  assert.equal(b.body.classList.contains("locked"),true);
  assert.match(b.elements.get("connection-message").textContent,/address and port/);
  await b.reconnect("http://127.0.0.1:8765/#token="+STALE);
  assert.equal(b.body.classList.contains("locked"),true);
  assert.equal(b.storage.has("studio-token"),false);
});

test("session recovery preserves unsaved visual changes", async()=>{
  const b=browser({hash:"#token="+VALID});
  await b.settle();
  const inputs=b.elements.get("person-fields").children;
  const nameInput=inputs[0].children[1];
  nameInput.listeners.input?.();
  // Simulate user edit through real event callback with a changed value.
  nameInput.value="Edited name";
  nameInput.listeners.input();
  assert.match(b.elements.get("state").textContent,/Unsaved/);
  b.context.lockSession("Session expired");
  assert.equal(b.body.classList.contains("locked"),true);
  await b.reconnect("#token="+VALID);
  assert.equal(b.body.classList.contains("locked"),false);
  assert.equal(b.elements.get("person-fields").children[0].children[1].value,"Edited name");
  assert.match(b.elements.get("state").textContent,/unsaved changes preserved/i);
});

test("portrait crop accepts integer coordinates and rejects malformed values", async()=>{
  const b=browser({hash:"#token="+VALID});
  await b.settle();
  const values=[75,12,520,640];
  ["crop-x","crop-y","crop-w","crop-h"].forEach((id,i)=>{
    b.elements.get(id).value=String(values[i]);
  });
  assert.equal(JSON.stringify(b.context.cropValues()),JSON.stringify(values));
  b.elements.get("crop-w").value="abc";
  assert.throws(()=>b.context.cropValues(),/four crop values/);
});

test("selected photo makes the draggable crop surface visible", async()=>{
  const b=browser({hash:"#token="+VALID});
  await b.settle();
  const fileInput=b.elements.get("portrait-file");
  fileInput.files=[{type:"image/png",size:1200}];
  b.elements.get("portrait-crop-stage").hidden=true;
  fileInput.listeners.change();
  const source=b.elements.get("portrait-source");
  assert.equal(source.src,"blob:portrait-test");
  source.onload();
  assert.equal(b.elements.get("portrait-crop-stage").hidden,false);
  assert.equal(b.elements.get("crop-x").value,"118");
  assert.equal(b.elements.get("crop-y").value,"0");
  assert.ok(b.elements.get("portrait-crop-box").style.width.endsWith("%"));
  assert.match(b.elements.get("portrait-status").textContent,/Source image ready/);
});

test("tonal controls clamp values to their advertised ranges", async()=>{
  const b=browser({hash:"#token="+VALID});
  await b.settle();
  for(const [id,min,max,defaultValue] of [
    ["tone-gamma",0.2,5,1],
    ["tone-black",0,40,1],
    ["tone-white",0,40,2],
    ["tone-sharpen",0,3,0.6]
  ]) {
    const field=b.elements.get(id);
    field.min=String(min);
    field.max=String(max);
    field.value=String(max+10);
    field.listeners.change();
    assert.equal(Number(field.value),max,id+" clamps upper range");
    field.value=String(min-10);
    field.listeners.change();
    assert.equal(Number(field.value),min,id+" clamps lower range");
    field.value="";
    field.listeners.change();
    assert.equal(Number(field.value),defaultValue,id+" restores default");
  }
});
