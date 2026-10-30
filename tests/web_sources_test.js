const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const script=fs.readFileSync('include/web_ui.hpp','utf8').split('<script>')[1].split('</script>')[0];new vm.Script(script);
const sourceFn=script.slice(script.indexOf('function renderSources()'),script.indexOf('function bindPatch'));
const make=(tag,text,cls)=>({tag,text,cls,style:{},children:[],attributes:{},append(...nodes){this.children.push(...nodes)},replaceChildren(...nodes){this.children=nodes},setAttribute(k,v){this.attributes[k]=v},focus(){},querySelector(selector){for(const child of this.children){if('.'+child.cls===selector)return child;const found=child.querySelector?.(selector);if(found)return found;}return null;}});
const root=make('main');let opened,saved;
const context={state:{revision:7,sources_revision:9,settings:{music_root:'/data/music',storage_folders:{'/mnt/usb7':'/mnt/usb7/Albums'},shares:[]},local_sources:[{id:-1,name:'Internal Storage',mount:'/data',folder:'/data/music'}]},shareOpen:false,
$:()=>root,E:make,field:(label,type,value)=>({wrap:make('label',label),input:{value,focus(){}}}),button:(text,action,cls)=>({...make('button',text,cls),action}),browse:(...args)=>opened=args,patch:(...args)=>{saved=args;return {ok:true}},renderPage(){}};
vm.createContext(context);vm.runInContext(sourceFn,context);
(async()=>{context.renderSources();const rows=()=>root.children.filter(n=>n.cls==='sourceRow');assert.equal(rows().length,1);assert(rows()[0].children[2].hidden);
context.state.local_sources.push({id:-3,name:'M.2 Storage',mount:'/mnt/ext1',folder:'/mnt/ext1/Music'},{id:-10,name:'USB Storage 1',mount:'/mnt/usb0',folder:'/mnt/usb0/Music'},{id:-2,name:'Physical Disc',mount:'cdda:',folder:'cdda:'});
context.renderSources();assert.equal(rows().length,4);rows()[1].children[0].action();assert.deepEqual(opened,[-3,'']);
const usb=rows()[2];assert(usb.children[2].hidden);usb.children[1].action();assert(!usb.children[2].hidden);await usb.children[2].children[1].children[0].action();assert(usb.children[2].hidden);assert.equal(saved[0].storage_folders['/mnt/usb0'],'/mnt/usb0/Music');assert.equal(saved[0].storage_folders['/mnt/usb7'],'/mnt/usb7/Albums');assert.equal(saved[1],7);
rows()[3].children[0].action();assert.deepEqual(opened,[-2,'']);rows()[3].children[1].action();assert.deepEqual(opened,[-2,'']);
context.state.local_sources=context.state.local_sources.slice(0,1);context.state.settings.shares=[{name:'NAS',server:'host',share:'Music'}];context.renderSources();assert.equal(rows().length,2);rows()[1].children[0].action();assert.deepEqual(opened,[0,'']);rows()[1].children[1].action();assert.equal(context.shareIndex,0);assert.equal(context.shareRevision,9);assert(context.shareOpen);
console.log('Compact web sources: device visibility, open/edit/save, folder revisions, retained drive settings, disc refresh and SMB editing passed; client parses.');})().catch(e=>{console.error(e);process.exitCode=1;});
