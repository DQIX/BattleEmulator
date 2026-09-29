// Locate the ROM-fixed s019 resource observed in Izayaaru's live membership.
// Read-only; does not generate substitute action data from the current battle.
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import vm from 'node:vm';
import {NitroFS} from '../../nitro-fs.js';
const root = import.meta.dirname;
const rom = await readFile(path.resolve(root, '../../dq9_new2.nds'));
const nitro = NitroFS.fromRom(rom.buffer.slice(rom.byteOffset, rom.byteOffset+rom.byteLength));
const context = vm.createContext({Uint8Array, ArrayBuffer, DataView});
vm.runInContext(await readFile(path.resolve(root, '../gp2.js'), 'utf8'), context, {filename:'gp2.js'});
const walk = (dir,prefix='') => [...dir.files.map(f=>prefix+f.name), ...dir.directories.flatMap(d=>walk(d,prefix+d.name+'/'))];
const paths = walk(nitro.fnt.tree), matches = [];
// The same NARC reader used by the general membership extractor. List all
// member names, not just .bact: battle archives can use another extension.
class BinReader {
  constructor(bytes) { this.bytes = Buffer.from(bytes); this.position = 0; }
  setPos(p) { this.position = p; } getPos() { return this.position; }
  skip(n) { this.position += n; } getByte() { return this.bytes[this.position++] ?? 0; }
  getLShort() { const p=this.position;this.position+=2;return this.bytes.readUInt16LE(p); }
  getLInt() { const p=this.position;this.position+=4;return this.bytes.readUInt32LE(p); }
  readString(n) { const p=this.position;this.position+=n;return this.bytes.subarray(p,p+n).toString('latin1'); }
  slice(n) { const p=this.position;this.position+=n;return this.bytes.subarray(p,p+n); }
}
const narcContext=vm.createContext({Uint8Array,ArrayBuffer,DataView,BinReader});
vm.runInContext(await readFile(path.resolve(root,'../narc.js'),'utf8')+'\nglobalThis.NarcReader=Narc;',narcContext);
const archiveMembers=[];
for (const p of paths.filter(p=>/^data\/chara_sub\/s019b(?:e|m)?\.chr$/.test(p))) {
  const archive=narcContext.NarcReader.load(new Uint8Array(nitro.readFile(p)));
  archiveMembers.push({path:p,members:archive.files.map((f,i)=>({id:i,name:archive.fnt.getFilenameOf(i),length:f.length,head:Buffer.from(f).subarray(0,32).toString('hex')}))});
}
for (const p of paths) {
  if (/s019/i.test(p)) {
    const bytes = Buffer.from(nitro.readFile(p));
    matches.push({path:p,length:bytes.length,head:bytes.subarray(0,40).toString('hex')});
  }
  if (!/\.gp2$/i.test(p)) continue;
  let members;
  try { members = context.NdsFontGp2.parseGp2(new Uint8Array(nitro.readFile(p))); }
  catch { continue; }
  for (const m of members) if (/s019/i.test(m.path)) {
    const bytes=Buffer.from(m.data);
    matches.push({container:p,path:m.path,length:bytes.length,head:bytes.subarray(0,40).toString('hex')});
  }
}
const extracted=JSON.parse(await readFile(path.resolve(root,'../freecam-membership-extract.json'),'utf8'));
const specialPrograms=(extracted.actorPrograms??[]).flatMap((p,index)=>/s019/i.test(p.actorResourcePath??'')?[{index,...p}]:[]);
console.log(JSON.stringify({archiveMembers,specialPrograms},null,2));
