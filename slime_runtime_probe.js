// @script-id slime_runtime_probe
// Read-only Slime.dst observations. RNG accounting remains Ctable_jp.js.
// No permanent stops, game-memory writes, or automatic input.
const CPU = 'arm9';
const IDS = [0, 1, 192, 193, 194];
const events = [];
const returnHooks = new Set();
let activeAi = null;
let serial = 0;
let stackBase = 0;
let writerRequest = null;
let writerActive = false;
let writerCaptures = [];
let writerLast = [null, null, null, null, null];
const writerInputs = new Map();
let activeGoalSetup = null;
const goalReturns = new Map();
const retreatReturns = new Map();
let lastFreecam = null;
const valid = (p, n = 1) => p >= 0x02000000 && p + n <= 0x03000000;
const reg = async name => (await memory.getregister(name, CPU)) >>> 0;
const bytes = async (p, n) => {
  if (!valid(p, n)) return null;
  const r = await mcp.call('dumpMemory', {address: p, length: n});
  if (!r.ok) throw new Error(JSON.stringify(r));
  return r.bytes;
};
const word = (b, o = 0) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const half = (b, o = 0) => b[o] | b[o + 1] << 8;
const u32 = async p => {const b = await bytes(p, 4); return b ? word(b) : 0;};
const f32 = u => {const b = new ArrayBuffer(4); const d = new DataView(b); d.setUint32(0, u, true); return d.getFloat32(0, true);};
const seed = async () => {const b = await bytes(0x02385f0c, 8); return '0x' + word(b, 4).toString(16).padStart(8, '0') + word(b).toString(16).padStart(8, '0');};
const retain = e => {events.push({sequence: ++serial, ...e}); if (events.length > 512) events.shift();};
async function actor(id) {
  const p = await u32(0x020f33e0 + id * 4);
  if (!valid(p, 0x17d) || !p) return {id, absent: true};
  const a = await bytes(p, 0x17d);
  const combat = word(a, 0x138), presentation = word(a, 0x13c), player = word(a, 0x144), ai = word(a, 0x148);
  const c = await bytes(combat, 0x40), v = await bytes(presentation, 0x58);
  const body = id < 4 ? await u32(player + 0x194) : 0;
  const weapon = id < 4 ? await u32(player + 0x294) : 0;
  const body4 = body ? await u32(body + 4) : 0, weapon4 = weapon ? await u32(weapon + 4) : 0;
  return {id, address: p, species: half(a, 2), group: a[0x17c], combat, player, ai,
    hp: half(c), mp: half(c, 2), maxHp: half(c, 4), maxMp: half(c, 6), attack: half(c, 8), defense: half(c, 10), speed: half(c, 12),
    combatWords: Array.from({length: 16}, (_, i) => word(c, i * 4)),
    bodyItemId: body4 & 0xfff, weaponItemId: weapon4 & 0xfff, bodyWord4: body4, weaponWord4: weapon4,
    bodyModel: body4 >>> 12 & 255, weaponModel: weapon4 >>> 12 & 255,
    tactics: id < 4 ? await u32(player + 0x8b4) : null,
    enemyRecord: id >= 192 && ai ? await bytes(ai, 0x28) : null,
    battleWorld: [word(a, 0x44) | 0, word(a, 0x48) | 0, word(a, 0x4c) | 0], radius: word(a, 0x64) | 0,
    presentation, baseWorld: [word(v, 4) | 0, word(v, 8) | 0, word(v, 12) | 0],
    world: [word(v, 16) | 0, word(v, 20) | 0, word(v, 24) | 0],
    start: v[0x1c], goal: v[0x1d], aux: v[0x1e], target: v[0x1f], flags: word(v, 0x20),
    route: v.slice(0x24, 0x24 + Math.min(v[0x34], 16)), movement: v[0x56]};
}
async function snapshot() {const actors = []; for (const id of IDS) actors.push(await actor(id)); return {seed: await seed(), actors};}
async function nodes() {
  const result = [];
  for (const id of IDS) {
    const p = await u32(0x020f33e0 + id * 4);
    if (!valid(p, 0x140)) continue;
    const v = await bytes(await u32(p + 0x13c), 0x58);
    const b = await bytes(p + 0x44, 12);
    result.push({id,start:v[0x1c],goal:v[0x1d],aux:v[0x1e],target:v[0x1f],flags:word(v,0x20),
      world:[word(v,16)|0,word(v,20)|0,word(v,24)|0],battle:[word(b)|0,word(b,4)|0,word(b,8)|0],
      route:v.slice(0x24,0x24+Math.min(v[0x34],16))});
  }
  return result;
}
async function aiSnapshot(p) {
  const b = await bytes(p, 0x6a0);
  const table = word(b, 0x28);
  return {address: p, actor: half(b, 4), tactics: b[6], mode: b[7], expectedTurns: word(b, 12),
    enabled: b.slice(0x10, 0x25), table, weights: valid(table, 84) ? await bytes(table, 84) : null,
    thresholds: b.slice(0x54, 0x69), float30: f32(word(b, 0x30)),
    allyHp: Array.from({length: 4}, (_, i) => word(b, 0x104 + i * 4)),
    allyRatio: Array.from({length: 4}, (_, i) => f32(word(b, 0x114 + i * 4))),
    healHpThreshold: f32(word(b, 0x124)),
    lowestAlly: word(b, 0x128), lowCount: word(b, 0x130), dyingCount: word(b, 0x134),
    actionCount: word(b, 0x174), actions: Array.from({length: Math.min(word(b, 0x174), 128)}, (_, i) => half(b, 0x178 + i * 4)),
    candidateWords: Array.from({length: 14}, (_, i) => Array.from({length: 12}, (_, j) => word(b, 0x3a8 + i * 0x30 + j * 4))),
    tacticsDispatch: await bytes(await u32(0x021f97f4), 48),
    operationDispatch: await bytes(await u32(0x021fa03c), 32)};
}
await memory.registerexec(0x021f96ec, async () => {
  const p = await reg('r0'), id = await reg('r2'), out = await reg('r3'), lr = await reg('r14');
  if (id !== 1 || !valid(p, 0x6a0) || !valid(out, 2)) return;
  activeAi = {p, id, out, lr, seedBefore: await seed(), flow: []};
  if (!returnHooks.has(lr)) {
    returnHooks.add(lr);
    await memory.registerexec(lr, async () => {
      if (!activeAi || activeAi.lr !== lr) return;
      const current = activeAi; activeAi = null;
      const a = await actor(1);
      retain({kind: 'guest-ai', ...current, seedAfter: await seed(), selectedAction: half(await bytes(current.out, 2)),
        context: await aiSnapshot(current.p), hp: [half(await bytes(0x020f38e0, 2)), a.hp],
        commandBytes: await bytes(a.player, 32)});
    }, {cpu: CPU});
  }
}, {cpu: CPU});
for (const address of [0x021f8910, 0x021f89a8, 0x021f8acc, 0x021f8bc4, 0x021f8cbc, 0x021f70e8]) {
  await memory.registerexec(address, async () => {
    if (activeAi) activeAi.flow.push({pc: address, r0: await reg('r0'), lr: await reg('r14')});
  }, {cpu: CPU});
}
// One bounded provenance interval: the previous action has completed, but
// the next DB91C/E08BC setup has not read its uninitialized stack scratch.
// Re-run this persistent script after capture to remove its write hooks.
async function installWriterHooks() {
await memory.registerexec(0x020b7714, async () => {
  if (!writerActive) return;
  const sp=await reg('sp'), context=await u32(sp), renderObject=await u32(context+4);
  const model=await u32(renderObject+4), source=await reg('r8');
  const poses=[];
  for(const id of [0,1]) {
    const p=await u32(0x020f33e0+4*id), b=await bytes(p+0x44,24);
    poses.push({id,position:[word(b)|0,word(b,4)|0,word(b,8)|0],angles:[word(b,12)|0,word(b,16)|0,word(b,20)|0]});
  }
  writerInputs.set(sp,{sp,context,renderObject,model,header:await bytes(model,64),
    matrix:await bytes(source,36),weight:await reg('r5'),camera:await bytes(0x0238f954,44),poses});
}, {cpu:CPU});
await memory.registerexec(0x021dbc84, async () => {
  if (writerRequest === null || writerActive) return;
  const index = await u32(0x0238edc0 + 0x57c8);
  if (index !== writerRequest || await u32(0x0238edc0 + 0xe28) !== 1) return;
  const base = (await reg('sp')) - 0x140;
  writerActive = true;
  writerCaptures.push({kind:'begin', index, base, bytes:await bytes(base,60)});
  for (let i=0;i<5;++i) {
    await memory.registerwrite(base+i*12, async hit => {
      if (writerActive) {
        const sp=await reg('sp'),r8=await reg('r8'),r6=await reg('r6');
        writerLast[i]={pc:Number(hit.pc)>>>0,value:Number(hit.value)>>>0,sp,lr:await reg('r14'),
          r5:await reg('r5'),r7:await reg('r7'),r8,r6,
          context:await bytes(await u32(sp),16),command:await bytes(r6,12),matrix:await bytes(r8,36)};
      }
      await emu.resume();
    }, {cpu:CPU});
  }
}, {cpu:CPU});
await memory.registerexec(0x02161ffc, async () => {
  if (!writerActive || await u32(0x0238edc0+0x57c8)!==writerRequest || await u32(0x0238edc0+0xe28)!==1) return;
  writerCaptures.push({kind:'end',index:writerRequest,lastFreecam,last:writerLast.map(e=>e?{...e}:null),inputs:Array.from(writerInputs.values())});
  writerActive=false; writerRequest=null;
  await emu.pause();
}, {cpu:CPU});
}
await memory.registerexec(0x0216fda4,async()=>{
  const sp=await reg('sp');
  lastFreecam={actor:await reg('r1'),target:await reg('r2'),param5:await u32(sp),seed:await seed()};
},{cpu:CPU});
for(const [address,key] of [[0x0216fe40,'roll100'],[0x0216fe68,'rollReset'],[0x0216fff8,'rollCamera']]) {
  await memory.registerexec(address,async()=>{
    if(lastFreecam) lastFreecam[key]=await reg('r0');
  },{cpu:CPU});
}
for (const address of [0x021db91c, 0x021dbaac, 0x021dc70c, 0x021dc80c, 0x021dca1c, 0x021e08bc]) {
  await memory.registerexec(address, async () => {
    if (address === 0x021db91c) stackBase = await reg('sp');
    if (!stackBase) return;
    const controller = 0x0238edc0;
    if (await u32(controller + 0xe28) !== 1) return;
    const index = await u32(controller + 0x57c8);
    if (index > 5) return;
    const words = [];
    for (let i = 0; i < 5; ++i) words.push(await u32(stackBase - 0x170 + i * 12));
    retain({kind: 'stack-checkpoint', pc: address, sp: await reg('sp'), index, words});
    if (address === 0x021e08bc) {
      const lr = await reg('r14');
      activeGoalSetup = {index,seed:await seed()};
      retain({kind:'goal-before',...activeGoalSetup,nodes:await nodes()});
      if (!goalReturns.has(lr)) {
        goalReturns.set(lr,true);
        await memory.registerexec(lr,async()=>{
          if (!activeGoalSetup) return;
          retain({kind:'goal-after',...activeGoalSetup,result:await reg('r0'),nodes:await nodes()});
          activeGoalSetup=null;
        },{cpu:CPU});
      }
    }
  }, {cpu: CPU});
}
await memory.registerexec(0x021e2664,async()=>{
  if (!activeGoalSetup) return;
  const p=await reg('r0'),grid=await reg('r1'),lr=await reg('r14');
  const row=await bytes(p,12);
  if (!row) return;
  const event={kind:'retreat',...activeGoalSetup,id:row[0],row4:word(row,4),grid:await bytes(grid,81),before:await nodes()};
  if (!retreatReturns.has(lr)) {
    retreatReturns.set(lr,[]);
    await memory.registerexec(lr,async()=>{
      const e=retreatReturns.get(lr).pop();
      if(e) retain({...e,after:await nodes()});
    },{cpu:CPU});
  }
  retreatReturns.get(lr).push(event);
},{cpu:CPU});
await memory.registerexec(0x021e1a10, async () => {
  const count = await reg('r7'), table = await reg('r10');
  if (count < 1 || count > 12 || !valid(table, count * 12)) return;
  const b = await bytes(table, count * 12), rows = [];
  for (let i = 0; i < count; ++i) rows.push({id: b[i * 12], presentationId: b[i * 12 + 1], field4: word(b, i * 12 + 4), actor: word(b, i * 12 + 8)});
  retain({kind: 'camera-roster', seed: await seed(), lastFreecam:lastFreecam?{...lastFreecam}:null, table, lr: await reg('r14'), index: await u32(0x0238edc0 + 0x57c8), rows});
}, {cpu: CPU});
return [
  {name:'getSlimeObservation',description:'Read Ctable_jp raw text and compact live observations; no derived RNG counter.',handler:async params=>{
    const c=await mcp.call('listScriptPrint',{id:5,max:8192});
    let texts=(c.logs??[]).map(e=>e.text);
    const trial=texts.findLastIndex(t=>t.includes('trial seed marker:'));
    if(trial>=0 && !params?.allTrials) texts=texts.slice(trial);
    if(params?.lastTurn){const at=texts.findLastIndex(t=>t.includes('turn-order speed: actor=0 '));if(at>0)texts=texts.slice(at-1);}
    const filter=params?.filter==='camera'?/freecam|camera|lr[= ]0x0216f|trial seed/:params?.filter==='rng'?/checkpoint|start FUN|end FUN|damage finalize|ProcessingDefense1|trial seed/:params?.filter==='boundary'?/start |end |trial seed|live seed/:null;
    return {ctable:filter?texts.filter(t=>filter.test(t)).join('\n'):texts.join('\n'),events:events.filter(e=>!params?.kind||e.kind===params.kind)};
  }},
  {name:'armSlimeRow4Writer',description:'After command submission, arm one next-action stack-writer interval; pauses at its endpoint. Reload the probe after reading to remove watches.',handler:async params=>{writerRequest=Number(params.index);writerActive=false;writerLast=[null,null,null,null,null];writerCaptures=[];await installWriterHooks();return{index:writerRequest};}},
  {name:'getSlimeRow4Writer',description:'Read bounded last-writer PC/value/SP/LR observations for the five physical camera scratch words.',handler:async()=>({writerRequest,writerActive,writerCaptures})},
  {name: 'getSlimeSnapshot', description: 'Read the five observed Slime.dst actor records and presentation state.', handler: async () => snapshot()},
  {name: 'getSlimeRuntimeProbe', description: 'Read retained guest-AI and camera-roster observations.', handler: async params => ({events: events.filter(e => !params?.kind || e.kind === params.kind), activeAi})},
  {name: 'clearSlimeRuntimeProbe', description: 'Clear this read-only probe history without changing game memory.', handler: async () => {events.length = 0; activeAi = null; return {ok: true};}},
];
