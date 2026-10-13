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
    addEventListener(type, callback) {
      const previous=this.listeners[type];
      this.listeners[type]=previous
        ? (...args)=>{ previous(...args); callback(...args); }
        : callback;
    },
    querySelector(selector) {
      const match=/^input\[value="([^"]+)"\]$/.exec(selector);
      if (!match) return null;
      for (const child of this.children) {
        for (const nested of child.children||[]) {
          if(nested.tag==="input" && nested.value===match[1]) return nested;
        }
      }
      return null;
    }
  };
}

function browser({hash="", cached="", serverToken=VALID, intercept=null}={}) {
  const elements = new Map(ids.map(id => [id, fakeNode(id === "target" ? "select" : "div")]));
  const body = fakeNode("body");
  body.classList.add("locked");
  const storage = new Map(cached ? [["studio-token", cached]] : []);
  const requests = [];
  const location = {hash, origin:"http://127.0.0.1:8765", pathname:"/", search:""};
  const window = {location, listeners:{}, addEventListener(type,callback) {
    this.listeners[type]=callback;
  }};
  const history = {replaceState(_a,_b,url) {location.hash=""; location.pathname=url;}};
  const form = {
    person:{name:"Alex",title:"Engineer",affiliation:"",event:"",interests:[]},
    qr:{show:"link",link:"https://example.org",caption:"Scan"},
    contacts:[], projects:[]
  };
  const document = {
    body,
    getElementById(id) {return elements.get(id) || null;},
    createElement(tag) {return fakeNode(tag);},
    createTextNode(text) {return {textContent:text};}
  };
  const fetch = async (url, options) => {
    requests.push({url,token:options.headers["X-Studio-Token"],body:options.body||null});
    if (options.headers["X-Studio-Token"] !== serverToken) {
      return {ok:false,status:403,statusText:"Forbidden",
        json:async()=>({detail:"Invalid session token"})};
    }
    if (intercept) {
      const custom=intercept(url,options);
      if (custom !== undefined) return await custom;
    }
    let data = {};
    if(url.endsWith("/targets")) data={targets:[{id:"badger2040",name:"Badger 2040",display:[296,128]}]};
    if(url.endsWith("/workspace")) data={target:"badger2040",toml:"form = 1"};
    if(url.endsWith("/parse")) data={ok:true,fields:JSON.parse(JSON.stringify(form))};
    if(url.endsWith("/compose")) data={ok:true,toml:"form = 1"};
    if(url.endsWith("/portrait/reference")) {
      const request=JSON.parse(options.body);
      data={ok:true,toml:request.toml.replace(
        'processed = "portrait-badger2040.png"',
        'processed = "'+(request.processed||"sample-"+request.target+".png")+'"')};
    }
    return {ok:true,status:200,statusText:"OK",json:async()=>data};
  };
  class MockURL extends URL { static createObjectURL(){return "blob:portrait-test";} static revokeObjectURL(){} }
  class MockFileReader {
    readAsDataURL(file) {
      this.result="data:image/png;base64,"+(file.base64||"YQ==");
      this.onload();
    }
  }
  const context = {document,window,history,sessionStorage:{
    getItem(k){return storage.get(k)||null;},
    setItem(k,v){storage.set(k,v);},
    removeItem(k){storage.delete(k);}
  },fetch,URL:MockURL,URLSearchParams,FileReader:MockFileReader,console};
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

test("target switching only changes portrait.processed through the server",async()=>{
  const b=browser({hash:"#token="+VALID});
  await b.settle();
  const target=b.elements.get("target");
  const editor=b.elements.get("toml");
  const alternate=fakeNode("option");
  alternate.value="badger2350";
  target.options.push(alternate);
  editor.value=[
    '[person]',
    'name = "portrait-badger2040.png"',
    '# sample-badger2040.png must remain exactly as written',
    '[portrait]',
    'processed = "portrait-badger2040.png"'
  ].join("\n");
  target.value="badger2350";
  target.listeners.change();
  await b.settle();
  assert.ok(editor.value.includes('processed = "sample-badger2350.png"'));
  assert.ok(editor.value.includes('name = "portrait-badger2040.png"'));
  assert.ok(editor.value.includes('# sample-badger2040.png must remain exactly as written'));
  assert.equal(target.disabled,false);
  assert.ok(b.requests.some(r=>r.url.endsWith("/portrait/reference") &&
       JSON.parse(r.body).target==="badger2350"));
});


function response(data,status=200) {
  return {ok:status>=200 && status<300,status,statusText:status===200?"OK":"Error",
    json:async()=>data};
}
function deferred() {
  let resolve;
  const promise=new Promise(done=>{resolve=done;});
  return {promise,resolve};
}

test("failed target switch preserves crop and portrait preview state",async()=>{
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/reference")) return response(
      {ok:false,problems:["The target's portrait could not be referenced"]},422);
  }});
  await b.settle();
  const target=b.elements.get("target");
  const option=fakeNode("option");option.value="badger2350";target.options.push(option);
  b.elements.get("crop-x").value="15";
  b.elements.get("crop-w").value="520";
  b.elements.get("portrait-native").classList.remove("hidden");
  b.elements.get("portrait-select").disabled=false;
  b.elements.get("portrait-status").textContent="Ready to use original portrait";
  target.value="badger2350";
  await target.listeners.change();
  assert.equal(target.value,"badger2040");
  assert.equal(target.disabled,false);
  assert.equal(b.elements.get("crop-x").value,"15");
  assert.equal(b.elements.get("crop-w").value,"520");
  assert.equal(b.elements.get("portrait-native").classList.contains("hidden"),false);
  assert.equal(b.elements.get("portrait-select").disabled,false);
  assert.equal(b.elements.get("portrait-status").textContent,"Ready to use original portrait");
  assert.match(b.elements.get("state").textContent,/Target change failed/);
});

test("successful target switch clears stale portrait preview after response",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/reference")) return gate.promise;
  }});
  await b.settle();
  const target=b.elements.get("target"), editor=b.elements.get("toml");
  const option=fakeNode("option");option.value="badger2350";target.options.push(option);
  editor.value='[portrait]\nprocessed = "portrait-badger2040.png"';
  b.elements.get("crop-w").value="520";
  b.elements.get("portrait-native").classList.remove("hidden");
  target.value="badger2350";
  const switching=target.listeners.change();
  assert.equal(b.elements.get("crop-w").value,"520", "pending switch preserves crop");
  assert.equal(b.elements.get("portrait-native").classList.contains("hidden"),false);
  gate.resolve(response({ok:true,toml:'[portrait]\nprocessed = "sample-badger2350.png"'}));
  await switching;
  assert.equal(target.value,"badger2350");
  assert.equal(b.elements.get("crop-w").value,"");
  assert.equal(b.elements.get("portrait-native").classList.contains("hidden"),true);
});

test("portrait selection drops an outdated response when TOML is edited",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/select")) return gate.promise;
  }});
  await b.settle();
  vm.runInNewContext('portraitVariants={atkinson:"dummyimage"};',b.context);
  const editor=b.elements.get("toml");
  editor.value='[portrait]\nprocessed = "sample-badger2040.png"';
  const selecting=b.elements.get("portrait-select").listeners.click();
  assert.equal(b.elements.get("portrait-select").disabled,true);
  editor.value+='\n# newer change';
  editor.listeners.input();
  gate.resolve(response({ok:true,processed:"portrait-badger2040-different.png"}));
  await selecting;
  assert.match(editor.value,/# newer change/);
  assert.equal(b.elements.get("portrait-select").disabled,false);
  assert.equal(b.requests.filter(r=>r.url.endsWith("/portrait/reference")).length,0);
  assert.match(b.elements.get("portrait-status").textContent,/settings changed/i);
});

test("portrait selection refuses to overwrite newer visual changes after reference response",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/select"))
      return response({ok:true,processed:"portrait-badger2040-fake.png"});
    if(url.endsWith("/portrait/reference")) return gate.promise;
  }});
  await b.settle();
  vm.runInNewContext('portraitVariants={atkinson:"dummyimage"};',b.context);
  const editor=b.elements.get("toml");
  editor.value='[portrait]\nprocessed = "sample-badger2040.png"';
  const selecting=b.elements.get("portrait-select").listeners.click();
  await b.settle();
  assert.equal(b.requests.filter(r=>r.url.endsWith("/portrait/reference")).length,1);
  // The visual editor changes without changing editor.value until composition.
  const nameInput=b.elements.get("person-fields").children[0].children[1];
  nameInput.value="Changed while selecting";
  nameInput.listeners.input();
  gate.resolve(response({ok:true,toml:'[portrait]\nprocessed = "portrait-badger2040-fake.png"'}));
  await selecting;
  assert.equal(editor.value,'[portrait]\nprocessed = "sample-badger2040.png"');
  assert.match(b.elements.get("portrait-status").textContent,/settings changed/i);
  assert.equal(b.elements.get("portrait-select").disabled,false);
});


function choosePortraitSource(b, base64="YQ==") {
  const file={type:"image/png",size:100,base64};
  const input=b.elements.get("portrait-file");
  input.files=[file];
  input.listeners.change();
  b.elements.get("portrait-source").onload();
  for(const [id,value] of [["tone-gamma","1"],["tone-black","1"],
                            ["tone-white","2"],["tone-sharpen","0.6"]]) {
    b.elements.get(id).value=value;
  }
  return file;
}

function assertUnsavedBeforeUnload(b, expected) {
  let prevented=false;
  const event={preventDefault(){prevented=true;},returnValue:null};
  b.context.window.listeners.beforeunload(event);
  assert.equal(prevented,expected);
}

test("late save success preserves newer TOML edits and the unload warning",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url,options){
    if(url.endsWith("/workspace") && options.method==="PUT") return gate.promise;
  }});
  await b.settle();
  b.context.showTab("toml");
  const editor=b.elements.get("toml");
  editor.value="earlier version";editor.listeners.input();
  const saving=b.elements.get("save").listeners.click();
  assert.equal(b.elements.get("save").disabled,true);
  const put=b.requests.find(r=>r.url.endsWith("/workspace") && r.body);
  assert.equal(JSON.parse(put.body).toml,"earlier version");
  editor.value="newer version";editor.listeners.input();
  gate.resolve(response({ok:true,problems:[],notes:[]}));
  await saving;
  assert.equal(editor.value,"newer version");
  assert.match(b.elements.get("state").textContent,/newer changes remain unsaved/i);
  assertUnsavedBeforeUnload(b,true);
  assert.equal(b.elements.get("save").disabled,false);
});

test("unchanged save response clears the unsaved indicator",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url,options){
    if(url.endsWith("/workspace") && options.method==="PUT") return gate.promise;
  }});
  await b.settle();
  b.context.showTab("toml");
  const editor=b.elements.get("toml");
  editor.value="updated";editor.listeners.input();
  const saving=b.elements.get("save").listeners.click();
  gate.resolve(response({ok:true,problems:[],notes:[]}));
  await saving;
  assert.equal(b.elements.get("state").textContent,"Validated and saved");
  assertUnsavedBeforeUnload(b,false);
});

test("edits during visual composition never overwrite newer changes or send a stale save",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/compose"))return gate.promise;
  }});
  await b.settle();
  const saving=b.elements.get("save").listeners.click();
  const name=b.elements.get("person-fields").children[0].children[1];
  name.value="Newer visual edit";name.listeners.input();
  gate.resolve(response({ok:true,toml:"old composition"}));
  await saving;
  assert.equal(b.requests.some(r=>r.url.endsWith("/workspace") && r.body),false);
  assert.match(b.elements.get("state").textContent,/changed while composing/i);
  assert.equal(b.elements.get("person-fields").children[0].children[1].value,"Newer visual edit");
  assertUnsavedBeforeUnload(b,true);
  assert.equal(b.elements.get("save").disabled,false);
});

test("late portrait preview cannot revive results after tone changes",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/preview"))return gate.promise;
  }});
  await b.settle();
  choosePortraitSource(b);
  const preview=b.elements.get("portrait-preview").listeners.click();
  await b.settle();
  assert.equal(b.requests.filter(r=>r.url.endsWith("/portrait/preview")).length,1);
  b.elements.get("tone-gamma").value="2";
  b.elements.get("tone-gamma").listeners.input();
  gate.resolve(response({variants:{atkinson:"c3RhbGU="},crop:[118,0,1018,1253]}));
  await preview;
  assert.equal(b.elements.get("portrait-select").disabled,true);
  assert.equal(b.elements.get("portrait-results").children.length,0);
  assert.equal(b.elements.get("portrait-native").classList.contains("hidden"),true);
  assert.match(b.elements.get("portrait-status").textContent,/settings changed/i);
});

test("late portrait preview cannot restore an obsolete source image",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/preview"))return gate.promise;
  }});
  await b.settle();
  choosePortraitSource(b,"YQ==");
  const preview=b.elements.get("portrait-preview").listeners.click();
  await b.settle();
  choosePortraitSource(b,"Yg==");
  gate.resolve(response({variants:{atkinson:"c3RhbGU="},crop:[118,0,1018,1253]}));
  await preview;
  assert.equal(b.elements.get("portrait-results").children.length,0);
  assert.equal(b.elements.get("portrait-select").disabled,true);
});

test("newest portrait preview wins when older HTTP response arrives last",async()=>{
  const gates=[deferred(),deferred()];
  let index=0;
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/preview"))return gates[index++].promise;
  }});
  await b.settle();
  choosePortraitSource(b);
  const first=b.elements.get("portrait-preview").listeners.click();
  await b.settle();
  b.elements.get("tone-gamma").value="2";
  b.elements.get("tone-gamma").listeners.input();
  const second=b.elements.get("portrait-preview").listeners.click();
  await b.settle();
  assert.equal(index,2);
  gates[1].resolve(response({variants:{atkinson:"bmV3"},crop:[118,0,1018,1253]}));
  await second;
  const src=b.elements.get("portrait-native-image").src;
  assert.match(src,/bmV3$/);
  assert.equal(b.elements.get("portrait-select").disabled,false);
  gates[0].resolve(response({variants:{atkinson:"b2xk"},crop:[118,0,1018,1253]}));
  await first;
  assert.equal(b.elements.get("portrait-native-image").src,src);
  assert.equal(b.elements.get("portrait-select").disabled,false);
});

test("portrait selection drops stale image before reference when tone changes",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/select")) return gate.promise;
  }});
  await b.settle();
  vm.runInNewContext('portraitVariants={atkinson:"original"};',b.context);
  const editor=b.elements.get("toml");
  editor.value='[portrait]\\nprocessed = "sample-badger2040.png"';
  const selecting=b.elements.get("portrait-select").listeners.click();
  assert.equal(b.requests.filter(r=>r.url.endsWith("/portrait/select")).length,1);
  b.elements.get("tone-gamma").value="2";
  b.elements.get("tone-gamma").listeners.input();
  gate.resolve(response({ok:true,processed:"portrait-badger2040-stale.png"}));
  await selecting;
  assert.equal(b.requests.filter(r=>r.url.endsWith("/portrait/reference")).length,0);
  assert.match(editor.value,/sample-badger2040.png/);
  assert.match(b.elements.get("portrait-status").textContent,/settings changed/i);
  assert.equal(b.elements.get("portrait-select").disabled,true);
});

test("portrait selection ignores late reference after source changes",async()=>{
  const gate=deferred();
  const b=browser({hash:"#token="+VALID,intercept(url){
    if(url.endsWith("/portrait/select"))
      return response({ok:true,processed:"portrait-badger2040-stale.png"});
    if(url.endsWith("/portrait/reference")) return gate.promise;
  }});
  await b.settle();
  vm.runInNewContext('portraitVariants={atkinson:"original"};',b.context);
  const editor=b.elements.get("toml");
  editor.value='[portrait]\\nprocessed = "sample-badger2040.png"';
  const selecting=b.elements.get("portrait-select").listeners.click();
  await b.settle();
  assert.equal(b.requests.filter(r=>r.url.endsWith("/portrait/reference")).length,1);
  b.elements.get("portrait-file").files=[{type:"image/png",size:64,base64:"bmV3"}];
  b.elements.get("portrait-file").listeners.change();
  gate.resolve(response({ok:true,toml:'[portrait]\\nprocessed = "portrait-badger2040-stale.png"'}));
  await selecting;
  assert.match(editor.value,/sample-badger2040.png/);
  assert.match(b.elements.get("portrait-status").textContent,/settings changed/i);
  assert.equal(b.elements.get("portrait-select").disabled,true);
});
